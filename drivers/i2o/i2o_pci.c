/*
 *	Find I2O capable controllers on the PCI bus, and register/install
 *	them with the I2O layer
 *
 *	(C) Copyright 1999   Red Hat Software
 *	
 *	Written by Alan Cox, Building Number Three Ltd
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 * 	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	TODO:
 *		Support polled I2O PCI controllers. 
 */
 
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/i2o.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <asm/io.h>

/*
 *	Free bus specific resources
 */
 
static void i2o_pci_dispose(struct i2o_controller *c)
{
	I2O_IRQ_WRITE32(c,0xFFFFFFFF);
	if(c->bus.pci.irq > 0)
		free_irq(c->bus.pci.irq, c);
	iounmap(((u8 *)c->post_port)-0x40);
}

/*
 *	No real bus specific handling yet (note that later we will
 *	need to 'steal' PCI devices on i960 mainboards)
 */
 
static int i2o_pci_bind(struct i2o_controller *c, struct i2o_device *dev)
{
	MOD_INC_USE_COUNT;
	return 0;
}

static int i2o_pci_unbind(struct i2o_controller *c, struct i2o_device *dev)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 *	Bus specific interrupt handler
 */
 
static void i2o_pci_interrupt(int irq, void *dev_id, struct pt_regs *r)
{
	struct i2o_controller *c = dev_id;
	i2o_run_queue(c);
}	

/*
 *	Install a PCI (or in theory AGP) i2o controller
 */
 
int __init i2o_pci_install(struct pci_dev *dev)
{
	struct i2o_controller *c=kmalloc(sizeof(struct i2o_controller),
						GFP_KERNEL);
	u8 *mem;
	u32 memptr = 0;
	u32 size;
	
	int i;
	
	if(c==NULL)
	{
		printk(KERN_ERR "i2o_pci: insufficient memory to add controller.\n");
		return -ENOMEM;
	}
	memset(c, 0, sizeof(*c));
	
	for(i=0; i<6; i++)
	{
		/* Skip I/O spaces */
		if(!(dev->base_address[i]&PCI_BASE_ADDRESS_SPACE))
		{
			memptr=PCI_BASE_ADDRESS_MEM_MASK&dev->base_address[i];
			break;
		}
	}
	
	if(i==6)
	{
		printk(KERN_ERR "i2o_pci: I2O controller has no memory regions defined.\n");
		return -ENOMEM;
	}
	
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_0+4*i, 0xFFFFFFFF);
	pci_read_config_dword(dev, PCI_BASE_ADDRESS_0+4*i, &size);
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_0+4*i, dev->base_address[i]);
	
	/* Map the I2O controller */
	
	printk(KERN_INFO "PCI I2O controller at 0x%08X size=%d\n", memptr, -size);
	mem = ioremap(memptr, -size);
	
	c->bus.pci.irq = -1;

	c->irq_mask = (volatile u32 *)(mem+0x34);
	c->post_port = (volatile u32 *)(mem+0x40);
	c->reply_port = (volatile u32 *)(mem+0x44);

	c->mem_phys = memptr;
	c->mem_offset = (u32)mem;
	c->destructor = i2o_pci_dispose;
	
	c->bind = i2o_pci_bind;
	c->unbind = i2o_pci_unbind;
	
	c->type = I2O_TYPE_PCI;

	I2O_IRQ_WRITE32(c,0xFFFFFFFF);
	
	i = i2o_install_controller(c);
	
	if(i<0)
	{
		printk(KERN_ERR "i2o: unable to install controller.\n");
		return i;
	}

	c->bus.pci.irq = dev->irq;
	if(c->bus.pci.irq)
	{
		i=request_irq(dev->irq, i2o_pci_interrupt, SA_SHIRQ,
			c->name, c);
		if(i<0)
		{
			printk(KERN_ERR "%s: unable to allocate interrupt %d.\n",
				c->name, dev->irq);
			c->bus.pci.irq = -1;
			i2o_delete_controller(c);
			return -EBUSY;
		}
	}
	return 0;	
}

int __init i2o_pci_scan(void)
{
	struct pci_dev *dev;
	int count=0;
	
	printk(KERN_INFO "Checking for PCI I2O controllers...\n");
	
	for(dev=pci_devices; dev!=NULL; dev=dev->next)
	{
		if((dev->class>>8)!=PCI_CLASS_INTELLIGENT_I2O)
			continue;
		if((dev->class&0xFF)>1)
		{
			printk(KERN_INFO "I2O controller found but does not support I2O 1.5 (skipping).\n");
			continue;
		}
		printk(KERN_INFO "I2O controller on bus %d at %d.\n",
			dev->bus->number, dev->devfn);
		if(!dev->master)
			printk(KERN_WARNING "Controller not master enabled.\n");
		if(i2o_pci_install(dev)==0)
			count++;
	}
	if(count)
		printk(KERN_INFO "%d I2O controller%s found and installed.\n", count,
			count==1?"":"s");
	return count?count:-ENODEV;
}

static void i2o_pci_unload(void)
{
	int i=0;
	struct i2o_controller *c;
	
	for(i = 0; i < MAX_I2O_CONTROLLERS; i++)
	{
		c=i2o_find_controller(i);
		if(c==NULL)
			continue;		
		i2o_unlock_controller(c);
		if(c->type == I2O_TYPE_PCI)
			i2o_delete_controller(c);
	}
}

static void i2o_pci_activate(void)
{
	int i=0;
	struct i2o_controller *c;
	
	for(i = 0; i < MAX_I2O_CONTROLLERS; i++)
	{
		c=i2o_find_controller(i);
		if(c==NULL)
			continue;		
		if(c->type == I2O_TYPE_PCI)
		{
			if(i2o_activate_controller(c))
			{
				printk("I2O: Failed to initialize iop%d\n", c->unit);
				i2o_unlock_controller(c);
				i2o_delete_controller(c);
				continue;
			}
		
			I2O_IRQ_WRITE32(c,0);
		}
		i2o_unlock_controller(c);
	}
}

#ifdef MODULE

EXPORT_NO_SYMBOLS;
MODULE_AUTHOR("Red Hat Software");
MODULE_DESCRIPTION("I2O PCI Interface");

int init_module(void)
{
	if(i2o_pci_scan()<0)
		return -ENODEV;
	i2o_pci_activate();
	return 0;
}

void cleanup_module(void)
{
	i2o_pci_unload();
}

#else
__init void i2o_pci_init(void)
{
	if(i2o_pci_scan()>=0)
	{
		printk(KERN_INFO "Linux I2O PCI support (c) 1999 Red Hat Software.\n");
		i2o_pci_activate();
	}
}
#endif
