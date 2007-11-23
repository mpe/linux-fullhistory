#ifndef _TIMER_H
#define _TIMER_H

/*
 * DON'T CHANGE THESE!! Most of them are hardcoded into some assembly language
 * as well as being defined here.
 */

/*
 * The timers are:
 *
 * BLANK_TIMER		console screen-saver timer
 *
 * BEEP_TIMER		console beep timer
 *
 * SERx_TIMER		serial incoming characters timer
 *
 * SERx_TIMEOUT		timeout for serial writes
 *
 * HD_TIMER		harddisk timer
 *
 * FLOPPY_TIMER		floppy disk timer (not used right now)
 * 
 * SCSI_TIMER		scsi.c timeout timer
 */

#define BLANK_TIMER	0
#define BEEP_TIMER	1

#define SER1_TIMER	2
#define SER2_TIMER	3
#define SER3_TIMER	4
#define SER4_TIMER	5

#define SER1_TIMEOUT	8
#define SER2_TIMEOUT	9
#define SER3_TIMEOUT	10
#define SER4_TIMEOUT	11

#define HD_TIMER	16
#define FLOPPY_TIMER	17
#define SCSI_TIMER 	18

struct timer_struct {
	unsigned long expires;
	void (*fn)(void);
};

extern unsigned long timer_active;
extern struct timer_struct timer_table[32];

#endif
