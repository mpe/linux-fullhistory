#ifndef _I8042_X86IA64IO_H
#define _I8042_X86IA64IO_H

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

/*
 * Names.
 */

#define I8042_KBD_PHYS_DESC "isa0060/serio0"
#define I8042_AUX_PHYS_DESC "isa0060/serio1"
#define I8042_MUX_PHYS_DESC "isa0060/serio%d"

/*
 * IRQs.
 */

#if defined(__ia64__)
# define I8042_MAP_IRQ(x)	isa_irq_to_vector((x))
#else
# define I8042_MAP_IRQ(x)	(x)
#endif

#define I8042_KBD_IRQ	i8042_kbd_irq
#define I8042_AUX_IRQ	i8042_aux_irq

static int i8042_kbd_irq;
static int i8042_aux_irq;

/*
 * Register numbers.
 */

#define I8042_COMMAND_REG	i8042_command_reg
#define I8042_STATUS_REG	i8042_command_reg
#define I8042_DATA_REG		i8042_data_reg

static int i8042_command_reg = 0x64;
static int i8042_data_reg = 0x60;


static inline int i8042_read_data(void)
{
	return inb(I8042_DATA_REG);
}

static inline int i8042_read_status(void)
{
	return inb(I8042_STATUS_REG);
}

static inline void i8042_write_data(int val)
{
	outb(val, I8042_DATA_REG);
}

static inline void i8042_write_command(int val)
{
	outb(val, I8042_COMMAND_REG);
}

#if defined(__i386__)

#include <linux/dmi.h>

static struct dmi_system_id __initdata i8042_dmi_noloop_table[] = {
	{
		.ident = "Compaq Proliant 8500",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Compaq"),
			DMI_MATCH(DMI_PRODUCT_NAME , "ProLiant"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "8500"),
		},
	},
	{
		.ident = "Compaq Proliant DL760",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Compaq"),
			DMI_MATCH(DMI_PRODUCT_NAME , "ProLiant"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "DL760"),
		},
	},
	{ }
};

/*
 * Some Fujitsu notebooks are ahving trouble with touhcpads if
 * active multiplexing mode is activated. Luckily they don't have
 * external PS/2 ports so we can safely disable it.
 */
static struct dmi_system_id __initdata i8042_dmi_nomux_table[] = {
	{
		.ident = "Fujitsu Lifebook P7010/P7010D",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "P7010"),
		},
	},
	{
		.ident = "Fujitsu Lifebook P5020D",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "LifeBook P Series"),
		},
	},
	{
		.ident = "Fujitsu Lifebook S2000",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "LifeBook S Series"),
		},
	},
	{
		.ident = "Fujitsu T70H",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "FMVLT70H"),
		},
	},
	{ }
};



#endif


#ifdef CONFIG_PNP
#include <linux/pnp.h>

static int i8042_pnp_kbd_registered;
static int i8042_pnp_aux_registered;

static int i8042_pnp_command_reg;
static int i8042_pnp_data_reg;
static int i8042_pnp_kbd_irq;
static int i8042_pnp_aux_irq;

static char i8042_pnp_kbd_name[32];
static char i8042_pnp_aux_name[32];

static int i8042_pnp_kbd_probe(struct pnp_dev *dev, const struct pnp_device_id *did)
{
	if (pnp_port_valid(dev, 0) && pnp_port_len(dev, 0) == 1)
		i8042_pnp_data_reg = pnp_port_start(dev,0);

	if (pnp_port_valid(dev, 1) && pnp_port_len(dev, 1) == 1)
		i8042_pnp_command_reg = pnp_port_start(dev, 1);

	if (pnp_irq_valid(dev,0))
		i8042_pnp_kbd_irq = pnp_irq(dev, 0);

	strncpy(i8042_pnp_kbd_name, did->id, sizeof(i8042_pnp_kbd_name));
	if (strlen(pnp_dev_name(dev))) {
		strncat(i8042_pnp_kbd_name, ":", sizeof(i8042_pnp_kbd_name));
		strncat(i8042_pnp_kbd_name, pnp_dev_name(dev), sizeof(i8042_pnp_kbd_name));
	}

	return 0;
}

static int i8042_pnp_aux_probe(struct pnp_dev *dev, const struct pnp_device_id *did)
{
	if (pnp_port_valid(dev, 0) && pnp_port_len(dev, 0) == 1)
		i8042_pnp_data_reg = pnp_port_start(dev,0);

	if (pnp_port_valid(dev, 1) && pnp_port_len(dev, 1) == 1)
		i8042_pnp_command_reg = pnp_port_start(dev, 1);

	if (pnp_irq_valid(dev, 0))
		i8042_pnp_aux_irq = pnp_irq(dev, 0);

	strncpy(i8042_pnp_aux_name, did->id, sizeof(i8042_pnp_aux_name));
	if (strlen(pnp_dev_name(dev))) {
		strncat(i8042_pnp_aux_name, ":", sizeof(i8042_pnp_aux_name));
		strncat(i8042_pnp_aux_name, pnp_dev_name(dev), sizeof(i8042_pnp_aux_name));
	}

	return 0;
}

static struct pnp_device_id pnp_kbd_devids[] = {
	{ .id = "PNP0303", .driver_data = 0 },
	{ .id = "PNP030b", .driver_data = 0 },
	{ .id = "", },
};

static struct pnp_driver i8042_pnp_kbd_driver = {
	.name           = "i8042 kbd",
	.id_table       = pnp_kbd_devids,
	.probe          = i8042_pnp_kbd_probe,
};

static struct pnp_device_id pnp_aux_devids[] = {
	{ .id = "PNP0f03", .driver_data = 0 },
	{ .id = "PNP0f0b", .driver_data = 0 },
	{ .id = "PNP0f0e", .driver_data = 0 },
	{ .id = "PNP0f12", .driver_data = 0 },
	{ .id = "PNP0f13", .driver_data = 0 },
	{ .id = "PNP0f19", .driver_data = 0 },
	{ .id = "PNP0f1c", .driver_data = 0 },
	{ .id = "SYN0801", .driver_data = 0 },
	{ .id = "", },
};

static struct pnp_driver i8042_pnp_aux_driver = {
	.name           = "i8042 aux",
	.id_table       = pnp_aux_devids,
	.probe          = i8042_pnp_aux_probe,
};

static void i8042_pnp_exit(void)
{
	if (i8042_pnp_kbd_registered)
		pnp_unregister_driver(&i8042_pnp_kbd_driver);

	if (i8042_pnp_aux_registered)
		pnp_unregister_driver(&i8042_pnp_aux_driver);
}

static int i8042_pnp_init(void)
{
	int result_kbd, result_aux;

	if (i8042_nopnp) {
		printk("i8042: PNP detection disabled\n");
		return 0;
	}

	if ((result_kbd = pnp_register_driver(&i8042_pnp_kbd_driver)) >= 0)
		i8042_pnp_kbd_registered = 1;
	if ((result_aux = pnp_register_driver(&i8042_pnp_aux_driver)) >= 0)
		i8042_pnp_aux_registered = 1;

	if (result_kbd <= 0 && result_aux <= 0) {
		i8042_pnp_exit();
#if defined(__ia64__)
		return -ENODEV;
#else
		printk(KERN_WARNING "PNP: No PS/2 controller found. Probing ports directly.\n");
		return 0;
#endif
	}

	if (((i8042_pnp_data_reg & ~0xf) == (i8042_data_reg & ~0xf) &&
	      i8042_pnp_data_reg != i8042_data_reg) || !i8042_pnp_data_reg) {
		printk(KERN_WARNING "PNP: PS/2 controller has invalid data port %#x; using default %#x\n",
			i8042_pnp_data_reg, i8042_data_reg);
		i8042_pnp_data_reg = i8042_data_reg;
	}

	if (((i8042_pnp_command_reg & ~0xf) == (i8042_command_reg & ~0xf) &&
	      i8042_pnp_command_reg != i8042_command_reg) || !i8042_pnp_command_reg) {
		printk(KERN_WARNING "PNP: PS/2 controller has invalid command port %#x; using default %#x\n",
			i8042_pnp_command_reg, i8042_command_reg);
		i8042_pnp_command_reg = i8042_command_reg;
	}

	if (!i8042_pnp_kbd_irq) {
		printk(KERN_WARNING "PNP: PS/2 controller doesn't have KBD irq; using default %#x\n", i8042_kbd_irq);
		i8042_pnp_kbd_irq = i8042_kbd_irq;
	}

	if (result_aux > 0 && !i8042_pnp_aux_irq) {
		printk(KERN_WARNING "PNP: PS/2 controller doesn't have AUX irq; using default %#x\n", i8042_aux_irq);
		i8042_pnp_aux_irq = i8042_aux_irq;
	}

#if defined(__ia64__)
	if (result_aux <= 0)
		i8042_noaux = 1;
#endif

	i8042_data_reg = i8042_pnp_data_reg;
	i8042_command_reg = i8042_pnp_command_reg;
	i8042_kbd_irq = i8042_pnp_kbd_irq;
	i8042_aux_irq = i8042_pnp_aux_irq;

	printk(KERN_INFO "PNP: PS/2 Controller [%s%s%s] at %#x,%#x irq %d%s%d\n",
		i8042_pnp_kbd_name, (result_kbd > 0 && result_aux > 0) ? "," : "", i8042_pnp_aux_name,
		i8042_data_reg, i8042_command_reg, i8042_kbd_irq,
		(result_aux > 0) ? "," : "", i8042_aux_irq);

	return 0;
}

#endif

static inline int i8042_platform_init(void)
{
/*
 * On ix86 platforms touching the i8042 data register region can do really
 * bad things. Because of this the region is always reserved on ix86 boxes.
 *
 *	if (!request_region(I8042_DATA_REG, 16, "i8042"))
 *		return -1;
 */

	i8042_kbd_irq = I8042_MAP_IRQ(1);
	i8042_aux_irq = I8042_MAP_IRQ(12);

#ifdef CONFIG_PNP
	if (i8042_pnp_init())
		return -1;
#endif

#if defined(__ia64__)
        i8042_reset = 1;
#endif

#if defined(__i386__)
	if (dmi_check_system(i8042_dmi_noloop_table))
		i8042_noloop = 1;

	if (dmi_check_system(i8042_dmi_nomux_table))
		i8042_nomux = 1;
#endif

	return 0;
}

static inline void i8042_platform_exit(void)
{
#ifdef CONFIG_PNP
	i8042_pnp_exit();
#endif
}

#endif /* _I8042_X86IA64IO_H */
