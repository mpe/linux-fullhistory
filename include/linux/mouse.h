#ifndef _LINUX_MOUSE_H
#define _LINUX_MOUSE_H

/*
 * linux/include/linux/mouse.h: header file for Logitech Bus Mouse driver
 * by James Banks
 *
 * based on information gleamed from various mouse drivers on the net
 *
 * Heavily modified by David giller (rafetmad@oxy.edu)
 *
 * Minor modifications for Linux 0.96c-pl1 by Nathan Laredo
 * gt7080a@prism.gatech.edu (13JUL92)
 *
 */

#define MOUSE_IRQ		5

#define	MSE_DATA_PORT		0x23c
#define	MSE_SIGNATURE_PORT	0x23d
#define	MSE_CONTROL_PORT	0x23e
#define MSE_INTERRUPT_PORT	0x23e
#define	MSE_CONFIG_PORT		0x23f

#define	MSE_ENABLE_INTERRUPTS	0x00
#define	MSE_DISABLE_INTERRUPTS	0x10

#define	MSE_READ_X_LOW		0x80
#define	MSE_READ_X_HIGH		0xa0
#define	MSE_READ_Y_LOW		0xc0
#define	MSE_READ_Y_HIGH		0xe0

/* Magic number used to check if the mouse exists */
#define MSE_CONFIG_BYTE		0x91
#define MSE_DEFAULT_MODE	0x90
#define MSE_SIGNATURE_BYTE	0xa5

/* useful macros */

#define MSE_INT_OFF()	outb(MSE_DISABLE_INTERRUPTS, MSE_CONTROL_PORT)
#define MSE_INT_ON()	outb(MSE_ENABLE_INTERRUPTS, MSE_CONTROL_PORT)
 
struct mouse_status
	{
	char		buttons;
	char		latch_buttons;
	int		dx;
	int		dy;	

	int 		present;
	int		ready;
	int		active;

	struct inode    *inode;
	};

/* Function Prototypes */
extern long mouse_init(long);

#endif

