/*
 * ipmi_poweroff.c
 *
 * MontaVista IPMI Poweroff extension to sys_reboot
 *
 * Author: MontaVista Software, Inc.
 *         Steven Dake <sdake@mvista.com>
 *         Corey Minyard <cminyard@mvista.com>
 *         source@mvista.com
 *
 * Copyright 2002,2004 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <asm/semaphore.h>
#include <linux/kdev_t.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/ipmi.h>
#include <linux/ipmi_smi.h>

#define PFX "IPMI poweroff: "
#define IPMI_POWEROFF_VERSION	"v33"

/* Where to we insert our poweroff function? */
extern void (*pm_power_off)(void);

/* Stuff from the get device id command. */
static unsigned int mfg_id;
static unsigned int prod_id;
static unsigned char capabilities;

/* We use our own messages for this operation, we don't let the system
   allocate them, since we may be in a panic situation.  The whole
   thing is single-threaded, anyway, so multiple messages are not
   required. */
static void dummy_smi_free(struct ipmi_smi_msg *msg)
{
}
static void dummy_recv_free(struct ipmi_recv_msg *msg)
{
}
static struct ipmi_smi_msg halt_smi_msg =
{
	.done = dummy_smi_free
};
static struct ipmi_recv_msg halt_recv_msg =
{
	.done = dummy_recv_free
};


/*
 * Code to send a message and wait for the reponse.
 */

static void receive_handler(struct ipmi_recv_msg *recv_msg, void *handler_data)
{
	struct semaphore *sem = recv_msg->user_msg_data;

	if (sem)
		up(sem);
}

static struct ipmi_user_hndl ipmi_poweroff_handler =
{
	.ipmi_recv_hndl = receive_handler
};


static int ipmi_request_wait_for_response(ipmi_user_t            user,
					  struct ipmi_addr       *addr,
					  struct kernel_ipmi_msg *send_msg)
{
	int              rv;
	struct semaphore sem;

	sema_init (&sem, 0);

	rv = ipmi_request_supply_msgs(user, addr, 0, send_msg, &sem,
				      &halt_smi_msg, &halt_recv_msg, 0);
	if (rv)
		return rv;

	down (&sem);

	return halt_recv_msg.msg.data[0];
}

/* We are in run-to-completion mode, no semaphore is desired. */
static int ipmi_request_in_rc_mode(ipmi_user_t            user,
				   struct ipmi_addr       *addr,
				   struct kernel_ipmi_msg *send_msg)
{
	int              rv;

	rv = ipmi_request_supply_msgs(user, addr, 0, send_msg, NULL,
				      &halt_smi_msg, &halt_recv_msg, 0);
	if (rv)
		return rv;

	return halt_recv_msg.msg.data[0];
}

/*
 * ATCA Support
 */

#define IPMI_NETFN_ATCA			0x2c
#define IPMI_ATCA_SET_POWER_CMD		0x11
#define IPMI_ATCA_GET_ADDR_INFO_CMD	0x01
#define IPMI_PICMG_ID			0

static int ipmi_atca_detect (ipmi_user_t user)
{
	struct ipmi_system_interface_addr smi_addr;
	struct kernel_ipmi_msg            send_msg;
	int                               rv;
	unsigned char                     data[1];

        /*
         * Configure IPMI address for local access
         */
        smi_addr.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
        smi_addr.channel = IPMI_BMC_CHANNEL;
        smi_addr.lun = 0;

	/*
	 * Use get address info to check and see if we are ATCA
	 */
	send_msg.netfn = IPMI_NETFN_ATCA;
	send_msg.cmd = IPMI_ATCA_GET_ADDR_INFO_CMD;
	data[0] = IPMI_PICMG_ID;
	send_msg.data = data;
	send_msg.data_len = sizeof(data);
	rv = ipmi_request_wait_for_response(user,
					    (struct ipmi_addr *) &smi_addr,
					    &send_msg);
	return !rv;
}

static void ipmi_poweroff_atca (ipmi_user_t user)
{
	struct ipmi_system_interface_addr smi_addr;
	struct kernel_ipmi_msg            send_msg;
	int                               rv;
	unsigned char                     data[4];

        /*
         * Configure IPMI address for local access
         */
        smi_addr.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
        smi_addr.channel = IPMI_BMC_CHANNEL;
        smi_addr.lun = 0;

	printk(KERN_INFO PFX "Powering down via ATCA power command\n");

	/*
	 * Power down
	 */
	send_msg.netfn = IPMI_NETFN_ATCA;
	send_msg.cmd = IPMI_ATCA_SET_POWER_CMD;
	data[0] = IPMI_PICMG_ID;
	data[1] = 0; /* FRU id */
	data[2] = 0; /* Power Level */
	data[3] = 0; /* Don't change saved presets */
	send_msg.data = data;
	send_msg.data_len = sizeof (data);
	rv = ipmi_request_in_rc_mode(user,
				     (struct ipmi_addr *) &smi_addr,
				     &send_msg);
	if (rv) {
		printk(KERN_ERR PFX "Unable to send ATCA powerdown message,"
		       " IPMI error 0x%x\n", rv);
		goto out;
	}

 out:
	return;
}

/*
 * CPI1 Support
 */

#define IPMI_NETFN_OEM_1				0xf8
#define OEM_GRP_CMD_SET_RESET_STATE		0x84
#define OEM_GRP_CMD_SET_POWER_STATE		0x82
#define IPMI_NETFN_OEM_8				0xf8
#define OEM_GRP_CMD_REQUEST_HOTSWAP_CTRL	0x80
#define OEM_GRP_CMD_GET_SLOT_GA			0xa3
#define IPMI_NETFN_SENSOR_EVT			0x10
#define IPMI_CMD_GET_EVENT_RECEIVER		0x01

#define IPMI_CPI1_PRODUCT_ID		0x000157
#define IPMI_CPI1_MANUFACTURER_ID	0x0108

static int ipmi_cpi1_detect (ipmi_user_t user)
{
	return ((mfg_id == IPMI_CPI1_MANUFACTURER_ID)
		&& (prod_id == IPMI_CPI1_PRODUCT_ID));
}

static void ipmi_poweroff_cpi1 (ipmi_user_t user)
{
	struct ipmi_system_interface_addr smi_addr;
	struct ipmi_ipmb_addr             ipmb_addr;
	struct kernel_ipmi_msg            send_msg;
	int                               rv;
	unsigned char                     data[1];
	int                               slot;
	unsigned char                     hotswap_ipmb;
	unsigned char                     aer_addr;
	unsigned char                     aer_lun;

        /*
         * Configure IPMI address for local access
         */
        smi_addr.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
        smi_addr.channel = IPMI_BMC_CHANNEL;
        smi_addr.lun = 0;

	printk(KERN_INFO PFX "Powering down via CPI1 power command\n");

	/*
	 * Get IPMI ipmb address
	 */
	send_msg.netfn = IPMI_NETFN_OEM_8 >> 2;
	send_msg.cmd = OEM_GRP_CMD_GET_SLOT_GA;
	send_msg.data = NULL;
	send_msg.data_len = 0;
	rv = ipmi_request_in_rc_mode(user,
				     (struct ipmi_addr *) &smi_addr,
				     &send_msg);
	if (rv)
		goto out;
	slot = halt_recv_msg.msg.data[1];
	hotswap_ipmb = (slot > 9) ? (0xb0 + 2 * slot) : (0xae + 2 * slot);

	/*
	 * Get active event receiver
	 */
	send_msg.netfn = IPMI_NETFN_SENSOR_EVT >> 2;
	send_msg.cmd = IPMI_CMD_GET_EVENT_RECEIVER;
	send_msg.data = NULL;
	send_msg.data_len = 0;
	rv = ipmi_request_in_rc_mode(user,
				     (struct ipmi_addr *) &smi_addr,
				     &send_msg);
	if (rv)
		goto out;
	aer_addr = halt_recv_msg.msg.data[1];
	aer_lun = halt_recv_msg.msg.data[2];

	/*
	 * Setup IPMB address target instead of local target
	 */
	ipmb_addr.addr_type = IPMI_IPMB_ADDR_TYPE;
	ipmb_addr.channel = 0;
	ipmb_addr.slave_addr = aer_addr;
	ipmb_addr.lun = aer_lun;

	/*
	 * Send request hotswap control to remove blade from dpv
	 */
	send_msg.netfn = IPMI_NETFN_OEM_8 >> 2;
	send_msg.cmd = OEM_GRP_CMD_REQUEST_HOTSWAP_CTRL;
	send_msg.data = &hotswap_ipmb;
	send_msg.data_len = 1;
	ipmi_request_in_rc_mode(user,
				(struct ipmi_addr *) &ipmb_addr,
				&send_msg);

	/*
	 * Set reset asserted
	 */
	send_msg.netfn = IPMI_NETFN_OEM_1 >> 2;
	send_msg.cmd = OEM_GRP_CMD_SET_RESET_STATE;
	send_msg.data = data;
	data[0] = 1; /* Reset asserted state */
	send_msg.data_len = 1;
	rv = ipmi_request_in_rc_mode(user,
				     (struct ipmi_addr *) &smi_addr,
				     &send_msg);
	if (rv)
		goto out;

	/*
	 * Power down
	 */
	send_msg.netfn = IPMI_NETFN_OEM_1 >> 2;
	send_msg.cmd = OEM_GRP_CMD_SET_POWER_STATE;
	send_msg.data = data;
	data[0] = 1; /* Power down state */
	send_msg.data_len = 1;
	rv = ipmi_request_in_rc_mode(user,
				     (struct ipmi_addr *) &smi_addr,
				     &send_msg);
	if (rv)
		goto out;

 out:
	return;
}

/*
 * Standard chassis support
 */

#define IPMI_NETFN_CHASSIS_REQUEST	0
#define IPMI_CHASSIS_CONTROL_CMD	0x02

static int ipmi_chassis_detect (ipmi_user_t user)
{
	/* Chassis support, use it. */
	return (capabilities & 0x80);
}

static void ipmi_poweroff_chassis (ipmi_user_t user)
{
	struct ipmi_system_interface_addr smi_addr;
	struct kernel_ipmi_msg            send_msg;
	int                               rv;
	unsigned char                     data[1];

        /*
         * Configure IPMI address for local access
         */
        smi_addr.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
        smi_addr.channel = IPMI_BMC_CHANNEL;
        smi_addr.lun = 0;

	printk(KERN_INFO PFX "Powering down via IPMI chassis control command\n");

	/*
	 * Power down
	 */
	send_msg.netfn = IPMI_NETFN_CHASSIS_REQUEST;
	send_msg.cmd = IPMI_CHASSIS_CONTROL_CMD;
	data[0] = 0; /* Power down */
	send_msg.data = data;
	send_msg.data_len = sizeof(data);
	rv = ipmi_request_in_rc_mode(user,
				     (struct ipmi_addr *) &smi_addr,
				     &send_msg);
	if (rv) {
		printk(KERN_ERR PFX "Unable to send chassis powerdown message,"
		       " IPMI error 0x%x\n", rv);
		goto out;
	}

 out:
	return;
}


/* Table of possible power off functions. */
struct poweroff_function {
	char *platform_type;
	int  (*detect)(ipmi_user_t user);
	void (*poweroff_func)(ipmi_user_t user);
};

static struct poweroff_function poweroff_functions[] = {
	{ .platform_type	= "ATCA",
	  .detect		= ipmi_atca_detect,
	  .poweroff_func	= ipmi_poweroff_atca },
	{ .platform_type	= "CPI1",
	  .detect		= ipmi_cpi1_detect,
	  .poweroff_func	= ipmi_poweroff_cpi1 },
	/* Chassis should generally be last, other things should override
	   it. */
	{ .platform_type	= "chassis",
	  .detect		= ipmi_chassis_detect,
	  .poweroff_func	= ipmi_poweroff_chassis },
};
#define NUM_PO_FUNCS (sizeof(poweroff_functions) \
		      / sizeof(struct poweroff_function))


/* Our local state. */
static int ready = 0;
static ipmi_user_t ipmi_user;
static void (*specific_poweroff_func)(ipmi_user_t user) = NULL;

/* Holds the old poweroff function so we can restore it on removal. */
static void (*old_poweroff_func)(void);


/* Called on a powerdown request. */
static void ipmi_poweroff_function (void)
{
	if (!ready)
		return;

	/* Use run-to-completion mode, since interrupts may be off. */
	ipmi_user_set_run_to_completion(ipmi_user, 1);
	specific_poweroff_func(ipmi_user);
	ipmi_user_set_run_to_completion(ipmi_user, 0);
}

/* Wait for an IPMI interface to be installed, the first one installed
   will be grabbed by this code and used to perform the powerdown. */
static void ipmi_po_new_smi(int if_num)
{
	struct ipmi_system_interface_addr smi_addr;
	struct kernel_ipmi_msg            send_msg;
	int                               rv;
	int                               i;

	if (ready)
		return;

	rv = ipmi_create_user(if_num, &ipmi_poweroff_handler, NULL, &ipmi_user);
	if (rv) {
		printk(KERN_ERR PFX "could not create IPMI user, error %d\n",
		       rv);
		return;
	}

        /*
         * Do a get device ide and store some results, since this is
	 * used by several functions.
         */
        smi_addr.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
        smi_addr.channel = IPMI_BMC_CHANNEL;
        smi_addr.lun = 0;

	send_msg.netfn = IPMI_NETFN_APP_REQUEST;
	send_msg.cmd = IPMI_GET_DEVICE_ID_CMD;
	send_msg.data = NULL;
	send_msg.data_len = 0;
	rv = ipmi_request_wait_for_response(ipmi_user,
					    (struct ipmi_addr *) &smi_addr,
					    &send_msg);
	if (rv) {
		printk(KERN_ERR PFX "Unable to send IPMI get device id info,"
		       " IPMI error 0x%x\n", rv);
		goto out_err;
	}

	if (halt_recv_msg.msg.data_len < 12) {
		printk(KERN_ERR PFX "(chassis) IPMI get device id info too,"
		       " short, was %d bytes, needed %d bytes\n",
		       halt_recv_msg.msg.data_len, 12);
		goto out_err;
	}

	mfg_id = (halt_recv_msg.msg.data[7]
		  | (halt_recv_msg.msg.data[8] << 8)
		  | (halt_recv_msg.msg.data[9] << 16));
	prod_id = (halt_recv_msg.msg.data[10]
		   | (halt_recv_msg.msg.data[11] << 8));
	capabilities = halt_recv_msg.msg.data[6];


	/* Scan for a poweroff method */
	for (i=0; i<NUM_PO_FUNCS; i++) {
		if (poweroff_functions[i].detect(ipmi_user))
			goto found;
	}

 out_err:
	printk(KERN_ERR PFX "Unable to find a poweroff function that"
	       " will work, giving up\n");
	ipmi_destroy_user(ipmi_user);
	return;

 found:
	printk(KERN_INFO PFX "Found a %s style poweroff function\n",
	       poweroff_functions[i].platform_type);
	specific_poweroff_func = poweroff_functions[i].poweroff_func;
	old_poweroff_func = pm_power_off;
	pm_power_off = ipmi_poweroff_function;
	ready = 1;
}

static void ipmi_po_smi_gone(int if_num)
{
	/* This can never be called, because once poweroff driver is
	   registered, the interface can't go away until the power
	   driver is unregistered. */
}

static struct ipmi_smi_watcher smi_watcher =
{
	.owner    = THIS_MODULE,
	.new_smi  = ipmi_po_new_smi,
	.smi_gone = ipmi_po_smi_gone
};


/*
 * Startup and shutdown functions.
 */
static int ipmi_poweroff_init (void)
{
	int rv;

	printk ("Copyright (C) 2004 MontaVista Software -"
		" IPMI Powerdown via sys_reboot version "
		IPMI_POWEROFF_VERSION ".\n");

	rv = ipmi_smi_watcher_register(&smi_watcher);
	if (rv)
		printk(KERN_ERR PFX "Unable to register SMI watcher: %d\n", rv);

	return rv;
}

#ifdef MODULE
static __exit void ipmi_poweroff_cleanup(void)
{
	int rv;

	ipmi_smi_watcher_unregister(&smi_watcher);

	if (ready) {
		rv = ipmi_destroy_user(ipmi_user);
		if (rv)
			printk(KERN_ERR PFX "could not cleanup the IPMI"
			       " user: 0x%x\n", rv);
		pm_power_off = old_poweroff_func;
	}
}
module_exit(ipmi_poweroff_cleanup);
#endif

module_init(ipmi_poweroff_init);
MODULE_LICENSE("GPL");
