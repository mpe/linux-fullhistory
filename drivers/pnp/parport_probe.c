/* $Id: parport_probe.c,v 1.3 1997/10/19 18:18:46 phil Exp $ 
 * Parallel port device probing code
 * 
 * Authors:    Carsten Gross, carsten@sol.wohnheim.uni-ulm.de
 *             Philip Blundell <Philip.Blundell@pobox.com>
 */

#include <linux/tasks.h>
#include <linux/parport.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/ctype.h>
#include <linux/module.h>

#include <linux/lp.h>

#include <asm/io.h>
#include <asm/dma.h>
#include <asm/uaccess.h>

#undef DEBUG_PROBE

static inline int read_nibble(struct parport *port) 
{
	unsigned char i;
	i = parport_read_status(port)>>3;
	i &= ~8;
	if ((i & 0x10) == 0) i |= 8;
	return (i & 0x0f);
}

static void read_terminate(struct parport *port) {
	parport_write_control(port, (parport_read_control(port) & ~2) | 8);
	/* SelectIN high, AutoFeed low */
	if (parport_wait_peripheral(port, 0x80, 0)) 
		/* timeout, SelectIN high, Autofeed low */
		return;
	parport_write_control(port, parport_read_control(port) | 2);
	/* AutoFeed high */
	parport_wait_peripheral(port, 0x80, 0x80);
	/* no timeout possible, Autofeed low, SelectIN high */
	parport_write_control(port, (parport_read_control(port) & ~2) | 8);
}

static long read_polled(struct parport *port, char *buf, 
			   unsigned long length)
{
	int i;
	char *temp=buf;
	unsigned int count = 0;
	unsigned char z=0;
	unsigned char Byte=0;
	unsigned long igiveupat=jiffies+5*HZ;

	for (i=0; time_before(jiffies, igiveupat); i++) {
	       /* if(current->need_resched) schedule(); */
		parport_write_control(port, parport_read_control(port) | 2); /* AutoFeed high */
		if (parport_wait_peripheral(port, 0x40, 0)) {
#ifdef DEBUG_PROBE
			/* Some peripherals just time out when they've sent
			   all their data.  */
			printk("%s: read1 timeout.\n", port->name);
#endif
			parport_write_control(port, parport_read_control(port) & ~2);
			break;
		}
		z = read_nibble(port);
		parport_write_control(port, parport_read_control(port) & ~2); /* AutoFeed low */
		if (parport_wait_peripheral(port, 0x40, 0x40)) {
			printk("%s: read2 timeout.\n", port->name);
			break;
		}
		if ((i & 1) != 0) {
			Byte |= (z<<4);
			if (temp) 
				*(temp++) = Byte; 
			if (++count == length)
				temp = NULL;
			/* Does the error line indicate end of data? */
			if ((parport_read_status(port) & LP_PERRORP) == 
			    LP_PERRORP) 
				break;
		} else 
			Byte=z;
	}
	read_terminate(port);
	return count; 
}

int parport_probe(struct parport *port, char *buffer, int len)
{
	struct pardevice *dev = parport_register_device(port, "IEEE 1284 probe", NULL, NULL, NULL, 0, &dev);

	int result = 0;

	if (!dev) {
		printk("%s: unable to register for probe.\n", port->name);
		return -EINVAL;
	}

	parport_claim_or_block(dev);

	switch (parport_ieee1284_nibble_mode_ok(port, 4)) {
	case 2:
		current->state=TASK_INTERRUPTIBLE;
		/* HACK: wait 10ms because printer seems to ack wrong */
		schedule_timeout((HZ+99)/100);	
		result = read_polled(port, buffer, len);
		break;
	default:
		result = -EIO;
		break;
	}

	parport_release(dev);
	parport_unregister_device(dev);

	return result;
}

static struct {
	char *token;
	char *descr;
} classes[] = {
	{ "",        "Legacy device" },
	{ "PRINTER", "Printer" }, 
	{ "MODEM",   "Modem" },
	{ "NET",     "Network device" },
	{ "HDC",     "Hard disk" },
	{ "PCMCIA",  "PCMCIA" },
	{ "MEDIA",   "Multimedia device" },
	{ "FDC",     "Floppy disk" },
	{ "PORTS",   "Ports" },
	{ "SCANNER", "Scanner" },
	{ "DIGICAM", "Digital camera" },
	{ "",        "Unknown device" },
	{ "",        "Unspecified" }, 
	{ NULL,      NULL }
};

static char *strdup(char *str)
{
	int n = strlen(str)+1;
	char *s = kmalloc(n, GFP_KERNEL);
	if (!s) return NULL;
	return strcpy(s, str);
}

static void parse_data(struct parport *port, char *str)
{
	char *txt = kmalloc(strlen(str)+1, GFP_KERNEL);
	char *p = txt, *q; 
	int guessed_class = PARPORT_CLASS_UNSPEC;

	if (!txt) {
		printk("%s probe: memory squeeze\n", port->name);
		return;
	}
	strcpy(txt, str);
	while (p) {
		char *sep; 
		q = strchr(p, ';');
		if (q) *q = 0;
		sep = strchr(p, ':');
		if (sep) {
			char *u = p;
			*(sep++) = 0;
			while (*u) {
				*u = toupper(*u);
				u++;
			}
			if (!strcmp(p, "MFG") || !strcmp(p, "MANUFACTURER")) {
				if (port->probe_info.mfr)
					kfree (port->probe_info.mfr);
				port->probe_info.mfr = strdup(sep);
			} else if (!strcmp(p, "MDL") || !strcmp(p, "MODEL")) {
				if (port->probe_info.model)
					kfree (port->probe_info.model);
				port->probe_info.model = strdup(sep);
			} else if (!strcmp(p, "CLS") || !strcmp(p, "CLASS")) {
				int i;
				if (port->probe_info.class_name)
					kfree (port->probe_info.class_name);
				port->probe_info.class_name = strdup(sep);
				for (u = sep; *u; u++)
					*u = toupper(*u);
				for (i = 0; classes[i].token; i++) {
					if (!strcmp(classes[i].token, sep)) {
						port->probe_info.class = i;
						goto rock_on;
					}
				}
				printk(KERN_WARNING "%s probe: warning, class '%s' not understood.\n", port->name, sep);
				port->probe_info.class = PARPORT_CLASS_OTHER;
			} else if (!strcmp(p, "CMD") || !strcmp(p, "COMMAND SET")) {
				if (port->probe_info.cmdset)
					kfree (port->probe_info.cmdset);
				port->probe_info.cmdset = strdup(sep);
				/* if it speaks printer language, it's
				   probably a printer */
				if (strstr(sep, "PJL") || strstr(sep, "PCL"))
					guessed_class = PARPORT_CLASS_PRINTER;
			} else if (!strcmp(p, "DES") || !strcmp(p, "DESCRIPTION")) {
				if (port->probe_info.description)
					kfree (port->probe_info.description);
				port->probe_info.description = strdup(sep);
			}
		}
	rock_on:
		if (q) p = q+1; else p=NULL;
	}

	/* If the device didn't tell us its class, maybe we have managed to
	   guess one from the things it did say. */
	if (port->probe_info.class == PARPORT_CLASS_UNSPEC)
		port->probe_info.class = guessed_class;

	kfree(txt);
}

static void pretty_print(struct parport *port)
{
	printk(KERN_INFO "%s: %s", port->name,
	       classes[port->probe_info.class].descr);
	if (port->probe_info.class) {
		printk(", %s %s", port->probe_info.mfr, 
		       port->probe_info.model);
	}
	printk("\n");
}

void parport_probe_one(struct parport *port)
{
	char *buffer = kmalloc(2048, GFP_KERNEL);
	int r;

	MOD_INC_USE_COUNT;
	port->probe_info.model = strdup ("Unknown device");
	port->probe_info.mfr = strdup ("Unknown vendor");
	port->probe_info.description = port->probe_info.cmdset = NULL;
	port->probe_info.class = PARPORT_CLASS_UNSPEC;
	port->probe_info.class_name = NULL;

	if (!buffer) {
		printk(KERN_ERR "%s probe: Memory squeeze.\n", port->name);
		return;
	}

	r = parport_probe(port, buffer, 2047);

	if (r < 0) {
		printk(KERN_INFO "%s: no IEEE-1284 device present.\n",
		       port->name);
		port->probe_info.class = PARPORT_CLASS_LEGACY;
	} else if (r == 0) {
		printk(KERN_INFO "%s: no ID data returned by device.\n",
		       port->name);
	} else {
		buffer[r] = 0; 
#ifdef DEBUG_PROBE
		printk("%s id: %s\n", port->name, buffer+2);
#endif
		parse_data(port, buffer+2); 
		pretty_print(port);
	}
	kfree(buffer);
	MOD_DEC_USE_COUNT;
}

#if MODULE
int init_module(void)
{
	struct parport *p;
	for (p = parport_enumerate(); p; p = p->next) 
		parport_probe_one(p);
	parport_probe_hook = &parport_probe_one;
	return 0;
}

void cleanup_module(void)
{
	parport_probe_hook = NULL;
}
#endif
