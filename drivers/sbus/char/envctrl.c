/* $Id: envctrl.c,v 1.9 1998/11/06 07:38:20 ecd Exp $
 * envctrl.c: Temperature and Fan monitoring on Machines providing it.
 *
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/miscdevice.h>

#include <asm/ebus.h>
#include <asm/uaccess.h>
#include <asm/envctrl.h>

#define ENVCTRL_MINOR	162


#undef DEBUG_BUS_SCAN


#define PCF8584_ADDRESS	0x55

#define CONTROL_PIN	0x80
#define CONTROL_ES0	0x40
#define CONTROL_ES1	0x20
#define CONTROL_ES2	0x10
#define CONTROL_ENI	0x08
#define CONTROL_STA	0x04
#define CONTROL_STO	0x02
#define CONTROL_ACK	0x01

#define STATUS_PIN	0x80
#define STATUS_STS	0x20
#define STATUS_BER	0x10
#define STATUS_LRB	0x08
#define STATUS_AD0	0x08
#define STATUS_AAB	0x04
#define STATUS_LAB	0x02
#define STATUS_BB	0x01

/*
 * CLK Mode Register.
 */
#define BUS_CLK_90	0x00
#define BUS_CLK_45	0x01
#define BUS_CLK_11	0x02
#define BUS_CLK_1_5	0x03

#define CLK_3		0x00
#define CLK_4_43	0x10
#define CLK_6		0x14
#define CLK_8		0x18
#define CLK_12		0x1c


#define I2C_WRITE	0x00
#define I2C_READ	0x01

struct pcf8584_reg
{
	__volatile__ unsigned char data;
	__volatile__ unsigned char csr;
};

static struct pcf8584_reg *i2c;


#ifdef DEBUG_BUS_SCAN
struct i2c_addr_map {
	unsigned char addr;
	unsigned char mask;
	char *name;
};

static struct i2c_addr_map devmap[] = {
	{ 0x38, 0x78, "PCF8574A" },
	{ 0x20, 0x78, "TDA8444" },
	{ 0x48, 0x78, "PCF8591" },
};
#define NR_DEVMAP (sizeof(devmap) / sizeof(devmap[0]))
#endif

static __inline__ int
PUT_DATA(__volatile__ unsigned char *data, char *buffer, int user)
{
	if (user) {
		if (put_user(*data, buffer))
			return -EFAULT;
	} else {
		*buffer = *data;
	}
	return 0;
}

static __inline__ int
GET_DATA(__volatile__ unsigned char *data, const char *buffer, int user)
{
	if (user) {
		if (get_user(*data, buffer))
			return -EFAULT;
	} else {
		*data = *buffer;
	}
	return 0;
}

static int
i2c_read(unsigned char dev, char *buffer, int len, int user)
{
	unsigned char dummy;
	unsigned char stat;
	int error = -ENODEV;
	int count = 0;

	i2c->data = (dev << 1) | I2C_READ;

	while (!(i2c->csr & STATUS_BB))
		udelay(1);

	i2c->csr = CONTROL_PIN | CONTROL_ES0 | CONTROL_STA | CONTROL_ACK;

	do {
		udelay(1);
		while ((stat = i2c->csr) & STATUS_PIN)
			udelay(1);

		if (stat & STATUS_LRB)
			goto stop;

		error = 0;
		if (len == 0) {
			count--;
			break;
		}

		if (count == (len - 1))
			break;

		if (count++ > 0) {
			error = PUT_DATA(&i2c->data, buffer++, user);
			if (error)
				break;
		} else
			dummy = i2c->data;
	} while (1);

	i2c->csr = CONTROL_ES0;
	if (!error && (count++ > 0))
		error = PUT_DATA(&i2c->data, buffer++, user);
	else
		dummy = i2c->data;

	udelay(1);
	while ((stat = i2c->csr) & STATUS_PIN)
		udelay(1);

stop:
	i2c->csr = CONTROL_PIN | CONTROL_ES0 | CONTROL_STO | CONTROL_ACK;
	if (!error && (count++ > 0))
		error = PUT_DATA(&i2c->data, buffer++, user);
	else
		dummy = i2c->data;

	if (error)
		return error;
	return count - 1;
}

static int
i2c_write(unsigned char dev, const char *buffer, int len, int user)
{
	int error = -ENODEV;
	int count = 0;

	while (!(i2c->csr & STATUS_BB))
		udelay(1);

	i2c->data = (dev << 1) | I2C_WRITE;
	i2c->csr = CONTROL_PIN | CONTROL_ES0 | CONTROL_STA | CONTROL_ACK;

	do {
		unsigned char stat;

		udelay(1);
		while ((stat = i2c->csr) & STATUS_PIN)
			udelay(1);

		if (stat & STATUS_LRB)
			break;

		error = count;
		if (count == len)
			break;

		error = GET_DATA(&i2c->data, buffer++, user);
		if (error)
			break;

		count++;
	} while (1);

	i2c->csr = CONTROL_PIN | CONTROL_ES0 | CONTROL_STO | CONTROL_ACK;
	return error;
}

__initfunc(static int i2c_scan_bus(void))
{
	unsigned char dev;
	int count = 0;

	for (dev = 1; dev < 128; dev++) {
		if (i2c_read(dev, 0, 0, 0) == 0) {
#ifdef DEBUG_BUS_SCAN
			int i;
			for (i = 0; i < NR_DEVMAP; i++)
				if ((dev & devmap[i].mask) == devmap[i].addr)
					break;
			printk("envctrl: i2c device at %02x: %s\n", dev,
			       i < NR_DEVMAP ? devmap[i].name : "unknown");
#endif
			count++;
		}
	}
	if (!count) {
		printk("%s: no devices found\n", __FUNCTION__);
		return -ENODEV;
	}
	return 0;
}

static loff_t
envctrl_llseek(struct file *file, loff_t offset, int type)
{
	return -ESPIPE;
}

static ssize_t
envctrl_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	unsigned long addr = (unsigned long)file->private_data;

	return i2c_read(addr, buf, count, 1);
}

static ssize_t
envctrl_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	unsigned long addr = (unsigned long)file->private_data;

	return i2c_write(addr, buf, count, 1);
}

static int
envctrl_ioctl(struct inode *inode, struct file *file,
	      unsigned int cmd, unsigned long arg)
{
	unsigned long data;
	int addr;

	switch (cmd) {
		case I2CIOCSADR:
			if (get_user(addr, (int *)arg))
				return -EFAULT;
			data = addr & 0x7f;
			file->private_data = (void *)data;
			break;
		case I2CIOCGADR:
			addr = (unsigned long)file->private_data;
			if (put_user(addr, (int *)arg))
				return -EFAULT;
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

static int
envctrl_open(struct inode *inode, struct file *file)
{
	file->private_data = 0;
	MOD_INC_USE_COUNT;
	return 0;
}

static int
envctrl_release(struct inode *inode, struct file *file)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

static struct file_operations envctrl_fops = {
	envctrl_llseek,
	envctrl_read,
	envctrl_write,
	NULL,		/* readdir */
	NULL,		/* poll */	
	envctrl_ioctl,
	NULL,		/* mmap */
	envctrl_open,
	NULL,		/* flush */
	envctrl_release
};

static struct miscdevice envctrl_dev = {
	ENVCTRL_MINOR,
	"envctrl",
	&envctrl_fops
};

#ifdef MODULE
int init_module(void)
#else
__initfunc(int envctrl_init(void))
#endif
{
#ifdef CONFIG_PCI
	struct linux_ebus *ebus;
	struct linux_ebus_device *edev = 0;
	int err;

	for_each_ebus(ebus) {
		for_each_ebusdev(edev, ebus) {
			if (!strcmp(edev->prom_name, "SUNW,envctrl"))
				goto ebus_done;
			if (!strcmp(edev->prom_name, "SUNW,rasctrl"))
				goto ebus_done;
		}
	}
ebus_done:
	if (!edev) {
		printk("%s: ebus device not found\n", __FUNCTION__);
		return -ENODEV;
	}

	if (check_region(edev->base_address[0], sizeof(*i2c))) {
		printk("%s: Can't get region %lx, %d\n",
		       __FUNCTION__, edev->base_address[0], (int)sizeof(*i2c));
		return -ENODEV;
	}

	i2c = (struct pcf8584_reg *)edev->base_address[0];

	request_region((unsigned long)i2c, sizeof(*i2c), "i2c");

	i2c->csr = CONTROL_PIN;
	i2c->data = PCF8584_ADDRESS;
	i2c->csr = CONTROL_PIN | CONTROL_ES1;
	i2c->data = CLK_4_43 | BUS_CLK_90;
	i2c->csr = CONTROL_PIN | CONTROL_ES0 | CONTROL_ACK;
	mdelay(10);

	if (misc_register(&envctrl_dev)) {
		printk("%s: unable to get misc minor %d\n",
		       __FUNCTION__, envctrl_dev.minor);
		release_region((unsigned long)i2c, sizeof(*i2c));
	}

	err = i2c_scan_bus();
	if (err)
		release_region((unsigned long)i2c, sizeof(*i2c));
	return err;
#else
	return -ENODEV;
#endif
}


#ifdef MODULE
void cleanup_module(void)
{
	misc_deregister(&envctrl_dev);
	release_region((unsigned long)i2c, sizeof(*i2c));
}
#endif
