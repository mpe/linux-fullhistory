/* $Id: envctrl.c,v 1.3 1998/04/10 08:42:24 jj Exp $
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

#include <asm/ebus.h>

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

static int
envctrl_read(unsigned char dev, char *buffer, int len)
{
	unsigned char dummy;
	unsigned char stat;
	int error = -ENODEV;
	int count = -1;

	if (len == 0)
		return 0;

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
		if (count == (len - 2))
			goto final;

		if (++count > 0)
			*buffer++ = i2c->data;
		else
			dummy = i2c->data;
	} while (1);

final:
	i2c->csr = CONTROL_ES0;
	if (++count > 0)
		*buffer++ = i2c->data;
	else
		dummy = i2c->data;

	udelay(1);
	while ((stat = i2c->csr) & STATUS_PIN)
		udelay(1);

stop:
	i2c->csr = CONTROL_PIN | CONTROL_ES0 | CONTROL_STO | CONTROL_ACK;
	if (++count > 0)
		*buffer++ = i2c->data;
	else
		dummy = i2c->data;

	if (error)
		return error;
	return count;
}

static int
envctrl_write(unsigned char dev, char *buffer, int len)
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
			goto stop;
		error = count;
		if (count == len)
			goto stop;

		i2c->data = *buffer++;
		count++;
	} while (1);

stop:
	i2c->csr = CONTROL_PIN | CONTROL_ES0 | CONTROL_STO | CONTROL_ACK;
	return error;
}

__initfunc(static int scan_bus(void))
{
	unsigned char dev;
	int count = 0;
	int i;

	/* scan */
	for (dev = 1; dev < 128; dev++)
		if (envctrl_write(dev, 0, 0) == 0) {
			for (i = 0; i < NR_DEVMAP; i++)
				if ((dev & devmap[i].mask) == devmap[i].addr)
					break;
			printk("envctrl: i2c device at %02x: %s\n", dev,
			       i < NR_DEVMAP ? devmap[i].name : "unknown");
{
			unsigned char buf[4];
			if (envctrl_read(dev, buf, 4) == 4)
				printk("envctrl: read %02x %02x %02x %02x\n",
			               buf[0], buf[1], buf[2], buf[3]);
}
			count++;
		}
	return count ? 0 : -ENODEV;
}

#ifdef MODULE
int init_module(void)
#else
__initfunc(int envctrl_init(void))
#endif
{
#ifdef CONFIG_PCI
	struct linux_ebus *ebus;
	struct linux_ebus_device *edev;

	for_all_ebusdev(edev, ebus)
		if (!strcmp(edev->prom_name, "SUNW,envctrl"))
			break;

	if (!edev)
		return -ENODEV;

	if (check_region(edev->base_address[0], sizeof(*i2c))) {
		prom_printf("%s: Can't get region %lx, %d\n",
			    __FUNCTION__, edev->base_address[0],
			    sizeof(*i2c));
		prom_halt();
	}

	request_region(edev->base_address[0],
		       sizeof(*i2c), "i2c");

	i2c = (struct pcf8584_reg *)edev->base_address[0];

	i2c->csr = CONTROL_PIN;
	i2c->data = PCF8584_ADDRESS;
	i2c->csr = CONTROL_PIN | CONTROL_ES1;
	i2c->data = CLK_4_43 | BUS_CLK_90;
	i2c->csr = CONTROL_PIN | CONTROL_ES0 | CONTROL_ACK;
	mdelay(10);

	return scan_bus();
#else
	return -ENODEV;
#endif
}


#ifdef MODULE
void cleanup_module(void)
{
}
#endif
