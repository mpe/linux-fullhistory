/* Parallel-port initialisation code.
 * 
 * Authors: David Campbell <campbell@torque.net>
 *          Tim Waugh <tim@cyberelk.demon.co.uk>
 *	    Jose Renau <renau@acm.org>
 *
 * based on work by Grant Guenther <grant@torque.net>
 *              and Philip Blundell <Philip.Blundell@pobox.com>
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/threads.h>

#include <linux/parport.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/init.h>

#ifndef MODULE
static int io[PARPORT_MAX+1] __initdata = { [0 ... PARPORT_MAX] = 0 };
static int io_hi[PARPORT_MAX+1] __initdata = { [0 ... PARPORT_MAX] = 0 };
static int irq[PARPORT_MAX] __initdata = { [0 ... PARPORT_MAX-1] = PARPORT_IRQ_PROBEONLY };
static int dma[PARPORT_MAX] __initdata = { [0 ... PARPORT_MAX-1] = PARPORT_DMA_NONE };

extern int parport_pc_init(int *io, int *io_hi, int *irq, int *dma);

static int parport_setup_ptr __initdata = 0;

__initfunc(void parport_setup(char *str, int *ints))
{
	if (ints[0] == 0) {
		if (str && !strncmp(str, "auto", 4)) {
			irq[0] = PARPORT_IRQ_AUTO;
			dma[0] = PARPORT_DMA_AUTO;
		}
		else if (str)
			printk (KERN_ERR "parport: `%s': huh?\n", str);
		else
			printk (KERN_ERR "parport: parport=.. what?\n");
		
		return;
	}
	else if (ints[1] == 0) {
		/* Disable parport if "parport=0" in cmdline */
		io[0] = PARPORT_DISABLE; 
		return;
	}

	if (parport_setup_ptr < PARPORT_MAX) {
		char *sep;
		io[parport_setup_ptr] = ints[1];
		irq[parport_setup_ptr] = PARPORT_IRQ_NONE;
		dma[parport_setup_ptr] = PARPORT_DMA_NONE;
		if (ints[0] > 1) {
			irq[parport_setup_ptr] = ints[2];
			if (ints[0] > 2) {
				dma[parport_setup_ptr] = ints[3];
				goto done;
			}

			if (str == NULL)
				goto done;

			goto dma_from_str;
		}
		else if (str == NULL)
			goto done;
		else if (!strncmp(str, "auto", 4))
			irq[parport_setup_ptr] = PARPORT_IRQ_AUTO;
		else if (strncmp(str, "none", 4) != 0) {
			printk(KERN_ERR "parport: bad irq `%s'\n", str);
			return;
		}

		if ((sep = strchr(str, ',')) == NULL) goto done;
		str = sep+1;
	dma_from_str:
		if (!strncmp(str, "auto", 4))
			dma[parport_setup_ptr] = PARPORT_DMA_AUTO;
		else if (strncmp(str, "none", 4) != 0) {
			char *ep;
			dma[parport_setup_ptr] = simple_strtoul(str, &ep, 0);
			if (ep == str) {
				printk(KERN_ERR "parport: bad dma `%s'\n",
				       str);
				return;
			}
		}
	done:
		parport_setup_ptr++;
	} else
		printk(KERN_ERR "parport=%s ignored, too many ports\n", str);
}
#endif

#ifdef MODULE
int init_module(void)
{
#ifdef CONFIG_SYSCTL
	parport_default_proc_register ();
#endif
	return 0;
}

void cleanup_module(void)
{
#ifdef CONFIG_SYSCTL
	parport_default_proc_unregister ();
#endif
}

#else

__initfunc(int parport_init(void))
{
	if (io[0] == PARPORT_DISABLE) 
		return 1;

#ifdef CONFIG_SYSCTL
	parport_default_proc_register ();
#endif

#ifdef CONFIG_PARPORT_PC
	parport_pc_init(io, io_hi, irq, dma);
#endif
#ifdef CONFIG_PARPORT_AMIGA
	parport_amiga_init();
#endif
#ifdef CONFIG_PARPORT_MFC3
	parport_mfc3_init();
#endif
#ifdef CONFIG_PARPORT_ATARI
	parport_atari_init();
#endif
#ifdef CONFIG_PARPORT_ARC
	parport_arc_init();
#endif
	return 0;
}
#endif

/* Exported symbols for modules. */

EXPORT_SYMBOL(parport_claim);
EXPORT_SYMBOL(parport_claim_or_block);
EXPORT_SYMBOL(parport_release);
EXPORT_SYMBOL(parport_register_port);
EXPORT_SYMBOL(parport_announce_port);
EXPORT_SYMBOL(parport_unregister_port);
EXPORT_SYMBOL(parport_register_driver);
EXPORT_SYMBOL(parport_unregister_driver);
EXPORT_SYMBOL(parport_register_device);
EXPORT_SYMBOL(parport_unregister_device);
EXPORT_SYMBOL(parport_enumerate);
EXPORT_SYMBOL(parport_negotiate);
EXPORT_SYMBOL(parport_write);
EXPORT_SYMBOL(parport_read);
EXPORT_SYMBOL(parport_ieee1284_wakeup);
EXPORT_SYMBOL(parport_wait_peripheral);
EXPORT_SYMBOL(parport_wait_event);
EXPORT_SYMBOL(parport_set_timeout);
EXPORT_SYMBOL(parport_ieee1284_interrupt);
EXPORT_SYMBOL(parport_ieee1284_ecp_write_data);
EXPORT_SYMBOL(parport_ieee1284_ecp_read_data);
EXPORT_SYMBOL(parport_ieee1284_ecp_write_addr);
EXPORT_SYMBOL(parport_ieee1284_write_compat);
EXPORT_SYMBOL(parport_ieee1284_read_nibble);
EXPORT_SYMBOL(parport_ieee1284_read_byte);
EXPORT_SYMBOL(parport_ieee1284_epp_write_data);
EXPORT_SYMBOL(parport_ieee1284_epp_read_data);
EXPORT_SYMBOL(parport_ieee1284_epp_write_addr);
EXPORT_SYMBOL(parport_ieee1284_epp_read_addr);
EXPORT_SYMBOL(parport_proc_register);
EXPORT_SYMBOL(parport_proc_unregister);
EXPORT_SYMBOL(parport_device_proc_register);
EXPORT_SYMBOL(parport_device_proc_unregister);
EXPORT_SYMBOL(parport_default_proc_register);
EXPORT_SYMBOL(parport_default_proc_unregister);
EXPORT_SYMBOL(parport_parse_irqs);
EXPORT_SYMBOL(parport_parse_dmas);
#ifdef CONFIG_PARPORT_1284
EXPORT_SYMBOL(parport_open);
EXPORT_SYMBOL(parport_close);
EXPORT_SYMBOL(parport_device_id);
EXPORT_SYMBOL(parport_device_num);
EXPORT_SYMBOL(parport_device_coords);
EXPORT_SYMBOL(parport_daisy_deselect_all);
EXPORT_SYMBOL(parport_daisy_select);
EXPORT_SYMBOL(parport_daisy_init);
#endif

void inc_parport_count(void)
{
#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif
}

void dec_parport_count(void)
{
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
}
