/*
 *  Driver for Zarlink DVB-T MT352 demodulator
 *
 *  Written by Holger Waechtler <holger@qanu.de>
 *	 and Daniel Mack <daniel@qanu.de>
 *
 *  AVerMedia AVerTV DVB-T 771 support by
 *       Wolfram Joost <dbox2@frokaschwei.de>
 *
 *  Support for Samsung TDTC9251DH01C(M) tuner
 *  Copyright (C) 2004 Antonio Mancuso <antonio.mancuso@digitaltelevision.it>
 *                     Amauri  Celani  <acelani@essegi.net>
 *
 *  DVICO FusionHDTV DVB-T1 and DVICO FusionHDTV DVB-T Lite support by
 *       Christopher Pascoe <c.pascoe@itee.uq.edu.au>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.=
 */

#ifndef MT352_H
#define MT352_H

#include <linux/dvb/frontend.h>

struct mt352_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/* Initialise the demodulator and PLL. Cannot be NULL */
	int (*demod_init)(struct dvb_frontend* fe);

	/* PLL setup - fill out the supplied 5 byte buffer with your PLL settings.
	 * byte0: Set to pll i2c address (nonlinux; left shifted by 1)
	 * byte1-4: PLL configuration.
	 */
	int (*pll_set)(struct dvb_frontend* fe, struct dvb_frontend_parameters* params, u8* pllbuf);
};

extern struct dvb_frontend* mt352_attach(const struct mt352_config* config,
					 struct i2c_adapter* i2c);

extern int mt352_write(struct dvb_frontend* fe, u8* ibuf, int ilen);
extern u8 mt352_read(struct dvb_frontend *fe, u8 reg);

#endif // MT352_H
