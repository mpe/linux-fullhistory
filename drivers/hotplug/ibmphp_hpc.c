/*
 * IBM Hot Plug Controller Driver
 *
 * Written By: Jyoti Shah, IBM Corporation
 *
 * Copyright (c) 2001,2001 IBM Corp.
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <gregkh@us.ibm.com>
 *                  <jshah@us.ibm.com>
 *
 */

//#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/smp_lock.h>
#include "ibmphp.h"

#define POLL_NO		0x01
#define POLL_YES	0x00

static int to_debug = FALSE;
#define debug_polling(fmt, arg...)	do { if (to_debug) debug (fmt, arg); } while (0)

//----------------------------------------------------------------------------
// timeout values
//----------------------------------------------------------------------------
#define CMD_COMPLETE_TOUT_SEC	60	// give HPC 60 sec to finish cmd
#define HPC_CTLR_WORKING_TOUT	60	// give HPC 60 sec to finish cmd
#define HPC_GETACCESS_TIMEOUT	60	// seconds
#define POLL_INTERVAL_SEC	2	// poll HPC every 2 seconds
#define POLL_LATCH_CNT		5	// poll latch 5 times, then poll slots

//----------------------------------------------------------------------------
// Winnipeg Architected Register Offsets
//----------------------------------------------------------------------------
#define WPG_I2CMBUFL_OFFSET	0x08	// I2C Message Buffer Low
#define WPG_I2CMOSUP_OFFSET	0x10	// I2C Master Operation Setup Reg
#define WPG_I2CMCNTL_OFFSET	0x20	// I2C Master Control Register
#define WPG_I2CPARM_OFFSET	0x40	// I2C Parameter Register
#define WPG_I2CSTAT_OFFSET	0x70	// I2C Status Register

//----------------------------------------------------------------------------
// Winnipeg Store Type commands (Add this commands to the register offset)
//----------------------------------------------------------------------------
#define WPG_I2C_AND		0x1000	// I2C AND operation
#define WPG_I2C_OR		0x2000	// I2C OR operation

//----------------------------------------------------------------------------
// Command set for I2C Master Operation Setup Regisetr
//----------------------------------------------------------------------------
#define WPG_READATADDR_MASK	0x00010000	// read,bytes,I2C shifted,index
#define WPG_WRITEATADDR_MASK	0x40010000	// write,bytes,I2C shifted,index
#define WPG_READDIRECT_MASK	0x10010000
#define WPG_WRITEDIRECT_MASK	0x60010000


//----------------------------------------------------------------------------
// bit masks for I2C Master Control Register
//----------------------------------------------------------------------------
#define WPG_I2CMCNTL_STARTOP_MASK	0x00000002	// Start the Operation

//----------------------------------------------------------------------------
//
//----------------------------------------------------------------------------
#define WPG_I2C_IOREMAP_SIZE	0x2044	// size of linear address interval
#define WPG_CTLR_MAX		0x01	// max controllers
#define WPG_SLOT_MAX		0x06	// max slots
#define WPG_CTLR_SLOT_MAX	0x06	// max slots per controller
#define WPG_FIRST_CTLR		0x00	// index of the controller

//----------------------------------------------------------------------------
// command index
//----------------------------------------------------------------------------
#define WPG_1ST_SLOT_INDEX	0x01	// index - 1st slot for ctlr
#define WPG_CTLR_INDEX		0x0F	// index - ctlr
#define WPG_1ST_EXTSLOT_INDEX	0x10	// index - 1st ext slot for ctlr
#define WPG_1ST_BUS_INDEX	0x1F	// index - 1st bus for ctlr

//----------------------------------------------------------------------------
// macro utilities
//----------------------------------------------------------------------------
// if bits 20,22,25,26,27,29,30 are OFF return TRUE
#define HPC_I2CSTATUS_CHECK(s)	((u8)((s & 0x00000A76) ? FALSE : TRUE))

// return code 0:poll slots, 1-POLL_LATCH_CNT:poll latch register
#define INCREMENT_POLLCNT(i)	((i < POLL_LATCH_CNT) ? i++ : (i=0))
//----------------------------------------------------------------------------
// global variables
//----------------------------------------------------------------------------
static int ibmphp_shutdown;
static int tid_poll;
static int stop_polling;		// 2 values: poll, don't poll
static struct semaphore sem_hpcaccess;	// lock access to HPC
static struct semaphore semOperations;	// lock all operations and
					// access to data structures
static struct semaphore sem_exit;	// make sure polling thread goes away
static struct semaphore sem_poll;	// make sure poll is idle 
//----------------------------------------------------------------------------
// local function prototypes
//----------------------------------------------------------------------------
static u8 ctrl_read (struct controller *, void *, u8);
static u8 ctrl_write (struct controller *, void *, u8, u8);
static u8 hpc_writecmdtoindex (u8, u8);
static u8 hpc_readcmdtoindex (u8, u8);
static void get_hpc_access (void);
static void free_hpc_access (void);
static void poll_hpc (void);
static int update_slot (struct slot *, u8);
static int process_changeinstatus (struct slot *, struct slot *);
static int process_changeinlatch (u8, u8);
static int hpc_poll_thread (void *);
static int hpc_wait_ctlr_notworking (int, struct controller *, void *, u8 *);
//----------------------------------------------------------------------------


/*----------------------------------------------------------------------
* Name:    ibmphp_hpc_initvars
*
* Action:  initialize semaphores and variables
*---------------------------------------------------------------------*/
void ibmphp_hpc_initvars (void)
{
	debug ("%s - Entry\n", __FUNCTION__);

	init_MUTEX (&sem_hpcaccess);
	init_MUTEX (&semOperations);
	init_MUTEX_LOCKED (&sem_exit);
	init_MUTEX_LOCKED (&sem_poll);
	stop_polling = POLL_YES;
	to_debug = FALSE;
	ibmphp_shutdown = FALSE;
	tid_poll = 0;

	debug ("%s - Exit\n", __FUNCTION__);
}

/*----------------------------------------------------------------------
* Name:    ctrl_read
*
* Action:  read from HPC over I2C
*
*---------------------------------------------------------------------*/
static u8 ctrl_read (struct controller *ctlr_ptr, void *WPGBbar, u8 index)
{
	u8 status;
	int i;
	void *wpg_addr;		// base addr + offset
	ulong wpg_data,		// data to/from WPG LOHI format
	ultemp, data;		// actual data HILO format


	debug_polling ("%s - Entry WPGBbar[%lx] index[%x] \n", __FUNCTION__, (ulong) WPGBbar, index);

	//--------------------------------------------------------------------
	// READ - step 1
	// read at address, byte length, I2C address (shifted), index
	// or read direct, byte length, index
	if (ctlr_ptr->ctlr_type == 0x02) {
		data = WPG_READATADDR_MASK;
		// fill in I2C address
		ultemp = (ulong) ctlr_ptr->u.wpeg_ctlr.i2c_addr;
		ultemp = ultemp >> 1;
		data |= (ultemp << 8);

		// fill in index
		data |= (ulong) index;
	} else if (ctlr_ptr->ctlr_type == 0x04) {
		data = WPG_READDIRECT_MASK;

		// fill in index
		ultemp = (ulong) index;
		ultemp = ultemp << 8;
		data |= ultemp;
	} else {
		err ("this controller type is not supported \n");
		return HPC_ERROR;
	}

	wpg_data = swab32 (data);	// swap data before writing
	(ulong) wpg_addr = (ulong) WPGBbar + (ulong) WPG_I2CMOSUP_OFFSET;
	writel (wpg_data, wpg_addr);

	//--------------------------------------------------------------------
	// READ - step 2 : clear the message buffer
	data = 0x00000000;
	wpg_data = swab32 (data);
	(ulong) wpg_addr = (ulong) WPGBbar + (ulong) WPG_I2CMBUFL_OFFSET;
	writel (wpg_data, wpg_addr);

	//--------------------------------------------------------------------
	// READ - step 3 : issue start operation, I2C master control bit 30:ON
	//                 2020 : [20] OR operation at [20] offset 0x20
	data = WPG_I2CMCNTL_STARTOP_MASK;
	wpg_data = swab32 (data);
	(ulong) wpg_addr = (ulong) WPGBbar + (ulong) WPG_I2CMCNTL_OFFSET + (ulong) WPG_I2C_OR;
	writel (wpg_data, wpg_addr);

	//--------------------------------------------------------------------
	// READ - step 4 : wait until start operation bit clears
	i = CMD_COMPLETE_TOUT_SEC;
	while (i) {
		long_delay (1 * HZ / 100);
		(ulong) wpg_addr = (ulong) WPGBbar + (ulong) WPG_I2CMCNTL_OFFSET;
		wpg_data = readl (wpg_addr);
		data = swab32 (wpg_data);
		if (!(data & WPG_I2CMCNTL_STARTOP_MASK))
			break;
		i--;
	}
	if (i == 0) {
		debug ("%s - Error : WPG timeout\n", __FUNCTION__);
		return HPC_ERROR;
	}
	//--------------------------------------------------------------------
	// READ - step 5 : read I2C status register
	i = CMD_COMPLETE_TOUT_SEC;
	while (i) {
		long_delay (1 * HZ / 100);
		(ulong) wpg_addr = (ulong) WPGBbar + (ulong) WPG_I2CSTAT_OFFSET;
		wpg_data = readl (wpg_addr);
		data = swab32 (wpg_data);
		if (HPC_I2CSTATUS_CHECK (data))
			break;
		i--;
	}
	if (i == 0) {
		debug ("ctrl_read - Exit Error:I2C timeout\n");
		return HPC_ERROR;
	}

	//--------------------------------------------------------------------
	// READ - step 6 : get DATA
	(ulong) wpg_addr = (ulong) WPGBbar + (ulong) WPG_I2CMBUFL_OFFSET;
	wpg_data = readl (wpg_addr);
	data = swab32 (wpg_data);

	status = (u8) data;

	debug_polling ("%s - Exit index[%x] status[%x]\n", __FUNCTION__, index, status);

	return (status);
}

/*----------------------------------------------------------------------
* Name:    ctrl_write
*
* Action:  write to HPC over I2C
*
* Return   0 or error codes
*---------------------------------------------------------------------*/
static u8 ctrl_write (struct controller *ctlr_ptr, void *WPGBbar, u8 index, u8 cmd)
{
	u8 rc;
	void *wpg_addr;		// base addr + offset
	ulong wpg_data,		// data to/from WPG LOHI format 
	ultemp, data;		// actual data HILO format
	int i;


	debug_polling ("%s - Entry WPGBbar[%lx] index[%x] cmd[%x]\n",
		       __FUNCTION__, (ulong) WPGBbar, index, cmd);

	rc = 0;
	//--------------------------------------------------------------------
	// WRITE - step 1
	// write at address, byte length, I2C address (shifted), index
	// or write direct, byte length, index
	data = 0x00000000;

	if (ctlr_ptr->ctlr_type == 0x02) {
		data = WPG_WRITEATADDR_MASK;
		// fill in I2C address
		ultemp = (ulong) ctlr_ptr->u.wpeg_ctlr.i2c_addr;
		ultemp = ultemp >> 1;
		data |= (ultemp << 8);

		// fill in index
		data |= (ulong) index;
	} else if (ctlr_ptr->ctlr_type == 0x04) {
		data = WPG_WRITEDIRECT_MASK;

		// fill in index
		ultemp = (ulong) index;
		ultemp = ultemp << 8;
		data |= ultemp;
	} else {
		err ("this controller type is not supported \n");
		return HPC_ERROR;
	}

	wpg_data = swab32 (data);	// swap data before writing
	(ulong) wpg_addr = (ulong) WPGBbar + (ulong) WPG_I2CMOSUP_OFFSET;
	writel (wpg_data, wpg_addr);

	//--------------------------------------------------------------------
	// WRITE - step 2 : clear the message buffer
	data = 0x00000000 | (ulong) cmd;
	wpg_data = swab32 (data);
	(ulong) wpg_addr = (ulong) WPGBbar + (ulong) WPG_I2CMBUFL_OFFSET;
	writel (wpg_data, wpg_addr);

	//--------------------------------------------------------------------
	// WRITE - step 3 : issue start operation,I2C master control bit 30:ON
	//                 2020 : [20] OR operation at [20] offset 0x20
	data = WPG_I2CMCNTL_STARTOP_MASK;
	wpg_data = swab32 (data);
	(ulong) wpg_addr = (ulong) WPGBbar + (ulong) WPG_I2CMCNTL_OFFSET + (ulong) WPG_I2C_OR;
	writel (wpg_data, wpg_addr);

	//--------------------------------------------------------------------
	// WRITE - step 4 : wait until start operation bit clears
	i = CMD_COMPLETE_TOUT_SEC;
	while (i) {
		long_delay (1 * HZ / 100);
		(ulong) wpg_addr = (ulong) WPGBbar + (ulong) WPG_I2CMCNTL_OFFSET;
		wpg_data = readl (wpg_addr);
		data = swab32 (wpg_data);
		if (!(data & WPG_I2CMCNTL_STARTOP_MASK))
			break;
		i--;
	}
	if (i == 0) {
		debug ("%s - Exit Error:WPG timeout\n", __FUNCTION__);
		rc = HPC_ERROR;
	}

	//--------------------------------------------------------------------
	// WRITE - step 5 : read I2C status register
	i = CMD_COMPLETE_TOUT_SEC;
	while (i) {
		long_delay (1 * HZ / 100);
		(ulong) wpg_addr = (ulong) WPGBbar + (ulong) WPG_I2CSTAT_OFFSET;
		wpg_data = readl (wpg_addr);
		data = swab32 (wpg_data);
		if (HPC_I2CSTATUS_CHECK (data))
			break;
		i--;
	}
	if (i == 0) {
		debug ("ctrl_read - Error : I2C timeout\n");
		rc = HPC_ERROR;
	}

	debug_polling ("%s Exit rc[%x]\n", __FUNCTION__, rc);
	return (rc);
}

/*----------------------------------------------------------------------
* Name:    hpc_writecmdtoindex()
*
* Action:  convert a write command to proper index within a controller
*
* Return   index, HPC_ERROR
*---------------------------------------------------------------------*/
static u8 hpc_writecmdtoindex (u8 cmd, u8 index)
{
	u8 rc;

	switch (cmd) {
	case HPC_CTLR_ENABLEIRQ:	// 0x00.N.15
	case HPC_CTLR_CLEARIRQ:	// 0x06.N.15
	case HPC_CTLR_RESET:	// 0x07.N.15
	case HPC_CTLR_IRQSTEER:	// 0x08.N.15
	case HPC_CTLR_DISABLEIRQ:	// 0x01.N.15
	case HPC_ALLSLOT_ON:	// 0x11.N.15
	case HPC_ALLSLOT_OFF:	// 0x12.N.15
		rc = 0x0F;
		break;

	case HPC_SLOT_OFF:	// 0x02.Y.0-14
	case HPC_SLOT_ON:	// 0x03.Y.0-14
	case HPC_SLOT_ATTNOFF:	// 0x04.N.0-14
	case HPC_SLOT_ATTNON:	// 0x05.N.0-14
	case HPC_SLOT_BLINKLED:	// 0x13.N.0-14
		rc = index;
		break;

	case HPC_BUS_33CONVMODE:
	case HPC_BUS_66CONVMODE:
	case HPC_BUS_66PCIXMODE:
	case HPC_BUS_100PCIXMODE:
	case HPC_BUS_133PCIXMODE:
		rc = index + WPG_1ST_BUS_INDEX - 1;
		break;

	default:
		err ("hpc_writecmdtoindex - Error invalid cmd[%x]\n", cmd);
		rc = HPC_ERROR;
	}

	return rc;
}

/*----------------------------------------------------------------------
* Name:    hpc_readcmdtoindex()
*
* Action:  convert a read command to proper index within a controller
*
* Return   index, HPC_ERROR
*---------------------------------------------------------------------*/
static u8 hpc_readcmdtoindex (u8 cmd, u8 index)
{
	u8 rc;

	switch (cmd) {
	case READ_CTLRSTATUS:
		rc = 0x0F;
		break;
	case READ_SLOTSTATUS:
	case READ_ALLSTAT:
		rc = index;
		break;
	case READ_EXTSLOTSTATUS:
		rc = index + WPG_1ST_EXTSLOT_INDEX;
		break;
	case READ_BUSSTATUS:
		rc = index + WPG_1ST_BUS_INDEX - 1;
		break;
	case READ_SLOTLATCHLOWREG:
		rc = 0x28;
		break;
	case READ_REVLEVEL:
		rc = 0x25;
		break;
	case READ_HPCOPTIONS:
		rc = 0x27;
		break;
	default:
		rc = HPC_ERROR;
	}
	return rc;
}

/*----------------------------------------------------------------------
* Name:    HPCreadslot()
*
* Action:  issue a READ command to HPC
*
* Input:   pslot   - can not be NULL for READ_ALLSTAT
*          pstatus - can be NULL for READ_ALLSTAT
*
* Return   0 or error codes
*---------------------------------------------------------------------*/
int ibmphp_hpc_readslot (struct slot * pslot, u8 cmd, u8 * pstatus)
{
	void *wpg_bbar;
	struct controller *ctlr_ptr;
	struct list_head *pslotlist;
	u8 index, status;
	int rc = 0;
	int busindex;

	debug_polling ("%s - Entry pslot[%lx] cmd[%x] pstatus[%lx]\n",
		       __FUNCTION__, (ulong) pslot, cmd, (ulong) pstatus);

	if ((pslot == NULL)
	    || ((pstatus == NULL) && (cmd != READ_ALLSTAT) && (cmd != READ_BUSSTATUS))) {
		rc = -EINVAL;
		err ("%s - Error invalid pointer, rc[%d]\n", __FUNCTION__, rc);
		return rc;
	}

	if (cmd == READ_BUSSTATUS) {
		busindex = ibmphp_get_bus_index (pslot->bus);
		if (busindex < 0) {
			rc = -EINVAL;
			err ("%s - Exit Error:invalid bus, rc[%d]\n", __FUNCTION__, rc);
			return rc;
		} else
			index = (u8) busindex;
	} else
		index = pslot->ctlr_index;

	index = hpc_readcmdtoindex (cmd, index);

	if (index == HPC_ERROR) {
		rc = -EINVAL;
		err ("%s - Exit Error:invalid index, rc[%d]\n", __FUNCTION__, rc);
		return rc;
	}

	ctlr_ptr = pslot->ctrl;

	get_hpc_access ();

	//--------------------------------------------------------------------
	// map physical address to logical address
	//--------------------------------------------------------------------
	wpg_bbar = ioremap (ctlr_ptr->u.wpeg_ctlr.wpegbbar, WPG_I2C_IOREMAP_SIZE);

	//--------------------------------------------------------------------
	// check controller status before reading
	//--------------------------------------------------------------------
	rc = hpc_wait_ctlr_notworking (HPC_CTLR_WORKING_TOUT, ctlr_ptr, wpg_bbar, &status);
	if (!rc) {
		switch (cmd) {
		case READ_ALLSTAT:
			// update the slot structure
			pslot->ctrl->status = status;
			pslot->status = ctrl_read (ctlr_ptr, wpg_bbar, index);
			rc = hpc_wait_ctlr_notworking (HPC_CTLR_WORKING_TOUT, ctlr_ptr, wpg_bbar,
						       &status);
			if (!rc)
				pslot->ext_status = ctrl_read (ctlr_ptr, wpg_bbar, index + WPG_1ST_EXTSLOT_INDEX);

			break;

		case READ_SLOTSTATUS:
			// DO NOT update the slot structure
			*pstatus = ctrl_read (ctlr_ptr, wpg_bbar, index);
			break;

		case READ_EXTSLOTSTATUS:
			// DO NOT update the slot structure
			*pstatus = ctrl_read (ctlr_ptr, wpg_bbar, index);
			break;

		case READ_CTLRSTATUS:
			// DO NOT update the slot structure
			*pstatus = status;
			break;

		case READ_BUSSTATUS:
			pslot->busstatus = ctrl_read (ctlr_ptr, wpg_bbar, index);
			break;
		case READ_REVLEVEL:
			*pstatus = ctrl_read (ctlr_ptr, wpg_bbar, index);
			break;
		case READ_HPCOPTIONS:
			*pstatus = ctrl_read (ctlr_ptr, wpg_bbar, index);
			break;
		case READ_SLOTLATCHLOWREG:
			// DO NOT update the slot structure
			*pstatus = ctrl_read (ctlr_ptr, wpg_bbar, index);
			break;

			// Not used
		case READ_ALLSLOT:
			list_for_each (pslotlist, &ibmphp_slot_head) {
				pslot = list_entry (pslotlist, struct slot, ibm_slot_list);
				index = pslot->ctlr_index;
				rc = hpc_wait_ctlr_notworking (HPC_CTLR_WORKING_TOUT, ctlr_ptr,
								wpg_bbar, &status);
				if (!rc) {
					pslot->status = ctrl_read (ctlr_ptr, wpg_bbar, index);
					rc = hpc_wait_ctlr_notworking (HPC_CTLR_WORKING_TOUT,
									ctlr_ptr, wpg_bbar, &status);
					if (!rc)
						pslot->ext_status =
						    ctrl_read (ctlr_ptr, wpg_bbar,
								index + WPG_1ST_EXTSLOT_INDEX);
				} else {
					err ("%s - Error ctrl_read failed\n", __FUNCTION__);
					rc = -EINVAL;
					break;
				}
			}
			break;
		default:
			rc = -EINVAL;
			break;
		}
	}
	//--------------------------------------------------------------------
	// cleanup
	//--------------------------------------------------------------------
	iounmap (wpg_bbar);	// remove physical to logical address mapping
	free_hpc_access ();

	debug_polling ("%s - Exit rc[%d]\n", __FUNCTION__, rc);
	return rc;
}

/*----------------------------------------------------------------------
* Name:    ibmphp_hpc_writeslot()
*
* Action: issue a WRITE command to HPC
*---------------------------------------------------------------------*/
int ibmphp_hpc_writeslot (struct slot * pslot, u8 cmd)
{
	void *wpg_bbar;
	struct controller *ctlr_ptr;
	u8 index, status;
	int busindex;
	u8 done;
	int rc = 0;
	int timeout;

	debug_polling ("%s - Entry pslot[%lx] cmd[%x]\n", __FUNCTION__, (ulong) pslot, cmd);
	if (pslot == NULL) {
		rc = -EINVAL;
		err ("%s - Error Exit rc[%d]\n", __FUNCTION__, rc);
		return rc;
	}

	if ((cmd == HPC_BUS_33CONVMODE) || (cmd == HPC_BUS_66CONVMODE) ||
		(cmd == HPC_BUS_66PCIXMODE) || (cmd == HPC_BUS_100PCIXMODE) ||
		(cmd == HPC_BUS_133PCIXMODE)) {
		busindex = ibmphp_get_bus_index (pslot->bus);
		if (busindex < 0) {
			rc = -EINVAL;
			err ("%s - Exit Error:invalid bus, rc[%d]\n", __FUNCTION__, rc);
			return rc;
		} else
			index = (u8) busindex;
	} else
		index = pslot->ctlr_index;

	index = hpc_writecmdtoindex (cmd, index);

	if (index == HPC_ERROR) {
		rc = -EINVAL;
		err ("%s - Error Exit rc[%d]\n", __FUNCTION__, rc);
		return rc;
	}

	ctlr_ptr = pslot->ctrl;

	get_hpc_access ();

	//--------------------------------------------------------------------
	// map physical address to logical address
	//--------------------------------------------------------------------
	wpg_bbar = ioremap (ctlr_ptr->u.wpeg_ctlr.wpegbbar, WPG_I2C_IOREMAP_SIZE);

	debug ("%s - ctlr id[%x] physical[%lx] logical[%lx] i2c[%x]\n", __FUNCTION__,
		ctlr_ptr->ctlr_id, (ulong) (ctlr_ptr->u.wpeg_ctlr.wpegbbar), (ulong) wpg_bbar,
		ctlr_ptr->u.wpeg_ctlr.i2c_addr);

	//--------------------------------------------------------------------
	// check controller status before writing
	//--------------------------------------------------------------------
	rc = hpc_wait_ctlr_notworking (HPC_CTLR_WORKING_TOUT, ctlr_ptr, wpg_bbar, &status);
	if (!rc) {

		ctrl_write (ctlr_ptr, wpg_bbar, index, cmd);

		//--------------------------------------------------------------------
		// check controller is still not working on the command
		//--------------------------------------------------------------------
		timeout = CMD_COMPLETE_TOUT_SEC;
		done = FALSE;
		while (!done) {
			rc = hpc_wait_ctlr_notworking (HPC_CTLR_WORKING_TOUT, ctlr_ptr, wpg_bbar,
							&status);
			if (!rc) {
				if (NEEDTOCHECK_CMDSTATUS (cmd)) {
					if (CTLR_FINISHED (status) == HPC_CTLR_FINISHED_YES)
						done = TRUE;
				} else
					done = TRUE;
			}
			if (!done) {
				long_delay (1 * HZ);
				if (timeout < 1) {
					done = TRUE;
					err ("%s - Error command complete timeout\n", __FUNCTION__);
					rc = -EFAULT;
				} else
					timeout--;
			}
		}
		ctlr_ptr->status = status;
	}
	// cleanup
	iounmap (wpg_bbar);	// remove physical to logical address mapping
	free_hpc_access ();

	debug_polling ("%s - Exit rc[%d]\n", __FUNCTION__, rc);
	return rc;
}

/*----------------------------------------------------------------------
* Name:    get_hpc_access()
*
* Action: make sure only one process can access HPC at one time
*---------------------------------------------------------------------*/
static void get_hpc_access (void)
{
	down (&sem_hpcaccess);
}

/*----------------------------------------------------------------------
* Name:    free_hpc_access()
*---------------------------------------------------------------------*/
void free_hpc_access (void)
{
	up (&sem_hpcaccess);
}

/*----------------------------------------------------------------------
* Name:    ibmphp_lock_operations()
*
* Action: make sure only one process can change the data structure
*---------------------------------------------------------------------*/
void ibmphp_lock_operations (void)
{
	down (&semOperations);
	stop_polling = POLL_NO;
	to_debug = TRUE;

	/* waiting for polling to actually stop */
	down (&sem_poll);
}

/*----------------------------------------------------------------------
* Name:    ibmphp_unlock_operations()
*---------------------------------------------------------------------*/
void ibmphp_unlock_operations (void)
{
	debug ("%s - Entry\n", __FUNCTION__);
	stop_polling = POLL_YES;
	to_debug = FALSE;
	up (&semOperations);
	debug ("%s - Exit\n", __FUNCTION__);
}

/*----------------------------------------------------------------------
* Name:    poll_hpc()
*---------------------------------------------------------------------*/
static void poll_hpc (void)
{
	struct slot myslot, *pslot = NULL;
	struct list_head *pslotlist;
	int rc;
	u8 oldlatchlow = 0x00;
	u8 curlatchlow = 0x00;
	int pollcnt = 0;
	u8 ctrl_count = 0x00;

	debug ("poll_hpc - Entry\n");

	while (!ibmphp_shutdown) {
		if (stop_polling) {
			debug ("poll_hpc - stop_polling\n");
			up (&sem_poll); 
			/* to prevent deadlock */
			if (ibmphp_shutdown)
				break;
			/* to make the thread sleep */
			down (&semOperations);
			up (&semOperations);
			debug ("poll_hpc - after stop_polling sleep\n");
		} else {
			if (pollcnt) {
				// only poll the latch register
				oldlatchlow = curlatchlow;

				ctrl_count = 0x00;
				list_for_each (pslotlist, &ibmphp_slot_head) {
					if (ctrl_count >= ibmphp_get_total_controllers())
						break;
					pslot = list_entry (pslotlist, struct slot, ibm_slot_list);
					if (pslot->ctrl->ctlr_relative_id == ctrl_count) {
						ctrl_count++;
						if (READ_SLOT_LATCH (pslot->ctrl)) {
							rc = ibmphp_hpc_readslot (pslot,
										  READ_SLOTLATCHLOWREG,
										  &curlatchlow);
							if (oldlatchlow != curlatchlow)
								process_changeinlatch (oldlatchlow,
											curlatchlow);
						}
					}
				}
			} else {
				list_for_each (pslotlist, &ibmphp_slot_head) {
					if (stop_polling)
						break;
					pslot = list_entry (pslotlist, struct slot, ibm_slot_list);
					// make a copy of the old status
					memcpy ((void *) &myslot, (void *) pslot,
						sizeof (struct slot));
					rc = ibmphp_hpc_readslot (pslot, READ_ALLSTAT, NULL);
					if ((myslot.status != pslot->status)
					    || (myslot.ext_status != pslot->ext_status))
						process_changeinstatus (pslot, &myslot);
				}

				if (!stop_polling) {
					ctrl_count = 0x00;
					list_for_each (pslotlist, &ibmphp_slot_head) {
						if (ctrl_count >= ibmphp_get_total_controllers())
							break;
						pslot =
						    list_entry (pslotlist, struct slot,
								ibm_slot_list);
						if (pslot->ctrl->ctlr_relative_id == ctrl_count) {
							ctrl_count++;
							if (READ_SLOT_LATCH (pslot->ctrl))
								rc = ibmphp_hpc_readslot (pslot,
											  READ_SLOTLATCHLOWREG,
											  &curlatchlow);
						}
					}
				}
			}
			INCREMENT_POLLCNT (pollcnt);
			long_delay (POLL_INTERVAL_SEC * HZ);	// snooze
		}
	}
	up (&sem_poll);
	up (&sem_exit);
	debug ("poll_hpc - Exit\n");
}


/* ----------------------------------------------------------------------
 *  Name:    ibmphp_hpc_fillhpslotinfo(hotplug_slot * phpslot)
 *
 *  Action:  fill out the hotplug_slot info
 *
 *  Input:   pointer to hotplug_slot
 *
 *  Return
 *  Value:   0 or error codes
 *-----------------------------------------------------------------------*/
int ibmphp_hpc_fillhpslotinfo (struct hotplug_slot *phpslot)
{
	int rc = 0;
	struct slot *pslot;

	if (phpslot && phpslot->private) {
		pslot = (struct slot *) phpslot->private;
		rc = update_slot (pslot, (u8) TRUE);
		if (!rc) {

			// power - enabled:1  not:0
			phpslot->info->power_status = SLOT_POWER (pslot->status);

			// attention - off:0, on:1, blinking:2
			phpslot->info->attention_status = SLOT_ATTN (pslot->status, pslot->ext_status);

			// latch - open:1 closed:0
			phpslot->info->latch_status = SLOT_LATCH (pslot->status);

			// pci board - present:1 not:0
			if (SLOT_PRESENT (pslot->status))
				phpslot->info->adapter_status = 1;
			else
				phpslot->info->adapter_status = 0;
/*
			if (pslot->bus_on->supported_bus_mode
				&& (pslot->bus_on->supported_speed == BUS_SPEED_66))
				phpslot->info->max_bus_speed_status = BUS_SPEED_66PCIX;
			else
				phpslot->info->max_bus_speed_status = pslot->bus_on->supported_speed;
*/		} else
			rc = -EINVAL;
	} else
		rc = -EINVAL;

	return rc;
}

/*----------------------------------------------------------------------
* Name:    update_slot
*
* Action:  fill out slot status and extended status, controller status
*
* Input:   pointer to slot struct
*---------------------------------------------------------------------*/
static int update_slot (struct slot *pslot, u8 update)
{
	int rc = 0;

	debug ("%s - Entry pslot[%lx]\n", __FUNCTION__, (ulong) pslot);
	rc = ibmphp_hpc_readslot (pslot, READ_ALLSTAT, NULL);
	debug ("%s - Exit rc[%d]\n", __FUNCTION__, rc);
	return rc;
}

/*----------------------------------------------------------------------
* Name:    process_changeinstatus
*
* Action:  compare old and new slot status, process the change in status
*
* Input:   pointer to slot struct, old slot struct
*
* Return   0 or error codes
* Value:
*
* Side
* Effects: None.
*
* Notes:
*---------------------------------------------------------------------*/
static int process_changeinstatus (struct slot *pslot, struct slot *poldslot)
{
	u8 status;
	int rc = 0;
	u8 disable = FALSE;
	u8 update = FALSE;

	debug ("process_changeinstatus - Entry pslot[%lx], poldslot[%lx]\n", (ulong) pslot,
	       (ulong) poldslot);

	// bit 0 - HPC_SLOT_POWER
	if ((pslot->status & 0x01) != (poldslot->status & 0x01))
		/* ????????? DO WE NEED TO UPDATE BUS SPEED INFO HERE ??? */
		update = TRUE;

	// bit 1 - HPC_SLOT_CONNECT
	// ignore

	// bit 2 - HPC_SLOT_ATTN
	if ((pslot->status & 0x04) != (poldslot->status & 0x04))
		update = TRUE;

	// bit 3 - HPC_SLOT_PRSNT2
	// bit 4 - HPC_SLOT_PRSNT1
	if (((pslot->status & 0x08) != (poldslot->status & 0x08))
		|| ((pslot->status & 0x10) != (poldslot->status & 0x10)))
		update = TRUE;

	// bit 5 - HPC_SLOT_PWRGD
	if ((pslot->status & 0x20) != (poldslot->status & 0x20))
		// OFF -> ON: ignore, ON -> OFF: disable slot
		if (poldslot->status & 0x20)
			disable = TRUE;

	// bit 6 - HPC_SLOT_BUS_SPEED
	// ignore

	// bit 7 - HPC_SLOT_LATCH
	if ((pslot->status & 0x80) != (poldslot->status & 0x80)) {
		update = TRUE;
		// OPEN -> CLOSE
		if (pslot->status & 0x80) {
			if (SLOT_POWER (pslot->status)) {
				// power goes on and off after closing latch
				// check again to make sure power is still ON
				long_delay (1 * HZ);
				rc = ibmphp_hpc_readslot (pslot, READ_SLOTSTATUS, &status);
				if (SLOT_POWER (status))
					update = TRUE;
				else	// overwrite power in pslot to OFF
					pslot->status &= ~HPC_SLOT_POWER;
			}
		}
		// CLOSE -> OPEN 
		else if ((SLOT_POWER (poldslot->status) == HPC_SLOT_POWER_ON)
			|| (SLOT_CONNECT (poldslot->status) == HPC_SLOT_CONNECTED)) {
			disable = TRUE;
		}
		// else - ignore
	}
	// bit 4 - HPC_SLOT_BLINK_ATTN
	if ((pslot->ext_status & 0x08) != (poldslot->ext_status & 0x08))
		update = TRUE;

	if (disable) {
		debug ("process_changeinstatus - disable slot\n");
		pslot->flag = FALSE;
		rc = ibmphp_disable_slot (pslot->hotplug_slot);
	}

	if (update || disable) {
		ibmphp_update_slot_info (pslot);
	}

	debug ("%s - Exit rc[%d] disable[%x] update[%x]\n", __FUNCTION__, rc, disable, update);

	return rc;
}

/*----------------------------------------------------------------------
* Name:    process_changeinlatch
*
* Action:  compare old and new latch reg status, process the change
*
* Input:   old and current latch register status
*
* Return   0 or error codes
* Value:
*---------------------------------------------------------------------*/
static int process_changeinlatch (u8 old, u8 new)
{
	struct slot myslot, *pslot;
	u8 i;
	u8 mask;
	int rc = 0;

	debug ("%s - Entry old[%x], new[%x]\n", __FUNCTION__, old, new);
	// bit 0 reserved, 0 is LSB, check bit 1-6 for 6 slots

	for (i = 1; i <= 6; i++) {
		mask = 0x01 << i;
		if ((mask & old) != (mask & new)) {
			pslot = ibmphp_get_slot_from_physical_num (i);
			if (pslot) {
				memcpy ((void *) &myslot, (void *) pslot, sizeof (struct slot));
				rc = ibmphp_hpc_readslot (pslot, READ_ALLSTAT, NULL);
				debug ("%s - call process_changeinstatus for slot[%d]\n", __FUNCTION__, i);
				process_changeinstatus (pslot, &myslot);
			} else {
				rc = -EINVAL;
				err ("%s - Error bad pointer for slot[%d]\n", __FUNCTION__, i);
			}
		}
	}
	debug ("%s - Exit rc[%d]\n", __FUNCTION__, rc);
	return rc;
}

/*----------------------------------------------------------------------
* Name:    hpc_poll_thread
*
* Action:  polling
*
* Return   0
* Value:
*---------------------------------------------------------------------*/
static int hpc_poll_thread (void *data)
{
	debug ("%s - Entry\n", __FUNCTION__);
	lock_kernel ();
	daemonize ();
	reparent_to_init ();

	//  New name
	strcpy (current->comm, "hpc_poll");

	unlock_kernel ();

	poll_hpc ();

	tid_poll = 0;
	debug ("%s - Exit\n", __FUNCTION__);
	return 0;
}


/*----------------------------------------------------------------------
* Name:    ibmphp_hpc_start_poll_thread
*
* Action:  start polling thread
*---------------------------------------------------------------------*/
int ibmphp_hpc_start_poll_thread (void)
{
	int rc = 0;

	debug ("ibmphp_hpc_start_poll_thread - Entry\n");

	tid_poll = kernel_thread (hpc_poll_thread, 0, 0);
	if (tid_poll < 0) {
		err ("ibmphp_hpc_start_poll_thread - Error, thread not started\n");
		rc = -1;
	}

	debug ("ibmphp_hpc_start_poll_thread - Exit tid_poll[%d] rc[%d]\n", tid_poll, rc);
	return rc;
}

/*----------------------------------------------------------------------
* Name:    ibmphp_hpc_stop_poll_thread
*
* Action:  stop polling thread and cleanup
*---------------------------------------------------------------------*/
void ibmphp_hpc_stop_poll_thread (void)
{
	debug ("ibmphp_hpc_stop_poll_thread - Entry\n");

	ibmphp_shutdown = TRUE;
	ibmphp_lock_operations ();

	// wait for poll thread to exit
	down (&sem_exit);

	// cleanup
	free_hpc_access ();
	ibmphp_unlock_operations ();
	up (&sem_poll);
	up (&sem_exit);

	debug ("ibmphp_hpc_stop_poll_thread - Exit\n");
}

/*----------------------------------------------------------------------
* Name:    hpc_wait_ctlr_notworking
*
* Action:  wait until the controller is in a not working state
*
* Return   0, HPC_ERROR
* Value:
*---------------------------------------------------------------------*/
static int hpc_wait_ctlr_notworking (int timeout, struct controller *ctlr_ptr, void *wpg_bbar,
				    u8 * pstatus)
{
	int rc = 0;
	u8 done = FALSE;

	debug_polling ("hpc_wait_ctlr_notworking - Entry timeout[%d]\n", timeout);

	while (!done) {
		*pstatus = ctrl_read (ctlr_ptr, wpg_bbar, WPG_CTLR_INDEX);
		if (*pstatus == HPC_ERROR) {
			rc = HPC_ERROR;
			done = TRUE;
		}
		if (CTLR_WORKING (*pstatus) == HPC_CTLR_WORKING_NO)
			done = TRUE;
		if (!done) {
			long_delay (1 * HZ);
			if (timeout < 1) {
				done = TRUE;
				err ("HPCreadslot - Error ctlr timeout\n");
				rc = HPC_ERROR;
			} else
				timeout--;
		}
	}
	debug_polling ("hpc_wait_ctlr_notworking - Exit rc[%x] status[%x]\n", rc, *pstatus);
	return rc;
}
