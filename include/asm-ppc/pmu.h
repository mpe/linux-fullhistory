/*
 * Definitions for talking to the PMU.  The PMU is a microcontroller
 * which controls battery charging and system power on PowerBook 3400
 * and 2400 models as well as the RTC and various other things.
 *
 * Copyright (C) 1998 Paul Mackerras.
 */

/*
 * PMU commands
 */
#define PMU_BACKLIGHT_CTRL	0x11	/* control backlight */
#define PMU_ADB_CMD		0x20	/* send ADB packet */
#define PMU_ADB_POLL_OFF	0x21	/* disable ADB auto-poll */
#define PMU_WRITE_NVRAM		0x33	/* write non-volatile RAM */
#define PMU_READ_NVRAM		0x3b	/* read non-volatile RAM */
#define PMU_SET_RTC		0x30	/* set real-time clock */
#define PMU_READ_RTC		0x38	/* read real-time clock */
#define PMU_SET_VOLBUTTON	0x40	/* set volume up/down position */
#define PMU_BACKLIGHT_BRIGHT	0x41	/* set backlight brightness */
#define PMU_GET_VOLBUTTON	0x48	/* get volume up/down position */
#define PMU_PCEJECT		0x4c	/* eject PC-card from slot */
#define PMU_BATTERY_STATE	0x6b	/* report battery state etc. */
#define PMU_SET_INTR_MASK	0x70	/* set PMU interrupt mask */
#define PMU_INT_ACK		0x78	/* read interrupt bits */
#define PMU_SHUTDOWN		0x7e	/* turn power off */
#define PMU_SLEEP		0x7f	/* put CPU to sleep */
#define PMU_RESET		0xd0	/* reset CPU */
#define PMU_GET_BRIGHTBUTTON	0xd9	/* report brightness up/down pos */
#define PMU_GET_COVER		0xdc	/* report cover open/closed */

/* Bits in PMU interrupt and interrupt mask bytes */
#define PMU_INT_ADB_AUTO	0x04	/* ADB autopoll, when PMU_INT_ADB */
#define PMU_INT_PCEJECT		0x04	/* PC-card eject buttons */
#define PMU_INT_SNDBRT		0x08	/* sound/brightness up/down buttons */
#define PMU_INT_ADB		0x10	/* ADB autopoll or reply data */
#define PMU_INT_TICK		0x80	/* 1-second tick interrupt */


/*
 * Ioctl commands for the /dev/pmu device
 */
#include <linux/ioctl.h>

#define PMU_IOC_SLEEP		_IO('B', 0)

#ifdef __KERNEL__
void find_via_pmu(void);
void via_pmu_init(void);
int pmu_request(struct adb_request *req,
		void (*done)(struct adb_request *), int nbytes, ...);
int pmu_send_request(struct adb_request *req);
void pmu_poll(void);

void pmu_enable_backlight(int on);

void pmu_restart(void);
void pmu_shutdown(void);

/*
 * Stuff for putting the powerbook to sleep and waking it again.
 */
#include <linux/notifier.h>

extern struct notifier_block *sleep_notifier_list;

/* code values for calling sleep/wakeup handlers */
#define PBOOK_SLEEP	1
#define PBOOK_WAKE	2

#endif	/* __KERNEL */
