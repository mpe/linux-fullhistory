/*
 * vrc4171_card.c, NEC VRC4171 Card Controller driver for Socket Services.
 *
 * Copyright (C) 2003  Yoichi Yuasa <yuasa@hh.iij4u.or.jp>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/types.h>

#include <asm/io.h>
#include <asm/vr41xx/vrc4171.h>

#include <pcmcia/ss.h>

#include "i82365.h"

MODULE_DESCRIPTION("NEC VRC4171 Card Controllers driver for Socket Services");
MODULE_AUTHOR("Yoichi Yuasa <yuasa@hh.iij4u.or.jp>");
MODULE_LICENSE("GPL");

#define CARD_MAX_SLOTS		2
#define CARD_SLOTA		0
#define CARD_SLOTB		1
#define CARD_SLOTB_OFFSET	0x40

#define CARD_MEM_START		0x10000000
#define CARD_MEM_END		0x13ffffff
#define CARD_MAX_MEM_OFFSET	0x3ffffff
#define CARD_MAX_MEM_SPEED	1000

#define CARD_CONTROLLER_INDEX	0x03e0
#define CARD_CONTROLLER_DATA	0x03e1
#define CARD_CONTROLLER_SIZE	2
 /* Power register */
  #define VPP_GET_VCC		0x01
  #define POWER_ENABLE		0x10
 #define CARD_VOLTAGE_SENSE	0x1f
  #define VCC_3VORXV_CAPABLE	0x00
  #define VCC_XV_ONLY		0x01
  #define VCC_3V_CAPABLE	0x02
  #define VCC_5V_ONLY		0x03
 #define CARD_VOLTAGE_SELECT	0x2f
  #define VCC_3V		0x01
  #define VCC_5V		0x00
  #define VCC_XV		0x02
  #define VCC_STATUS_3V		0x02
  #define VCC_STATUS_5V		0x01
  #define VCC_STATUS_XV		0x03
 #define GLOBAL_CONTROL		0x1e
  #define EXWRBK		0x04
  #define IRQPM_EN		0x08
  #define CLRPMIRQ		0x10

#define IO_MAX_MAPS	2
#define MEM_MAX_MAPS	5

enum {
	SLOT_PROBE = 0,
	SLOT_NOPROBE_IO,
	SLOT_NOPROBE_MEM,
	SLOT_NOPROBE_ALL
};

typedef struct vrc4171_socket {
	int noprobe;
	struct pcmcia_socket pcmcia_socket;
	char name[24];
	int csc_irq;
	int io_irq;
} vrc4171_socket_t;

static vrc4171_socket_t vrc4171_sockets[CARD_MAX_SLOTS];
static int vrc4171_slotb = SLOTB_IS_NONE;
static unsigned int vrc4171_irq;
static uint16_t vrc4171_irq_mask = 0xdeb8;

static inline uint8_t exca_read_byte(int slot, uint8_t index)
{
	if (slot == CARD_SLOTB)
		index += CARD_SLOTB_OFFSET;

	outb(index, CARD_CONTROLLER_INDEX);
	return inb(CARD_CONTROLLER_DATA);
}

static inline uint16_t exca_read_word(int slot, uint8_t index)
{
	uint16_t data;

	if (slot == CARD_SLOTB)
		index += CARD_SLOTB_OFFSET;

	outb(index++, CARD_CONTROLLER_INDEX);
	data = inb(CARD_CONTROLLER_DATA);

	outb(index, CARD_CONTROLLER_INDEX);
	data |= ((uint16_t)inb(CARD_CONTROLLER_DATA)) << 8;

	return data;
}

static inline uint8_t exca_write_byte(int slot, uint8_t index, uint8_t data)
{
	if (slot == CARD_SLOTB)
		index += CARD_SLOTB_OFFSET;

	outb(index, CARD_CONTROLLER_INDEX);
	outb(data, CARD_CONTROLLER_DATA);

	return data;
}

static inline uint16_t exca_write_word(int slot, uint8_t index, uint16_t data)
{
	if (slot == CARD_SLOTB)
		index += CARD_SLOTB_OFFSET;

	outb(index++, CARD_CONTROLLER_INDEX);
	outb(data, CARD_CONTROLLER_DATA);

	outb(index, CARD_CONTROLLER_INDEX);
	outb((uint8_t)(data >> 8), CARD_CONTROLLER_DATA);

	return data;
}

static inline int search_nonuse_irq(void)
{
	int i;

	for (i = 0; i < 16; i++) {
		if (vrc4171_irq_mask & (1 << i)) {
			vrc4171_irq_mask &= ~(1 << i);
			return i;
		}
	}

	return -1;
}

static int pccard_init(struct pcmcia_socket *sock)
{
	vrc4171_socket_t *socket;
	unsigned int slot;

	sock->features |= SS_CAP_PCCARD | SS_CAP_PAGE_REGS;
	sock->irq_mask = 0;
	sock->map_size = 0x1000;
	sock->pci_irq = vrc4171_irq;

	slot = sock->sock;
	socket = &vrc4171_sockets[slot];
	socket->csc_irq = search_nonuse_irq();
	socket->io_irq = search_nonuse_irq();

	return 0;
}

static int pccard_suspend(struct pcmcia_socket *sock)
{
	return -EINVAL;
}

static int pccard_get_status(struct pcmcia_socket *sock, u_int *value)
{
	unsigned int slot;
	uint8_t status, sense;
	u_int val = 0;

	if (sock == NULL || sock->sock >= CARD_MAX_SLOTS || value == NULL)
		return -EINVAL;

	slot = sock->sock;

	status = exca_read_byte(slot, I365_STATUS);
	if (exca_read_byte(slot, I365_INTCTL) & I365_PC_IOCARD) {
		if (status & I365_CS_STSCHG)
			val |= SS_STSCHG;
	} else {
		if (!(status & I365_CS_BVD1))
			val |= SS_BATDEAD;
		else if ((status & (I365_CS_BVD1 | I365_CS_BVD2)) == I365_CS_BVD1)
			val |= SS_BATWARN;
	}
	if ((status & I365_CS_DETECT) == I365_CS_DETECT)
		val |= SS_DETECT;
	if (status & I365_CS_WRPROT)
		val |= SS_WRPROT;
	if (status & I365_CS_READY)
		val |= SS_READY;
	if (status & I365_CS_POWERON)
		val |= SS_POWERON;

	sense = exca_read_byte(slot, CARD_VOLTAGE_SENSE);
	switch (sense) {
	case VCC_3VORXV_CAPABLE:
		val |= SS_3VCARD | SS_XVCARD;
		break;
	case VCC_XV_ONLY:
		val |= SS_XVCARD;
		break;
	case VCC_3V_CAPABLE:
		val |= SS_3VCARD;
		break;
	default:
		/* 5V only */
		break;
	}

	*value = val;

	return 0;
}

static inline u_char get_Vcc_value(uint8_t voltage)
{
	switch (voltage) {
	case VCC_STATUS_3V:
		return 33;
	case VCC_STATUS_5V:
		return 50;
	default:
		break;
	}

	return 0;
}

static inline u_char get_Vpp_value(uint8_t power, u_char Vcc)
{
	if ((power & 0x03) == 0x01 || (power & 0x03) == 0x02)
		return Vcc;

	return 0;
}

static int pccard_get_socket(struct pcmcia_socket *sock, socket_state_t *state)
{
	unsigned int slot;
	uint8_t power, voltage, control, cscint;

	if (sock == NULL || sock->sock >= CARD_MAX_SLOTS || state == NULL)
		return -EINVAL;

	slot = sock->sock;

	power = exca_read_byte(slot, I365_POWER);
	voltage = exca_read_byte(slot, CARD_VOLTAGE_SELECT);

	state->Vcc = get_Vcc_value(voltage);
	state->Vpp = get_Vpp_value(power, state->Vcc);

	state->flags = 0;
	if (power & POWER_ENABLE)
		state->flags |= SS_PWR_AUTO;
	if (power & I365_PWR_OUT)
		state->flags |= SS_OUTPUT_ENA;

	control = exca_read_byte(slot, I365_INTCTL);
	if (control & I365_PC_IOCARD)
		state->flags |= SS_IOCARD;
	if (!(control & I365_PC_RESET))
		state->flags |= SS_RESET;

        cscint = exca_read_byte(slot, I365_CSCINT);
	state->csc_mask = 0;
	if (state->flags & SS_IOCARD) {
		if (cscint & I365_CSC_STSCHG)
			state->flags |= SS_STSCHG;
	} else {
		if (cscint & I365_CSC_BVD1)
			state->csc_mask |= SS_BATDEAD;
		if (cscint & I365_CSC_BVD2)
			state->csc_mask |= SS_BATWARN;
	}
	if (cscint & I365_CSC_READY)
		state->csc_mask |= SS_READY;
	if (cscint & I365_CSC_DETECT)
		state->csc_mask |= SS_DETECT;

	return 0;
}

static inline uint8_t set_Vcc_value(u_char Vcc)
{
	switch (Vcc) {
	case 33:
		return VCC_3V;
	case 50:
		return VCC_5V;
	}

	/* Small voltage is chosen for safety. */
	return VCC_3V;
}

static int pccard_set_socket(struct pcmcia_socket *sock, socket_state_t *state)
{
	vrc4171_socket_t *socket;
	unsigned int slot;
	uint8_t voltage, power, control, cscint;

	if (sock == NULL || sock->sock >= CARD_MAX_SLOTS ||
	    (state->Vpp != state->Vcc && state->Vpp != 0) ||
	    (state->Vcc != 50 && state->Vcc != 33 && state->Vcc != 0))
		return -EINVAL;

	slot = sock->sock;
	socket = &vrc4171_sockets[slot];

	spin_lock_irq(&sock->lock);

	voltage = set_Vcc_value(state->Vcc);
	exca_write_byte(slot, CARD_VOLTAGE_SELECT, voltage);

	power = POWER_ENABLE;
	if (state->Vpp == state->Vcc)
		power |= VPP_GET_VCC;
	if (state->flags & SS_OUTPUT_ENA)
		power |= I365_PWR_OUT;
	exca_write_byte(slot, I365_POWER, power);

	control = 0;
	if (state->io_irq != 0)
		control |= socket->io_irq;
	if (state->flags & SS_IOCARD)
		control |= I365_PC_IOCARD;
	if (state->flags & SS_RESET)
		control	&= ~I365_PC_RESET;
	else
		control |= I365_PC_RESET;
	exca_write_byte(slot, I365_INTCTL, control);

        cscint = 0;
        exca_write_byte(slot, I365_CSCINT, cscint);
	exca_read_byte(slot, I365_CSC);	/* clear CardStatus change */
	if (state->csc_mask != 0)
		cscint |= socket->csc_irq << 8;
	if (state->flags & SS_IOCARD) {
		if (state->csc_mask & SS_STSCHG)
			cscint |= I365_CSC_STSCHG;
	} else {
		if (state->csc_mask & SS_BATDEAD)
			cscint |= I365_CSC_BVD1;
		if (state->csc_mask & SS_BATWARN)
			cscint |= I365_CSC_BVD2;
	}
	if (state->csc_mask & SS_READY)
		cscint |= I365_CSC_READY;
	if (state->csc_mask & SS_DETECT)
		cscint |= I365_CSC_DETECT;
        exca_write_byte(slot, I365_CSCINT, cscint);

	spin_unlock_irq(&sock->lock);

	return 0;
}

static int pccard_set_io_map(struct pcmcia_socket *sock, struct pccard_io_map *io)
{
	unsigned int slot;
	uint8_t ioctl, addrwin;
	u_char map;

	if (sock == NULL || sock->sock >= CARD_MAX_SLOTS ||
	    io == NULL || io->map >= IO_MAX_MAPS ||
	    io->start > 0xffff || io->stop > 0xffff || io->start > io->stop)
		return -EINVAL;

	slot = sock->sock;
	map = io->map;

	addrwin = exca_read_byte(slot, I365_ADDRWIN);
	if (addrwin & I365_ENA_IO(map)) {
		addrwin &= ~I365_ENA_IO(map);
		exca_write_byte(slot, I365_ADDRWIN, addrwin);
	}

	exca_write_word(slot, I365_IO(map)+I365_W_START, io->start);
	exca_write_word(slot, I365_IO(map)+I365_W_STOP, io->stop);

	ioctl = 0;
	if (io->speed > 0)
		ioctl |= I365_IOCTL_WAIT(map);
	if (io->flags & MAP_16BIT)
		ioctl |= I365_IOCTL_16BIT(map);
	if (io->flags & MAP_AUTOSZ)
		ioctl |= I365_IOCTL_IOCS16(map);
	if (io->flags & MAP_0WS)
		ioctl |= I365_IOCTL_0WS(map);
	exca_write_byte(slot, I365_IOCTL, ioctl);

	if (io->flags & MAP_ACTIVE) {
		addrwin |= I365_ENA_IO(map);
		exca_write_byte(slot, I365_ADDRWIN, addrwin);
	}

	return 0;
}

static int pccard_set_mem_map(struct pcmcia_socket *sock, struct pccard_mem_map *mem)
{
	unsigned int slot;
	uint16_t start, stop, offset;
	uint8_t addrwin;
	u_char map;

	if (sock == NULL || sock->sock >= CARD_MAX_SLOTS ||
	    mem == NULL || mem->map >= MEM_MAX_MAPS ||
	    mem->sys_start < CARD_MEM_START || mem->sys_start > CARD_MEM_END ||
	    mem->sys_stop < CARD_MEM_START || mem->sys_stop > CARD_MEM_END ||
	    mem->sys_start > mem->sys_stop ||
	    mem->card_start > CARD_MAX_MEM_OFFSET ||
	    mem->speed > CARD_MAX_MEM_SPEED)
		return -EINVAL;

	slot = sock->sock;
	map = mem->map;

	addrwin = exca_read_byte(slot, I365_ADDRWIN);
	if (addrwin & I365_ENA_MEM(map)) {
		addrwin &= ~I365_ENA_MEM(map);
		exca_write_byte(slot, I365_ADDRWIN, addrwin);
	}

	start = (mem->sys_start >> 12) & 0x3fff;
	if (mem->flags & MAP_16BIT)
		start |= I365_MEM_16BIT;
	exca_write_word(slot, I365_MEM(map)+I365_W_START, start);

	stop = (mem->sys_stop >> 12) & 0x3fff;
	switch (mem->speed) {
	case 0:
		break;
	case 1:
		stop |= I365_MEM_WS0;
		break;
	case 2:
		stop |= I365_MEM_WS1;
		break;
	default:
		stop |= I365_MEM_WS0 | I365_MEM_WS1;
		break;
	}
	exca_write_word(slot, I365_MEM(map)+I365_W_STOP, stop);

	offset = (mem->card_start >> 12) & 0x3fff;
	if (mem->flags & MAP_ATTRIB)
		offset |= I365_MEM_REG;
	if (mem->flags & MAP_WRPROT)
		offset |= I365_MEM_WRPROT;
	exca_write_word(slot, I365_MEM(map)+I365_W_OFF, offset);

	if (mem->flags & MAP_ACTIVE) {
		addrwin |= I365_ENA_MEM(map);
		exca_write_byte(slot, I365_ADDRWIN, addrwin);
	}

	return 0;
}

static struct pccard_operations vrc4171_pccard_operations = {
	.init			= pccard_init,
	.suspend		= pccard_suspend,
	.get_status		= pccard_get_status,
	.get_socket		= pccard_get_socket,
	.set_socket		= pccard_set_socket,
	.set_io_map		= pccard_set_io_map,
	.set_mem_map		= pccard_set_mem_map,
};

static inline unsigned int get_events(int slot)
{
	unsigned int events = 0;
	uint8_t status, csc;

	status = exca_read_byte(slot, I365_STATUS);
	csc = exca_read_byte(slot, I365_CSC);

	if (exca_read_byte(slot, I365_INTCTL) & I365_PC_IOCARD) {
		if ((csc & I365_CSC_STSCHG) && (status & I365_CS_STSCHG))
			events |= SS_STSCHG;
	} else {
		if (csc & (I365_CSC_BVD1 | I365_CSC_BVD2)) {
			if (!(status & I365_CS_BVD1))
				events |= SS_BATDEAD;
			else if ((status & (I365_CS_BVD1 | I365_CS_BVD2)) == I365_CS_BVD1)
				events |= SS_BATWARN;
		}
	}
	if ((csc & I365_CSC_READY) && (status & I365_CS_READY))
		events |= SS_READY;
	if ((csc & I365_CSC_DETECT) && ((status & I365_CS_DETECT) == I365_CS_DETECT))
		events |= SS_DETECT;

	return events;
}

static irqreturn_t pccard_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	vrc4171_socket_t *socket;
	unsigned int events;
	irqreturn_t retval = IRQ_NONE;
	uint16_t status;

	status = vrc4171_get_irq_status();
	if (status & IRQ_A) {
		socket = &vrc4171_sockets[CARD_SLOTA];
		if (socket->noprobe == SLOT_PROBE) {
			if (status & (1 << socket->csc_irq)) {
				events = get_events(CARD_SLOTA);
				if (events != 0) {
					pcmcia_parse_events(&socket->pcmcia_socket, events);
					retval = IRQ_HANDLED;
				}
			}
		}
	}

	if (status & IRQ_B) {
		socket = &vrc4171_sockets[CARD_SLOTB];
		if (socket->noprobe == SLOT_PROBE) {
			if (status & (1 << socket->csc_irq)) {
				events = get_events(CARD_SLOTB);
				if (events != 0) {
					pcmcia_parse_events(&socket->pcmcia_socket, events);
					retval = IRQ_HANDLED;
				}
			}
		}
	}

	return retval;
}

static inline void reserve_using_irq(int slot)
{
	unsigned int irq;

	irq = exca_read_byte(slot, I365_INTCTL);
	irq &= 0x0f;
	vrc4171_irq_mask &= ~(1 << irq);

	irq = exca_read_byte(slot, I365_CSCINT);
	irq = (irq & 0xf0) >> 4;
	vrc4171_irq_mask &= ~(1 << irq);
}

static int __devinit vrc4171_add_socket(int slot)
{
	vrc4171_socket_t *socket;
	int retval;

	if (slot >= CARD_MAX_SLOTS)
		return -EINVAL;

	socket = &vrc4171_sockets[slot];
	if (socket->noprobe != SLOT_PROBE) {
		uint8_t addrwin;

		switch (socket->noprobe) {
		case SLOT_NOPROBE_MEM:
			addrwin = exca_read_byte(slot, I365_ADDRWIN);
			addrwin &= 0x1f;
			exca_write_byte(slot, I365_ADDRWIN, addrwin);
			break;
		case SLOT_NOPROBE_IO:
			addrwin = exca_read_byte(slot, I365_ADDRWIN);
			addrwin &= 0xc0;
			exca_write_byte(slot, I365_ADDRWIN, addrwin);
			break;
		default:
			break;
		}

		reserve_using_irq(slot);

		return 0;
	}

	sprintf(socket->name, "NEC VRC4171 Card Slot %1c", 'A' + slot);

	socket->pcmcia_socket.ops = &vrc4171_pccard_operations;

	retval = pcmcia_register_socket(&socket->pcmcia_socket);
	if (retval != 0)
		return retval;

	exca_write_byte(slot, I365_ADDRWIN, 0);

	exca_write_byte(slot, GLOBAL_CONTROL, 0);

	return 0;
}

static void vrc4171_remove_socket(int slot)
{
	vrc4171_socket_t *socket;

	if (slot >= CARD_MAX_SLOTS)
		return;

	socket = &vrc4171_sockets[slot];

	pcmcia_unregister_socket(&socket->pcmcia_socket);
}

static int __devinit vrc4171_card_setup(char *options)
{
	if (options == NULL || *options == '\0')
		return 0;

	if (strncmp(options, "irq:", 4) == 0) {
		int irq;
		options += 4;
		irq = simple_strtoul(options, &options, 0);
		if (irq >= 0 && irq < NR_IRQS)
			vrc4171_irq = irq;

		if (*options != ',')
			return 0;
		options++;
	}

	if (strncmp(options, "slota:", 6) == 0) {
		options += 6;
		if (*options != '\0') {
			if (strncmp(options, "memnoprobe", 10) == 0) {
				vrc4171_sockets[CARD_SLOTA].noprobe = SLOT_NOPROBE_MEM;
				options += 10;
			} else if (strncmp(options, "ionoprobe", 9) == 0) {
				vrc4171_sockets[CARD_SLOTA].noprobe = SLOT_NOPROBE_IO;
				options += 9;
			} else if ( strncmp(options, "noprobe", 7) == 0) {
				vrc4171_sockets[CARD_SLOTA].noprobe = SLOT_NOPROBE_ALL;
				options += 7;
			}

			if (*options != ',')
				return 0;
			options++;
		} else
			return 0;

	}

	if (strncmp(options, "slotb:", 6) == 0) {
		options += 6;
		if (*options != '\0') {
			if (strncmp(options, "pccard", 6) == 0) {
				vrc4171_slotb = SLOTB_IS_PCCARD;
				options += 6;
			} else if (strncmp(options, "cf", 2) == 0) {
				vrc4171_slotb = SLOTB_IS_CF;
				options += 2;
			} else if (strncmp(options, "flashrom", 8) == 0) {
				vrc4171_slotb = SLOTB_IS_FLASHROM;
				options += 8;
			} else if (strncmp(options, "none", 4) == 0) {
				vrc4171_slotb = SLOTB_IS_NONE;
				options += 4;
			}

			if (*options != ',')
				return 0;
			options++;

			if (strncmp(options, "memnoprobe", 10) == 0)
				vrc4171_sockets[CARD_SLOTB].noprobe = SLOT_NOPROBE_MEM;
			if (strncmp(options, "ionoprobe", 9) == 0)
				vrc4171_sockets[CARD_SLOTB].noprobe = SLOT_NOPROBE_IO;
			if (strncmp(options, "noprobe", 7) == 0)
				vrc4171_sockets[CARD_SLOTB].noprobe = SLOT_NOPROBE_ALL;
		}
	}

	return 0;
}

__setup("vrc4171_card=", vrc4171_card_setup);

static int __devinit vrc4171_card_init(void)
{
	int retval, slot;

	vrc4171_set_multifunction_pin(vrc4171_slotb);

	if (request_region(CARD_CONTROLLER_INDEX, CARD_CONTROLLER_SIZE,
	                       "NEC VRC4171 Card Controller") == NULL)
		return -EBUSY;

	for (slot = 0; slot < CARD_MAX_SLOTS; slot++) {
		if (slot == CARD_SLOTB && vrc4171_slotb == SLOTB_IS_NONE)
			break;

		retval = vrc4171_add_socket(slot);
		if (retval != 0)
			return retval;
	}

	retval = request_irq(vrc4171_irq, pccard_interrupt, SA_SHIRQ,
	                     "NEC VRC4171 Card Controller", vrc4171_sockets);
	if (retval < 0) {
		for (slot = 0; slot < CARD_MAX_SLOTS; slot++)
			vrc4171_remove_socket(slot);

		return retval;
	}

	printk(KERN_INFO "NEC VRC4171 Card Controller, connected to IRQ %d\n", vrc4171_irq);

	return 0;
}

static void __devexit vrc4171_card_exit(void)
{
	int slot;

	for (slot = 0; slot < CARD_MAX_SLOTS; slot++)
		vrc4171_remove_socket(slot);

	release_region(CARD_CONTROLLER_INDEX, CARD_CONTROLLER_SIZE);
}

module_init(vrc4171_card_init);
module_exit(vrc4171_card_exit);
