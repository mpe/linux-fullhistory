/* $Id: time.c,v 1.16 1998/09/05 17:25:28 jj Exp $
 * time.c: UltraSparc timer and TOD clock support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998 Eddie C. Dost   (ecd@skynet.be)
 *
 * Based largely on code which is:
 *
 * Copyright (C) 1996 Thomas K. Dyas (tdyas@eden.rutgers.edu)
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/init.h>
#include <linux/ioport.h>

#include <asm/oplib.h>
#include <asm/mostek.h>
#include <asm/timer.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/sbus.h>
#include <asm/fhc.h>
#include <asm/pbm.h>
#include <asm/ebus.h>

struct mostek48t02 *mstk48t02_regs = 0;
static struct mostek48t08 *mstk48t08_regs = 0;
static struct mostek48t59 *mstk48t59_regs = 0;

static int set_rtc_mmss(unsigned long);

/* timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 *
 * NOTE: On SUN5 systems the ticker interrupt comes in using 2
 *       interrupts, one at level14 and one with softint bit 0.
 */
unsigned long timer_tick_offset;
static unsigned long timer_tick_compare;
static unsigned long timer_ticks_per_usec;

static __inline__ void timer_check_rtc(void)
{
	/* last time the cmos clock got updated */
	static long last_rtc_update=0;

	/* Determine when to update the Mostek clock. */
	if ((time_status & STA_UNSYNC) == 0 &&
	    xtime.tv_sec > last_rtc_update + 660 &&
	    xtime.tv_usec >= 500000 - ((unsigned) tick) / 2 &&
	    xtime.tv_usec <= 500000 + ((unsigned) tick) / 2) {
		if (set_rtc_mmss(xtime.tv_sec) == 0)
			last_rtc_update = xtime.tv_sec;
		else
			last_rtc_update = xtime.tv_sec - 600;
			/* do it again in 60 s */
	}
}

static void timer_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	unsigned long ticks;

	do {
		do_timer(regs);

		__asm__ __volatile__("
			rd	%%tick_cmpr, %0
			add	%0, %2, %0
			wr	%0, 0, %%tick_cmpr
			rd	%%tick, %1"
			: "=&r" (timer_tick_compare), "=r" (ticks)
			: "r" (timer_tick_offset));
	} while (ticks >= timer_tick_compare);

	timer_check_rtc();
}

#ifdef __SMP__
void timer_tick_interrupt(struct pt_regs *regs)
{
	do_timer(regs);

	/*
	 * Only keep timer_tick_offset uptodate, but don't set TICK_CMPR.
	 */
	__asm__ __volatile__("
		rd	%%tick_cmpr, %0
		add	%0, %1, %0"
		: "=&r" (timer_tick_compare)
		: "r" (timer_tick_offset));

	timer_check_rtc();
}
#endif

/* Converts Gregorian date to seconds since 1970-01-01 00:00:00.
 * Assumes input in normal date format, i.e. 1980-12-31 23:59:59
 * => year=1980, mon=12, day=31, hour=23, min=59, sec=59.
 *
 * [For the Julian calendar (which was used in Russia before 1917,
 * Britain & colonies before 1752, anywhere else before 1582,
 * and is still in use by some communities) leave out the
 * -year/100+year/400 terms, and add 10.]
 *
 * This algorithm was first published by Gauss (I think).
 *
 * WARNING: this function will overflow on 2106-02-07 06:28:16 on
 * machines were long is 32-bit! (However, as time_t is signed, we
 * will already get problems at other places on 2038-01-19 03:14:08)
 */
static inline unsigned long mktime(unsigned int year, unsigned int mon,
	unsigned int day, unsigned int hour,
	unsigned int min, unsigned int sec)
{
	if (0 >= (int) (mon -= 2)) {	/* 1..12 -> 11,12,1..10 */
		mon += 12;	/* Puts Feb last since it has leap day */
		year -= 1;
	}
	return (((
	    (unsigned long)(year/4 - year/100 + year/400 + 367*mon/12 + day) +
	      year*365 - 719499
	    )*24 + hour /* now have hours */
	   )*60 + min /* now have minutes */
	  )*60 + sec; /* finally seconds */
}

/* Kick start a stopped clock (procedure from the Sun NVRAM/hostid FAQ). */
static void __init kick_start_clock(void)
{
	register struct mostek48t02 *regs = mstk48t02_regs;
	unsigned char sec;
	int i, count;

	prom_printf("CLOCK: Clock was stopped. Kick start ");

	/* Turn on the kick start bit to start the oscillator. */
	regs->creg |= MSTK_CREG_WRITE;
	regs->sec &= ~MSTK_STOP;
	regs->hour |= MSTK_KICK_START;
	regs->creg &= ~MSTK_CREG_WRITE;

	/* Delay to allow the clock oscillator to start. */
	sec = MSTK_REG_SEC(regs);
	for (i = 0; i < 3; i++) {
		while (sec == MSTK_REG_SEC(regs))
			for (count = 0; count < 100000; count++)
				/* nothing */ ;
		prom_printf(".");
		sec = regs->sec;
	}
	prom_printf("\n");

	/* Turn off kick start and set a "valid" time and date. */
	regs->creg |= MSTK_CREG_WRITE;
	regs->hour &= ~MSTK_KICK_START;
	MSTK_SET_REG_SEC(regs,0);
	MSTK_SET_REG_MIN(regs,0);
	MSTK_SET_REG_HOUR(regs,0);
	MSTK_SET_REG_DOW(regs,5);
	MSTK_SET_REG_DOM(regs,1);
	MSTK_SET_REG_MONTH(regs,8);
	MSTK_SET_REG_YEAR(regs,1996 - MSTK_YEAR_ZERO);
	regs->creg &= ~MSTK_CREG_WRITE;

	/* Ensure the kick start bit is off. If it isn't, turn it off. */
	while (regs->hour & MSTK_KICK_START) {
		prom_printf("CLOCK: Kick start still on!\n");
		regs->creg |= MSTK_CREG_WRITE;
		regs->hour &= ~MSTK_KICK_START;
		regs->creg &= ~MSTK_CREG_WRITE;
	}

	prom_printf("CLOCK: Kick start procedure successful.\n");
}

/* Return nonzero if the clock chip battery is low. */
static int __init has_low_battery(void)
{
	register struct mostek48t02 *regs = mstk48t02_regs;
	unsigned char data1, data2;

	data1 = regs->eeprom[0];	/* Read some data. */
	regs->eeprom[0] = ~data1;	/* Write back the complement. */
	data2 = regs->eeprom[0];	/* Read back the complement. */
	regs->eeprom[0] = data1;	/* Restore the original value. */

	return (data1 == data2);	/* Was the write blocked? */
}


/* Probe for the real time clock chip. */
static void __init set_system_time(void)
{
	unsigned int year, mon, day, hour, min, sec;
	struct mostek48t02 *mregs;

	do_get_fast_time = do_gettimeofday;

	mregs = mstk48t02_regs;
	if(!mregs) {
		prom_printf("Something wrong, clock regs not mapped yet.\n");
		prom_halt();
	}		

	mregs->creg |= MSTK_CREG_READ;
	sec = MSTK_REG_SEC(mregs);
	min = MSTK_REG_MIN(mregs);
	hour = MSTK_REG_HOUR(mregs);
	day = MSTK_REG_DOM(mregs);
	mon = MSTK_REG_MONTH(mregs);
	year = MSTK_CVT_YEAR( MSTK_REG_YEAR(mregs) );
	xtime.tv_sec = mktime(year, mon, day, hour, min, sec);
	xtime.tv_usec = 0;
	mregs->creg &= ~MSTK_CREG_READ;
}

void __init clock_probe(void)
{
	struct linux_prom_registers clk_reg[2];
	char model[128];
	int node, busnd = -1, err;
	unsigned long flags;
#ifdef CONFIG_PCI
	struct linux_ebus *ebus = 0;
#endif

	__save_and_cli(flags);

	if(central_bus != NULL) {
		busnd = central_bus->child->prom_node;
	}
#ifdef CONFIG_PCI
	else if (ebus_chain != NULL) {
		ebus = ebus_chain;
		busnd = ebus->prom_node;
	}
#endif
	else {
		busnd = SBus_chain->prom_node;
	}

	if(busnd == -1) {
		prom_printf("clock_probe: problem, cannot find bus to search.\n");
		prom_halt();
	}

	node = prom_getchild(busnd);

	while(1) {
		prom_getstring(node, "model", model, sizeof(model));
		if(strcmp(model, "mk48t02") &&
		   strcmp(model, "mk48t08") &&
		   strcmp(model, "mk48t59")) {
			node = prom_getsibling(node);
#ifdef CONFIG_PCI
			if ((node == 0) && ebus) {
				ebus = ebus->next;
				if (ebus) {
					busnd = ebus->prom_node;
					node = prom_getchild(busnd);
				}
			}
#endif
			if(node == 0) {
				prom_printf("clock_probe: Cannot find timer chip\n");
				prom_halt();
			}
			continue;
		}

		err = prom_getproperty(node, "reg", (char *)clk_reg,
				       sizeof(clk_reg));
		if(err == -1) {
			prom_printf("clock_probe: Cannot get Mostek reg property\n");
			prom_halt();
		}

		if(central_bus) {
			prom_apply_fhc_ranges(central_bus->child, clk_reg, 1);
			prom_apply_central_ranges(central_bus, clk_reg, 1);
		}
#ifdef CONFIG_PCI
		else if (ebus_chain) {
			struct linux_ebus_device *edev;

			for_each_ebusdev(edev, ebus)
				if (edev->prom_node == node)
					break;
			if (!edev) {
				prom_printf("%s: Mostek not probed by EBUS\n",
					    __FUNCTION__);
				prom_halt();
			}

			if (check_region(edev->base_address[0],
					 sizeof(struct mostek48t59))) {
				prom_printf("%s: Can't get region %lx, %d\n",
					    __FUNCTION__, edev->base_address[0],
					    sizeof(struct mostek48t59));
				prom_halt();
			}
			request_region(edev->base_address[0],
				       sizeof(struct mostek48t59), "clock");

			mstk48t59_regs = (struct mostek48t59 *)
						edev->base_address[0];
			mstk48t02_regs = &mstk48t59_regs->regs;
			break;
		}
#endif
		else {
			prom_adjust_regs(clk_reg, 1,
					 SBus_chain->sbus_ranges,
					 SBus_chain->num_sbus_ranges);
		}

		if(model[5] == '0' && model[6] == '2') {
			mstk48t02_regs = (struct mostek48t02 *)
				sparc_alloc_io(clk_reg[0].phys_addr,
					       (void *) 0, sizeof(*mstk48t02_regs),
					       "clock", clk_reg[0].which_io, 0x0);
		} else if(model[5] == '0' && model[6] == '8') {
			mstk48t08_regs = (struct mostek48t08 *)
				sparc_alloc_io(clk_reg[0].phys_addr,
					       (void *) 0, sizeof(*mstk48t08_regs),
					       "clock", clk_reg[0].which_io, 0x0);
			mstk48t02_regs = &mstk48t08_regs->regs;
		} else {
			mstk48t59_regs = (struct mostek48t59 *)
				sparc_alloc_io(clk_reg[0].phys_addr,
					       (void *) 0, sizeof(*mstk48t59_regs),
					       "clock", clk_reg[0].which_io, 0x0);
			mstk48t02_regs = &mstk48t59_regs->regs;
		}
		break;
	}

	/* Report a low battery voltage condition. */
	if (has_low_battery())
		prom_printf("NVRAM: Low battery voltage!\n");

	/* Kick start the clock if it is completely stopped. */
	if (mstk48t02_regs->sec & MSTK_STOP)
		kick_start_clock();

	set_system_time();
	
	__restore_flags(flags);
}

#ifndef BCD_TO_BIN
#define BCD_TO_BIN(val) (((val)&15) + ((val)>>4)*10)
#endif

#ifndef BIN_TO_BCD
#define BIN_TO_BCD(val) ((((val)/10)<<4) + (val)%10)
#endif

extern void init_timers(void (*func)(int, void *, struct pt_regs *),
			unsigned long *);

void __init time_init(void)
{
	/* clock_probe() is now done at end of [se]bus_init on sparc64
	 * so that sbus, fhc and ebus bus information is probed and
	 * available.
	 */
	unsigned long clock;

	init_timers(timer_interrupt, &clock);
	timer_tick_offset = clock / HZ;
	timer_ticks_per_usec = clock / 1000000;
}

static __inline__ unsigned long do_gettimeoffset(void)
{
	unsigned long ticks;

	__asm__ __volatile__("
		rd	%%tick, %%g1
		add	%1, %%g1, %0
		sub	%0, %2, %0
"
		: "=r" (ticks)
		: "r" (timer_tick_offset), "r" (timer_tick_compare)
		: "g1", "g2");

	return ticks / timer_ticks_per_usec;
}

void do_gettimeofday(struct timeval *tv)
{
	/* Load doubles must be used on xtime so that what we get
	 * is guarenteed to be atomic, this is why we can run this
	 * with interrupts on full blast.  Don't touch this... -DaveM
	 *
	 * Note with time_t changes to the timeval type, I must now use
	 * nucleus atomic quad 128-bit loads.
	 */
	__asm__ __volatile__("
	sethi	%hi(timer_tick_offset), %g3
	sethi	%hi(xtime), %g2
	sethi	%hi(timer_tick_compare), %g1
	ldx	[%g3 + %lo(timer_tick_offset)], %g3
	or	%g2, %lo(xtime), %g2
	or	%g1, %lo(timer_tick_compare), %g1
1:	ldda	[%g2] 0x24, %o4
	membar	#LoadLoad | #MemIssue
	rd	%tick, %o1
	ldx	[%g1], %g7
	membar	#LoadLoad | #MemIssue
	ldda	[%g2] 0x24, %o2
	membar	#LoadLoad
	xor	%o4, %o2, %o2
	xor	%o5, %o3, %o3
	orcc	%o2, %o3, %g0
	bne,pn	%xcc, 1b
	 sethi	%hi(lost_ticks), %o2
	sethi	%hi(timer_ticks_per_usec), %o3
	ldx	[%o2 + %lo(lost_ticks)], %o2
	add	%g3, %o1, %o1
	ldx	[%o3 + %lo(timer_ticks_per_usec)], %o3
	sub	%o1, %g7, %o1
	brz,pt	%o2, 1f
	 udivx	%o1, %o3, %o1
	sethi	%hi(10000), %g2
	or	%g2, %lo(10000), %g2
	add	%o1, %g2, %o1
1:	sethi	%hi(1000000), %o2
	srlx	%o5, 32, %o5
	or	%o2, %lo(1000000), %o2
	add	%o5, %o1, %o5
	cmp	%o5, %o2
	bl,a,pn	%xcc, 1f
	 stx	%o4, [%o0 + 0x0]
	add	%o4, 0x1, %o4
	sub	%o5, %o2, %o5
	stx	%o4, [%o0 + 0x0]
1:	st	%o5, [%o0 + 0x8]");
}

void do_settimeofday(struct timeval *tv)
{
	cli();

	tv->tv_usec -= do_gettimeoffset();
	if(tv->tv_usec < 0) {
		tv->tv_usec += 1000000;
		tv->tv_sec--;
	}

	xtime = *tv;
	time_adjust = 0;		/* stop active adjtime() */
	time_status |= STA_UNSYNC;
	time_state = TIME_ERROR;	/* p. 24, (a) */
	time_maxerror = NTP_PHASE_LIMIT;
	time_esterror = NTP_PHASE_LIMIT;
	sti();
}

static int set_rtc_mmss(unsigned long nowtime)
{
	int real_seconds, real_minutes, mostek_minutes;
	struct mostek48t02 *regs = mstk48t02_regs;

	/* Not having a register set can lead to trouble. */
	if (!regs) 
		return -1;

	/* Read the current RTC minutes. */
	regs->creg |= MSTK_CREG_READ;
	mostek_minutes = MSTK_REG_MIN(regs);
	regs->creg &= ~MSTK_CREG_READ;

	/*
	 * since we're only adjusting minutes and seconds,
	 * don't interfere with hour overflow. This avoids
	 * messing with unknown time zones but requires your
	 * RTC not to be off by more than 15 minutes
	 */
	real_seconds = nowtime % 60;
	real_minutes = nowtime / 60;
	if (((abs(real_minutes - mostek_minutes) + 15)/30) & 1)
		real_minutes += 30;	/* correct for half hour time zone */
	real_minutes %= 60;

	if (abs(real_minutes - mostek_minutes) < 30) {
		regs->creg |= MSTK_CREG_WRITE;
		MSTK_SET_REG_SEC(regs,real_seconds);
		MSTK_SET_REG_MIN(regs,real_minutes);
		regs->creg &= ~MSTK_CREG_WRITE;
	} else
		return -1;

	return 0;
}
