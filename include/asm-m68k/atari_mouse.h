#ifndef _LINUX_ATARI_MOUSE_H
#define _LINUX_ATARI_MOUSE_H

/*
 * linux/include/linux/atari_mouse.h
 * header file for Atari Mouse driver
 * by Robert de Vries (robert@and.nl) on 19Jul93
 */

struct mouse_status {
	char		buttons;
	short		dx;
	short		dy;
	int		ready;
	int		active;
	struct wait_queue *wait;
	struct fasync_struct *fasyncptr;
};

#endif
