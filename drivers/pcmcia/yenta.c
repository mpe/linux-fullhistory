/*
 * Regular lowlevel cardbus driver ("yenta")
 *
 * (C) Copyright 1999 Linus Torvalds
 */
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include <pcmcia/ss.h>

#include <asm/io.h>

#include "yenta.h"
#include "i82365.h"
#include "ricoh.h"

/* Don't ask.. */
#define to_cycles(ns)	((ns)/120)
#define to_ns(cycles)	((cycles)*120)

/*
 * Generate easy-to-use ways of reading a cardbus sockets
 * regular memory space ("cb_xxx"), configuration space
 * ("config_xxx") and compatibility space ("exca_xxxx")
 */
#define cb_readl(sock,reg)	readl((sock)->base + (reg))
#define cb_writel(sock,reg,val) writel(val,(sock)->base + (reg))

static inline u8 config_readb(pci_socket_t *socket, unsigned offset)
{
	u8 val;
	pci_read_config_byte(socket->dev, offset, &val);
	return val;
}

static inline u16 config_readw(pci_socket_t *socket, unsigned offset)
{
	u16 val;
	pci_read_config_word(socket->dev, offset, &val);
	return val;
}

static inline u32 config_readl(pci_socket_t *socket, unsigned offset)
{
	u32 val;
	pci_read_config_dword(socket->dev, offset, &val);
	return val;
}

#define config_writeb(s,off,val) pci_write_config_byte((s)->dev, (off), (val))
#define config_writew(s,off,val) pci_write_config_word((s)->dev, (off), (val))
#define config_writel(s,off,val) pci_write_config_dword((s)->dev, (off), (val))

#define exca_readb(sock,reg)	readb((sock)->base + 0x800 + (reg))
#define exca_writeb(sock,reg,v)	writeb((v), (sock)->base + 0x800 + (reg))

static u16 exca_readw(pci_socket_t *socket, unsigned reg)
{
	return exca_readb(socket, reg) | (exca_readb(socket, reg+1) << 8);
}

static void exca_writew(pci_socket_t *socket, unsigned reg, u16 val)
{
	exca_writeb(socket, reg, val);
	exca_writeb(socket, reg+1, val >> 8);
}

static int yenta_inquire(pci_socket_t *socket, socket_cap_t *cap)
{
	*cap = socket->cap;
	return 0;
}

/*
 * Ugh, mixed-mode cardbus and 16-bit pccard state: things depend
 * on what kind of card is inserted..
 */
static int yenta_get_status(pci_socket_t *socket, unsigned int *value)
{
	unsigned int val;
	u32 state = cb_readl(socket, CB_SOCKET_STATE);

	val  = (state & CB_3VCARD) ? SS_3VCARD : 0;
	val |= (state & CB_XVCARD) ? SS_XVCARD : 0;

	if (state & CB_CBCARD) {
		val |= SS_CARDBUS;	
		val |= (state & CB_CARDSTS) ? SS_STSCHG : 0;
		val |= (state & (CB_CDETECT1 | CB_CDETECT2)) ? 0 : SS_DETECT;
		val |= (state & CB_PWRCYCLE) ? SS_POWERON | SS_READY : 0;
	} else {
		u8 status = exca_readb(socket, I365_STATUS);
		val |= ((status & I365_CS_DETECT) == I365_CS_DETECT) ? SS_DETECT : 0;
		if (exca_readb(socket, I365_INTCTL) & I365_PC_IOCARD) {
			val |= (status & I365_CS_STSCHG) ? 0 : SS_STSCHG;
		} else {
			val |= (status & I365_CS_BVD1) ? 0 : SS_BATDEAD;
			val |= (status & I365_CS_BVD2) ? 0 : SS_BATWARN;
		}
		val |= (status & I365_CS_WRPROT) ? SS_WRPROT : 0;
		val |= (status & I365_CS_READY) ? SS_READY : 0;
		val |= (status & I365_CS_POWERON) ? SS_POWERON : 0;
	}

	*value = val;
	return 0;
}

static int yenta_Vcc_power(u32 control)
{
	switch (control & CB_SC_VCC_MASK) {
	case CB_SC_VCC_5V: return 50;
	case CB_SC_VCC_3V: return 33;
	default: return 0;
	}
}

static int yenta_Vpp_power(u32 control)
{
	switch (control & CB_SC_VPP_MASK) {
	case CB_SC_VPP_12V: return 120;
	case CB_SC_VPP_5V: return 50;
	case CB_SC_VPP_3V: return 33;
	default: return 0;
	}
}

static int yenta_get_socket(pci_socket_t *socket, socket_state_t *state)
{
	u8 reg;
	u32 control;

	control = cb_readl(socket, CB_SOCKET_CONTROL);

	state->Vcc = yenta_Vcc_power(control);
	state->Vpp = yenta_Vpp_power(control);
	state->io_irq = socket->io_irq;

	if (cb_readl(socket, CB_SOCKET_STATE) & CB_CBCARD) {
		u16 bridge = config_readw(socket, CB_BRIDGE_CONTROL);
		if (bridge & CB_BRIDGE_CRST)
			state->flags |= SS_RESET;
		return 0;
	}

	/* 16-bit card state.. */
	reg = exca_readb(socket, I365_POWER);
	state->flags = (reg & I365_PWR_AUTO) ? SS_PWR_AUTO : 0;
	state->flags |= (reg & I365_PWR_OUT) ? SS_OUTPUT_ENA : 0;

	reg = exca_readb(socket, I365_INTCTL);
	state->flags |= (reg & I365_PC_RESET) ? 0 : SS_RESET;
	state->flags |= (reg & I365_PC_IOCARD) ? SS_IOCARD : 0;

	reg = exca_readb(socket, I365_CSCINT);
	state->csc_mask = (reg & I365_CSC_DETECT) ? SS_DETECT : 0;
	if (state->flags & SS_IOCARD) {
		state->csc_mask |= (reg & I365_CSC_STSCHG) ? SS_STSCHG : 0;
	} else {
		state->csc_mask |= (reg & I365_CSC_BVD1) ? SS_BATDEAD : 0;
		state->csc_mask |= (reg & I365_CSC_BVD2) ? SS_BATWARN : 0;
		state->csc_mask |= (reg & I365_CSC_READY) ? SS_READY : 0;
	}

	return 0;
}

static void yenta_set_power(pci_socket_t *socket, socket_state_t *state)
{
	u32 reg = 0;	/* CB_SC_STPCLK? */
	switch (state->Vcc) {
	case 33: reg = CB_SC_VCC_3V; break;
	case 50: reg = CB_SC_VCC_5V; break;
	default: reg = 0; break;
	}
	switch (state->Vpp) {
	case 33:  reg |= CB_SC_VPP_3V; break;
	case 50:  reg |= CB_SC_VPP_5V; break;
	case 120: reg |= CB_SC_VPP_12V; break;
	}
	if (reg != cb_readl(socket, CB_SOCKET_CONTROL))
		cb_writel(socket, CB_SOCKET_CONTROL, reg);
}

static void yenta_bridge_control(pci_socket_t *socket, u16 bridgectl)
{
	struct pci_dev *dev = socket->dev;

	/* MAGIC NUMBERS! Fixme */
	config_writew(socket, CB_BRIDGE_CONTROL, bridgectl);
	config_writel(socket, CB_LEGACY_MODE_BASE, 0);

	config_writel(socket, PCI_BASE_ADDRESS_0, dev->resource[0].start);
	config_writew(socket, PCI_COMMAND,
			PCI_COMMAND_IO |
			PCI_COMMAND_MEMORY |
			PCI_COMMAND_MASTER |
			PCI_COMMAND_WAIT);
	config_writeb(socket, PCI_CACHE_LINE_SIZE, 32);
	config_writeb(socket, PCI_LATENCY_TIMER, 168);
	config_writeb(socket, PCI_SEC_LATENCY_TIMER, 176);
	config_writeb(socket, PCI_PRIMARY_BUS, dev->bus->number);
	config_writeb(socket, PCI_SECONDARY_BUS, dev->subordinate->number);
	config_writeb(socket, PCI_SUBORDINATE_BUS, dev->subordinate->number);
	config_writew(socket, CB_BRIDGE_CONTROL, bridgectl);
}

static int yenta_set_socket(pci_socket_t *socket, socket_state_t *state)
{
	u16 bridge;

	yenta_set_power(socket, state);

	bridge = config_readw(socket, CB_BRIDGE_CONTROL) & ~CB_BRIDGE_CRST;

	if (cb_readl(socket, CB_SOCKET_STATE) & CB_CBCARD) {
		bridge |= (state->flags & SS_RESET) ? CB_BRIDGE_CRST : 0;

		/* ISA interrupt control? */
		if (bridge & CB_BRIDGE_INTR) {
			u8 intr = exca_readb(socket, I365_INTCTL);
			intr = (intr & ~0xf) | state->io_irq;
			exca_writeb(socket, I365_INTCTL, intr);
		}
	} else {
		u8 reg;

		reg = exca_readb(socket, I365_INTCTL) & I365_RING_ENA;
		reg |= (state->flags & SS_RESET) ? 0 : I365_PC_RESET;
		reg |= (state->flags & SS_IOCARD) ? I365_PC_IOCARD : 0;
		reg |= I365_INTR_ENA;			/* CSC to PCI interrupt */
		reg |= state->io_irq;
		exca_writeb(socket, I365_INTCTL, reg);

		reg = exca_readb(socket, I365_POWER) & (I365_VCC_MASK|I365_VPP1_MASK);
		reg |= I365_PWR_NORESET;
		if (state->flags & SS_PWR_AUTO) reg |= I365_PWR_AUTO;
		if (state->flags & SS_OUTPUT_ENA) reg |= I365_PWR_OUT;
		if (exca_readb(socket, I365_POWER) != reg)
			exca_writeb(socket, I365_POWER, reg);

		/* CSC interrupt: no ISA irq for CSC */
		reg = I365_CSC_DETECT;
		if (state->flags & SS_IOCARD) {
			if (state->csc_mask & SS_STSCHG) reg |= I365_CSC_STSCHG;
		} else {
			if (state->csc_mask & SS_BATDEAD) reg |= I365_CSC_BVD1;
			if (state->csc_mask & SS_BATWARN) reg |= I365_CSC_BVD2;
			if (state->csc_mask & SS_READY) reg |= I365_CSC_READY;
		}
		exca_writeb(socket, I365_CSCINT, reg);
		exca_readb(socket, I365_CSC);
	}
	yenta_bridge_control(socket, bridge);

	/* Socket event mask: get card insert/remove events.. */
	cb_writel(socket, CB_SOCKET_EVENT, -1);
	cb_writel(socket, CB_SOCKET_MASK, CB_CDMASK);
	return 0;
}

static int yenta_get_io_map(pci_socket_t *socket, struct pccard_io_map *io)
{
	int map;
	unsigned char ioctl, addr;

	map = io->map;
	if (map > 1)
		return -EINVAL;

	io->start = exca_readw(socket, I365_IO(map)+I365_W_START);
	io->stop = exca_readw(socket, I365_IO(map)+I365_W_STOP);

	ioctl = exca_readb(socket, I365_IOCTL);
	addr = exca_readb(socket, I365_ADDRWIN);
	io->speed = to_ns(ioctl & I365_IOCTL_WAIT(map)) ? 1 : 0;
	io->flags  = (addr & I365_ENA_IO(map)) ? MAP_ACTIVE : 0;
	io->flags |= (ioctl & I365_IOCTL_0WS(map)) ? MAP_0WS : 0;
	io->flags |= (ioctl & I365_IOCTL_16BIT(map)) ? MAP_16BIT : 0;
	io->flags |= (ioctl & I365_IOCTL_IOCS16(map)) ? MAP_AUTOSZ : 0;

	return 0;
}

static int yenta_set_io_map(pci_socket_t *socket, struct pccard_io_map *io)
{
	int map;
	unsigned char ioctl, addr, enable;

	map = io->map;

	if (map > 1)
		return -EINVAL;

	enable = I365_ENA_IO(map);
	addr = exca_readb(socket, I365_ADDRWIN);

	/* Disable the window before changing it.. */
	if (addr & enable) {
		addr &= ~enable;
		exca_writeb(socket, I365_ADDRWIN, addr);
	}

	exca_writew(socket, I365_IO(map)+I365_W_START, io->start);
	exca_writew(socket, I365_IO(map)+I365_W_STOP, io->stop);

	ioctl = exca_readb(socket, I365_IOCTL) & ~I365_IOCTL_MASK(map);
	if (io->flags & MAP_0WS) ioctl |= I365_IOCTL_0WS(map);
	if (io->flags & MAP_16BIT) ioctl |= I365_IOCTL_16BIT(map);
	if (io->flags & MAP_AUTOSZ) ioctl |= I365_IOCTL_IOCS16(map);
	exca_writeb(socket, I365_IOCTL, ioctl);

	if (io->flags & MAP_ACTIVE)
		exca_writeb(socket, I365_ADDRWIN, addr | enable);
	return 0;
}

static int yenta_get_mem_map(pci_socket_t *socket, struct pccard_mem_map *mem)
{
	int map;
	unsigned char addr;
	unsigned int start, stop, page, offset;

	map = mem->map;
	if (map > 4)
		return -EINVAL;

	addr = exca_readb(socket, I365_ADDRWIN);
	mem->flags = (addr & I365_ENA_MEM(map)) ? MAP_ACTIVE : 0;

	start = exca_readw(socket, I365_MEM(map) + I365_W_START);
	mem->flags |= (start & I365_MEM_16BIT) ? MAP_16BIT : 0;
	mem->flags |= (start & I365_MEM_0WS) ? MAP_0WS : 0;
	start = (start & 0x0fff) << 12;

	stop = exca_readw(socket, I365_MEM(map) + I365_W_STOP);
	mem->speed = to_ns(stop >> 14);
	stop = ((stop & 0x0fff) << 12) + 0x0fff;

	offset = exca_readw(socket, I365_MEM(map) + I365_W_OFF);
	mem->flags |= (offset & I365_MEM_WRPROT) ? MAP_WRPROT : 0;
	mem->flags |= (offset & I365_MEM_REG) ? MAP_ATTRIB : 0;
	offset = ((offset & 0x3fff) << 12) + start;
	mem->card_start = offset & 0x3ffffff;

	page = exca_readb(socket, CB_MEM_PAGE(map)) << 24;
	mem->sys_start = start + page;
	mem->sys_stop = start + page;

	return 0;
}

static int yenta_set_mem_map(pci_socket_t *socket, struct pccard_mem_map *mem)
{
	int map;
	unsigned char addr, enable;
	unsigned int start, stop, card_start;
	unsigned short word;

	map = mem->map;
	start = mem->sys_start;
	stop = mem->sys_stop;
	card_start = mem->card_start;

	if (map > 4 || start > stop || ((start ^ stop) >> 24) ||
	    (card_start >> 26) || mem->speed > 1000)
		return -EINVAL;

	enable = I365_ENA_MEM(map);
	addr = exca_readb(socket, I365_ADDRWIN);
	if (addr & enable) {
		addr &= ~enable;
		exca_writeb(socket, I365_ADDRWIN, addr);
	}

	exca_writeb(socket, CB_MEM_PAGE(map), start >> 24);

	word = (start >> 12) & 0x0fff;
	if (mem->flags & MAP_16BIT)
		word |= I365_MEM_16BIT;
	if (mem->flags & MAP_0WS)
		word |= I365_MEM_0WS;
	exca_writew(socket, I365_MEM(map) + I365_W_START, word);

	word = (stop >> 12) & 0x0fff;
	switch (to_cycles(mem->speed)) {
		case 0: break;
		case 1:  word |= I365_MEM_WS0; break;
		case 2:  word |= I365_MEM_WS1; break;
		default: word |= I365_MEM_WS1 | I365_MEM_WS0; break;
	}
	exca_writew(socket, I365_MEM(map) + I365_W_STOP, word);

	word = ((card_start - start) >> 12) & 0x3fff;
	if (mem->flags & MAP_WRPROT)
		word |= I365_MEM_WRPROT;
	if (mem->flags & MAP_ATTRIB)
		word |= I365_MEM_REG;
	exca_writew(socket, I365_MEM(map) + I365_W_OFF, word);

	if (mem->flags & MAP_ACTIVE)
		exca_writeb(socket, I365_ADDRWIN, addr | enable);
	return 0;
}

static int yenta_get_bridge(pci_socket_t *socket, struct cb_bridge_map *m)
{
	unsigned map;
	
	map = m->map;
	if (map > 1)
		return -EINVAL;

	m->flags &= MAP_IOSPACE;
	map += (m->flags & MAP_IOSPACE) ? 2 : 0;
	m->start = config_readl(socket, CB_BRIDGE_BASE(map));
	m->stop = config_readl(socket, CB_BRIDGE_LIMIT(map));
	if (m->start || m->stop) {
		m->flags |= MAP_ACTIVE;
		m->stop |= (map > 1) ? 3 : 0x0fff;
	}

	/* Get prefetch state for memory mappings */
	if (map < 2) {
		u16 ctrl, prefetch_mask = CB_BRIDGE_PREFETCH0 << map;

		ctrl = config_readw(socket, CB_BRIDGE_CONTROL);
		m->flags |= (ctrl & prefetch_mask) ? MAP_PREFETCH : 0;
	}
	return 0;
}

static int yenta_set_bridge(pci_socket_t *socket, struct cb_bridge_map *m)
{
	unsigned map;
	u32 start, end;
	
	map = m->map;
	if (map > 1 || m->stop < m->start)
		return -EINVAL;

	if (m->flags & MAP_IOSPACE) {
		if ((m->stop > 0xffff) || (m->start & 3) ||
		    ((m->stop & 3) != 3))
			return -EINVAL;
		map += 2;
	} else {
		u16 ctrl, prefetch_mask = CB_BRIDGE_PREFETCH0 << map;

		if ((m->start & 0x0fff) || ((m->stop & 0x0fff) != 0x0fff))
			return -EINVAL;
		ctrl = config_readw(socket, CB_BRIDGE_CONTROL);
		ctrl &= ~prefetch_mask;
		ctrl |= (m->flags & MAP_PREFETCH) ? prefetch_mask : 0;
		config_writew(socket, CB_BRIDGE_CONTROL, ctrl);
	}

	start = 0;
	end = 0;
	if (m->flags & MAP_ACTIVE) {
		start = m->start;
		end = m->stop;
	}
	config_writel(socket, CB_BRIDGE_BASE(map), start);
	config_writel(socket, CB_BRIDGE_LIMIT(map), end);
	return 0;
}

static void yenta_proc_setup(pci_socket_t *socket, struct proc_dir_entry *base)
{
	/* Not done yet */
}

static void yenta_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	u8 csc;
	u32 cb_event;
	unsigned int events;
	pci_socket_t *socket = (pci_socket_t *) dev_id;

	/* Clear interrupt status for the event */
	cb_event = cb_readl(socket, CB_SOCKET_EVENT);
	cb_writel(socket, CB_SOCKET_EVENT, cb_event);

	csc = exca_readb(socket, I365_CSC);

	events = (cb_event & (CB_CD1EVENT | CB_CD2EVENT)) ? SS_DETECT : 0 ;
	events |= (csc & I365_CSC_DETECT) ? SS_DETECT : 0;
	if (exca_readb(socket, I365_INTCTL) & I365_PC_IOCARD) {
		events |= (csc & I365_CSC_STSCHG) ? SS_STSCHG : 0;
	} else {
		events |= (csc & I365_CSC_BVD1) ? SS_BATDEAD : 0;
		events |= (csc & I365_CSC_BVD2) ? SS_BATWARN : 0;
		events |= (csc & I365_CSC_READY) ? SS_READY : 0;
	}

	if (events && socket->handler)
		socket->handler(socket->info, events);
}

static unsigned int yenta_probe_irq(pci_socket_t *socket)
{
	int i;
	unsigned long val;
	u16 bridge_ctrl;

	/* Are we set up to route the IO irq to the PCI irq? */
	bridge_ctrl = config_readw(socket, CB_BRIDGE_CONTROL);
	if (!(bridge_ctrl & CB_BRIDGE_INTR)) {
		socket->io_irq = socket->cb_irq;
		if (socket->cb_irq && socket->cb_irq < 16)
			return 1 << socket->cb_irq;

		printk("CardBus: Hmm.. Routing interrupts to bad PCI irq %d\n", socket->cb_irq);
		return 0;
	}

	cb_writel(socket, CB_SOCKET_EVENT, -1);
	cb_writel(socket, CB_SOCKET_MASK, CB_CSTSMASK);
	val = probe_irq_on();
	for (i = 1; i < 16; i++) {
		if (!((val >> i) & 1))
			continue;
		exca_writeb(socket, I365_CSCINT, I365_CSC_STSCHG | (i << 4));
		cb_writel(socket, CB_SOCKET_FORCE, CB_FCARDSTS);
		udelay(100);
		cb_writel(socket, CB_SOCKET_EVENT, -1);
	}
	cb_writel(socket, CB_SOCKET_MASK, 0);
	return probe_irq_mask(val);
}

/* Called at resume and initialization events */
static int yenta_init(pci_socket_t *socket)
{
	int i;
	pccard_io_map io = { 0, 0, 0, 0, 1 };
	pccard_mem_map mem = { 0, 0, 0, 0, 0, 0 };

	pci_set_power_state(socket->dev, 0);

	/* MAGIC NUMBERS! Fixme */
	config_writeb(socket, PCI_LATENCY_TIMER, 168);
	config_writeb(socket, PCI_SEC_LATENCY_TIMER, 176);

	exca_writeb(socket, I365_GBLCTL, 0x00);
	exca_writeb(socket, I365_GENCTL, 0x00);

	mem.sys_stop = 0x0fff;
	yenta_set_socket(socket, &dead_socket);
	for (i = 0; i < 2; i++) {
		io.map = i;
		yenta_set_io_map(socket, &io);
	}
	for (i = 0; i < 5; i++) {
		mem.map = i;
		yenta_set_mem_map(socket, &mem);
	}
	return 0;
}

/*
 * More of an example than anything else... The standard
 * yenta init code works well enough - but this is how
 * you'd do it if you wanted to have a special init sequence.
 */
static int ricoh_init(pci_socket_t *socket)
{
	u16 misc = config_readw(socket, RL5C4XX_MISC);
	u16 ctl = config_readw(socket, RL5C4XX_16BIT_CTL);
	u16 io = config_readw(socket, RL5C4XX_16BIT_IO_0);
	u16 mem = config_readw(socket, RL5C4XX_16BIT_MEM_0);

	ctl = RL5C4XX_16CTL_IO_TIMING | RL5C4XX_16CTL_MEM_TIMING;

	config_writew(socket, RL5C4XX_MISC, misc);
	config_writew(socket, RL5C4XX_16BIT_CTL, ctl);
	config_writew(socket, RL5C4XX_16BIT_IO_0, io);
	config_writew(socket, RL5C4XX_16BIT_MEM_0, mem);

	return yenta_init(socket);
}


static int yenta_suspend(pci_socket_t *socket)
{
	yenta_set_socket(socket, &dead_socket);
	pci_set_power_state(socket->dev, 3);
	return 0;
}

/*
 * Set static data that doesn't need re-initializing..
 */
static void yenta_get_socket_capabilities(pci_socket_t *socket)
{
	socket->cap.features |= SS_CAP_PAGE_REGS | SS_CAP_PCCARD | SS_CAP_CARDBUS;
	socket->cap.map_size = 0x1000;
	socket->cap.pci_irq = socket->cb_irq;
	socket->cap.irq_mask = yenta_probe_irq(socket);
	socket->cap.cb_dev = socket->dev;
	socket->cap.bus = NULL;

	printk("Yenta IRQ list %04x\n", socket->cap.irq_mask);
}

/*
 * Initialize a cardbus controller. Make sure we have a usable
 * interrupt, and that we can map the cardbus area. Fill in the
 * socket information structure..
 */
static int yenta_open(pci_socket_t *socket)
{
	struct pci_dev *dev = socket->dev;

	/*
	 * Do some basic sanity checking..
	 */
	if (pci_enable_device(dev)) {
		printk("Unable to enable device\n");
		return -1;
	}
	if (!dev->resource[0].start) {
		printk("No cardbus resource!\n");
		return -1;
	}

	/*
	 * Ok, start setup.. Map the cardbus registers,
	 * and request the IRQ.
	 */
	socket->base = ioremap(dev->resource[0].start, 0x1000);
	if (!socket->base)
		return -1;

	if (dev->irq && !request_irq(dev->irq, yenta_interrupt, SA_SHIRQ, dev->name, socket))
		socket->cb_irq = dev->irq;

	/* Figure out what the dang thing can do.. */
	yenta_get_socket_capabilities(socket);

	/* Disable all events */
	cb_writel(socket, CB_SOCKET_MASK, 0x0);

	printk("Socket status: %08x\n", cb_readl(socket, CB_SOCKET_STATE));
	return 0;
}

/*
 * Close it down - release our resources and go home..
 */
static void yenta_close(pci_socket_t *sock)
{
	if (sock->cb_irq)
		free_irq(sock->cb_irq, sock);
	if (sock->base)
		iounmap(sock->base);
}

/*
 * Standard plain cardbus - no frills, no extensions
 */
struct pci_socket_ops yenta_operations = {
	yenta_open,
	yenta_close,
	yenta_init,
	yenta_suspend,
	yenta_inquire,
	yenta_get_status,
	yenta_get_socket,
	yenta_set_socket,
	yenta_get_io_map,
	yenta_set_io_map,
	yenta_get_mem_map,
	yenta_set_mem_map,
	yenta_get_bridge,
	yenta_set_bridge,
	yenta_proc_setup
};

/*
 * Ricoh cardbus bridge: standard cardbus, except it needs
 * some extra init code to set timings etc.
 */
struct pci_socket_ops ricoh_operations = {
	yenta_open,
	yenta_close,
	ricoh_init,
	yenta_suspend,
	yenta_inquire,
	yenta_get_status,
	yenta_get_socket,
	yenta_set_socket,
	yenta_get_io_map,
	yenta_set_io_map,
	yenta_get_mem_map,
	yenta_set_mem_map,
	yenta_get_bridge,
	yenta_set_bridge,
	yenta_proc_setup
};
