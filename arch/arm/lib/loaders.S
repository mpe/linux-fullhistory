/*
 * linux/arch/arm/lib/loaders.S
 *
 * This file contains the ROM loaders for buggy cards
 */
#include <linux/linkage.h>
#include <asm/assembler.h>

/*
 * Oak SCSI
 */

ENTRY(oak_scsi_loader)
			b	Loak_scsi_read
			.word	0
Loak_scsi_reset:	bic	r10, r11, #0x00ff0000
			ldr	r2, [r10]
			RETINSTR(mov,pc,lr)

Loak_scsi_read:		mov	r2, r1, lsr #3
			and	r2, r2, #15 << 9
			bic	r10, r11, #0x00ff0000
			ldr	r2, [r10, r2]
			mov	r2, r1, lsl #20
			ldrb	r0, [r11, r2, lsr #18]
			ldr	r2, [r10]
			RETINSTR(mov,pc,lr)

ENTRY(atomwide_serial_loader)
			b	Latomwide_serial_read
			.word	0
Latomwide_serial_reset:	mov	r2, #0x3c00
			strb	r2, [r11, r2]
			RETINSTR(mov,pc,lr)

Latomwide_serial_read:	cmp	r1, #0x8000
			RETINSTR(movhi,pc,lr)
			add	r0, r1, #0x800
			mov	r0, r0, lsr #11
			mov	r3, #0x3c00
			strb	r0, [r11, r3]
			mov	r2, r1, lsl #21
			ldrb	r0, [r11, r2, lsr #19]
			strb	r2, [r11, r3]
			RETINSTR(mov,pc,lr)

/*
 * Cards we don't know about yet
 */
ENTRY(noloader)
			mov	r0, r0
			mov	r0, #0
			RETINSTR(mov,pc,lr)
