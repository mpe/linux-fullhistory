/* $Id: envctrl.c,v 1.12 1999/08/31 06:58:04 davem Exp $
 * envctrl.c: Temperature and Fan monitoring on Machines providing it.
 *
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 */

#include <linux/version.h>
#include <linux/config.h>
#include <linux/module.h>

#define __KERNEL_SYSCALLS__
#include <linux/sched.h>
#include <linux/unistd.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/miscdevice.h>
#include <linux/smp_lock.h>

#include <asm/ebus.h>
#include <asm/uaccess.h>
#include <asm/envctrl.h>

#define ENVCTRL_MINOR	162


#undef U450_SUPPORT		/* might fry you machine, careful here !!! */


#define DEBUG		1
#define DEBUG_BUS_SCAN	1


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,2,0)
#define schedule_timeout(a) { current->timeout = jiffies + (a); schedule(); }
#endif

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

/* PCF8584 register offsets */
#define I2C_DATA	0x00UL
#define I2C_CSR		0x01UL

struct i2c_device {
	unsigned char		addr;
	struct i2c_device	*next;
};

static unsigned long i2c_regs;
static struct i2c_device  *i2c_devices;

static int errno;

#define MAX_TEMPERATURE		111
#define MAX_FAN_SPEED		63


/*
 * UltraAXi constants.
 */
#define AXI_THERM_ADDR		0x9e
#define AXI_THERM_PORT_CPU	0
#define AXI_THERM_PORT_MOD	1
#define AXI_THERM_PORT_PCI	2
#define AXI_THERM_PORT_DISK	3

#define AXI_FAN_ADDR		0x4e
#define AXI_FAN_PORT_FRONT	0
#define AXI_FAN_PORT_BACK	1

#define AXI_PIO_ADDR		0x70

/*
 * Ultra 450 constants.
 */
#define U450_FAN_ADDR		0x4e
#define U450_FAN_PORT_CPU	0
#define U450_FAN_PORT_PS	1

#define U450_PIO_ADDR		0x70
#define U450_TIMER_ADDR		0xa0

static unsigned char
axi_cpu_temp_table[256] =
{
	0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x69, 0x67,
	0x66, 0x65, 0x64, 0x63, 0x61, 0x60, 0x5f, 0x5e,
	0x5d, 0x5b, 0x5a, 0x59, 0x58, 0x57, 0x55, 0x54,
	0x53, 0x52, 0x50, 0x4f, 0x4e, 0x4d, 0x4c, 0x4a,
	0x49, 0x48, 0x47, 0x46, 0x44, 0x43, 0x42, 0x41,
	0x40, 0x3e, 0x3d, 0x3c, 0x3c, 0x3b, 0x3b, 0x3a,
	0x3a, 0x39, 0x39, 0x38, 0x38, 0x37, 0x37, 0x36,
	0x36, 0x35, 0x35, 0x34, 0x34, 0x33, 0x33, 0x32,
	0x32, 0x31, 0x31, 0x30, 0x30, 0x2f, 0x2f, 0x2e,
	0x2d, 0x2d, 0x2c, 0x2c, 0x2b, 0x2b, 0x2a, 0x2a,
	0x29, 0x29, 0x28, 0x28, 0x27, 0x27, 0x26, 0x26,
	0x25, 0x25, 0x24, 0x24, 0x23, 0x23, 0x22, 0x22,
	0x21, 0x21, 0x20, 0x20, 0x1f, 0x1f, 0x1e, 0x1e,
	0x1e, 0x1e, 0x1d, 0x1d, 0x1d, 0x1d, 0x1c, 0x1c,
	0x1c, 0x1c, 0x1b, 0x1b, 0x1b, 0x1b, 0x1a, 0x1a,
	0x1a, 0x1a, 0x1a, 0x19, 0x19, 0x19, 0x19, 0x18,
	0x18, 0x18, 0x18, 0x17, 0x17, 0x17, 0x17, 0x16,
	0x16, 0x16, 0x16, 0x16, 0x16, 0x15, 0x15, 0x15,
	0x15, 0x14, 0x14, 0x14, 0x14, 0x13, 0x13, 0x13,
	0x13, 0x12, 0x12, 0x12, 0x12, 0x12, 0x11, 0x11,
	0x11, 0x11, 0x10, 0x10, 0x10, 0x10, 0x0f, 0x0f,
	0x0f, 0x0f, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0d,
	0x0d, 0x0d, 0x0d, 0x0c, 0x0c, 0x0c, 0x0c, 0x0b,
	0x0b, 0x0b, 0x0b, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a,
	0x09, 0x09, 0x09, 0x09, 0x08, 0x08, 0x08, 0x08,
	0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06,
	0x06, 0x05, 0x05, 0x05, 0x05, 0x04, 0x04, 0x04,
	0x04, 0x03, 0x03, 0x03, 0x02, 0x02, 0x02, 0x02,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static unsigned char
axi_mod_temp_table[256] =
{
	0x65, 0x64, 0x63, 0x62, 0x61, 0x60, 0x5f, 0x5e,
	0x5d, 0x5c, 0x5b, 0x5a, 0x59, 0x58, 0x57, 0x56,
	0x55, 0x54, 0x53, 0x52, 0x51, 0x50, 0x4f, 0x4e,
	0x4d, 0x4c, 0x4b, 0x4a, 0x49, 0x48, 0x47, 0x46,
	0x45, 0x44, 0x43, 0x42, 0x41, 0x40, 0x3f, 0x3e,
	0x3d, 0x3c, 0x3b, 0x3b, 0x3b, 0x3a, 0x3a, 0x39,
	0x39, 0x38, 0x38, 0x37, 0x37, 0x36, 0x36, 0x35,
	0x35, 0x35, 0x34, 0x34, 0x33, 0x33, 0x32, 0x32,
	0x31, 0x31, 0x30, 0x30, 0x2f, 0x2f, 0x2e, 0x2e,
	0x2e, 0x2d, 0x2d, 0x2c, 0x2c, 0x2b, 0x2b, 0x2a,
	0x2a, 0x29, 0x29, 0x29, 0x28, 0x28, 0x27, 0x27,
	0x26, 0x26, 0x25, 0x25, 0x24, 0x24, 0x23, 0x23,
	0x23, 0x22, 0x22, 0x21, 0x21, 0x20, 0x20, 0x1f,
	0x1f, 0x1e, 0x1e, 0x1e, 0x1d, 0x1d, 0x1d, 0x1d,
	0x1d, 0x1c, 0x1c, 0x1c, 0x1c, 0x1b, 0x1b, 0x1b,
	0x1b, 0x1a, 0x1a, 0x1a, 0x1a, 0x1a, 0x19, 0x19,
	0x19, 0x19, 0x18, 0x18, 0x18, 0x18, 0x17, 0x17,
	0x17, 0x17, 0x17, 0x16, 0x16, 0x16, 0x16, 0x15,
	0x15, 0x15, 0x15, 0x14, 0x14, 0x14, 0x14, 0x13,
	0x13, 0x13, 0x13, 0x13, 0x12, 0x12, 0x12, 0x12,
	0x11, 0x11, 0x11, 0x11, 0x10, 0x10, 0x10, 0x10,
	0x10, 0x0f, 0x0f, 0x0f, 0x0f, 0x0e, 0x0e, 0x0e,
	0x0e, 0x0d, 0x0d, 0x0d, 0x0d, 0x0d, 0x0c, 0x0c,
	0x0c, 0x0c, 0x0b, 0x0b, 0x0b, 0x0b, 0x0a, 0x0a,
	0x0a, 0x0a, 0x09, 0x09, 0x09, 0x09, 0x09, 0x08,
	0x08, 0x08, 0x08, 0x07, 0x07, 0x07, 0x07, 0x06,
	0x06, 0x06, 0x06, 0x06, 0x05, 0x05, 0x05, 0x05,
	0x04, 0x04, 0x04, 0x04, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x02, 0x02, 0x02, 0x02, 0x01, 0x01, 0x01,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static unsigned char
axi_fan_speeds[112] =
{
	0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f,
	0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f,
	0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f,
	0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x20,
	0x22, 0x23, 0x25, 0x27, 0x28, 0x2a, 0x2b, 0x2d,
	0x2f, 0x30, 0x32, 0x33, 0x35, 0x37, 0x38, 0x3a,
	0x3b, 0x3d, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f,
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f,
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f,
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f,
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f,
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f,
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f,
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f
};


struct therm_regs {
	u32	addr;
	u32	port;
	u32	min_temp;
	u32	warning;
	u32	shutdown;
	u32	num;
	u32	den;
};

struct thermistor {
	char			name[8];
	struct therm_regs	regs;
	unsigned char		(*temperature) (struct thermistor *);
	unsigned char		(*fan_speed) (struct thermistor *);
	struct thermistor	*next;		/* all thermistors */
	struct thermistor	*chain;		/* thermistors for one fan */
};

struct fan_regs {
	u32	addr;
	u32	port;
};

struct fan {
	char			name[8];
	struct fan_regs		regs;
	int			(*set_speed)(struct fan *, unsigned char value);
	int			(*check_failure)(struct fan *);
	unsigned char		value;
	struct thermistor	*monitor;
	struct fan		*next;
};


struct environment {
	struct thermistor	*thermistors;
	struct fan		*fans;
	unsigned char		*cpu_temp_table;
	unsigned char		*cpu_fan_speeds;
	unsigned char		*ps_temp_table;
	unsigned char		*ps_fan_speeds;
	void			(*enable) (struct environment *);
	void			(*disable) (struct environment *);
	void			(*keep_alive) (struct environment *);
	int			interval;
	pid_t			kenvd_pid;
	wait_queue_head_t	kenvd_wait;
	int			terminate;
};


static struct environment	envctrl;


#ifdef DEBUG_BUS_SCAN
struct i2c_addr_map {
	unsigned char addr;
	unsigned char mask;
	char *name;
};

static struct i2c_addr_map devmap[] = {
	{ 0x70, 0xf0, "PCF8574A" },
	{ 0x40, 0xf0, "TDA8444" },
	{ 0x90, 0xf0, "PCF8591" },
	{ 0xa0, 0xf0, "PCF8583" },
};
#define NR_DEVMAP (sizeof(devmap) / sizeof(devmap[0]))
#endif

static __inline__ int
PUT_DATA(unsigned long data, char *buffer, int user)
{
	if (user) {
		u8 tmp = readb(data);
		if (put_user(tmp, buffer))
			return -EFAULT;
	} else {
		*buffer = readb(data);
	}
	return 0;
}

static __inline__ int
GET_DATA(unsigned long data, const char *buffer, int user)
{
	if (user) {
		u8 tmp;
		if (get_user(tmp, buffer))
			return -EFAULT;
		writeb(tmp, data);
	} else {
		writeb(*buffer, data);
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

	writeb((dev & 0xfe) | I2C_READ, i2c_regs + I2C_DATA);

	while (!(readb(i2c_regs + I2C_CSR) & STATUS_BB))
		udelay(1);

	writeb(CONTROL_PIN | CONTROL_ES0 | CONTROL_STA | CONTROL_ACK,
	       i2c_regs + I2C_CSR);

	do {
		udelay(1);
		while ((stat = readb(i2c_regs + I2C_CSR)) & STATUS_PIN)
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
			error = PUT_DATA(i2c_regs + I2C_DATA, buffer++, user);
			if (error)
				break;
		} else
			dummy = readb(i2c_regs + I2C_DATA);
	} while (1);

	writeb(CONTROL_ES0, i2c_regs + I2C_CSR);
	if (!error && (count++ > 0))
		error = PUT_DATA(i2c_regs + I2C_DATA, buffer++, user);
	else
		dummy = readb(i2c_regs + I2C_DATA);

	udelay(1);
	while ((stat = readb(i2c_regs + I2C_CSR)) & STATUS_PIN)
		udelay(1);

stop:
	writeb(CONTROL_PIN | CONTROL_ES0 | CONTROL_STO | CONTROL_ACK,
	       i2c_regs + I2C_CSR);
	if (!error && (count++ > 0))
		error = PUT_DATA(i2c_regs + I2C_DATA, buffer++, user);
	else
		dummy = readb(i2c_regs + I2C_DATA);

	if (error)
		return error;
	return count - 1;
}

static int
i2c_write(unsigned char dev, const char *buffer, int len, int user)
{
	int error = -ENODEV;
	int count = 0;
	int timeout;

	timeout = 1000000;
	while (!(readb(i2c_regs + I2C_CSR) & STATUS_BB) && --timeout)
		udelay(1);
	if (!timeout) {
		printk("%s [%d]: TIMEOUT\n", __FUNCTION__, __LINE__);
		return -ENODEV;
	}

	writeb((dev & 0xfe) | I2C_WRITE, i2c_regs + I2C_DATA);
	writeb(CONTROL_PIN | CONTROL_ES0 | CONTROL_STA | CONTROL_ACK,
	       i2c_regs + I2C_CSR);

	do {
		unsigned char stat;

		udelay(1);
		timeout = 1000000;
		while (((stat = readb(i2c_regs + I2C_CSR)) & STATUS_PIN) && --timeout)
			udelay(1);

		if (!timeout) {
			printk("%s [%d]: TIMEOUT\n", __FUNCTION__, __LINE__);
			break;
		}

		if (stat & STATUS_LRB)
			break;

		error = count;
		if (count == len)
			break;

		error = GET_DATA(i2c_regs + I2C_DATA, buffer++, user);
		if (error)
			break;

		count++;
	} while (1);

	writeb(CONTROL_PIN | CONTROL_ES0 | CONTROL_STO | CONTROL_ACK, 
	       i2c_regs + I2C_CSR);
	return error;
}

#ifdef U450_SUPPORT
static int
i2c_write_read(unsigned char dev, char *outbuf, int outlen,
				  char *inbuf, int inlen, int user)
{
	unsigned char dummy;
	unsigned char stat;
	int error = -ENODEV;
	int count = 0;

	while (!(readb(i2c_regs + I2C_CSR) & STATUS_BB))
		udelay(1);

	writeb((dev & 0xfe) | I2C_WRITE, i2c_regs + I2C_DATA);
	writeb(CONTROL_PIN | CONTROL_ES0 | CONTROL_STA | CONTROL_ACK, 
	       i2c_regs + I2C_CSR);

	do {
		unsigned char stat;

		udelay(1);
		while ((stat = readb(i2c_regs + I2C_CSR)) & STATUS_PIN)
			udelay(1);

		if (stat & STATUS_LRB)
			break;

		error = count;
		if (count == outlen)
			break;

		error = GET_DATA(i2c_regs + I2C_DATA, outbuf++, user);
		if (error)
			break;

		count++;
	} while (1);

	if (error < 0) {
		writeb(CONTROL_PIN | CONTROL_ES0 |
		       CONTROL_STO | CONTROL_ACK, i2c_regs + I2C_CSR);
		return error;
	}

	writeb(CONTROL_ES0 | CONTROL_STA | CONTROL_ACK, i2c_regs + I2C_CSR);
	udelay(1);
	writeb((dev & 0xfe) | I2C_READ, i2c_regs + I2C_DATA);

	count = 0;
	do {
		udelay(1);
		while ((stat = readb(i2c_regs + I2C_CSR)) & STATUS_PIN)
			udelay(1);

		if (stat & STATUS_LRB)
			goto stop;

		error = 0;
		if (inlen == 0) {
			count--;
			break;
		}

		if (count == (inlen - 1))
			break;

		if (count++ > 0) {
			error = PUT_DATA(i2c_regs + I2C_DATA, inbuf++, user);
			if (error)
				break;
		} else
			dummy = readb(i2c_regs + I2C_DATA);
	} while (1);

	writeb(CONTROL_ES0, i2c_regs + I2C_CSR);
	if (!error && (count++ > 0))
		error = PUT_DATA(i2c_regs + I2C_DATA, inbuf++, user);
	else
		dummy = readb(i2c_regs + I2C_DATA);

	udelay(1);
	while ((stat = readb(i2c_regs + I2C_CSR)) & STATUS_PIN)
		udelay(1);

stop:
	writeb(CONTROL_PIN | CONTROL_ES0 | CONTROL_STO | CONTROL_ACK,
	       i2c_regs + I2C_CSR);
	if (!error && (count++ > 0))
		error = PUT_DATA(i2c_regs + I2C_DATA, inbuf++, user);
	else
		dummy = readb(i2c_regs + I2C_DATA);

	if (error)
		return error;
	return count - 1;
}
#endif /* U450_SUPPORT */

static struct i2c_device *
i2c_find_device(unsigned char addr)
{
	struct i2c_device *dev;

	for (dev = i2c_devices; dev; dev = dev->next) {
		if (dev->addr == addr)
			return dev;
	}
	return 0;
}

static void
i2c_free_devices(void)
{
	struct i2c_device *dev;

	dev = i2c_devices;
	while (dev) {
		i2c_devices = dev->next;
		kfree(dev);
		dev = i2c_devices;
	}
}

static __init int i2c_scan_bus(void)
{
	struct i2c_device *dev, **last;
	unsigned int addr;
	int count = 0;

	last = &i2c_devices;
	for (addr = 0; addr < 256; addr += 2) {
		if (i2c_write(addr, 0, 0, 0) == 0) {
#ifdef DEBUG_BUS_SCAN
			int i;
			for (i = 0; i < NR_DEVMAP; i++)
				if ((addr & devmap[i].mask) == devmap[i].addr)
					break;
			printk("envctrl: i2c device at %02x: %s\n", addr,
			       i < NR_DEVMAP ? devmap[i].name : "unknown");
#endif

			dev = kmalloc(sizeof(struct i2c_device), GFP_KERNEL);
			if (!dev) {
				printk("i2c: can't alloc i2c_device\n");
				i2c_free_devices();
				return -ENOMEM;
			}
			memset(dev, 0, sizeof(struct i2c_device));

			dev->addr = addr;

			*last = dev;
			last = &dev->next;

			count++;
		}
	}
	if (!count) {
		printk("%s: no devices found\n", __FUNCTION__);
		return -ENODEV;
	}
	return 0;
}


static int
read_8591(unsigned char dev, unsigned char offset, unsigned char *value)
{
	unsigned char data[2];

	data[0] = 0x40 | offset;
	if (i2c_write(dev, data, 1, 0) != 1)
		return -1;
	if (i2c_read(dev, data, 2, 0) != 2)
		return -1;
	*value = data[1];
	return 0;
}

static int
write_8444(unsigned char dev, unsigned char offset, unsigned char value)
{
	unsigned char data[2];

	data[0] = offset;
	data[1] = value;
	if (i2c_write(dev, data, 2, 0) != 2)
		return -1;
	return 0;
}

#ifdef U450_SUPPORT
static int
read_8583(unsigned char dev, unsigned char offset, unsigned char *value)
{
	unsigned char data;

	data = offset;
	if (i2c_write_read(dev, &data, 1, &data, 1, 0) != 1)
		return -1;
	*value = data;
	return 0;
}

static int
write_8583(unsigned char dev, unsigned char offset, unsigned char value)
{
	unsigned char data[2];

	data[0] = offset;
	data[1] = value;
	if (i2c_write(dev, data, 2, 0) != 2)
		return -1;
	return 0;
}
#endif /* U450_SUPPORT */

struct thermistor *
find_thermistor(const char *name, struct thermistor *from)
{
	int n;

	if (!from)
		from = envctrl.thermistors;
	else
		from = from->next;

	n = strlen(name);
	while (from && strncmp(from->name, name, n))
		from = from->next;

	return from;
}

void
check_temperatures(struct environment *env)
{
	struct thermistor *t;

	for (t = env->thermistors; t; t = t->next) {
#ifdef DEBUG
		printk("Thermistor `%s' [%02x:%d]: "
		       "%d C (%d C, %d C)\n",
		       t->name, t->regs.addr, t->regs.port,
		       t->temperature(t), t->regs.warning, t->regs.shutdown);
#endif

		/*
		 * Implement slow-down or shutdown here...
		 */
	}
}

void
check_fan_speeds(struct environment *env)
{
	unsigned char speed, max;
	struct thermistor *t;
	struct fan *f;

	for (f = env->fans; f; f = f->next) {
#ifdef DEBUG
		printk("Fan `%s' [%02x:%d]:", f->name,
		       f->regs.addr, f->regs.port);
#endif
		max = 0;
		for (t = f->monitor; t; t = t->chain) {
			speed = t->fan_speed(t);
			if (speed > max)
				max = speed;
#ifdef DEBUG
			printk(" %s:%02x", t->name, speed);
#endif
		}

		f->set_speed(f, max);
#ifdef DEBUG
		printk(" -> %02x\n", f->value);
#endif
	}
}

void
envctrl_fans_blast(struct environment *env)
{
	struct fan *f;

	for (f = env->fans; f; f = f->next)
		f->set_speed(f, MAX_FAN_SPEED);
}

int
kenvd(void *data)
{
	struct environment *env = data;

	MOD_INC_USE_COUNT;
	lock_kernel();

	env->kenvd_pid = current->pid;

	exit_files(current);
	exit_mm(current);

	spin_lock_irq(&current->sigmask_lock);
	siginitsetinv(&current->blocked, sigmask(SIGKILL));
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	current->session = 1;
	current->pgrp = 1;
	strcpy(current->comm, "kenvd");

	if (env->enable)
		env->enable(env);

	while (!env->terminate) {

		check_temperatures(env);
		check_fan_speeds(env);
		if (env->keep_alive)
			env->keep_alive(env);

		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(env->interval * HZ);

		if (signal_pending(current)) {
			spin_lock_irq(&current->sigmask_lock);
			flush_signals(current);
			spin_unlock_irq(&current->sigmask_lock);
			break;
		}
	}

	if (env->disable)
		env->disable(env);

	env->kenvd_pid = 0;
	wake_up(&envctrl.kenvd_wait);

	MOD_DEC_USE_COUNT;
	return 0;
}

void
envctrl_stop(void)
{
	DECLARE_WAITQUEUE(wait, current);
	struct thermistor *t;
	struct fan *f;
	pid_t pid;

	if (envctrl.kenvd_pid) {
		pid = envctrl.kenvd_pid;

		current->state = TASK_INTERRUPTIBLE;
		add_wait_queue(&envctrl.kenvd_wait, &wait);

		envctrl.terminate = 1;
		kill_proc(pid, SIGKILL, 1);

		schedule();

		remove_wait_queue(&envctrl.kenvd_wait, &wait);
		current->state = TASK_RUNNING;
	}

	t = envctrl.thermistors;
	while (t) {
		envctrl.thermistors = t->next;
		kfree(t);
		t = envctrl.thermistors;
	}

	f = envctrl.fans;
	while (f) {
		envctrl.fans = f->next;
		kfree(f);
		f = envctrl.fans;
	}

	if (envctrl.cpu_temp_table)
		kfree(envctrl.cpu_temp_table);

	if (envctrl.cpu_fan_speeds)
		kfree(envctrl.cpu_fan_speeds);

	if (envctrl.ps_temp_table)
		kfree(envctrl.ps_temp_table);

	if (envctrl.ps_fan_speeds)
		kfree(envctrl.ps_fan_speeds);
}


static unsigned char
axi_get_temperature(struct thermistor *t)
{
	unsigned char value;

	if (read_8591(t->regs.addr, t->regs.port, &value) < 0)
		return MAX_TEMPERATURE;
	if (t->regs.port == AXI_THERM_PORT_CPU)
		return axi_cpu_temp_table[value];
	else
		return axi_mod_temp_table[value];
}

static unsigned char
axi_get_fan_speed(struct thermistor *t)
{
	unsigned char temp;

	temp = t->temperature(t);
	if (temp >= MAX_TEMPERATURE)
		return MAX_FAN_SPEED;

	return axi_fan_speeds[temp];
}

static int
axi_set_fan_speed(struct fan *f, unsigned char value)
{
	if (value != f->value) {
		if (write_8444(f->regs.addr, f->regs.port, value))
			return -1;
		f->value = value;
	}
	return 0;
}

static void
axi_toggle_i2c_int(struct environment *env)
{
	unsigned char data;

	if (i2c_read(AXI_PIO_ADDR, &data, 1, 0) != 1)
		return;

	data &= ~(0x08);
	if (i2c_write(AXI_PIO_ADDR, &data, 1, 0) != 1)
		return;
	mdelay(1);

	data |= 0x08;
	if (i2c_write(AXI_PIO_ADDR, &data, 1, 0) != 1)
		return;
	mdelay(1);
}


static int
rasctrl_setup(int node)
{
	struct thermistor *t, **tlast;
	struct fan *f, **flast;
	char tmp[32];
	int monitor;
	int shutdown;
	int warning;
	int i;

	prom_getstring(prom_root_node, "name", tmp, sizeof(tmp));
	if (strcmp(tmp, "SUNW,UltraSPARC-IIi-Engine")) {
		printk("SUNW,rasctrl will work only on Ultra AXi\n");
		return -ENODEV;
	}

	monitor = prom_getintdefault(node, "env-monitor", 0);
	if (monitor == 0)
		return -ENODEV;

	envctrl.interval = prom_getintdefault(node, "env-mon-interval", 60);
	warning = prom_getintdefault(node, "warning-temp", 55);
	shutdown = prom_getintdefault(node, "shutdown-temp", 58);

	tlast = &envctrl.thermistors;
	for (i = 0; i < 4; i++) {
		t = kmalloc(sizeof(struct thermistor), GFP_KERNEL);
		if (!t)
			goto out;
		memset(t, 0, sizeof(struct thermistor));

		t->regs.addr = AXI_THERM_ADDR;
		t->regs.port = i;
		t->regs.warning = warning;
		t->regs.shutdown = shutdown;

		switch (i) {
		case AXI_THERM_PORT_CPU:
			sprintf(t->name, "%.7s", "CPU");
			break;
		case AXI_THERM_PORT_MOD:
			sprintf(t->name, "%.7s", "MOD");
			break;
		case AXI_THERM_PORT_PCI:
			sprintf(t->name, "%.7s", "PCI");
			break;
		case AXI_THERM_PORT_DISK:
			sprintf(t->name, "%.7s", "DISK");
			break;
		}

		t->temperature = axi_get_temperature;
		t->fan_speed = axi_get_fan_speed;

		if (!i2c_find_device(t->regs.addr)) {
			printk("envctrl: `%s': i2c device %02x not found\n",
			       t->name, t->regs.addr);
			kfree(t);
			continue;
		}

		*tlast = t;
		tlast = &t->next;
	}

	flast = &envctrl.fans;
	for (i = 0; i < 2; i++) {
		f = kmalloc(sizeof(struct fan), GFP_KERNEL);
		if (!f)
			goto out;
		memset(f, 0, sizeof(struct fan));

		f->regs.addr = AXI_FAN_ADDR;
		f->regs.port = i;

		switch (i) {
		case AXI_FAN_PORT_FRONT:
			sprintf(f->name, "%.7s", "FRONT");
			t = NULL;
			while ((t = find_thermistor("CPU", t))) {
				t->chain = f->monitor;
				f->monitor = t;
			}
			break;
		case AXI_FAN_PORT_BACK:
			sprintf(f->name, "%.7s", "BACK");
			t = NULL;
			while ((t = find_thermistor("PCI", t))) {
				t->chain = f->monitor;
				f->monitor = t;
			}
			break;
		}

		if (!f->monitor) {
			kfree(f);
			continue;
		}

		if (!i2c_find_device(f->regs.addr)) {
			printk("envctrl: `%s': i2c device %02x not found\n",
			       f->name, f->regs.addr);
			kfree(f);
			continue;
		}

		*flast = f;
		flast = &f->next;

		f->check_failure = NULL;
		f->set_speed = axi_set_fan_speed;
	}

	envctrl.enable = axi_toggle_i2c_int;
	envctrl.disable = envctrl_fans_blast;

#ifdef DEBUG
	printk("Warn: %d C, Shutdown %d C, Interval %d s, Monitor %d\n",
		warning, shutdown, envctrl.interval, monitor);
#endif
	return 0;

out:
	envctrl_stop();
	return -ENODEV;
}


#ifdef U450_SUPPORT

static unsigned char
envctrl_get_temperature(struct thermistor *t)
{
	unsigned char value;

	if (read_8591(t->regs.addr, t->regs.port, &value) < 0)
		return MAX_TEMPERATURE;
	if (!strncmp(t->name, "CPU", 3))
		return envctrl.cpu_temp_table[value];
	else
		return envctrl.ps_temp_table[value];
}

static unsigned char
envctrl_get_fan_speed(struct thermistor *t)
{
	unsigned char temp;

	temp = t->temperature(t);
	if (temp >= MAX_TEMPERATURE)
		return MAX_FAN_SPEED;

	if (!strncmp(t->name, "CPU", 3))
		return envctrl.cpu_fan_speeds[temp];
	else
		return envctrl.ps_fan_speeds[temp];
}

static int
envctrl_set_fan_speed(struct fan *f, unsigned char value)
{
	if (value != f->value) {
		if (write_8444(f->regs.addr, f->regs.port, value))
			return -1;
		f->value = value;
	}

	return 0;
}

static unsigned char u450_default_thermisters[] =
{
	/* CPU0 */
	0x00, 0x00, 0x00, 0x9e, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x46,
	0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x01,
	0x00, 0x00, 0x00, 0x01, 0x43, 0x50, 0x55, 0x30, 0x00,
	/* CPU1 */
	0x00, 0x00, 0x00, 0x9c, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x46,
	0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x01,
	0x00, 0x00, 0x00, 0x01, 0x43, 0x50, 0x55, 0x31, 0x00,
	/* CPU2 */
	0x00, 0x00, 0x00, 0x98, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x46,
	0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x01,
	0x00, 0x00, 0x00, 0x01, 0x43, 0x50, 0x55, 0x32, 0x00,
	/* CPU3 */
	0x00, 0x00, 0x00, 0x96, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x46,
	0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x01,
	0x00, 0x00, 0x00, 0x01, 0x43, 0x50, 0x55, 0x33, 0x00,
	/* PS0 */
	0x00, 0x00, 0x00, 0x90, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x5a,
	0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x01,
	0x00, 0x00, 0x00, 0x01, 0x50, 0x53, 0x30, 0x00,
	/* PS1 */
	0x00, 0x00, 0x00, 0x92, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x5a,
	0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x01,
	0x00, 0x00, 0x00, 0x01, 0x50, 0x53, 0x31, 0x00,
	/* PS2 */
	0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x5a,
	0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x01,
	0x00, 0x00, 0x00, 0x01, 0x50, 0x53, 0x32, 0x00,
	/* AMB */
	0x00, 0x00, 0x00, 0x9a, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x28,
	0x00, 0x00, 0x00, 0x32, 0x00, 0x00, 0x00, 0x01,
	0x00, 0x00, 0x00, 0x01, 0x41, 0x4d, 0x42, 0x00
};

static unsigned char u450_default_cpu_temp_factors[] =
{
	0x96, 0x96, 0x96, 0x96, 0x96, 0x96, 0x96, 0x96,
	0x96, 0x96, 0x96, 0x96, 0x96, 0x96, 0x96, 0x96,
	0x96, 0x96, 0x96, 0x96, 0x96, 0x96, 0x96, 0x96,
	0x96, 0x94, 0x92, 0x90, 0x8f, 0x8e, 0x8d, 0x8c,
	0x8a, 0x88, 0x87, 0x86, 0x85, 0x84, 0x83, 0x82,
	0x81, 0x80, 0x7f, 0x7e, 0x7d, 0x7c, 0x7b, 0x7a,
	0x79, 0x79, 0x78, 0x78, 0x77, 0x76, 0x75, 0x74,
	0x73, 0x72, 0x71, 0x70, 0x70, 0x6f, 0x6f, 0x6e,
	0x6e, 0x6e, 0x6d, 0x6d, 0x6c, 0x6b, 0x6a, 0x6a,
	0x69, 0x69, 0x68, 0x67, 0x66, 0x65, 0x65, 0x64,
	0x64, 0x64, 0x63, 0x63, 0x62, 0x62, 0x61, 0x61,
	0x60, 0x60, 0x5f, 0x5f, 0x5e, 0x5e, 0x5d, 0x5d,
	0x5c, 0x5c, 0x5b, 0x5b, 0x5b, 0x5a, 0x5a, 0x5a,
	0x59, 0x59, 0x58, 0x58, 0x57, 0x57, 0x56, 0x56,
	0x55, 0x55, 0x54, 0x54, 0x53, 0x53, 0x52, 0x52,
	0x52, 0x51, 0x51, 0x50, 0x50, 0x50, 0x50, 0x4f,
	0x4f, 0x4f, 0x4e, 0x4e, 0x4e, 0x4d, 0x4d, 0x4d,
	0x4c, 0x4c, 0x4c, 0x4b, 0x4b, 0x4b, 0x4a, 0x4a,
	0x4a, 0x49, 0x49, 0x49, 0x48, 0x48, 0x48, 0x47,
	0x47, 0x47, 0x46, 0x46, 0x46, 0x46, 0x45, 0x45,
	0x45, 0x44, 0x44, 0x44, 0x44, 0x43, 0x43, 0x43,
	0x43, 0x42, 0x42, 0x42, 0x42, 0x41, 0x41, 0x41,
	0x40, 0x40, 0x40, 0x3f, 0x3f, 0x3f, 0x3e, 0x3e,
	0x3e, 0x3d, 0x3d, 0x3d, 0x3d, 0x3c, 0x3c, 0x3c,
	0x3c, 0x3b, 0x3b, 0x3b, 0x3a, 0x3a, 0x3a, 0x39,
	0x39, 0x39, 0x38, 0x38, 0x38, 0x38, 0x37, 0x37,
	0x37, 0x37, 0x36, 0x36, 0x36, 0x35, 0x35, 0x35,
	0x34, 0x34, 0x34, 0x33, 0x33, 0x33, 0x33, 0x32,
	0x32, 0x32, 0x31, 0x31, 0x31, 0x30, 0x30, 0x30,
	0x2f, 0x2f, 0x2f, 0x2e, 0x2e, 0x2e, 0x2d, 0x2d,
	0x2d, 0x2c, 0x2c, 0x2c, 0x2b, 0x2b, 0x2b, 0x2a,
	0x2a, 0x2a, 0x29, 0x29, 0x29, 0x28, 0x28, 0x28
};

static unsigned char u450_default_cpu_fan_speeds[] =
{
	0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f,
	0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f,
	0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f,
	0x1f, 0x1f, 0x1f, 0x1f, 0x20, 0x21, 0x22, 0x23,
	0x24, 0x25, 0x26, 0x27, 0x28, 0x2a, 0x2b, 0x2d,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
	0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f,
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f,
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f,
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f,
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f,
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f,
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f
};

static unsigned char u450_default_ps_temp_factors[] =
{
	0x9a, 0x96, 0x82, 0x7d, 0x78, 0x73, 0x6e, 0x6b,
	0x69, 0x67, 0x64, 0x5f, 0x5a, 0x57, 0x55, 0x53,
	0x51, 0x50, 0x4e, 0x4d, 0x4c, 0x4b, 0x49, 0x47,
	0x46, 0x45, 0x44, 0x43, 0x42, 0x41, 0x40, 0x3f,
	0x3e, 0x3d, 0x3c, 0x3c, 0x3b, 0x3a, 0x39, 0x39,
	0x38, 0x37, 0x37, 0x36, 0x35, 0x35, 0x34, 0x33,
	0x32, 0x32, 0x32, 0x31, 0x31, 0x30, 0x30, 0x2f,
	0x2f, 0x2e, 0x2e, 0x2d, 0x2d, 0x2c, 0x2c, 0x2b,
	0x2a, 0x2a, 0x29, 0x29, 0x28, 0x28, 0x27, 0x27,
	0x26, 0x26, 0x25, 0x25, 0x25, 0x25, 0x24, 0x24,
	0x23, 0x23, 0x23, 0x22, 0x22, 0x22, 0x21, 0x21,
	0x21, 0x20, 0x20, 0x20, 0x1f, 0x1f, 0x1e, 0x1e,
	0x1e, 0x1d, 0x1d, 0x1d, 0x1d, 0x1c, 0x1c, 0x1c,
	0x1b, 0x1b, 0x1b, 0x1a, 0x1a, 0x1a, 0x19, 0x19,
	0x19, 0x18, 0x18, 0x18, 0x18, 0x17, 0x17, 0x17,
	0x17, 0x16, 0x16, 0x16, 0x16, 0x15, 0x15, 0x15,
	0x14, 0x14, 0x14, 0x13, 0x13, 0x13, 0x13, 0x13,
	0x12, 0x12, 0x12, 0x12, 0x11, 0x11, 0x11, 0x11,
	0x10, 0x10, 0x10, 0x10, 0x0f, 0x0f, 0x0f, 0x0f,
	0x0f, 0x0e, 0x0e, 0x0e, 0x0e, 0x0d, 0x0d, 0x0d,
	0x0d, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0b, 0x0b,
	0x0b, 0x0b, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a,
	0x09, 0x09, 0x09, 0x09, 0x08, 0x08, 0x08, 0x08,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06,
	0x06, 0x06, 0x05, 0x05, 0x05, 0x05, 0x05, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x02, 0x02, 0x02, 0x02, 0x02, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static unsigned char u450_default_ps_fan_speeds[] =
{
	0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f,
	0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f,
	0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f,
	0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x21, 0x22, 0x23,
	0x24, 0x25, 0x26, 0x26, 0x27, 0x28, 0x29, 0x2a,
	0x2b, 0x2d, 0x2e, 0x2f, 0x30, 0x30, 0x30, 0x30,
	0x30, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36,
	0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e,
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f,
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f,
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f,
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f,
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f,
	0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f
};

static void
u450_toggle_i2c_int(struct environment *env)
{
	unsigned char tmp[80];
	unsigned char data;
	int i, n;

	write_8583(U450_TIMER_ADDR, 0, 0x84);
	write_8583(U450_TIMER_ADDR, 8, 0x0a);
	write_8583(U450_TIMER_ADDR, 7, 0x00);
	write_8583(U450_TIMER_ADDR, 0, 0x04);

	n = sprintf(tmp, "envctrl: PCF8583:");
	for (i = 0; i < 16; i++) {
		if (read_8583(U450_TIMER_ADDR, i, &data) < 0) {
			printk("envctrl: error reading PCF8583\n");
			break;
		}
		n += sprintf(tmp+n, " %02x", data);
	}
	printk("%s\n", tmp);

#if 1
	data = 0x70;
	if (i2c_write(U450_PIO_ADDR, &data, 1, 0) != 1)
		return;
	mdelay(1);

	data = 0x78;
	if (i2c_write(U450_PIO_ADDR, &data, 1, 0) != 1)
		return;
	mdelay(1);
#endif
}

static void
u450_set_egg_timer(struct environment *env)
{
	unsigned char value;

#if 0
	write_8583(U450_TIMER_ADDR, 0x00, 0x84);
	read_8583(U450_TIMER_ADDR, 0x07, &value);
	write_8583(U450_TIMER_ADDR, 0x07, 0x00);
	write_8583(U450_TIMER_ADDR, 0x00, 0x04);
#else
	read_8583(U450_TIMER_ADDR, 0x07, &value);
	printk("envctrl: TIMER [%02x:07]: %02x\n", U450_TIMER_ADDR, value);
	read_8583(U450_TIMER_ADDR, 0x00, &value);
	printk("envctrl: TIMER [%02x:00]: %02x\n", U450_TIMER_ADDR, value);
#endif
}

static int
envctrl_setup(int node)
{
	struct thermistor *t, **tlast;
	struct fan *f, **flast;
	unsigned char *tmp = NULL, *p;
	int len, n, err;
	int defaults = 0;

	len = prom_getproplen(node, "thermisters");
	if (len <= 0) {
		printk("envctrl: no property `thermisters', using defaults\n");
		defaults++;
		len = sizeof(u450_default_thermisters);
	}

	tmp = (unsigned char *)kmalloc(len, GFP_KERNEL);
	if (!tmp) {
		printk("envctrl: can't allocate property buffer\n");
		return -ENODEV;
	}

	if (defaults) {
		memcpy(tmp, u450_default_thermisters, len);
	} else {
		err = prom_getproperty(node, "thermisters", tmp, len);
		if (err < 0) {
			printk("envctrl: error reading property `thermisters'\n");
			kfree(tmp);
			return -ENODEV;
		}
	}

	p = tmp;
	err = -ENOMEM;

	tlast = &envctrl.thermistors;
	while (len > sizeof(struct therm_regs)) {
		t = kmalloc(sizeof(struct thermistor), GFP_KERNEL);
		if (!t) {
			printk("envctrl: can't allocate thermistor struct\n");
			goto out;
		}
		memset(t, 0, sizeof(struct thermistor));

		memcpy(&t->regs, p, sizeof(struct therm_regs));
		p += sizeof(struct therm_regs);
		len -= sizeof(struct therm_regs);

		n = strlen(p) + 1;
		strncpy(t->name, p, 7);
		p += n;
		len -= n;

		if (!i2c_find_device(t->regs.addr)) {
			printk("envctrl: `%s': i2c device %02x not found\n",
			       t->name, t->regs.addr);
			kfree(t);
			continue;
		}

		t->temperature = envctrl_get_temperature;
		t->fan_speed = envctrl_get_fan_speed;

		*tlast = t;
		tlast = &t->next;
	}

	flast = &envctrl.fans;
	for (n = 0; n < 2; n++) {
		f = kmalloc(sizeof(struct fan), GFP_KERNEL);
		if (!f)
			goto out;
		memset(f, 0, sizeof(struct fan));

		f->regs.addr = U450_FAN_ADDR;
		f->regs.port = n;

		switch (n) {
		case U450_FAN_PORT_CPU:
			sprintf(f->name, "%.7s", "CPU");
			t = NULL;
			while ((t = find_thermistor("CPU", t))) {
				t->chain = f->monitor;
				f->monitor = t;
			}
			break;
		case U450_FAN_PORT_PS:
			sprintf(f->name, "%.7s", "PS");
			t = NULL;
			while ((t = find_thermistor("PS", t))) {
				t->chain = f->monitor;
				f->monitor = t;
			}
			break;
		}

		if (!f->monitor) {
			kfree(f);
			continue;
		}

		if (!i2c_find_device(f->regs.addr)) {
			printk("envctrl: `%s': i2c device %02x not found\n",
			       f->name, f->regs.addr);
			kfree(f);
			continue;
		}

		*flast = f;
		flast = &f->next;

		f->check_failure = NULL;
		f->set_speed = envctrl_set_fan_speed;
	}

	envctrl.cpu_temp_table = kmalloc(256, GFP_KERNEL);
	if (!envctrl.cpu_temp_table) {
		printk("envctrl: can't allocate temperature table\n");
		goto out;
	}
	if (defaults) {
		memcpy(envctrl.cpu_temp_table,
		       u450_default_cpu_temp_factors, 256);
	} else {
		err = prom_getproperty(node, "cpu-temp-factors",
				       envctrl.cpu_temp_table, 256);
		if (err) {
			printk("envctrl: can't read `cpu-temp-factors'\n");
			goto out;
		}
	}

	envctrl.cpu_fan_speeds = kmalloc(112, GFP_KERNEL);
	if (!envctrl.cpu_fan_speeds) {
		printk("envctrl: can't allocate fan speed table\n");
		goto out;
	}
	if (defaults) {
		memcpy(envctrl.cpu_fan_speeds,
		       u450_default_cpu_fan_speeds, 112);
	} else {
		err = prom_getproperty(node, "cpu-fan-speeds",
				       envctrl.cpu_fan_speeds, 112);
		if (err) {
			printk("envctrl: can't read `cpu-fan-speeds'\n");
			goto out;
		}
	}

	envctrl.ps_temp_table = kmalloc(256, GFP_KERNEL);
	if (!envctrl.ps_temp_table) {
		printk("envctrl: can't allocate temperature table\n");
		goto out;
	}
	if (defaults) {
		memcpy(envctrl.ps_temp_table,
		       u450_default_ps_temp_factors, 256);
	} else {
		err = prom_getproperty(node, "ps-temp-factors",
				       envctrl.ps_temp_table, 256);
		if (err) {
			printk("envctrl: can't read `ps-temp-factors'\n");
			goto out;
		}
	}

	envctrl.ps_fan_speeds = kmalloc(112, GFP_KERNEL);
	if (!envctrl.ps_fan_speeds) {
		printk("envctrl: can't allocate fan speed table\n");
		goto out;
	}
	if (defaults) {
		memcpy(envctrl.ps_fan_speeds,
		       u450_default_ps_fan_speeds, 112);
	} else {
		err = prom_getproperty(node, "ps-fan-speeds",
				       envctrl.ps_fan_speeds, 112);
		if (err) {
			printk("envctrl: can't read `ps-fan-speeds'\n");
			goto out;
		}
	}

	envctrl.enable = u450_toggle_i2c_int;
	envctrl.keep_alive = u450_set_egg_timer;
	envctrl.disable = envctrl_fans_blast;
	envctrl.interval = 60;

	kfree(tmp);
	return 0;

out:
	if (tmp)
		kfree(tmp);

	envctrl_stop();
	return err;
}
#endif /* U450_SUPPORT */



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
			data = addr & 0xfe;
			if (!i2c_find_device(addr & 0xfe))
				return -ENODEV;
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
int __init envctrl_init(void)
#endif
{
#ifdef CONFIG_PCI
	struct linux_ebus *ebus;
	struct linux_ebus_device *edev = 0;
	pid_t pid;
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

	i2c_regs = edev->resource[0].start;
	writeb(CONTROL_PIN, i2c_regs + I2C_CSR);
	writeb(PCF8584_ADDRESS >> 1, i2c_regs + I2C_DATA);
	writeb(CONTROL_PIN | CONTROL_ES1, i2c_regs + I2C_CSR);
	writeb(CLK_4_43 | BUS_CLK_90, i2c_regs + I2C_DATA);
	writeb(CONTROL_PIN | CONTROL_ES0 | CONTROL_ACK, i2c_regs + I2C_CSR);
	mdelay(10);

	if (misc_register(&envctrl_dev)) {
		printk("%s: unable to get misc minor %d\n",
		       __FUNCTION__, envctrl_dev.minor);
		return -ENODEV;
	}

	err = i2c_scan_bus();
	if (err) {
		i2c_free_devices();
		misc_deregister(&envctrl_dev);
		return err;
	}

	memset(&envctrl, 0, sizeof(struct environment));

	err = -ENODEV;
	if (!strcmp(edev->prom_name, "SUNW,rasctrl"))
		err = rasctrl_setup(edev->prom_node);
#ifdef U450_SUPPORT
	else if (!strcmp(edev->prom_name, "SUNW,envctrl"))
		err = envctrl_setup(edev->prom_node);
#endif

	if (err) {
		envctrl_stop();
		i2c_free_devices();
		misc_deregister(&envctrl_dev);
		return err;
	}

	init_waitqueue_head(&envctrl.kenvd_wait);

	pid = kernel_thread(kenvd, (void *)&envctrl, CLONE_FS);
	if (pid < 0) {
		envctrl_stop();
		i2c_free_devices();
		misc_deregister(&envctrl_dev);
		return -ENODEV;
	}

	return 0;
#else
	return -ENODEV;
#endif
}


#ifdef MODULE
void cleanup_module(void)
{
	envctrl_stop();
	i2c_free_devices();
	misc_deregister(&envctrl_dev);
}
#endif
