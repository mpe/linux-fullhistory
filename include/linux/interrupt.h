/* interrupt.h */
#ifndef _LINUX_INTERRUPT_H
#define _LINUX_INTERRUPT_H

struct bh_struct {
	void (*routine)(void *);
	void *data;
};

extern int bh_active;
extern struct bh_struct bh_base[32];

/* Who gets which entry in bh_base.  Things which will occur most often
   should come first. */
enum {
	TIMER_BH = 0,
	CONSOLE_BH,
	SERIAL_BH,
	INET_BH
};

void do_bottom_half();

#endif
