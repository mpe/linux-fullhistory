/* 
 * dvb_frontend.h
 *
 * Copyright (C) 2001 convergence integrated media GmbH
 * Copyright (C) 2004 convergence GmbH
 *
 * Written by Ralph Metzler
 * Overhauled by Holger Waechtler
 * Kernel I2C stuff by Michael Hunold <hunold@convergence.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *

 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#ifndef _DVB_FRONTEND_H_
#define _DVB_FRONTEND_H_

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/ioctl.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/delay.h>

#include <linux/dvb/frontend.h>

#include "dvbdev.h"

/* FIXME: Move to i2c-id.h */
#define I2C_DRIVERID_DVBFE_SP8870	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_CX22700	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_AT76C651	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_CX24110	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_CX22702	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_DIB3000MB	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_DST		I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_DUMMY	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_L64781	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_MT312	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_MT352	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_NXT6000	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_SP887X	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_STV0299	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_TDA1004X	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_TDA8083	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_VES1820	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_VES1X93	I2C_DRIVERID_EXP2
#define I2C_DRIVERID_DVBFE_TDA80XX	I2C_DRIVERID_EXP2


struct dvb_frontend_tune_settings {
        int min_delay_ms;
        int step_size;
        int max_drift;
        struct dvb_frontend_parameters parameters;
};

struct dvb_frontend;

struct dvb_frontend_ops {

	struct dvb_frontend_info info;

	void (*release)(struct dvb_frontend* fe);

	int (*init)(struct dvb_frontend* fe);
	int (*sleep)(struct dvb_frontend* fe);

	int (*set_frontend)(struct dvb_frontend* fe, struct dvb_frontend_parameters* params);
	int (*get_frontend)(struct dvb_frontend* fe, struct dvb_frontend_parameters* params);
	int (*get_tune_settings)(struct dvb_frontend* fe, struct dvb_frontend_tune_settings* settings);

	int (*read_status)(struct dvb_frontend* fe, fe_status_t* status);
	int (*read_ber)(struct dvb_frontend* fe, u32* ber);
	int (*read_signal_strength)(struct dvb_frontend* fe, u16* strength);
	int (*read_snr)(struct dvb_frontend* fe, u16* snr);
	int (*read_ucblocks)(struct dvb_frontend* fe, u32* ucblocks);

	int (*diseqc_reset_overload)(struct dvb_frontend* fe);
	int (*diseqc_send_master_cmd)(struct dvb_frontend* fe, struct dvb_diseqc_master_cmd* cmd);
	int (*diseqc_recv_slave_reply)(struct dvb_frontend* fe, struct dvb_diseqc_slave_reply* reply);
	int (*diseqc_send_burst)(struct dvb_frontend* fe, fe_sec_mini_cmd_t minicmd);
	int (*set_tone)(struct dvb_frontend* fe, fe_sec_tone_mode_t tone);
	int (*set_voltage)(struct dvb_frontend* fe, fe_sec_voltage_t voltage);
	int (*enable_high_lnb_voltage)(struct dvb_frontend* fe, int arg);
	int (*dishnetwork_send_legacy_command)(struct dvb_frontend* fe, unsigned int cmd);
};

#define MAX_EVENT 8

struct dvb_fe_events {
	struct dvb_frontend_event events[MAX_EVENT];
	int			  eventw;
	int			  eventr;
	int			  overflow;
	wait_queue_head_t	  wait_queue;
	struct semaphore	  sem;
};

struct dvb_frontend {
	struct dvb_frontend_ops* ops;
	struct dvb_adapter *dvb;
	void* demodulator_priv;
	void* frontend_priv;
};

extern int dvb_register_frontend(struct dvb_adapter* dvb,
				 struct dvb_frontend* fe);

extern int dvb_unregister_frontend(struct dvb_frontend* fe);

#endif
