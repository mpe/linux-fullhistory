/* ------------------------------------------------------------------------- */
/* i2c-philips-par.c i2c-hw access for philips style parallel port adapters  */
/* ------------------------------------------------------------------------- */
/*   Copyright (C) 1995-2000 Simon G. Vogl

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.		     */
/* ------------------------------------------------------------------------- */ 

/* With some changes from Kyösti Mälkki <kmalkki@cc.hut.fi> and even
   Frodo Looijaard <frodol@dds.nl> */

/* $Id: i2c-philips-par.c,v 1.16 2000/01/18 23:54:07 frodo Exp $ */

#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/init.h>
#include <asm/io.h>
#include <linux/stddef.h>

#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>

#define DEFAULT_BASE 0x378
static int base=0;

/* Note: all we need to know is the base address of the parallel port, so
 * instead of having a dedicated struct to store this value, we store this
 * int in the pointer field (=bit_lp_ops.data) itself.
 */

/* Note2: as the hw that implements the i2c bus on the parallel port is 
 * incompatible with other epp stuff etc., we access the port exclusively
 * and don't cooperate with parport functions.
 */

/* ----- global defines -----------------------------------------------	*/
#define DEB(x)		/* should be reasonable open, close &c. 	*/
#define DEB2(x) 	/* low level debugging - very slow 		*/
#define DEBE(x)	x	/* error messages 				*/

/* ----- printer port defines ------------------------------------------*/
					/* Pin Port  Inverted	name	*/
#define I2C_ON		0x20		/* 12 status N	paper		*/
					/* ... only for phil. not used  */
#define I2C_SDA		0x80		/*  9 data   N	data7		*/
#define I2C_SCL		0x08		/* 17 ctrl   N	dsel		*/

#define I2C_SDAIN	0x80		/* 11 stat   Y	busy		*/
#define I2C_SCLIN	0x08		/* 15 stat   Y	enable		*/

#define I2C_DMASK	0x7f
#define I2C_CMASK	0xf7

/* --- Convenience defines for the parallel port:			*/
#define BASE	(unsigned int)(data)
#define DATA	BASE			/* Centronics data port		*/
#define STAT	(BASE+1)		/* Centronics status port	*/
#define CTRL	(BASE+2)		/* Centronics control port	*/

/* ----- local functions ----------------------------------------------	*/

static void bit_lp_setscl(void *data, int state)
{
	/*be cautious about state of the control register - 
		touch only the one bit needed*/
	if (state) {
		outb(inb(CTRL)|I2C_SCL,   CTRL);
	} else {
		outb(inb(CTRL)&I2C_CMASK, CTRL);
	}
}

static void bit_lp_setsda(void *data, int state)
{
	if (state) {
		outb(I2C_DMASK , DATA);
	} else {
		outb(I2C_SDA , DATA);
	}
}

static int bit_lp_getscl(void *data)
{
	return ( 0 != ( (inb(STAT)) & I2C_SCLIN ) );
}

static int bit_lp_getsda(void *data)
{
	return ( 0 != ( (inb(STAT)) & I2C_SDAIN ) );
}

static int bit_lp_init(void)
{
	if (check_region(base,(base == 0x3bc)? 3 : 8) < 0 ) {
		return -ENODEV;
	} else {
		request_region(base,(base == 0x3bc)? 3 : 8,
			"i2c (parallel port adapter)");
		/* reset hardware to sane state */
		bit_lp_setsda((void*)base,1);
		bit_lp_setscl((void*)base,1);
	}
	return 0;
}

static void bit_lp_exit(void)
{
	release_region( base , (base == 0x3bc)? 3 : 8 );
}

static int bit_lp_reg(struct i2c_client *client)
{
	return 0;
}

static int bit_lp_unreg(struct i2c_client *client)
{
	return 0;
}

static void bit_lp_inc_use(struct i2c_adapter *adap)
{
	MOD_INC_USE_COUNT;
}

static void bit_lp_dec_use(struct i2c_adapter *adap)
{
	MOD_DEC_USE_COUNT;
}

/* ------------------------------------------------------------------------
 * Encapsulate the above functions in the correct operations structure.
 * This is only done when more than one hardware adapter is supported.
 */
 
static struct i2c_algo_bit_data bit_lp_data = {
	NULL,
	bit_lp_setsda,
	bit_lp_setscl,
	bit_lp_getsda,
	bit_lp_getscl,
	80, 80, 100,		/*	waits, timeout */
}; 

static struct i2c_adapter bit_lp_ops = {
	"Philips Parallel port adapter",
	I2C_HW_B_LP,
	NULL,
	&bit_lp_data,
	bit_lp_inc_use,
	bit_lp_dec_use,
	bit_lp_reg,
	bit_lp_unreg,
};


int __init i2c_bitlp_init(void)
{
	printk("i2c-philips-par.o: i2c Philips parallel port adapter module\n");
	if (base==0) {
		/* probe some values */
		base=DEFAULT_BASE;
		bit_lp_data.data=(void*)DEFAULT_BASE;
		if (bit_lp_init()==0) {
			if (i2c_bit_add_bus(&bit_lp_ops) < 0)
				return -ENODEV;
		} else {
			return -ENODEV;
		}
	} else {
		bit_lp_data.data=(void*)base;
		if (bit_lp_init()==0) {
			if (i2c_bit_add_bus(&bit_lp_ops) < 0)
				return -ENODEV;
		} else {
			return -ENODEV;
		}
	}
	printk("i2c-philips-par.o: found device at %#x.\n",base);
	return 0;
}

EXPORT_NO_SYMBOLS;

#ifdef MODULE
MODULE_AUTHOR("Simon G. Vogl <simon@tk.uni-linz.ac.at>");
MODULE_DESCRIPTION("I2C-Bus adapter routines for Philips parallel port adapter");

MODULE_PARM(base, "i");

int init_module(void) 
{
	return i2c_bitlp_init();
}

void cleanup_module(void) 
{
	i2c_bit_del_bus(&bit_lp_ops);
	bit_lp_exit();
}

#endif



