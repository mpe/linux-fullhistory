/*
 * PS/2 mouse driver
 *
 * Copyright (c) 1999-2002 Vojtech Pavlik
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/init.h>
#include "psmouse.h"
#include "synaptics.h"
#include "logips2pp.h"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_DESCRIPTION("PS/2 mouse driver");
MODULE_LICENSE("GPL");

static char *psmouse_proto;
static unsigned int psmouse_max_proto = -1U;
module_param_named(proto, psmouse_proto, charp, 0);
MODULE_PARM_DESC(proto, "Highest protocol extension to probe (bare, imps, exps). Useful for KVM switches.");

int psmouse_resolution = 200;
module_param_named(resolution, psmouse_resolution, uint, 0);
MODULE_PARM_DESC(resolution, "Resolution, in dpi.");

unsigned int psmouse_rate = 100;
module_param_named(rate, psmouse_rate, uint, 0);
MODULE_PARM_DESC(rate, "Report rate, in reports per second.");

int psmouse_smartscroll = 1;
module_param_named(smartscroll, psmouse_smartscroll, bool, 0);
MODULE_PARM_DESC(smartscroll, "Logitech Smartscroll autorepeat, 1 = enabled (default), 0 = disabled.");

unsigned int psmouse_resetafter;
module_param_named(resetafter, psmouse_resetafter, uint, 0);
MODULE_PARM_DESC(resetafter, "Reset Synaptics Touchpad after so many bad packets (0 = never).");

static char *psmouse_protocols[] = { "None", "PS/2", "PS2++", "PS2T++", "GenPS/2", "ImPS/2", "ImExPS/2", "SynPS/2"};

/*
 * psmouse_process_packet() analyzes the PS/2 mouse packet contents and
 * reports relevant events to the input module.
 */

static void psmouse_process_packet(struct psmouse *psmouse, struct pt_regs *regs)
{
	struct input_dev *dev = &psmouse->dev;
	unsigned char *packet = psmouse->packet;

	input_regs(dev, regs);

/*
 * The PS2++ protocol is a little bit complex
 */

	if (psmouse->type == PSMOUSE_PS2PP || psmouse->type == PSMOUSE_PS2TPP)
		ps2pp_process_packet(psmouse);

/*
 * Scroll wheel on IntelliMice, scroll buttons on NetMice
 */

	if (psmouse->type == PSMOUSE_IMPS || psmouse->type == PSMOUSE_GENPS)
		input_report_rel(dev, REL_WHEEL, -(signed char) packet[3]);

/*
 * Scroll wheel and buttons on IntelliMouse Explorer
 */

	if (psmouse->type == PSMOUSE_IMEX) {
		input_report_rel(dev, REL_WHEEL, (int) (packet[3] & 8) - (int) (packet[3] & 7));
		input_report_key(dev, BTN_SIDE, (packet[3] >> 4) & 1);
		input_report_key(dev, BTN_EXTRA, (packet[3] >> 5) & 1);
	}

/*
 * Extra buttons on Genius NewNet 3D
 */

	if (psmouse->type == PSMOUSE_GENPS) {
		input_report_key(dev, BTN_SIDE, (packet[0] >> 6) & 1);
		input_report_key(dev, BTN_EXTRA, (packet[0] >> 7) & 1);
	}

/*
 * Generic PS/2 Mouse
 */

	input_report_key(dev, BTN_LEFT,    packet[0]       & 1);
	input_report_key(dev, BTN_MIDDLE, (packet[0] >> 2) & 1);
	input_report_key(dev, BTN_RIGHT,  (packet[0] >> 1) & 1);

	input_report_rel(dev, REL_X, packet[1] ? (int) packet[1] - (int) ((packet[0] << 4) & 0x100) : 0);
	input_report_rel(dev, REL_Y, packet[2] ? (int) ((packet[0] << 3) & 0x100) - (int) packet[2] : 0);

	input_sync(dev);
}

/*
 * psmouse_interrupt() handles incoming characters, either gathering them into
 * packets or passing them to the command routine as command output.
 */

static irqreturn_t psmouse_interrupt(struct serio *serio,
		unsigned char data, unsigned int flags, struct pt_regs *regs)
{
	struct psmouse *psmouse = serio->private;

	if (psmouse->state == PSMOUSE_IGNORE)
		goto out;

	if (flags & (SERIO_PARITY|SERIO_TIMEOUT)) {
		if (psmouse->state == PSMOUSE_ACTIVATED)
			printk(KERN_WARNING "psmouse.c: bad data from KBC -%s%s\n",
				flags & SERIO_TIMEOUT ? " timeout" : "",
				flags & SERIO_PARITY ? " bad parity" : "");
		if (psmouse->acking) {
			psmouse->ack = -1;
			psmouse->acking = 0;
		}
		psmouse->pktcnt = 0;
		goto out;
	}

	if (psmouse->acking) {
		switch (data) {
			case PSMOUSE_RET_ACK:
				psmouse->ack = 1;
				break;
			case PSMOUSE_RET_NAK:
				psmouse->ack = -1;
				break;
			default:
				psmouse->ack = 1;	/* Workaround for mice which don't ACK the Get ID command */
				if (psmouse->cmdcnt)
					psmouse->cmdbuf[--psmouse->cmdcnt] = data;
				break;
		}
		psmouse->acking = 0;
		goto out;
	}

	if (psmouse->cmdcnt) {
		psmouse->cmdbuf[--psmouse->cmdcnt] = data;
		goto out;
	}

	if (psmouse->state == PSMOUSE_ACTIVATED &&
	    psmouse->pktcnt && time_after(jiffies, psmouse->last + HZ/2)) {
		printk(KERN_WARNING "psmouse.c: %s at %s lost synchronization, throwing %d bytes away.\n",
		       psmouse->name, psmouse->phys, psmouse->pktcnt);
		psmouse->pktcnt = 0;
	}
	
	psmouse->last = jiffies;
	psmouse->packet[psmouse->pktcnt++] = data;

	if (psmouse->packet[0] == PSMOUSE_RET_BAT) {
		if (psmouse->pktcnt == 1)
			goto out;
		
		if (psmouse->pktcnt == 2) {
			if (psmouse->packet[1] == PSMOUSE_RET_ID) {
				psmouse->state = PSMOUSE_IGNORE;
				serio_rescan(serio);
				goto out;
			}
			if (psmouse->type == PSMOUSE_SYNAPTICS) {
				/* neither 0xAA nor 0x00 are valid first bytes
				 * for a packet in absolute mode
				 */
				psmouse->pktcnt = 0;
				goto out;
			}
		}
	}

	if (psmouse->type == PSMOUSE_SYNAPTICS) {
		/*
		 * The synaptics driver has its own resync logic,
		 * so it needs to receive all bytes one at a time.
		 */
		synaptics_process_byte(psmouse, regs);
		goto out;
	}

	if (psmouse->pktcnt == 3 + (psmouse->type >= PSMOUSE_GENPS)) {
		psmouse_process_packet(psmouse, regs);
		psmouse->pktcnt = 0;
		goto out;
	}
out:
	return IRQ_HANDLED;
}

/*
 * psmouse_sendbyte() sends a byte to the mouse, and waits for acknowledge.
 * It doesn't handle retransmission, though it could - because when there would
 * be need for retransmissions, the mouse has to be replaced anyway.
 */

static int psmouse_sendbyte(struct psmouse *psmouse, unsigned char byte)
{
	int timeout = 10000; /* 100 msec */
	psmouse->ack = 0;
	psmouse->acking = 1;

	if (serio_write(psmouse->serio, byte)) {
		psmouse->acking = 0;
		return -1;
	}

	while (!psmouse->ack && timeout--) udelay(10);

	return -(psmouse->ack <= 0);
}

/*
 * psmouse_command() sends a command and its parameters to the mouse,
 * then waits for the response and puts it in the param array.
 */

int psmouse_command(struct psmouse *psmouse, unsigned char *param, int command)
{
	int timeout = 500000; /* 500 msec */
	int send = (command >> 12) & 0xf;
	int receive = (command >> 8) & 0xf;
	int i;

	psmouse->cmdcnt = receive;

	if (command == PSMOUSE_CMD_RESET_BAT)
                timeout = 4000000; /* 4 sec */

	/* initialize cmdbuf with preset values from param */
	if (receive)
	   for (i = 0; i < receive; i++)
		psmouse->cmdbuf[(receive - 1) - i] = param[i];

	if (command & 0xff)
		if (psmouse_sendbyte(psmouse, command & 0xff))
			return (psmouse->cmdcnt = 0) - 1;

	for (i = 0; i < send; i++)
		if (psmouse_sendbyte(psmouse, param[i]))
			return (psmouse->cmdcnt = 0) - 1;

	while (psmouse->cmdcnt && timeout--) {
	
		if (psmouse->cmdcnt == 1 && command == PSMOUSE_CMD_RESET_BAT &&
				timeout > 100000) /* do not run in a endless loop */
			timeout = 100000; /* 1 sec */

		if (psmouse->cmdcnt == 1 && command == PSMOUSE_CMD_GETID &&
		    psmouse->cmdbuf[1] != 0xab && psmouse->cmdbuf[1] != 0xac) {
			psmouse->cmdcnt = 0;
			break;
		}

		udelay(1);
	}

	for (i = 0; i < receive; i++)
		param[i] = psmouse->cmdbuf[(receive - 1) - i];

	if (psmouse->cmdcnt)
		return (psmouse->cmdcnt = 0) - 1;

	return 0;
}

/*
 * Genius NetMouse magic init.
 */
static int genius_detect(struct psmouse *psmouse)
{
	unsigned char param[4];

	param[0] = 3;
	psmouse_command(psmouse, param, PSMOUSE_CMD_SETRES);
	psmouse_command(psmouse,  NULL, PSMOUSE_CMD_SETSCALE11);
	psmouse_command(psmouse,  NULL, PSMOUSE_CMD_SETSCALE11);
	psmouse_command(psmouse,  NULL, PSMOUSE_CMD_SETSCALE11);
	psmouse_command(psmouse, param, PSMOUSE_CMD_GETINFO);

	return param[0] == 0x00 && param[1] == 0x33 && param[2] == 0x55;
}

/*
 * IntelliMouse magic init.
 */
static int intellimouse_detect(struct psmouse *psmouse)
{
	unsigned char param[2];

	param[0] = 200;
	psmouse_command(psmouse, param, PSMOUSE_CMD_SETRATE);
	param[0] = 100;
	psmouse_command(psmouse, param, PSMOUSE_CMD_SETRATE);
	param[0] =  80;
	psmouse_command(psmouse, param, PSMOUSE_CMD_SETRATE);
	psmouse_command(psmouse, param, PSMOUSE_CMD_GETID);

	return param[0] == 3;
}

/*
 * Try IntelliMouse/Explorer magic init.
 */
static int im_explorer_detect(struct psmouse *psmouse)
{
	unsigned char param[2];

	param[0] = 200;
	psmouse_command(psmouse, param, PSMOUSE_CMD_SETRATE);
	param[0] = 200;
	psmouse_command(psmouse, param, PSMOUSE_CMD_SETRATE);
	param[0] =  80;
	psmouse_command(psmouse, param, PSMOUSE_CMD_SETRATE);
	psmouse_command(psmouse, param, PSMOUSE_CMD_GETID);

	return param[0] == 4;
}

/*
 * psmouse_extensions() probes for any extensions to the basic PS/2 protocol
 * the mouse may have.
 */

static int psmouse_extensions(struct psmouse *psmouse)
{
	int synaptics_hardware = 0;

	psmouse->vendor = "Generic";
	psmouse->name = "Mouse";
	psmouse->model = 0;

/*
 * Try Synaptics TouchPad
 */
	if (psmouse_max_proto > PSMOUSE_PS2 && synaptics_detect(psmouse)) {
		synaptics_hardware = 1;
		psmouse->vendor = "Synaptics";
		psmouse->name = "TouchPad";

		if (psmouse_max_proto > PSMOUSE_IMEX) {
			if (synaptics_init(psmouse) == 0)
				return PSMOUSE_SYNAPTICS;
/*
 * Some Synaptics touchpads can emulate extended protocols (like IMPS/2).
 * Unfortunately Logitech/Genius probes confuse some firmware versions so
 * we'll have to skip them.
 */
			psmouse_max_proto = PSMOUSE_IMEX;
		}
	}

	if (psmouse_max_proto > PSMOUSE_IMEX && genius_detect(psmouse)) {
		set_bit(BTN_EXTRA, psmouse->dev.keybit);
		set_bit(BTN_SIDE, psmouse->dev.keybit);
		set_bit(REL_WHEEL, psmouse->dev.relbit);

		psmouse->vendor = "Genius";
		psmouse->name = "Wheel Mouse";
		return PSMOUSE_GENPS;
	}

	if (psmouse_max_proto > PSMOUSE_IMEX) {
		int type = ps2pp_detect(psmouse);
		if (type)
			return type;
	}

	if (psmouse_max_proto >= PSMOUSE_IMPS && intellimouse_detect(psmouse)) {
		set_bit(REL_WHEEL, psmouse->dev.relbit);

		if (psmouse_max_proto >= PSMOUSE_IMEX &&
					im_explorer_detect(psmouse)) {
			set_bit(BTN_SIDE, psmouse->dev.keybit);
			set_bit(BTN_EXTRA, psmouse->dev.keybit);

			psmouse->name = "Explorer Mouse";
			return PSMOUSE_IMEX;
		}

		psmouse->name = "Wheel Mouse";
		return PSMOUSE_IMPS;
	}

/*
 * Okay, all failed, we have a standard mouse here. The number of the buttons
 * is still a question, though. We assume 3.
 */
	if (synaptics_hardware) {
/*
 * We detected Synaptics hardware but it did not respond to IMPS/2 probes.
 * We need to reset the touchpad because if there is a track point on the
 * pass through port it could get disabled while probing for protocol
 * extensions.
 */
		psmouse_command(psmouse, NULL, PSMOUSE_CMD_RESET_DIS);
	}

	return PSMOUSE_PS2;
}

/*
 * psmouse_probe() probes for a PS/2 mouse.
 */

static int psmouse_probe(struct psmouse *psmouse)
{
	unsigned char param[2];

/*
 * First, we check if it's a mouse. It should send 0x00 or 0x03
 * in case of an IntelliMouse in 4-byte mode or 0x04 for IM Explorer.
 */

	param[0] = 0xa5;

	if (psmouse_command(psmouse, param, PSMOUSE_CMD_GETID))
		return -1;

	if (param[0] != 0x00 && param[0] != 0x03 && param[0] != 0x04)
		return -1;

/*
 * Then we reset and disable the mouse so that it doesn't generate events.
 */

	if (psmouse_command(psmouse, NULL, PSMOUSE_CMD_RESET_DIS))
		return -1;

/*
 * And here we try to determine if it has any extensions over the
 * basic PS/2 3-button mouse.
 */

	return psmouse->type = psmouse_extensions(psmouse);
}

/*
 * Here we set the mouse resolution.
 */

static void psmouse_set_resolution(struct psmouse *psmouse)
{
	unsigned char param[1];

	if (psmouse->type == PSMOUSE_PS2PP && psmouse_resolution > 400) {
		ps2pp_set_800dpi(psmouse);
		return;
	}

	if (!psmouse_resolution || psmouse_resolution >= 200)
		param[0] = 3;
	else if (psmouse_resolution >= 100)
		param[0] = 2;
	else if (psmouse_resolution >= 50)
		param[0] = 1;
	else if (psmouse_resolution)
		param[0] = 0;

        psmouse_command(psmouse, param, PSMOUSE_CMD_SETRES);
}

/*
 * Here we set the mouse report rate.
 */

static void psmouse_set_rate(struct psmouse *psmouse)
{
	unsigned char rates[] = { 200, 100, 80, 60, 40, 20, 10, 0 };
	int i = 0;

	while (rates[i] > psmouse_rate) i++;
	psmouse_command(psmouse, rates + i, PSMOUSE_CMD_SETRATE);
}

/*
 * psmouse_initialize() initializes the mouse to a sane state.
 */

static void psmouse_initialize(struct psmouse *psmouse)
{
	unsigned char param[2];
	
/*
 * We set the mouse report rate, resolution and scaling.
 */

	if (psmouse_max_proto != PSMOUSE_PS2) {
		psmouse_set_rate(psmouse);
		psmouse_set_resolution(psmouse);
		psmouse_command(psmouse,  NULL, PSMOUSE_CMD_SETSCALE11);
	}

/*
 * We set the mouse into streaming mode.
 */

	psmouse_command(psmouse, param, PSMOUSE_CMD_SETSTREAM);
}

/*
 * psmouse_activate() enables the mouse so that we get motion reports from it.
 */

static void psmouse_activate(struct psmouse *psmouse)
{
	if (psmouse_command(psmouse, NULL, PSMOUSE_CMD_ENABLE))
		printk(KERN_WARNING "psmouse.c: Failed to enable mouse on %s\n", psmouse->serio->phys);

	psmouse->state = PSMOUSE_ACTIVATED;
}

/*
 * psmouse_cleanup() resets the mouse into power-on state.
 */

static void psmouse_cleanup(struct serio *serio)
{
	struct psmouse *psmouse = serio->private;
	unsigned char param[2];
	psmouse_command(psmouse, param, PSMOUSE_CMD_RESET_BAT);
}

/*
 * psmouse_disconnect() closes and frees.
 */

static void psmouse_disconnect(struct serio *serio)
{
	struct psmouse *psmouse = serio->private;

	psmouse->state = PSMOUSE_IGNORE;

	if (psmouse->ptport) {
		if (psmouse->ptport->deactivate)
			psmouse->ptport->deactivate(psmouse);
		__serio_unregister_port(&psmouse->ptport->serio); /* we have serio_sem */
		kfree(psmouse->ptport);
		psmouse->ptport = NULL;
	}

	if (psmouse->disconnect)
		psmouse->disconnect(psmouse);

	input_unregister_device(&psmouse->dev);
	serio_close(serio);
	kfree(psmouse);
}

/*
 * psmouse_connect() is a callback from the serio module when
 * an unhandled serio port is found.
 */
static void psmouse_connect(struct serio *serio, struct serio_dev *dev)
{
	struct psmouse *psmouse;
	
	if ((serio->type & SERIO_TYPE) != SERIO_8042 &&
	    (serio->type & SERIO_TYPE) != SERIO_PS_PSTHRU)
		return;

	if (!(psmouse = kmalloc(sizeof(struct psmouse), GFP_KERNEL)))
		return;

	memset(psmouse, 0, sizeof(struct psmouse));

	init_input_dev(&psmouse->dev);
	psmouse->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_REL);
	psmouse->dev.keybit[LONG(BTN_MOUSE)] = BIT(BTN_LEFT) | BIT(BTN_MIDDLE) | BIT(BTN_RIGHT);
	psmouse->dev.relbit[0] = BIT(REL_X) | BIT(REL_Y);

	psmouse->state = PSMOUSE_NEW_DEVICE;
	psmouse->serio = serio;
	psmouse->dev.private = psmouse;

	serio->private = psmouse;
	if (serio_open(serio, dev)) {
		kfree(psmouse);
		serio->private = NULL;
		return;
	}

	if (psmouse_probe(psmouse) <= 0) {
		serio_close(serio);
		kfree(psmouse);
		serio->private = NULL;
		return;
	}
	
	sprintf(psmouse->devname, "%s %s %s",
		psmouse_protocols[psmouse->type], psmouse->vendor, psmouse->name);
	sprintf(psmouse->phys, "%s/input0",
		serio->phys);

	psmouse->dev.name = psmouse->devname;
	psmouse->dev.phys = psmouse->phys;
	psmouse->dev.id.bustype = BUS_I8042;
	psmouse->dev.id.vendor = 0x0002;
	psmouse->dev.id.product = psmouse->type;
	psmouse->dev.id.version = psmouse->model;

	input_register_device(&psmouse->dev);
	
	printk(KERN_INFO "input: %s on %s\n", psmouse->devname, serio->phys);

	psmouse_initialize(psmouse);

	if (psmouse->ptport) {
		printk(KERN_INFO "serio: %s port at %s\n", psmouse->ptport->serio.name, psmouse->phys);
		__serio_register_port(&psmouse->ptport->serio); /* we have serio_sem */
		if (psmouse->ptport->activate)
			psmouse->ptport->activate(psmouse);
	}

	psmouse_activate(psmouse);
}


static int psmouse_reconnect(struct serio *serio)
{
	struct psmouse *psmouse = serio->private;
	struct serio_dev *dev = serio->dev;
	int old_type = psmouse->type;

	if (!dev) {
		printk(KERN_DEBUG "psmouse: reconnect request, but serio is disconnected, ignoring...\n");
		return -1;
	}

	psmouse->state = PSMOUSE_NEW_DEVICE;
	psmouse->type = psmouse->acking = psmouse->cmdcnt = psmouse->pktcnt = 0;
	if (psmouse->reconnect) {
	       if (psmouse->reconnect(psmouse))
			return -1;
	} else if (psmouse_probe(psmouse) != old_type)
		return -1;

	/* ok, the device type (and capabilities) match the old one,
	 * we can continue using it, complete intialization
	 */
	psmouse->type = old_type;
	psmouse_initialize(psmouse);

	if (psmouse->ptport) {
       		if (psmouse_reconnect(&psmouse->ptport->serio)) {
			__serio_unregister_port(&psmouse->ptport->serio);
			__serio_register_port(&psmouse->ptport->serio);
			if (psmouse->ptport->activate)
				psmouse->ptport->activate(psmouse);
		}
	}

	psmouse_activate(psmouse);
	return 0;
}


static struct serio_dev psmouse_dev = {
	.interrupt =	psmouse_interrupt,
	.connect =	psmouse_connect,
	.reconnect =	psmouse_reconnect,
	.disconnect =	psmouse_disconnect,
	.cleanup =	psmouse_cleanup,
};

static inline void psmouse_parse_proto(void)
{
	if (psmouse_proto) {
		if (!strcmp(psmouse_proto, "bare"))
			psmouse_max_proto = PSMOUSE_PS2;
		else if (!strcmp(psmouse_proto, "imps"))
			psmouse_max_proto = PSMOUSE_IMPS;
		else if (!strcmp(psmouse_proto, "exps"))
			psmouse_max_proto = PSMOUSE_IMEX;
		else
			printk(KERN_ERR "psmouse: unknown protocol type '%s'\n", psmouse_proto);
	}
}

int __init psmouse_init(void)
{
	psmouse_parse_proto();
	serio_register_device(&psmouse_dev);
	return 0;
}

void __exit psmouse_exit(void)
{
	serio_unregister_device(&psmouse_dev);
}

module_init(psmouse_init);
module_exit(psmouse_exit);
