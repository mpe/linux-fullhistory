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
	    shrl $2, %%edx
	    testl %%esi, %%esi
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
 * copy from fs while checksumming, otherwise like csum_partial
 */

unsigned int csum_partial_copy_fromuser(const char *src, char *dst, 
				  int len, int sum) {
    __asm__("
	testl $2, %%edi		# Check alignment.
	jz 2f			# Jump if alignment is ok.
	subl $2, %%ecx		# Alignment uses up two bytes.
	jae 1f			# Jump if we had at least two bytes.
	addl $2, %%ecx		# ecx was < 2.  Deal with it.
	jmp 4f
1:	movw %%fs:(%%esi), %%bx
	addl $2, %%esi
	movw %%bx, (%%edi)
	addl $2, %%edi
	addw %%bx, %%ax
	adcl $0, %%eax
2:
	movl %%ecx, %%edx
	shrl $5, %%ecx
	jz 2f
	testl %%esi, %%esi
1:	movl %%fs:(%%esi), %%ebx
	adcl %%ebx, %%eax
	movl %%ebx, (%%edi)

	movl %%fs:4(%%esi), %%ebx
	adcl %%ebx, %%eax
	movl %%ebx, 4(%%edi)

	movl %%fs:8(%%esi), %%ebx
	adcl %%ebx, %%eax
	movl %%ebx, 8(%%edi)

	movl %%fs:12(%%esi), %%ebx
	adcl %%ebx, %%eax
	movl %%ebx, 12(%%edi)

	movl %%fs:16(%%esi), %%ebx
	adcl %%ebx, %%eax
	movl %%ebx, 16(%%edi)

	movl %%fs:20(%%esi), %%ebx
	adcl %%ebx, %%eax
	movl %%ebx, 20(%%edi)

	movl %%fs:24(%%esi), %%ebx
	adcl %%ebx, %%eax
	movl %%ebx, 24(%%edi)

	movl %%fs:28(%%esi), %%ebx
	adcl %%ebx, %%eax
	movl %%ebx, 28(%%edi)

	lea 32(%%esi), %%esi
	lea 32(%%edi), %%edi
	dec %%ecx
	jne 1b
	adcl $0, %%eax
2:	movl %%edx, %%ecx
	andl $28, %%edx
	je 4f
	shrl $2, %%edx
	testl %%esi, %%esi
3:	movl %%fs:(%%esi), %%ebx
	adcl %%ebx, %%eax
	movl %%ebx, (%%edi)
	lea 4(%%esi), %%esi
	lea 4(%%edi), %%edi
	dec %%edx
	jne 3b
	adcl $0, %%eax
4:	andl $3, %%ecx
	jz 7f
	cmpl $2, %%ecx
	jb 5f
	movw %%fs:(%%esi), %%cx
	leal 2(%%esi), %%esi
	movw %%cx, (%%edi)
	leal 2(%%edi), %%edi
	je 6f
	shll $16,%%ecx
5:	movb %%fs:(%%esi), %%cl
	movb %%cl, (%%edi)
6:	addl %%ecx, %%eax
	adcl $0, %%eax
7:
	"
	: "=a" (sum)
	: "0"(sum), "c"(len), "S"(src), "D" (dst)
	: "bx", "cx", "dx", "si", "di" );
    return(sum);
}
/*
 * copy from ds while checksumming, otherwise like csum_partial
 */

unsigned int csum_partial_copy(const char *src, char *dst, 
				  int len, int sum) {
    __asm__("
	testl $2, %%edi		# Check alignment.
	jz 2f			# Jump if alignment is ok.
	subl $2, %%ecx		# Alignment uses up two bytes.
	jae 1f			# Jump if we had at least two bytes.
	addl $2, %%ecx		# ecx was < 2.  Deal with it.
	jmp 4f
1:	movw (%%esi), %%bx
	addl $2, %%esi
	movw %%bx, (%%edi)
	addl $2, %%edi
	addw %%bx, %%ax
	adcl $0, %%eax
2:
	movl %%ecx, %%edx
	shrl $5, %%ecx
	jz 2f
	testl %%esi, %%esi
1:	movl (%%esi), %%ebx
	adcl %%ebx, %%eax
	movl %%ebx, (%%edi)

	movl 4(%%esi), %%ebx
	adcl %%ebx, %%eax
	movl %%ebx, 4(%%edi)

	movl 8(%%esi), %%ebx
	adcl %%ebx, %%eax
	movl %%ebx, 8(%%edi)

	movl 12(%%esi), %%ebx
	adcl %%ebx, %%eax
	movl %%ebx, 12(%%edi)

	movl 16(%%esi), %%ebx
	adcl %%ebx, %%eax
	movl %%ebx, 16(%%edi)

	movl 20(%%esi), %%ebx
	adcl %%ebx, %%eax
	movl %%ebx, 20(%%edi)

	movl 24(%%esi), %%ebx
	adcl %%ebx, %%eax
	movl %%ebx, 24(%%edi)

	movl 28(%%esi), %%ebx
	adcl %%ebx, %%eax
	movl %%ebx, 28(%%edi)

	lea 32(%%esi), %%esi
	lea 32(%%edi), %%edi
	dec %%ecx
	jne 1b
	adcl $0, %%eax
2:	movl %%edx, %%ecx
	andl $28, %%edx
	je 4f
	shrl $2, %%edx
	testl %%esi, %%esi
3:	movl (%%esi), %%ebx
	adcl %%ebx, %%eax
	movl %%ebx, (%%edi)
	lea 4(%%esi), %%esi
	lea 4(%%edi), %%edi
	dec %%edx
	jne 3b
	adcl $0, %%eax
4:	andl $3, %%ecx
	jz 7f
	cmpl $2, %%ecx
	jb 5f
	movw (%%esi), %%cx
	leal 2(%%esi), %%esi
	movw %%cx, (%%edi)
	leal 2(%%edi), %%edi
	je 6f
	shll $16,%%ecx
5:	movb (%%esi), %%cl
	movb %%cl, (%%edi)
6:	addl %%ecx, %%eax
	adcl $0, %%eax
7:
	"
	: "=a" (sum)
	: "0"(sum), "c"(len), "S"(src), "D" (dst)
	: "bx", "cx", "dx", "si", "di" );
    return(sum);
}
