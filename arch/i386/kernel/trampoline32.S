!
!	32bit side of the trampoline code
!
#define __ASSEMBLY__
#include <asm/segment.h>
!
!
!	Anything but a relative address here will be wrong by 8K...
!
	.globl startup32
.text
startup32:
!	Run the kernel
	mov	eax,#KERNEL_DS
	mov	ds,ax
	mov	eax,#0xA5A5A5A5
	mov	[8192],eax
	jmpi	0x100000,KERNEL_CS
l1:
	.byte	0xEA,0x00,0x00,0x10,0x00,0x10,0x00
