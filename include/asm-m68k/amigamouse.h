#ifndef _ASMm68k_AMIGAMOUSE_H
#define _ASMm68k_AMIGAMOUSE_H

/*
 *  linux/include/asm-m68k/amigamouse.h: header file for Amiga Mouse driver
 *  by Michael Rausch
 */

/*
#define MSE_INT_OFF()	outb(MSE_DISABLE_INTERRUPTS, MSE_CONTROL_PORT)
#define MSE_INT_ON()	outb(MSE_ENABLE_INTERRUPTS, MSE_CONTROL_PORT)
*/ 

struct mouse_status {
	unsigned char	buttons;
	unsigned char	latch_buttons;
	int		dx;
	int		dy;	
	int 		present;
	int		ready;
	int		active;
	struct wait_queue *wait;
	struct fasync_struct *fasyncptr;
};

#endif
