/*
 * sound/uart401.c
 *
 * MPU-401 UART driver (formerly uart401_midi.c)
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 * Changes:
 *	Alan Cox	Reformatted, removed sound_mem usage, use normal Linux
 *			interrupt allocation. Protect against bogus unload
 *			Fixed to allow IRQ > 15
 *
 * Status:
 *		Untested
 */
 
#include <linux/config.h>
#include <linux/module.h>

#include "sound_config.h"
#include "soundmodule.h"

#ifdef CONFIG_UART401
#ifdef CONFIG_MIDI

typedef struct uart401_devc
{
	int             base;
	int             irq;
	int            *osp;
	void            (*midi_input_intr) (int dev, unsigned char data);
	int             opened, disabled;
	volatile unsigned char input_byte;
	int             my_dev;
	int             share_irq;
}
uart401_devc;

static uart401_devc *detected_devc = NULL;

#define	DATAPORT   (devc->base)
#define	COMDPORT   (devc->base+1)
#define	STATPORT   (devc->base+1)

static int uart401_status(uart401_devc * devc)
{
	return inb(STATPORT);
}

#define input_avail(devc) (!(uart401_status(devc)&INPUT_AVAIL))
#define output_ready(devc)	(!(uart401_status(devc)&OUTPUT_READY))

static void uart401_cmd(uart401_devc * devc, unsigned char cmd)
{
	outb((cmd), COMDPORT);
}

static int uart401_read(uart401_devc * devc)
{
	return inb(DATAPORT);
}

static void uart401_write(uart401_devc * devc, unsigned char byte)
{
	outb((byte), DATAPORT);
}

#define	OUTPUT_READY	0x40
#define	INPUT_AVAIL	0x80
#define	MPU_ACK		0xFE
#define	MPU_RESET	0xFF
#define	UART_MODE_ON	0x3F

static int      reset_uart401(uart401_devc * devc);
static void     enter_uart_mode(uart401_devc * devc);

static void uart401_input_loop(uart401_devc * devc)
{
	int work_limit=30000;
	
	while (input_avail(devc) && --work_limit)
	{
		unsigned char   c = uart401_read(devc);

		if (c == MPU_ACK)
			devc->input_byte = c;
		else if (devc->opened & OPEN_READ && devc->midi_input_intr)
			devc->midi_input_intr(devc->my_dev, c);
	}
	if(work_limit==0)
		printk(KERN_WARNING "Too much work in interrupt on uart401 (0x%X). UART jabbering ??\n", devc->base);
}

void uart401intr(int irq, void *dev_id, struct pt_regs *dummy)
{
	uart401_devc *devc = dev_id;

	if (devc == NULL)
	{
		printk(KERN_ERR "uart401: bad devc\n");
		return;
	}

	if (input_avail(devc))
		uart401_input_loop(devc);
}

static int
uart401_open(int dev, int mode,
	     void            (*input) (int dev, unsigned char data),
	     void            (*output) (int dev)
)
{
	uart401_devc *devc = (uart401_devc *) midi_devs[dev]->devc;

	if (devc->opened)
		return -EBUSY;

	/* Flush the UART */
	
	while (input_avail(devc))
		uart401_read(devc);

	devc->midi_input_intr = input;
	devc->opened = mode;
	enter_uart_mode(devc);
	devc->disabled = 0;

	return 0;
}

static void uart401_close(int dev)
{
	uart401_devc *devc = (uart401_devc *) midi_devs[dev]->devc;

	reset_uart401(devc);
	devc->opened = 0;
}

static int uart401_out(int dev, unsigned char midi_byte)
{
	int timeout;
	unsigned long flags;
	uart401_devc *devc = (uart401_devc *) midi_devs[dev]->devc;

	if (devc->disabled)
		return 1;
	/*
	 * Test for input since pending input seems to block the output.
	 */

	save_flags(flags);
	cli();

	if (input_avail(devc))
		uart401_input_loop(devc);

	restore_flags(flags);

	/*
	 * Sometimes it takes about 13000 loops before the output becomes ready
	 * (After reset). Normally it takes just about 10 loops.
	 */

	for (timeout = 30000; timeout > 0 && !output_ready(devc); timeout--);

	if (!output_ready(devc))
	{
		  printk(KERN_WARNING "uart401: Timeout - Device not responding\n");
		  devc->disabled = 1;
		  reset_uart401(devc);
		  enter_uart_mode(devc);
		  return 1;
	}
	uart401_write(devc, midi_byte);
	return 1;
}

static int uart401_start_read(int dev)
{
	return 0;
}

static int uart401_end_read(int dev)
{
	return 0;
}

static void uart401_kick(int dev)
{
}

static int uart401_buffer_status(int dev)
{
	return 0;
}

#define MIDI_SYNTH_NAME	"MPU-401 UART"
#define MIDI_SYNTH_CAPS	SYNTH_CAP_INPUT
#include "midi_synth.h"

static struct midi_operations uart401_operations =
{
	{
		"MPU-401 (UART) MIDI", 0, 0, SNDCARD_MPU401
	},
	&std_midi_synth,
	{0},
	uart401_open,
	uart401_close,
	NULL, /* ioctl */
	uart401_out,
	uart401_start_read,
	uart401_end_read,
	uart401_kick,
	NULL,
	uart401_buffer_status,
	NULL
};

static void enter_uart_mode(uart401_devc * devc)
{
	int ok, timeout;
	unsigned long flags;

	save_flags(flags);
	cli();
	for (timeout = 30000; timeout > 0 && !output_ready(devc); timeout--);

	devc->input_byte = 0;
	uart401_cmd(devc, UART_MODE_ON);

	ok = 0;
	for (timeout = 50000; timeout > 0 && !ok; timeout--)
		if (devc->input_byte == MPU_ACK)
			ok = 1;
		else if (input_avail(devc))
			if (uart401_read(devc) == MPU_ACK)
				ok = 1;

	restore_flags(flags);
}

void attach_uart401(struct address_info *hw_config)
{
	uart401_devc *devc;
	char *name = "MPU-401 (UART) MIDI";

	
	if (hw_config->name)
		name = hw_config->name;

	if (detected_devc == NULL)
		return;


	devc = (uart401_devc *) kmalloc(sizeof(uart401_devc), GFP_KERNEL);
	if (devc == NULL)
	{
		printk(KERN_WARNING "uart401: Can't allocate memory\n");
		return;
	}
	memcpy((char *) devc, (char *) detected_devc, sizeof(uart401_devc));
	detected_devc = NULL;

	devc->irq = hw_config->irq;
	if (devc->irq < 0)
	{
		devc->share_irq = 1;
		devc->irq *= -1;
	}
	else
		devc->share_irq = 0;

	if (!devc->share_irq)
	{
		if (request_irq(devc->irq, uart401intr, 0, "MPU-401 UART", devc) < 0)
		{
			printk(KERN_WARNING "uart401: Failed to allocate IRQ%d\n", devc->irq);
			devc->share_irq = 1;
		}
	}
	devc->my_dev = sound_alloc_mididev();

	request_region(hw_config->io_base, 4, "MPU-401 UART");
	enter_uart_mode(devc);

	if (devc->my_dev == -1)
	{
		printk(KERN_INFO "uart401: Too many midi devices detected\n");
		kfree(devc);
		return;
	}
	conf_printf(name, hw_config);

	std_midi_synth.midi_dev = devc->my_dev;
	midi_devs[devc->my_dev] = (struct midi_operations *)kmalloc(sizeof(struct midi_operations), GFP_KERNEL);
	if (midi_devs[devc->my_dev] == NULL)
	{
		printk(KERN_ERR "uart401: Failed to allocate memory\n");
		sound_unload_mididev(devc->my_dev);
		kfree(devc);
		devc=NULL;
		return;
	}
	memcpy((char *) midi_devs[devc->my_dev], (char *) &uart401_operations,
	       sizeof(struct midi_operations));

	midi_devs[devc->my_dev]->devc = devc;
	midi_devs[devc->my_dev]->converter = (struct synth_operations *)kmalloc(sizeof(struct synth_operations), GFP_KERNEL);
	if (midi_devs[devc->my_dev]->converter == NULL)
	{
		printk(KERN_WARNING "uart401: Failed to allocate memory\n");
		kfree(midi_devs[devc->my_dev]);
		kfree(devc);
		sound_unload_mididev(devc->my_dev);
		devc=NULL;
		return;
	}
	memcpy((char *) midi_devs[devc->my_dev]->converter, (char *) &std_midi_synth,
	       sizeof(struct synth_operations));

	strcpy(midi_devs[devc->my_dev]->info.name, name);
	midi_devs[devc->my_dev]->converter->id = "UART401";
	hw_config->slots[4] = devc->my_dev;
	sequencer_init();
	devc->opened = 0;
}

static int reset_uart401(uart401_devc * devc)
{
	int ok, timeout, n;

	/*
	 * Send the RESET command. Try again if no success at the first time.
	 */

	ok = 0;

	for (n = 0; n < 2 && !ok; n++)
	{
		for (timeout = 30000; timeout > 0 && !output_ready(devc); timeout--);
		devc->input_byte = 0;
		uart401_cmd(devc, MPU_RESET);

		/*
		 * Wait at least 25 msec. This method is not accurate so let's make the
		 * loop bit longer. Cannot sleep since this is called during boot.
		 */

		for (timeout = 50000; timeout > 0 && !ok; timeout--)
		{
			if (devc->input_byte == MPU_ACK)	/* Interrupt */
				ok = 1;
			else if (input_avail(devc))
			{
				if (uart401_read(devc) == MPU_ACK)
					ok = 1;
			}
		}
	}


	if (ok)
	{
		DEB(printk("Reset UART401 OK\n"));
	}
	else
		DDB(printk("Reset UART401 failed - No hardware detected.\n"));

	if (ok)
		uart401_input_loop(devc);	/*
						 * Flush input before enabling interrupts
						 */

	return ok;
}

int probe_uart401(struct address_info *hw_config)
{
	int ok = 0;
	unsigned long flags;
	static uart401_devc hw_info;
	uart401_devc *devc = &hw_info;

	DDB(printk("Entered probe_uart401()\n"));

	/* Default to "not found" */
	hw_config->slots[4] = -1;
	detected_devc = NULL;

	if (check_region(hw_config->io_base, 4))
		return 0;

	devc->base = hw_config->io_base;
	devc->irq = hw_config->irq;
	devc->osp = hw_config->osp;
	devc->midi_input_intr = NULL;
	devc->opened = 0;
	devc->input_byte = 0;
	devc->my_dev = 0;
	devc->share_irq = 0;

	save_flags(flags);
	cli();
	ok = reset_uart401(devc);
	restore_flags(flags);

	if (ok)
		detected_devc = devc;

	return ok;
}

void unload_uart401(struct address_info *hw_config)
{
	uart401_devc *devc;
	int n=hw_config->slots[4];
	
	/* Not set up */
	if(n==-1 || midi_devs[n]==NULL)
		return;
		
	/* Not allocated (erm ??) */
	
	devc = midi_devs[hw_config->slots[4]]->devc;
	if (devc == NULL)
		return;

	reset_uart401(devc);
	release_region(hw_config->io_base, 4);

	if (!devc->share_irq)
		free_irq(devc->irq, devc);
	if (devc)
	{
		kfree(midi_devs[devc->my_dev]->converter);
		kfree(midi_devs[devc->my_dev]);
		kfree(devc);
		devc = NULL;
	}
	/* This kills midi_devs[x] */
	sound_unload_mididev(hw_config->slots[4]);
}

#ifdef MODULE

int io = -1;
int irq = -1;

MODULE_PARM(io, "i");
MODULE_PARM(irq, "i");
struct address_info hw;

int init_module(void)
{
	/* Can be loaded either for module use or to provide functions
	   to others */
	if (io != -1 && irq != -1)
	{
		printk(KERN_INFO "MPU-401 UART driver Copyright (C) Hannu Savolainen 1993-1997");
		hw.irq = irq;
		hw.io_base = io;
		if (probe_uart401(&hw) == 0)
			return -ENODEV;
		attach_uart401(&hw);
	}
	SOUND_LOCK;
	return 0;
}

void cleanup_module(void)
{
	if (io != -1 && irq != -1)
		unload_uart401(&hw);
	/*  FREE SYMTAB */
	SOUND_LOCK_END;
}

#endif

EXPORT_SYMBOL(attach_uart401);
EXPORT_SYMBOL(probe_uart401);
EXPORT_SYMBOL(unload_uart401);
EXPORT_SYMBOL(uart401intr);

#endif
#endif
