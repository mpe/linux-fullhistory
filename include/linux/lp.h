/*
$Header: /usr/src/linux/include/linux/lp.h,v 1.2 1992/01/21 23:59:24 james_r_wiegand Exp james_r_wiegand $
*/

#include <errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/io.h>
#include <asm/segment.h>

/*
 * usr/include/linux/lp.h c.1991-1992 James Wiegand
 */

/*
 * caveat: my machine only has 1 printer @ lpt2 so lpt1 & lpt3 are 
 * implemented but UNTESTED
 */

/*
 * Per POSIX guidelines, this module reserves the LP and lp prefixes
 */
#define LP_EXIST 0x0001
#define LP_SELEC 0x0002
#define LP_BUSY	 0x0004
#define LP_OFFL	 0x0008
#define LP_NOPA  0x0010
#define LP_ERR   0x0020

#define LP_TIMEOUT 200000

#define LP_B(minor)	lp_table[(minor)].base
#define LP_F(minor)	lp_table[(minor)].flags
#define LP_S(minor)	inb(LP_B((minor)) + 1)

/* 
since we are dealing with a horribly slow device
I don't see the need for a queue
*/
#ifndef __LP_C__
	extern
#endif
struct lp_struct {
	int base;
	int flags;
};

/* 
 * the BIOS manuals say there can be up to 4 lpt devices
 * but I have not seen a board where the 4th address is listed
 * if you have different hardware change the table below 
 * please let me know if you have different equipment
 * if you have more than 3 printers, remember to increase LP_NO
 */
#ifndef __LP_C__
	extern
#endif   
struct lp_struct lp_table[] = {
	{ 0x3bc, 0, },
	{ 0x378, 0, },
	{ 0x278, 0, }
}; 

#define LP_NO 3

/* 
 * bit defines for 8255 status port
 * base + 1
 */
#define LP_PBUSY	0x80 /* active low */
#define LP_PACK		0x40 /* active low */
#define LP_POUTPA	0x20
#define LP_PSELECD	0x10
#define LP_PERRORP	0x08 /*å active low*/
#define LP_PIRQ		0x04 /* active low */

/* 
 * defines for 8255 control port
 * base + 2 
 */
#define LP_PIRQEN	0x10
#define LP_PSELECP	0x08
#define LP_PINITP	0x04  /* active low */
#define LP_PAUTOLF	0x02
#define LP_PSTROBE	0x01

/* 
 * the value written to ports to test existence. PC-style ports will 
 * return the value written. AT-style ports will return 0. so why not
 * make them the same ? 
 */
#define LP_DUMMY	0x00

/*
 * this is the port delay time. your mileage may vary
 */
#define LP_DELAY 	150000

/*
 * function prototypes
 */

extern long lp_init(long);
