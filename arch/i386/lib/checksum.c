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
 * The macros SRC and DST specify wether there should be exception handling
 * for the source and/or the destination addresses.
 *
 * FIXME: could someone double check wether i havent mixed up some SRC and
 *	  DST definitions? It's damn hard to trigger all cases, i hope i got
 *	  them all but theres no guarantee ...
 */

#define csum_partial_copy_type(type) \
unsigned int csum_partial_copy ##type (int * __csum_err, const char *src, char *dst,   \
				  int len, int sum) {  \
    __asm__( 									  \
"		testl $2, %%edi		# Check alignment. 			\n" \
"		jz 2f			# Jump if alignment is ok. 		\n" \
"		subl $2, %%ecx		# Alignment uses up two bytes. 		\n" \
"		jae 1f			# Jump if we had at least two bytes. 	\n" \
"		addl $2, %%ecx		# ecx was < 2.  Deal with it. 		\n" \
"		jmp 4f 								\n" \
"	1000: 									\n" \
"	 1:	movw (%%esi), %%bx						\n" \
"		addl $2, %%esi							\n" \
"	1001:									\n" \
"		movw %%bx, (%%edi)						\n" \
"		addl $2, %%edi							\n" \
"		addw %%bx, %%ax							\n" \
"		adcl $0, %%eax							\n" \
"	2:									\n" \
"		pushl %%ecx							\n" \
"		shrl $5, %%ecx							\n" \
"		jz 2f								\n" \
"		testl %%esi, %%esi						\n" \
"	1002:									\n" \
"	1:	movl (%%esi), %%ebx						\n" \
"	1003:									\n" \
"		movl 4(%%esi), %%edx						\n" \
"		adcl %%ebx, %%eax						\n" \
"	1004:									\n" \
"		movl %%ebx, (%%edi)						\n" \
"		adcl %%edx, %%eax						\n" \
"	1005:									\n" \
"		movl %%edx, 4(%%edi)						\n" \
"	 									\n" \
"	1006:									\n" \
"		movl 8(%%esi), %%ebx						\n" \
"	1007:									\n" \
"		movl 12(%%esi), %%edx						\n" \
"		adcl %%ebx, %%eax						\n" \
"	1008:									\n" \
"		movl %%ebx, 8(%%edi)						\n" \
"		adcl %%edx, %%eax						\n" \
"	1009:									\n" \
"		movl %%edx, 12(%%edi)						\n" \
"	 									\n" \
"	1010: 									\n" \
"		movl 16(%%esi), %%ebx 						\n" \
"	1011: 									\n" \
"		movl 20(%%esi), %%edx						\n" \
"		adcl %%ebx, %%eax						\n" \
"	1012:									\n" \
"		movl %%ebx, 16(%%edi)						\n" \
"		adcl %%edx, %%eax						\n" \
"	1013:									\n" \
"		movl %%edx, 20(%%edi)						\n" \
"										\n" \
"	1014:									\n" \
"		movl 24(%%esi), %%ebx						\n" \
"	1015:									\n" \
"		movl 28(%%esi), %%edx						\n" \
"		adcl %%ebx, %%eax						\n" \
"	1016:									\n" \
"		movl %%ebx, 24(%%edi)						\n" \
"		adcl %%edx, %%eax						\n" \
"	1017:									\n" \
"		movl %%edx, 28(%%edi)						\n" \
"	 									\n" \
"	1018:									\n" \
"		lea 32(%%esi), %%esi						\n" \
"	1019:									\n" \
"		lea 32(%%edi), %%edi						\n" \
"		dec %%ecx							\n" \
"		jne 1b								\n" \
"		adcl $0, %%eax							\n" \
"	2:	popl %%edx							\n" \
"		movl %%edx, %%ecx						\n" \
"		andl $0x1c, %%edx						\n" \
"		je 4f								\n" \
"		shrl $2, %%edx		# This clears CF			\n" \
"	1020:									\n" \
"	3:	movl (%%esi), %%ebx						\n" \
"		adcl %%ebx, %%eax						\n" \
"	1021:									\n" \
"		movl %%ebx, (%%edi)						\n" \
"	1022:									\n" \
"		lea 4(%%esi), %%esi						\n" \
"	1023:									\n" \
"		lea 4(%%edi), %%edi						\n" \
"		dec %%edx							\n" \
"		jne 3b								\n" \
"		adcl $0, %%eax							\n" \
"	4:	andl $3, %%ecx							\n" \
"		jz 7f								\n" \
"		cmpl $2, %%ecx							\n" \
"		jb 5f								\n" \
"	1024:									\n" \
"		movw (%%esi), %%cx						\n" \
"	1025:									\n" \
"		leal 2(%%esi), %%esi						\n" \
"	1026:									\n" \
"		movw %%cx, (%%edi)						\n" \
"	1027:									\n" \
"		leal 2(%%edi), %%edi						\n" \
"		je 6f								\n" \
"		shll $16,%%ecx							\n" \
"	1028:									\n" \
"	5:	movb (%%esi), %%cl						\n" \
"	1029:									\n" \
"		movb %%cl, (%%edi)						\n" \
"	6:	addl %%ecx, %%eax						\n" \
"		adcl $0, %%eax							\n" \
"	7:									\n" \
"	2000:									\n" \
"     	   .section .fixup,\"ax\"						\n" \
"	3000:		movl %7,%1						\n" \
/* FIXME: zero out the rest of the buffer here !!!!!! */			\
"		jmp 2000b							\n" \
"		.previous							\n" \
"		.section __ex_table,\"a\"					\n" \
"		.align 4                   					\n" \
"										\n" \
SRC(  "        .long 1000b,3000b        \n  "    ) \
DST(  "        .long 1001b,3000b        \n  "    ) \
SRC(  "        .long 1002b,3000b        \n  "    ) \
SRC(  "        .long 1003b,3000b        \n  "    ) \
DST(  "        .long 1004b,3000b        \n  "    ) \
DST(  "        .long 1005b,3000b        \n  "    ) \
SRC(  "        .long 1006b,3000b        \n  "    ) \
SRC(  "        .long 1007b,3000b        \n  "    ) \
DST(  "        .long 1008b,3000b        \n  "    ) \
DST(  "        .long 1009b,3000b        \n  "    ) \
SRC(  "        .long 1010b,3000b        \n  "    ) \
SRC(  "        .long 1011b,3000b        \n  "    ) \
DST(  "        .long 1012b,3000b        \n  "    ) \
DST(  "        .long 1013b,3000b        \n  "    ) \
SRC(  "        .long 1014b,3000b        \n  "    ) \
SRC(  "        .long 1015b,3000b        \n  "    ) \
DST(  "        .long 1016b,3000b        \n  "    ) \
DST(  "        .long 1017b,3000b        \n  "    ) \
SRC(  "        .long 1018b,3000b        \n  "    ) \
DST(  "        .long 1019b,3000b        \n  "    ) \
SRC(  "        .long 1020b,3000b        \n  "    ) \
DST(  "        .long 1021b,3000b        \n  "    ) \
SRC(  "        .long 1022b,3000b        \n  "    ) \
DST(  "        .long 1023b,3000b        \n  "    ) \
SRC(  "        .long 1024b,3000b        \n  "    ) \
SRC(  "        .long 1025b,3000b        \n  "    ) \
DST(  "        .long 1026b,3000b        \n  "    ) \
DST(  "        .long 1027b,3000b        \n  "    ) \
SRC(  "        .long 1028b,3000b        \n  "    ) \
DST(  "        .long 1029b,3000b        \n  "    ) \
"        .previous                      \n  " 				\
	: "=a" (sum), "=r" (*__csum_err)   				\
	:  "0" (sum), "c" (len), "S" (src), "D" (dst), 			\
		"1" (*__csum_err), "i" (-EFAULT)   			\
	: "bx", "cx", "dx", "si", "di" );  				\
 									\
    return(sum);  							\
}

/*
 *  Currently we need only 2 out of the 4 possible type combinations:
 */

/*
 * Generate 'csum_partial_copy_from_user()', we need to do exception
 * handling for source addresses.
 */

#define SRC(x) x
#define DST(x)
csum_partial_copy_type(_from_user)
#undef SRC
#undef DST

/*
 * Generate 'csum_partial_copy_nocheck()', no need to do exception
 * handling.
 */

#define SRC(x)
#define DST(x)
csum_partial_copy_type(_nocheck_generic)
#undef SRC
#undef DST

/*
 * Generate 'csum_partial_copy_old()', old and slow compability stuff,
 * full checking.
 *
 * tell us if you see something printk-ing on this. This function will be
 * removed soon.
 */

#define SRC(x) x
#define DST(x) x
csum_partial_copy_type(_old)
#undef SRC
#undef DST

unsigned int csum_partial_copy ( const char *src, char *dst, 
				  int len, int sum)
{
	int ret;
	int error = 0;

	ret = csum_partial_copy_old (&error, src, dst, len, sum);

	if (error)
		printk("csum_partial_copy_old(): tell mingo to convert me!\n");

	return ret;
}

