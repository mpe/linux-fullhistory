/*
   Driver for the Spase sp887x demodulator
*/

#ifndef SP887X_H
#define SP887X_H

#include <linux/dvb/frontend.h>
#include <linux/firmware.h>

struct sp887x_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/* PLL maintenance */
	int (*pll_init)(struct dvb_frontend* fe);

	/* this should return the actual frequency tuned to */
	int (*pll_set)(struct dvb_frontend* fe, struct dvb_frontend_parameters* params);

	/* request firmware for device */
	int (*request_firmware)(struct dvb_frontend* fe, const struct firmware **fw, char* name);
};

extern struct dvb_frontend* sp887x_attach(const struct sp887x_config* config,
					  struct i2c_adapter* i2c);

#endif // SP887X_H
