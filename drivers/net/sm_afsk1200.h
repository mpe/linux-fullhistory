/*****************************************************************************/

/*
 *	sm_afsk1200.h  -- soundcard radio modem driver, 1200 baud AFSK modem
 *
 *	Copyright (C) 1996  Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Please note that the GPL allows you to use the driver, NOT the radio.
 *  In order to use the radio, you need a license from the communications
 *  authority of your country.
 *
 */

#include <linux/config.h>

/* --------------------------------------------------------------------- */

struct demod_state_afsk12 {
	unsigned int shreg;
	unsigned int bit_pll;
	unsigned char last_sample;
	unsigned int dcd_shreg;
	int dcd_sum0, dcd_sum1, dcd_sum2;
	unsigned int dcd_time;
	unsigned char last_rxbit;
	union {
		signed char c[8];
		float f[8];
	} filt;
};

struct mod_state_afsk12 {
	unsigned int shreg;
	unsigned char tx_bit;
	unsigned int bit_pll;
};

#define DEMOD_STATE ((struct demod_state_afsk12 *)(&sm->d))
#define MOD_STATE ((struct mod_state_afsk12 *)(&sm->m))

/* --------------------------------------------------------------------- */

static const char sinetab[] = {
	 128,  140,  152,  164,  176,  187,  198,  208,
	 217,  226,  233,  240,  245,  249,  252,  254,
	 255,  254,  252,  249,  245,  240,  233,  226,
	 217,  208,  198,  187,  176,  164,  152,  140,
	 128,  116,  104,   92,   80,   69,   58,   48,
	  39,   30,   23,   16,   11,    7,    4,    2,
	   1,    2,    4,    7,   11,   16,   23,   30,
	  39,   48,   58,   69,   80,   92,  104,  116
};

/* --------------------------------------------------------------------- */

static void modulator_1200(struct sm_state *sm, unsigned char *buf, int buflen)
{
	static const int dds_inc[2] = { 8192, 15019 };
	int j, k;

	for (; buflen >= 8; buflen -= 8) {
		if (MOD_STATE->shreg <= 1)
			MOD_STATE->shreg = hdlcdrv_getbits(&sm->hdrv) | 0x10000;
		MOD_STATE->tx_bit = (MOD_STATE->tx_bit ^ 
				       (!(MOD_STATE->shreg & 1))) & 1;
		MOD_STATE->shreg >>= 1;
		k = dds_inc[MOD_STATE->tx_bit & 1];
		for (j = 0; j < 8; j++) {
			*buf++ = sinetab[(MOD_STATE->bit_pll >> 10) & 0x3f];
			MOD_STATE->bit_pll += k;
		}
	}
}

/* --------------------------------------------------------------------- */

/*
 * should eventually move to an asm header file
 */
#if defined (CONFIG_SOUNDMODEM__AFSK1200_FP) && (defined(CONFIG_M586) || defined(CONFIG_M686))

#define ENV_STORAGE unsigned char fpu_save[108];

#define ENV_SAVE asm("fsave %0;\n\tfninit;\n\t" : "=m" (*fpu_save) : : "memory");
#define ENV_RESTORE asm("frstor %0;\n\t" : : "m" (*fpu_save));

static const float tx_lo_i_f[] = { 
	1.000000, 0.707107, 0.000000, -0.707107, -1.000000, -0.707107, -0.000000, 0.707107 
};

static const float tx_lo_q_f[] = { 
	0.000000, 0.707107, 1.000000, 0.707107, 0.000000, -0.707107, -1.000000, -0.707107 
};

static const float tx_hi_i_f[] = { 
	1.000000, 0.130526, -0.965926, -0.382683, 0.866025, 0.608761, -0.707107, -0.793353 
};

static const float tx_hi_q_f[] = { 
	0.000000, 0.991445, 0.258819, -0.923880, -0.500000, 0.793353, 0.707107, -0.608761 
};

static inline float convolution8(const float *st, const float *coeff)
{
	float f;

	/*
	 * from Phil Karn, KA9Q's home page
	 */
	asm volatile ("flds (%1);\n\t"
	    "fmuls (%2);\n\t"
	    "flds 4(%1);\n\t"
	    "fmuls 4(%2);\n\t"
	    "flds 8(%1);\n\t"
	    "fmuls 8(%2);\n\t"
	    "fxch %%st(2);\n\t"
	    "faddp;\n\t"
	    "flds 12(%1);\n\t"
	    "fmuls 12(%2);\n\t"
	    "fxch %%st(2);\n\t"
	    "faddp;\n\t"
	    "flds 16(%1);\n\t"
	    "fmuls 16(%2);\n\t"
	    "fxch %%st(2);\n\t"
	    "faddp;\n\t"
	    "flds 20(%1);\n\t"
	    "fmuls 20(%2);\n\t"
	    "fxch %%st(2);\n\t"
	    "faddp;\n\t"
	    "flds 24(%1);\n\t"
	    "fmuls 24(%2);\n\t"
	    "fxch %%st(2);\n\t"
	    "faddp;\n\t"
	    "flds 28(%1);\n\t"
	    "fmuls 28(%2);\n\t"
	    "fxch %%st(2);\n\t"
	    "faddp;\n\t"
	    "faddp;\n\t"
	    "fmul %%st(0),%%st;\n\t" :
	    "=t" (f) :
	    "r" (st),
	    "r" (coeff) : "memory");

	return f;
}

static inline int do_filter_1200(struct sm_state *sm, unsigned char newval)
{
	float sum;

	memmove(DEMOD_STATE->filt.f+1, DEMOD_STATE->filt.f,
		sizeof(DEMOD_STATE->filt.f) - sizeof(DEMOD_STATE->filt.f[0]));
	DEMOD_STATE->filt.f[0] = (((int)newval)-0x80);

	sum = convolution8(DEMOD_STATE->filt.f, tx_lo_i);
	sum += convolution8(DEMOD_STATE->filt.f, tx_lo_q);
	sum -= convolution8(DEMOD_STATE->filt.f, tx_hi_i);
	sum -= convolution8(DEMOD_STATE->filt.f, tx_hi_q);
	return sum;
}

#else /* defined (CONFIG_SOUNDMODEM__AFSK1200_FP) && (defined(CONFIG_M586) || defined(CONFIG_M686)) */

#define ENV_STORAGE 
#define ENV_SAVE
#define ENV_RESTORE

static const char tx_lo_i[] = { 127, 89, 0, -89, -127, -89, 0, 89 };
static const char tx_lo_q[] = { 0, 89, 127, 89, 0, -89, -127, -89 };
static const char tx_hi_i[] = { 127, 16, -122, -48, 109, 77, -89, -100 };
static const char tx_hi_q[] = { 0, 125, 32, -117, -63, 100, 89, -77 };

static inline void datamove8(signed char *st, unsigned char newval)
{
	memmove(st+1, st, 7);
	*st = newval - 0x80;
}

static inline int convolution8(const signed char *st, const signed char *coeff)
{
	int sum = (st[0] * coeff[0]);
	
	sum += (st[1] * coeff[1]);
	sum += (st[2] * coeff[2]);
	sum += (st[3] * coeff[3]);
	sum += (st[4] * coeff[4]);
	sum += (st[5] * coeff[5]);
	sum += (st[6] * coeff[6]);
	sum += (st[7] * coeff[7]);

	sum >>= 7;
	return sum * sum;
}

static inline int do_filter_1200(struct sm_state *sm, unsigned char newval)
{
	int sum;

	datamove8(DEMOD_STATE->filt.c, newval);
	
	sum = convolution8(DEMOD_STATE->filt.c, tx_lo_i);
	sum += convolution8(DEMOD_STATE->filt.c, tx_lo_q);
	sum -= convolution8(DEMOD_STATE->filt.c, tx_hi_i);
	sum -= convolution8(DEMOD_STATE->filt.c, tx_hi_q);
	return sum;
}

#endif /* defined (CONFIG_SOUNDMODEM__AFSK1200_FP) && (defined(CONFIG_M586) || defined(CONFIG_M686)) */


/* --------------------------------------------------------------------- */

static void demodulator_1200(struct sm_state *sm, unsigned char *buf, int buflen)
{
	static const int pll_corr[2] = { -0x1000, 0x1000 };
	int j;
	int sum;
	unsigned char newsample;
	ENV_STORAGE;

	ENV_SAVE;
	for (; buflen > 0; buflen--, buf++) {
		sum = do_filter_1200(sm, *buf);
		DEMOD_STATE->dcd_shreg <<= 1;
		DEMOD_STATE->bit_pll += 0x2000;
		newsample = (sum > 0);
		if (DEMOD_STATE->last_sample ^ newsample) {
			DEMOD_STATE->last_sample = newsample;
			DEMOD_STATE->dcd_shreg |= 1;
			DEMOD_STATE->bit_pll += pll_corr
				[DEMOD_STATE->bit_pll < 0x9000];
			j = 4 * hweight8(DEMOD_STATE->dcd_shreg & 0x38)
				- hweight16(DEMOD_STATE->dcd_shreg & 0x7c0);
			DEMOD_STATE->dcd_sum0 += j;
		}
		hdlcdrv_channelbit(&sm->hdrv, DEMOD_STATE->last_sample);
		if ((--DEMOD_STATE->dcd_time) <= 0) {
			hdlcdrv_setdcd(&sm->hdrv, (DEMOD_STATE->dcd_sum0 + 
						   DEMOD_STATE->dcd_sum1 + 
						   DEMOD_STATE->dcd_sum2) < 0);
			DEMOD_STATE->dcd_sum2 = DEMOD_STATE->dcd_sum1;
			DEMOD_STATE->dcd_sum1 = DEMOD_STATE->dcd_sum0;
			DEMOD_STATE->dcd_sum0 = 2; /* slight bias */
			DEMOD_STATE->dcd_time = 120;
		}
		if (DEMOD_STATE->bit_pll >= 0x10000) {
			DEMOD_STATE->bit_pll &= 0xffff;
			DEMOD_STATE->shreg >>= 1;
			DEMOD_STATE->shreg |= (!(DEMOD_STATE->last_rxbit ^
						 DEMOD_STATE->last_sample)) << 16;
			DEMOD_STATE->last_rxbit = DEMOD_STATE->last_sample;
			diag_trigger(sm);
			if (DEMOD_STATE->shreg & 1) {
				hdlcdrv_putbits(&sm->hdrv, DEMOD_STATE->shreg >> 1);
				DEMOD_STATE->shreg = 0x10000;
			}
		}
		diag_add(sm, (((int)*buf)-0x80) << 8, sum);
	}
	ENV_RESTORE;
}

/* --------------------------------------------------------------------- */

static void demod_init_1200(struct sm_state *sm)
{
       	DEMOD_STATE->dcd_time = 120;
	DEMOD_STATE->dcd_sum0 = 2;	
}

/* --------------------------------------------------------------------- */

static const struct modem_tx_info afsk1200_tx = {
	NEXT_TX_INFO, "afsk1200", sizeof(struct mod_state_afsk12), 9600, 1200, 8, 
	modulator_1200, NULL
};
#undef NEXT_TX_INFO
#define NEXT_TX_INFO (&afsk1200_tx)

static const struct modem_rx_info afsk1200_rx = {
	NEXT_RX_INFO, "afsk1200", sizeof(struct demod_state_afsk12), 9600, 1200, 8, 8, 
	demodulator_1200, demod_init_1200
};
#undef NEXT_RX_INFO
#define NEXT_RX_INFO (&afsk1200_rx)

/* --------------------------------------------------------------------- */

#undef DEMOD_STATE
#undef MOD_STATE
