/*
 * Support code for the SCOOP interface found on various Sharp PDAs
 *
 * Copyright (c) 2004 Richard Purdie
 *
 *	Based on code written by Sharp/Lineo for 2.4 kernels
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/device.h>
#include <asm/io.h>
#include <asm/hardware/scoop.h>

#define SCOOP_REG(d,adr) (*(volatile unsigned short*)(d +(adr)))

struct  scoop_dev {
	void  *base;
	spinlock_t scoop_lock;
	u32 scoop_gpwr;
};

void reset_scoop(struct device *dev)
{
	struct scoop_dev *sdev = dev_get_drvdata(dev);

	SCOOP_REG(sdev->base,SCOOP_MCR) = 0x0100;  // 00
	SCOOP_REG(sdev->base,SCOOP_CDR) = 0x0000;  // 04
	SCOOP_REG(sdev->base,SCOOP_CPR) = 0x0000;  // 0C
	SCOOP_REG(sdev->base,SCOOP_CCR) = 0x0000;  // 10
	SCOOP_REG(sdev->base,SCOOP_IMR) = 0x0000;  // 18
	SCOOP_REG(sdev->base,SCOOP_IRM) = 0x00FF;  // 14
	SCOOP_REG(sdev->base,SCOOP_ISR) = 0x0000;  // 1C
	SCOOP_REG(sdev->base,SCOOP_IRM) = 0x0000;
}

unsigned short set_scoop_gpio(struct device *dev, unsigned short bit)
{
	unsigned short gpio_bit;
	unsigned long flag;
	struct scoop_dev *sdev = dev_get_drvdata(dev);

	spin_lock_irqsave(&sdev->scoop_lock, flag);
	gpio_bit = SCOOP_REG(sdev->base, SCOOP_GPWR) | bit;
	SCOOP_REG(sdev->base, SCOOP_GPWR) = gpio_bit;
	spin_unlock_irqrestore(&sdev->scoop_lock, flag);

	return gpio_bit;
}

unsigned short reset_scoop_gpio(struct device *dev, unsigned short bit)
{
	unsigned short gpio_bit;
	unsigned long flag;
	struct scoop_dev *sdev = dev_get_drvdata(dev);

	spin_lock_irqsave(&sdev->scoop_lock, flag);
	gpio_bit = SCOOP_REG(sdev->base, SCOOP_GPWR) & ~bit;
	SCOOP_REG(sdev->base,SCOOP_GPWR) = gpio_bit;
	spin_unlock_irqrestore(&sdev->scoop_lock, flag);

	return gpio_bit;
}

EXPORT_SYMBOL(set_scoop_gpio);
EXPORT_SYMBOL(reset_scoop_gpio);

unsigned short read_scoop_reg(struct device *dev, unsigned short reg)
{
	struct scoop_dev *sdev = dev_get_drvdata(dev);
	return SCOOP_REG(sdev->base,reg);
}

void write_scoop_reg(struct device *dev, unsigned short reg, unsigned short data)
{
	struct scoop_dev *sdev = dev_get_drvdata(dev);
	SCOOP_REG(sdev->base,reg)=data;
}

EXPORT_SYMBOL(reset_scoop);
EXPORT_SYMBOL(read_scoop_reg);
EXPORT_SYMBOL(write_scoop_reg);

#ifdef CONFIG_PM
static int scoop_suspend(struct device *dev, uint32_t state, uint32_t level)
{
	if (level == SUSPEND_POWER_DOWN) {
		struct scoop_dev *sdev = dev_get_drvdata(dev);

		sdev->scoop_gpwr = SCOOP_REG(sdev->base,SCOOP_GPWR);
		SCOOP_REG(sdev->base,SCOOP_GPWR) = 0;
	}
	return 0;
}

static int scoop_resume(struct device *dev, uint32_t level)
{
	if (level == RESUME_POWER_ON) {
		struct scoop_dev *sdev = dev_get_drvdata(dev);

		SCOOP_REG(sdev->base,SCOOP_GPWR) = sdev->scoop_gpwr;
	}
	return 0;
}
#else
#define scoop_suspend	NULL
#define scoop_resume	NULL
#endif

int __init scoop_probe(struct device *dev)
{
	struct scoop_dev *devptr;
	struct scoop_config *inf;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!mem)
		return -EINVAL;

	devptr = kmalloc(sizeof(struct scoop_dev), GFP_KERNEL);

	if (!devptr)
		return  -ENOMEM;

	memset(devptr, 0, sizeof(struct scoop_dev));
	spin_lock_init(&devptr->scoop_lock);

	inf = dev->platform_data;
	devptr->base = ioremap(mem->start, mem->end - mem->start + 1);

	if (!devptr->base) {
		kfree(devptr);
		return -ENOMEM;
	}

	dev_set_drvdata(dev, devptr);

	printk("Sharp Scoop Device found at 0x%08x -> 0x%08x\n",(unsigned int)mem->start,(unsigned int)devptr->base);

	SCOOP_REG(devptr->base, SCOOP_MCR) = 0x0140;
	reset_scoop(dev);
	SCOOP_REG(devptr->base, SCOOP_GPCR) = inf->io_dir & 0xffff;
	SCOOP_REG(devptr->base, SCOOP_GPWR) = inf->io_out & 0xffff;

	return 0;
}

static int scoop_remove(struct device *dev)
{
	struct scoop_dev *sdev = dev_get_drvdata(dev);
	if (sdev) {
		iounmap(sdev->base);
		kfree(sdev);
		dev_set_drvdata(dev, NULL);
	}
	return 0;
}

static struct device_driver scoop_driver = {
	.name		= "sharp-scoop",
	.bus		= &platform_bus_type,
	.probe		= scoop_probe,
	.remove 	= scoop_remove,
	.suspend	= scoop_suspend,
	.resume		= scoop_resume,
};

int __init scoop_init(void)
{
	return driver_register(&scoop_driver);
}

subsys_initcall(scoop_init);
