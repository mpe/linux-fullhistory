/* $Id: su.c,v 1.3 1997/09/03 11:54:56 ecd Exp $
 * su.c: Small serial driver for keyboard/mouse interface on Ultra/AX
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 *
 * This is mainly a very stripped down version of drivers/char/serial.c,
 * credits go to authors mentioned therein.
 */

#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/interrupt.h>
#include <linux/serial.h>
#include <linux/serial_reg.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/init.h>

#include <asm/oplib.h>
#include <asm/io.h>
#include <asm/ebus.h>

#include "sunserial.h"
#include "sunkbd.h"
#include "sunmouse.h"

static char *serial_name = "kbd/mouse serial driver";
static char *serial_version = "1.0";

/* Set of debugging defines */
#undef SERIAL_DEBUG_INTR
#undef SERIAL_DEBUG_OPEN

/* We are on a NS PC87303 clocked with 24.0 MHz, which results
 * in a UART clock of 1.8462 MHz.
 */
#define BAUD_BASE	(1846200 / 16)

struct su_struct {
	int		 magic;
	unsigned long	 port;
	int		 baud_base;
	int		 type;
	int		 irq;
	int		 flags;
	unsigned char	 IER;
	unsigned char	 MCR;
	int		 line;
	int		 cflag;
	int		 kbd_node;
	int		 ms_node;
	char		 name[16];
};

static struct su_struct su_table[] = {
	{ 0, 0, BAUD_BASE, PORT_UNKNOWN },
	{ 0, 0, BAUD_BASE, PORT_UNKNOWN }
};

#define NR_PORTS (sizeof(su_table) / sizeof(struct su_struct))

static void autoconfig(struct su_struct * info);
static void change_speed(struct su_struct *info);

/*
 * Here we define the default xmit fifo size used for each type of
 * UART
 */
static struct serial_uart_config uart_config[] = {
	{ "unknown", 1, 0 }, 
	{ "8250", 1, 0 }, 
	{ "16450", 1, 0 }, 
	{ "16550", 1, 0 }, 
	{ "16550A", 16, UART_CLEAR_FIFO | UART_USE_FIFO }, 
	{ "cirrus", 1, 0 }, 
	{ "ST16650", 1, UART_CLEAR_FIFO |UART_STARTECH }, 
	{ "ST16650V2", 32, UART_CLEAR_FIFO | UART_USE_FIFO |
		  UART_STARTECH }, 
	{ "TI16750", 64, UART_CLEAR_FIFO | UART_USE_FIFO},
	{ 0, 0}
};

/*
 * This is used to figure out the divisor speeds and the timeouts
 */
static int baud_table[] = {
	0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800,
	9600, 19200, 38400, 57600, 115200, 230400, 460800, 0 };

static inline
unsigned int su_inb(struct su_struct *info, unsigned long offset)
{
	return inb(info->port + offset);
}

static inline void
su_outb(struct su_struct *info, unsigned long offset, int value)
{
	outb(value, info->port + offset);
}

static inline void
receive_chars(struct su_struct *info, struct pt_regs *regs)
{
	unsigned char status = 0;
	unsigned char ch;

	do {
		ch = su_inb(info, UART_RX);

#ifdef SERIAL_DEBUG_INTR
		printk("DR%02x:%02x...", ch, status);
#endif

		if (info->kbd_node) {
			if(ch == SUNKBD_RESET) {
                        	l1a_state.kbd_id = 1;
                        	l1a_state.l1_down = 0;
                	} else if(l1a_state.kbd_id) {
                        	l1a_state.kbd_id = 0;
                	} else if(ch == SUNKBD_L1) {
                        	l1a_state.l1_down = 1;
                	} else if(ch == (SUNKBD_L1|SUNKBD_UP)) {
                        	l1a_state.l1_down = 0;
                	} else if(ch == SUNKBD_A && l1a_state.l1_down) {
                        	/* whee... */
                        	batten_down_hatches();
                        	/* Continue execution... */
                        	l1a_state.l1_down = 0;
                        	l1a_state.kbd_id = 0;
                        	return;
                	}
                	sunkbd_inchar(ch, regs);
		} else {
			sun_mouse_inbyte(ch);
		}

		status = su_inb(info, UART_LSR);
	} while (status & UART_LSR_DR);
}

/*
 * This is the serial driver's generic interrupt routine
 */
static void
su_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	struct su_struct *info = (struct su_struct *)dev_id;
	unsigned char status;

	/*
	 * We might share interrupts with ps2kbd/ms driver,
	 * in case we want to use the 16550A as general serial
	 * driver in the presence of ps2 devices, so do a
	 * sanity check here, needs to be done in ps2kbd/ms
	 * driver, too.
	 */
	if (!info || info->magic != SERIAL_MAGIC)
		return;

#ifdef SERIAL_DEBUG_INTR
	printk("su_interrupt(%d)...", irq);
#endif

	if (su_inb(info, UART_IIR) & UART_IIR_NO_INT)
		return;

	status = su_inb(info, UART_LSR);
#ifdef SERIAL_DEBUG_INTR
	printk("status = %x...", status);
#endif
	if (status & UART_LSR_DR)
		receive_chars(info, regs);

#ifdef SERIAL_DEBUG_INTR
	printk("end.\n");
#endif
}

static int
startup(struct su_struct * info)
{
	unsigned long flags;
	int	retval=0;

	save_flags(flags); cli();

	if (info->flags & ASYNC_INITIALIZED) {
		goto errout;
	}

	if (!info->port || !info->type) {
		goto errout;
	}

#ifdef SERIAL_DEBUG_OPEN
	printk("starting up su%d (irq %x)...", info->line, info->irq);
#endif

	if (info->type == PORT_16750)
		su_outb(info, UART_IER, 0);

	/*
	 * Clear the FIFO buffers and disable them
	 * (they will be reenabled in change_speed())
	 */
	if (uart_config[info->type].flags & UART_CLEAR_FIFO)
		su_outb(info, UART_FCR, (UART_FCR_CLEAR_RCVR |
					     UART_FCR_CLEAR_XMIT));

	/*
	 * At this point there's no way the LSR could still be 0xFF;
	 * if it is, then bail out, because there's likely no UART
	 * here.
	 */
	if (su_inb(info, UART_LSR) == 0xff) {
		retval = -ENODEV;
		goto errout;
	}
	
	/*
	 * Allocate the IRQ if necessary
	 */
	retval = request_irq(info->irq, su_interrupt, SA_SHIRQ,
			     info->name, info);
	if (retval)
		goto errout;

	/*
	 * Clear the interrupt registers.
	 */
	su_inb(info, UART_RX);
	su_inb(info, UART_IIR);
	su_inb(info, UART_MSR);

	/*
	 * Now, initialize the UART 
	 */
	su_outb(info, UART_LCR, UART_LCR_WLEN8);	/* reset DLAB */

	info->MCR = UART_MCR_OUT2;
	su_outb(info, UART_MCR, info->MCR);

	/*
	 * Finally, enable interrupts
	 */
	info->IER = UART_IER_RLSI | UART_IER_RDI;
	su_outb(info, UART_IER, info->IER);	/* enable interrupts */
	
	/*
	 * And clear the interrupt registers again for luck.
	 */
	su_inb(info, UART_LSR);
	su_inb(info, UART_RX);
	su_inb(info, UART_IIR);
	su_inb(info, UART_MSR);

	/*
	 * and set the speed of the serial port
	 */
	change_speed(info);

	info->flags |= ASYNC_INITIALIZED;
	restore_flags(flags);
	return 0;
	
errout:
	restore_flags(flags);
	return retval;
}

/*
 * This routine is called to set the UART divisor registers to match
 * the specified baud rate for a serial port.
 */
static void
change_speed(struct su_struct *info)
{
	unsigned char cval, fcr = 0;
	int quot = 0;
	unsigned long flags;
	int i;

	/* byte size and parity */
	switch (info->cflag & CSIZE) {
		case CS5: cval = 0x00; break;
		case CS6: cval = 0x01; break;
		case CS7: cval = 0x02; break;
		case CS8: cval = 0x03; break;
		/* Never happens, but GCC is too dumb to figure it out */
		default:  cval = 0x00; break;
	}
	if (info->cflag & CSTOPB) {
		cval |= 0x04;
	}
	if (info->cflag & PARENB) {
		cval |= UART_LCR_PARITY;
	}
	if (!(info->cflag & PARODD))
		cval |= UART_LCR_EPAR;
#ifdef CMSPAR
	if (info->cflag & CMSPAR)
		cval |= UART_LCR_SPAR;
#endif

	/* Determine divisor based on baud rate */
	i = info->cflag & CBAUD;
	if (i & CBAUDEX) {
		i &= ~CBAUDEX;
		if (i < 1 || i > 4) 
			info->cflag &= ~CBAUDEX;
		else
			i += 15;
	}
	if (!quot) {
		if (baud_table[i] == 134)
			/* Special case since 134 is really 134.5 */
			quot = (2 * info->baud_base / 269);
		else if (baud_table[i])
			quot = info->baud_base / baud_table[i];
		/* If the quotient is ever zero, default to 1200 bps */
		if (!quot)
			quot = info->baud_base / 1200;
	}

	/* Set up FIFO's */
	if (uart_config[info->type].flags & UART_USE_FIFO)
		fcr = UART_FCR_ENABLE_FIFO | UART_FCR_TRIGGER_1;

	su_outb(info, UART_IER, info->IER);

	save_flags(flags); cli();
	su_outb(info, UART_LCR, cval | UART_LCR_DLAB);	/* set DLAB */
	su_outb(info, UART_DLL, quot & 0xff);	/* LS of divisor */
	su_outb(info, UART_DLM, quot >> 8);		/* MS of divisor */
	if (info->type == PORT_16750)
		su_outb(info, UART_FCR, fcr); 	/* set fcr */
	su_outb(info, UART_LCR, cval);		/* reset DLAB */
	if (info->type != PORT_16750)
		su_outb(info, UART_FCR, fcr); 	/* set fcr */
	restore_flags(flags);
}

static void
su_put_char(unsigned char c)
{
	struct su_struct *info = su_table;
	int lsr;

	if (!info->kbd_node)
		++info;

	do {
		lsr = inb(info->port + UART_LSR);
	} while (!(lsr & UART_LSR_THRE));

	/* Send the character out. */
	su_outb(info, UART_TX, c);
}

static void
su_change_mouse_baud(int baud)
{
	struct su_struct *info = su_table;

	if (!info->ms_node)
		++info;
	if (!info)
		return;

	info->cflag &= ~(CBAUDEX | CBAUD);
	switch(baud) {
		case 1200:
			info->cflag |= B1200;
			break;
		case 2400:
			info->cflag |= B2400;
			break;
		case 4800:
			info->cflag |= B4800;
			break;
		case 9600:
			info->cflag |= B9600;
			break;
		default:
			printk("su_change_mouse_baud: unknown baud rate %d, "
			       "defaulting to 1200\n", baud);
			info->cflag |= 1200;
			break;
	}
	change_speed(info);
}

/*
 * This routine prints out the appropriate serial driver version
 * number, and identifies which options were configured into this
 * driver.
 */
static inline void
show_su_version(void)
{
 	printk(KERN_INFO "%s version %s\n", serial_name, serial_version);
}

/*
 * This routine is called by su_init() to initialize a specific serial
 * port.  It determines what type of UART chip this serial port is
 * using: 8250, 16450, 16550, 16550A.  The important question is
 * whether or not this UART is a 16550A or not, since this will
 * determine whether or not we can use its FIFO features or not.
 */
static void
autoconfig(struct su_struct *info)
{
	unsigned char status1, status2, scratch, scratch2;
	struct linux_ebus_device *dev;
	struct linux_ebus *ebus;
	unsigned long flags;

	for_all_ebusdev(dev, ebus) {
		if (!strncmp(dev->prom_name, "su", 2)) {
			if (dev->prom_node == info->kbd_node)
				break;
			if (dev->prom_node == info->ms_node)
				break;
		}
	}
	if (!dev)
		return;

	info->port = dev->base_address[0];
	if (check_region(info->port, 8))
		return;

	info->irq = dev->irqs[0];

#ifdef DEBUG_SERIAL_OPEN
	printk("Found 'su' at %016lx IRQ %08x\n",
	       dev->base_address[0], dev->irqs[0]);
#endif

	info->magic = SERIAL_MAGIC;

	save_flags(flags); cli();
	
	/*
	 * Do a simple existence test first; if we fail this, there's
	 * no point trying anything else.
	 *
	 * 0x80 is used as a nonsense port to prevent against false
	 * positives due to ISA bus float.  The assumption is that
	 * 0x80 is a non-existent port; which should be safe since
	 * include/asm/io.h also makes this assumption.
	 */
	scratch = su_inb(info, UART_IER);
	su_outb(info, UART_IER, 0);
	scratch2 = su_inb(info, UART_IER);
	su_outb(info, UART_IER, scratch);
	if (scratch2) {
		restore_flags(flags);
		return;		/* We failed; there's nothing here */
	}

	scratch = su_inb(info, UART_MCR);
	su_outb(info, UART_MCR, UART_MCR_LOOP | scratch);
	scratch2 = su_inb(info, UART_MSR);
	su_outb(info, UART_MCR, UART_MCR_LOOP | 0x0A);
	status1 = su_inb(info, UART_MSR) & 0xF0;
	su_outb(info, UART_MCR, scratch);
	su_outb(info, UART_MSR, scratch2);
	if (status1 != 0x90) {
		restore_flags(flags);
		return;
	} 

	scratch2 = su_inb(info, UART_LCR);
	su_outb(info, UART_LCR, 0xBF);	/* set up for StarTech test */
	su_outb(info, UART_EFR, 0);	/* EFR is the same as FCR */
	su_outb(info, UART_LCR, 0);
	su_outb(info, UART_FCR, UART_FCR_ENABLE_FIFO);
	scratch = su_inb(info, UART_IIR) >> 6;
	switch (scratch) {
		case 0:
			info->type = PORT_16450;
			break;
		case 1:
			info->type = PORT_UNKNOWN;
			break;
		case 2:
			info->type = PORT_16550;
			break;
		case 3:
			info->type = PORT_16550A;
			break;
	}
	if (info->type == PORT_16550A) {
		/* Check for Startech UART's */
		su_outb(info, UART_LCR, scratch2 | UART_LCR_DLAB);
		if (su_inb(info, UART_EFR) == 0) {
			info->type = PORT_16650;
		} else {
			su_outb(info, UART_LCR, 0xBF);
			if (su_inb(info, UART_EFR) == 0)
				info->type = PORT_16650V2;
		}
	}
	if (info->type == PORT_16550A) {
		/* Check for TI 16750 */
		su_outb(info, UART_LCR, scratch2 | UART_LCR_DLAB);
		su_outb(info, UART_FCR,
			    UART_FCR_ENABLE_FIFO | UART_FCR7_64BYTE);
		scratch = su_inb(info, UART_IIR) >> 5;
		if (scratch == 7) {
			su_outb(info, UART_LCR, 0);
			su_outb(info, UART_FCR, UART_FCR_ENABLE_FIFO);
			scratch = su_inb(info, UART_IIR) >> 5;
			if (scratch == 6)
				info->type = PORT_16750;
		}
		su_outb(info, UART_FCR, UART_FCR_ENABLE_FIFO);
	}
	su_outb(info, UART_LCR, scratch2);
	if (info->type == PORT_16450) {
		scratch = su_inb(info, UART_SCR);
		su_outb(info, UART_SCR, 0xa5);
		status1 = su_inb(info, UART_SCR);
		su_outb(info, UART_SCR, 0x5a);
		status2 = su_inb(info, UART_SCR);
		su_outb(info, UART_SCR, scratch);

		if ((status1 != 0xa5) || (status2 != 0x5a))
			info->type = PORT_8250;
	}

	if (info->type == PORT_UNKNOWN) {
		restore_flags(flags);
		return;
	}

	sprintf(info->name, "su(%s)", info->ms_node ? "mouse" : "kbd");
	request_region(info->port, 8, info->name);

	/*
	 * Reset the UART.
	 */
	su_outb(info, UART_MCR, 0x00);
	su_outb(info, UART_FCR, (UART_FCR_CLEAR_RCVR |
				     UART_FCR_CLEAR_XMIT));
	su_inb(info, UART_RX);

	restore_flags(flags);
}

/*
 * The serial driver boot-time initialization code!
 */
__initfunc(int su_init(void))
{
	int i;
	struct su_struct *info;
	
	show_su_version();

	for (i = 0, info = su_table; i < NR_PORTS; i++, info++) {
		info->line = i;
		if (info->kbd_node)
			info->cflag = B1200 | CS8 | CREAD;
		else
			info->cflag = B4800 | CS8 | CREAD;

		autoconfig(info);
		if (info->type == PORT_UNKNOWN)
			continue;

		printk(KERN_INFO "%s at %16lx (irq = %08x) is a %s\n",
		       info->name, info->port, info->irq,
		       uart_config[info->type].name);

		startup(info);
		if (info->kbd_node)
			keyboard_zsinit(su_put_char);
		else
			sun_mouse_zsinit();
	}
	return 0;
}

__initfunc(int su_probe (unsigned long *memory_start))
{
	struct su_struct *info = su_table;
        int node, enode, sunode;
	int kbnode = 0, msnode = 0;
	int devices = 0;
	char prop[128];
	int len;

	/*
	 * Get the nodes for keyboard and mouse from 'aliases'...
	 */
        node = prom_getchild(prom_root_node);
	node = prom_searchsiblings(node, "aliases");
	if (!node)
		return -ENODEV;

	len = prom_getproperty(node, "keyboard", prop, sizeof(prop));
	if (len > 0)
		kbnode = prom_pathtoinode(prop);
	if (!kbnode)
		return -ENODEV;

	len = prom_getproperty(node, "mouse", prop, sizeof(prop));
	if (len > 0)
		msnode = prom_pathtoinode(prop);
	if (!msnode)
		return -ENODEV;

	/*
	 * Find matching EBus nodes...
	 */
        node = prom_getchild(prom_root_node);
	node = prom_searchsiblings(node, "pci");

	/*
	 * For each PCI bus...
	 */
	while (node) {
		enode = prom_getchild(node);
		enode = prom_searchsiblings(enode, "ebus");

		/*
		 * For each EBus on this PCI...
		 */
		while (enode) {
			sunode = prom_getchild(enode);
			sunode = prom_searchsiblings(sunode, "su");

			/*
			 * For each 'su' on this EBus...
			 */
			while (sunode) {
				/*
				 * Does it match?
				 */
				if (sunode == kbnode) {
					info->kbd_node = kbnode;
					++info;
					++devices;
				}
				if (sunode == msnode) {
					info->ms_node = msnode;
					++info;
					++devices;
				}

				/*
				 * Found everything we need?
				 */
				if (devices == NR_PORTS)
					goto found;

				sunode = prom_getsibling(sunode);
				sunode = prom_searchsiblings(sunode, "su");
			}
			enode = prom_getsibling(enode);
			enode = prom_searchsiblings(enode, "ebus");
		}
		node = prom_getsibling(node);
		node = prom_searchsiblings(node, "pci");
	}
	return -ENODEV;

found:
        sunserial_setinitfunc(memory_start, su_init);
        rs_ops.rs_change_mouse_baud = su_change_mouse_baud;
	return 0;
}
