/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		IP/TCP/UDP checksumming routines
 *
 * Authors:	Jorge Cwik, <jorge@laser.satlink.net>
 *		Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *		Tom May, <ftom@netcom.com>
 *		Lots of code moved from tcp.c and ip.c; see those files
 *		for more names.
 *
 * Changes:     Ingo Molnar, converted csum_partial_copy() to 2.1 exception
 *			     handling.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <net/checksum.h>

/*
 * computes a partial checksum, e.g. for TCP/UDP fragments
 */

unsigned int csum_partial(const unsigned char * buff, int len, unsigned int sum) {
	  /*
	   * Experiments with ethernet and slip connections show that buff
	   * is aligned on either a 2-byte or 4-byte boundary.  We get at
	   * least a 2x speedup on 486 and Pentium if it is 4-byte aligned.
	   * Fortunately, it is easy to convert 2-byte alignment to 4-byte
	   * alignment for the unrolled loop.
	   */
	__asm__("
	    testl $2, %%esi		# Check alignment.
	    jz 2f			# Jump if alignment is ok.
	    subl $2, %%ecx		# Alignment uses up two bytes.
	    jae 1f			# Jump if we had at least two bytes.
	    addl $2, %%ecx		# ecx was < 2.  Deal with it.
	    jmp 4f
1:	    movw (%%esi), %%bx
	    addl $2, %%esi
	    addw %%bx, %%ax
	    adcl $0, %%eax
2:
	    movl %%ecx, %%edx
	    shrl $5, %%ecx
	    jz 2f
	    testl %%esi, %%esi
1:	    movl (%%esi), %%ebx
	    adcl %%ebx, %%eax
	    movl 4(%%esi), %%ebx
	    adcl %%ebx, %%eax
	    movl 8(%%esi), %%ebx
	    adcl %%ebx, %%eax
	    movl 12(%%esi), %%ebx
	    adcl %%ebx, %%eax
	    movl 16(%%esi), %%ebx
	    adcl %%ebx, %%eax
	    movl 20(%%esi), %%ebx
	    adcl %%ebx, %%eax
	    movl 24(%%esi), %%ebx
	    adcl %%ebx, %%eax
	    movl 28(%%esi), %%ebx
	    adcl %%ebx, %%eax
	    lea 32(%%esi), %%esi
	    dec %%ecx
	    jne 1b
	    adcl $0, %%eax
2:	    movl %%edx, %%ecx
	    andl $0x1c, %%edx
	    je 4f
	    shrl $2, %%edx	# This clears CF
3:	    adcl (%%esi), %%eax
	    lea 4(%%esi), %%esi
	    dec %%edx
	    jne 3b
	    adcl $0, %%eax
4:	    andl $3, %%ecx
	    jz 7f
	    cmpl $2, %%ecx
	    jb 5f
	    movw (%%esi),%%cx
	    leal 2(%%esi),%%esi
	    je 6f
	    shll $16,%%ecx
5:	    movb (%%esi),%%cl
6:	    addl %%ecx,%%eax
	    adcl $0, %%eax 
7:	    "
	: "=a"(sum)
	: "0"(sum), "c"(len), "S"(buff)
	: "bx", "cx", "dx", "si");
	return(sum);
}

/*
 * Copy from ds while checksumming, otherwise like csum_partial
 *
 * The macros SRC and DST specify the type of access for the instruction.
 * thus we can call a custom exception handler for all access types.
 *
 * FIXME: could someone double check wether i havent mixed up some SRC and
 *	  DST definitions? It's damn hard to trigger all cases, i hope i got
 *	  them all but theres no guarantee ...
 */

#define SRC(y...)			\
"	9999: "#y";			\n \
	.section __ex_table, \"a\";	\n \
	.long 9999b, 6001f		\n \
	.previous"

#define DST(y...)			\
"	9999: "#y";			\n \
	.section __ex_table, \"a\";	\n \
	.long 9999b, 6002f		\n \
	.previous"

unsigned int csum_partial_copy_generic (const char *src, char *dst,
				  int len, int sum, int *src_err_ptr, int *dst_err_ptr)
{
    __asm__ __volatile__ ( "
		testl $2, %%edi		# Check alignment. 
		jz 2f			# Jump if alignment is ok.
		subl $2, %%ecx		# Alignment uses up two bytes.
		jae 1f			# Jump if we had at least two bytes.
		addl $2, %%ecx		# ecx was < 2.  Deal with it.
		jmp 4f
"SRC(	1:	movw (%%esi), %%bx				)"
		addl $2, %%esi
"DST(		movw %%bx, (%%edi)				)"
		addl $2, %%edi
		addw %%bx, %%ax	
		adcl $0, %%eax
	2:
		pushl %%ecx
		shrl $5, %%ecx
		jz 2f
		testl %%esi, %%esi
"SRC(	1:	movl (%%esi), %%ebx				)"
"SRC(		movl 4(%%esi), %%edx				)"
		adcl %%ebx, %%eax
"DST(		movl %%ebx, (%%edi)				)"
		adcl %%edx, %%eax
"DST(		movl %%edx, 4(%%edi)				)"

"SRC(		movl 8(%%esi), %%ebx				)"
"SRC(		movl 12(%%esi), %%edx				)"
		adcl %%ebx, %%eax
"DST(		movl %%ebx, 8(%%edi)				)"
		adcl %%edx, %%eax
"DST(		movl %%edx, 12(%%edi)				)"

"SRC(		movl 16(%%esi), %%ebx 				)"
"SRC(		movl 20(%%esi), %%edx				)"
		adcl %%ebx, %%eax
"DST(		movl %%ebx, 16(%%edi)				)"
		adcl %%edx, %%eax
"DST(		movl %%edx, 20(%%edi)				)"

"SRC(		movl 24(%%esi), %%ebx				)"
"SRC(		movl 28(%%esi), %%edx				)"
		adcl %%ebx, %%eax
"DST(		movl %%ebx, 24(%%edi)				)"
		adcl %%edx, %%eax
"DST(		movl %%edx, 28(%%edi)				)"

"SRC(		lea 32(%%esi), %%esi				)"
"DST(		lea 32(%%edi), %%edi				)"
		dec %%ecx
		jne 1b
		adcl $0, %%eax
	2:	popl %%edx
		movl %%edx, %%ecx
		andl $0x1c, %%edx
		je 4f
		shrl $2, %%edx		# This clears CF
"SRC(	3:	movl (%%esi), %%ebx				)"
		adcl %%ebx, %%eax
"DST(		movl %%ebx, (%%edi)				)"
"SRC(		lea 4(%%esi), %%esi				)"
"DST(		lea 4(%%edi), %%edi				)"
		dec %%edx
		jne 3b
		adcl $0, %%eax
	4:	andl $3, %%ecx
		jz 7f
		cmpl $2, %%ecx
		jb 5f
"SRC(		movw (%%esi), %%cx				)"
"SRC(		leal 2(%%esi), %%esi				)"
"DST(		movw %%cx, (%%edi)				)"
"DST(		leal 2(%%edi), %%edi				)"
		je 6f
		shll $16,%%ecx
"SRC(	5:	movb (%%esi), %%cl				)"
"DST(		movb %%cl, (%%edi)				)"
	6:	addl %%ecx, %%eax
		adcl $0, %%eax
	7:

5000:

# Exception handler:
################################################
						#
.section .fixup, \"ax\"				#
						#
6000:						#
						#
	movl	%7, (%%ebx)			#
						#
# FIXME: do zeroing of rest of the buffer here. #
						#
	jmp	5000b				#
						#
6001:						#
	movl    %1, %%ebx			#
	jmp	6000b				#
						#
6002:						#
	movl    %2, %%ebx			#
	jmp	6000b				#
						#
.previous					#
						#
################################################

"
	: "=a" (sum), "=m" (src_err_ptr), "=m" (dst_err_ptr)
	:  "0" (sum), "c" (len), "S" (src), "D" (dst),
		"i" (-EFAULT)
	: "bx", "cx", "dx", "si", "di" );

    return(sum);
}

#undef SRC
#undef DST

/*
 * FIXME: old compatibility stuff, will be removed soon.
 */

unsigned int csum_partial_copy( const char *src, char *dst, int len, int sum)
{
	int src_err=0, dst_err=0;

	sum = csum_partial_copy_generic ( src, dst, len, sum, &src_err, &dst_err);

	if (src_err || dst_err)
		printk("old csum_partial_copy_fromuser(), tell mingo to convert me.\n");

	return sum;
}


