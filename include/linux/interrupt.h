/* interrupt.h */
#ifndef _LINUX_INTERRUPT_H
#define _LINUX_INTERRUPT_H

#include <asm/bitops.h>

struct bh_struct {
	void (*routine)(void *);
	void *data;
};

extern unsigned long bh_active;
extern unsigned long bh_mask;
extern struct bh_struct bh_base[32];

/* Who gets which entry in bh_base.  Things which will occur most often
   should come first - in which case NET should be up the top with SERIAL/TQUEUE! */
   
enum {
	TIMER_BH = 0,
	CONSOLE_BH,
	TQUEUE_BH,
	SERIAL_BH,
	NET_BH,
	IMMEDIATE_BH,
	KEYBOARD_BH,
	CYCLADES_BH
};

extern inline void mark_bh(int nr)
{
	set_bit(nr, &bh_active);
}

extern inline void disable_bh(int nr)
{
	clear_bit(nr, &bh_mask);
}

extern inline void enable_bh(int nr)
{
	set_bit(nr, &bh_mask);
}

#endif
