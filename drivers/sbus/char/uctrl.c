/* $Id: uctrl.c,v 1.2 1999/09/07 23:11:08 shadow Exp $
 * uctrl.c: TS102 Microcontroller interface on Tadpole Sparcbook 3
 *
 * Copyright 1999 Derrick J Brashear (shadow@dementia.org)
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>

#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/sbus.h>

#define UCTRL_MINOR	174

struct uctrl_driver {
	volatile u32 *regs;
	int irq;
};

static struct uctrl_driver drv;

static loff_t
uctrl_llseek(struct file *file, loff_t offset, int type)
{
	return -ESPIPE;
}

static int
uctrl_ioctl(struct inode *inode, struct file *file,
	      unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
		default:
			return -EINVAL;
	}
	return 0;
}

static int
uctrl_open(struct inode *inode, struct file *file)
{
	MOD_INC_USE_COUNT;
	return 0;
}

static int
uctrl_release(struct inode *inode, struct file *file)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

void uctrl_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uctrl_driver *driver = (struct uctrl_driver *)dev_id;
	printk("in uctrl_interrupt\n");
}

static struct file_operations uctrl_fops = {
	uctrl_llseek,
	NULL,           /* read */
	NULL,           /* write */
	NULL,		/* readdir */
	NULL,		/* poll */	
	uctrl_ioctl,
	NULL,		/* mmap */
	uctrl_open,
	NULL,		/* flush */
	uctrl_release
};

static struct miscdevice uctrl_dev = {
	UCTRL_MINOR,
	"uctrl",
	&uctrl_fops
};

#ifdef MODULE
int init_module(void)
#else
int __init uctrl_init(void)
#endif
{
	struct uctrl_driver *driver = &drv;
	int len;
	struct linux_prom_irqs tmp_irq[2];
        unsigned int vaddr[2] = { 0, 0 };
	int tmpnode, uctrlnode = prom_getchild(prom_root_node);

	tmpnode = prom_searchsiblings(uctrlnode, "obio");

	if (tmpnode)
	  uctrlnode = prom_getchild(tmpnode);

	uctrlnode = prom_searchsiblings(uctrlnode, "uctrl");

	if (!uctrlnode)
		return -ENODEV;

	/* the prom mapped it for us */
	len = prom_getproperty(uctrlnode, "address", (void *) vaddr,
			       sizeof(vaddr));
	driver->regs = vaddr[0];

	len = prom_getproperty(uctrlnode, "intr", (char *) tmp_irq,
			       sizeof(tmp_irq));
	if(!driver->irq) 
		driver->irq = tmp_irq[0].pri;

	request_irq(driver->irq, uctrl_interrupt, 0, 
		    "uctrl", driver);

	enable_irq(driver->irq);

	if (misc_register(&uctrl_dev)) {
		printk("%s: unable to get misc minor %d\n",
		       __FUNCTION__, uctrl_dev.minor);
		disable_irq(driver->irq);
		free_irq(driver->irq, driver);
		return -ENODEV;
	}

	printk(KERN_INFO, "uctrl: 0x%x (irq %d)\n", driver->regs, __irq_itoa(driver->irq));
        return 0;
}


#ifdef MODULE
void cleanup_module(void)
{
	struct uctrl_driver *driver = &drv;

	misc_deregister(&uctrl_dev);
	if (driver->irq) {
		disable_irq(driver->irq);
		free_irq(driver->irq, driver);
	}
	if (driver->regs)
		driver->regs = 0;
}
#endif
