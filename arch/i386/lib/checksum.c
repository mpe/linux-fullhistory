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
 *              Pentium Pro/II routines:
 *              Alexander Kjeldaas <astor@guardian.no>
 *              Finn Arne Gangstad <finnag@guardian.no>
 *		Lots of code moved from tcp.c and ip.c; see those files
 *		for more names.
 *
 * Changes:     Ingo Molnar, converted csum_partial_copy() to 2.1 exception
 *			     handling.
 *		Andi Kleen,  add zeroing on error, fix constraints.
 *
 * To fix:
 *		Convert to pure asm, because this file is too hard
 *		for gcc's register allocator and it is not clear if the
 *	 	contraints are correct.
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

#if CPU!=686

unsigned int csum_partial(const unsigned char * buff, int len, unsigned int sum) {
	  /*
	   * Experiments with Ethernet and SLIP connections show that buff
	   * is aligned on either a 2-byte or 4-byte boundary.  We get at
	   * least a twofold speedup on 486 and Pentium if it is 4-byte aligned.
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
	: "bx", "dx", "si", "cx", "memory");
	return(sum);
}

#else  /* 686 */

unsigned int csum_partial(const unsigned char * buf, int len, unsigned int sum) {
         __asm__ ("
            testl $2, %%esi         
            jnz 30f                 
10:
            movl %%ecx, %%edx
            movl %%ecx, %%ebx
            andl $0x7c, %%ebx
            shrl $7, %%ecx
            addl %%ebx,%%esi
            shrl $2, %%ebx  
            negl %%ebx
            lea 45f(%%ebx,%%ebx,2), %%ebx
            testl %%esi, %%esi
            jmp *%%ebx

            # Handle 2-byte-aligned regions
20:         addw (%%esi), %%ax
            lea 2(%%esi), %%esi
            adcl $0, %%eax
            jmp 10b

30:         subl $2, %%ecx          
            ja 20b                 
            je 32f
            movzbl (%%esi),%%ebx # csumming 1 byte, 2-aligned
            addl %%ebx, %%eax
            adcl $0, %%eax
            jmp 80f
32:
            addw (%%esi), %%ax # csumming 2 bytes, 2-aligned
            adcl $0, %%eax
            jmp 80f

40: 
	    addl -128(%%esi), %%eax
            adcl -124(%%esi), %%eax
            adcl -120(%%esi), %%eax
            adcl -116(%%esi), %%eax   
	    adcl -112(%%esi), %%eax   
            adcl -108(%%esi), %%eax
            adcl -104(%%esi), %%eax
            adcl -100(%%esi), %%eax
            adcl -96(%%esi), %%eax
            adcl -92(%%esi), %%eax
            adcl -88(%%esi), %%eax
            adcl -84(%%esi), %%eax
            adcl -80(%%esi), %%eax
            adcl -76(%%esi), %%eax
            adcl -72(%%esi), %%eax
            adcl -68(%%esi), %%eax
	    adcl -64(%%esi), %%eax     
            adcl -60(%%esi), %%eax     
            adcl -56(%%esi), %%eax     
            adcl -52(%%esi), %%eax   
            adcl -48(%%esi), %%eax   
            adcl -44(%%esi), %%eax
            adcl -40(%%esi), %%eax
            adcl -36(%%esi), %%eax
            adcl -32(%%esi), %%eax
            adcl -28(%%esi), %%eax
            adcl -24(%%esi), %%eax
            adcl -20(%%esi), %%eax
            adcl -16(%%esi), %%eax
            adcl -12(%%esi), %%eax
            adcl -8(%%esi), %%eax
            adcl -4(%%esi), %%eax
45:
            lea 128(%%esi), %%esi
            adcl $0, %%eax
            dec %%ecx
            jge 40b
            movl %%edx, %%ecx
50:         andl $3, %%ecx
            jz 80f

            # Handle the last 1-3 bytes without jumping
            notl %%ecx            # 1->2, 2->1, 3->0, higher bits are masked
	    movl $0xffffff,%%ebx  # by the shll and shrl instructions
	    shll $3,%%ecx
	    shrl %%cl,%%ebx
	    andl -128(%%esi),%%ebx # esi is 4-aligned so should be ok
	    addl %%ebx,%%eax
	    adcl $0,%%eax
80:          "
        : "=a"(sum)
        : "0"(sum), "c"(len), "S"(buf)
        : "bx", "dx", "cx", "si", "memory");
        return(sum);
}

#endif

/*
 * Copy from ds while checksumming, otherwise like csum_partial
 *
 * The macros SRC and DST specify the type of access for the instruction.
 * thus we can call a custom exception handler for all access types.
 *
 * FIXME: could someone double-check whether I haven't mixed up some SRC and
 *	  DST definitions? It's damn hard to trigger all cases.  I hope I got
 *	  them all but there's no guarantee.
 */

#define SRC(y...)			\
"	9999: "#y";			\n \
	.section __ex_table, \"a\";	\n \
	.long 9999b, 6001f		\n \
	.previous\n"

#define DST(y...)			\
"	9999: "#y";			\n \
	.section __ex_table, \"a\";	\n \
	.long 9999b, 6002f		\n \
	.previous\n"

#if CPU!=686

unsigned int csum_partial_copy_generic (const char *src, char *dst,
				  int len, int sum, int *src_err_ptr, int *dst_err_ptr)
{
    __u32 tmp_var;

    __asm__ __volatile__ ( "
		movl  %6,%%edi
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
		movl %%ecx, %8
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
	2:	movl %8, %%edx
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
# zero the complete destination - computing the rest
# is too much work 
	movl	%6, %%edi
	movl	%9, %%ecx
	xorl	%%eax,%%eax
	rep ; stosb
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
	: "=a" (sum)
	: "m" (src_err_ptr), "m" (dst_err_ptr),
	  "0" (sum), "c" (len), "S" (src), "m" (dst),
		"i" (-EFAULT), "m"(tmp_var),
		"m" (len)
	: "bx", "dx", "si", "di", "cx", "memory" );

    return(sum);
}

#else /* CPU == 686 */

#define ROUND1(x) \
        SRC(movl x(%%esi), %%ebx         ) \
        "addl %%ebx, %%eax\n" \
        DST(movl %%ebx, x(%%edi)         )

#define ROUND(x) \
        SRC(movl x(%%esi), %%ebx         ) \
        "adcl %%ebx, %%eax\n" \
        DST(movl %%ebx, x(%%edi)         )

unsigned int csum_partial_copy_generic (const char *src, char *dst,
				  int len, int sum, int *src_err_ptr, int *dst_err_ptr)
{
	__asm__ __volatile__ ("
	movl %4,%%ecx
        movl %%ecx, %%edx  
        movl %%ecx, %%ebx  
        shrl $6, %%ecx     
        andl $0x3c, %%ebx  
        negl %%ebx
        subl %%ebx, %%esi  
        subl %%ebx, %%edi  
        lea 3f(%%ebx,%%ebx), %%ebx
        testl %%esi, %%esi 
        jmp *%%ebx         
1:      addl $64,%%esi
        addl $64,%%edi\n" 
ROUND1(-64) ROUND(-60) ROUND(-56) ROUND(-52)
ROUND (-48) ROUND(-44) ROUND(-40) ROUND(-36)
ROUND (-32) ROUND(-28) ROUND(-24) ROUND(-20)
ROUND (-16) ROUND(-12) ROUND(-8)  ROUND(-4)
"3:     adcl $0,%%eax
        dec %%ecx
        jge 1b
4:      andl $3, %%edx
        jz 7f
        cmpl $2, %%edx
        jb 5f
  " SRC(movw (%%esi), %%dx         )"
        leal 2(%%esi), %%esi
  " DST(movw %%dx, (%%edi)         )"
        leal 2(%%edi), %%edi
        je 6f
        shll $16,%%edx
5:" SRC(movb (%%esi), %%dl         )"
  " DST(movb %%dl, (%%edi)         )"
6:      addl %%edx, %%eax
        adcl $0, %%eax
7:
.section .fixup, \"ax\"
6000:	movl	%7, (%%ebx)
# zero the complete destination (computing the rest is too much work)
	movl	%8,%%edi
	movl	%4,%%ecx
	xorl	%%eax,%%eax
	rep ; stosb
	jmp	7b
6001:	movl    %1, %%ebx	
	jmp	6000b	
6002:	movl    %2, %%ebx
	jmp	6000b
.previous
        "
	: "=a"(sum)
        : "m"(src_err_ptr), "m"(dst_err_ptr), 
	  "0"(sum), "m"(len), "S"(src), "D" (dst),
	  "i" (-EFAULT),
	  "m" (dst)
        : "bx", "cx", "si", "di", "dx", "memory" );
	return(sum);
}

#undef ROUND
#undef ROUND1

#endif


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


