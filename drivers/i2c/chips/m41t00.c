/*
 * drivers/i2c/chips/m41t00.c
 *
 * I2C client/driver for the ST M41T00 Real-Time Clock chip.
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2005 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
/*
 * This i2c client/driver wedges between the drivers/char/genrtc.c RTC
 * interface and the SMBus interface of the i2c subsystem.
 * It would be more efficient to use i2c msgs/i2c_transfer directly but, as
 * recommened in .../Documentation/i2c/writing-clients section
 * "Sending and receiving", using SMBus level communication is preferred.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/rtc.h>
#include <linux/bcd.h>

#include <asm/time.h>
#include <asm/rtc.h>

#define	M41T00_DRV_NAME		"m41t00"

static DECLARE_MUTEX(m41t00_mutex);

static struct i2c_driver m41t00_driver;
static struct i2c_client *save_client;

static unsigned short ignore[] = { I2C_CLIENT_END };
static unsigned short normal_addr[] = { 0x68, I2C_CLIENT_END };

static struct i2c_client_address_data addr_data = {
	.normal_i2c		= normal_addr,
	.normal_i2c_range	= ignore,
	.probe			= ignore,
	.probe_range		= ignore,
	.ignore			= ignore,
	.ignore_range		= ignore,
	.force			= ignore,
};

ulong
m41t00_get_rtc_time(void)
{
	s32	sec, min, hour, day, mon, year;
	s32	sec1, min1, hour1, day1, mon1, year1;
	ulong	limit = 10;

	sec = min = hour = day = mon = year = 0;
	sec1 = min1 = hour1 = day1 = mon1 = year1 = 0;

	down(&m41t00_mutex);
	do {
		if (((sec = i2c_smbus_read_byte_data(save_client, 0)) >= 0)
			&& ((min = i2c_smbus_read_byte_data(save_client, 1))
				>= 0)
			&& ((hour = i2c_smbus_read_byte_data(save_client, 2))
				>= 0)
			&& ((day = i2c_smbus_read_byte_data(save_client, 4))
				>= 0)
			&& ((mon = i2c_smbus_read_byte_data(save_client, 5))
				>= 0)
			&& ((year = i2c_smbus_read_byte_data(save_client, 6))
				>= 0)
			&& ((sec == sec1) && (min == min1) && (hour == hour1)
				&& (day == day1) && (mon == mon1)
				&& (year == year1)))

				break;

		sec1 = sec;
		min1 = min;
		hour1 = hour;
		day1 = day;
		mon1 = mon;
		year1 = year;
	} while (--limit > 0);
	up(&m41t00_mutex);

	if (limit == 0) {
		dev_warn(&save_client->dev,
			"m41t00: can't read rtc chip\n");
		sec = min = hour = day = mon = year = 0;
	}

	sec &= 0x7f;
	min &= 0x7f;
	hour &= 0x3f;
	day &= 0x3f;
	mon &= 0x1f;
	year &= 0xff;

	BCD_TO_BIN(sec);
	BCD_TO_BIN(min);
	BCD_TO_BIN(hour);
	BCD_TO_BIN(day);
	BCD_TO_BIN(mon);
	BCD_TO_BIN(year);

	year += 1900;
	if (year < 1970)
		year += 100;

	return mktime(year, mon, day, hour, min, sec);
}

static void
m41t00_set_tlet(ulong arg)
{
	struct rtc_time	tm;
	ulong	nowtime = *(ulong *)arg;

	to_tm(nowtime, &tm);
	tm.tm_year = (tm.tm_year - 1900) % 100;

	BIN_TO_BCD(tm.tm_sec);
	BIN_TO_BCD(tm.tm_min);
	BIN_TO_BCD(tm.tm_hour);
	BIN_TO_BCD(tm.tm_mon);
	BIN_TO_BCD(tm.tm_mday);
	BIN_TO_BCD(tm.tm_year);

	down(&m41t00_mutex);
	if ((i2c_smbus_write_byte_data(save_client, 0, tm.tm_sec & 0x7f) < 0)
		|| (i2c_smbus_write_byte_data(save_client, 1, tm.tm_min & 0x7f)
			< 0)
		|| (i2c_smbus_write_byte_data(save_client, 2, tm.tm_hour & 0x7f)
			< 0)
		|| (i2c_smbus_write_byte_data(save_client, 4, tm.tm_mday & 0x7f)
			< 0)
		|| (i2c_smbus_write_byte_data(save_client, 5, tm.tm_mon & 0x7f)
			< 0)
		|| (i2c_smbus_write_byte_data(save_client, 6, tm.tm_year & 0x7f)
			< 0))

		dev_warn(&save_client->dev,"m41t00: can't write to rtc chip\n");

	up(&m41t00_mutex);
	return;
}

ulong	new_time;

DECLARE_TASKLET_DISABLED(m41t00_tasklet, m41t00_set_tlet, (ulong)&new_time);

int
m41t00_set_rtc_time(ulong nowtime)
{
	new_time = nowtime;

	if (in_interrupt())
		tasklet_schedule(&m41t00_tasklet);
	else
		m41t00_set_tlet((ulong)&new_time);

	return 0;
}

/*
 *****************************************************************************
 *
 *	Driver Interface
 *
 *****************************************************************************
 */
static int
m41t00_probe(struct i2c_adapter *adap, int addr, int kind)
{
	struct i2c_client *client;
	int rc;

	client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;

	memset(client, 0, sizeof(struct i2c_client));
	strncpy(client->name, M41T00_DRV_NAME, I2C_NAME_SIZE);
	client->flags = I2C_DF_NOTIFY;
	client->addr = addr;
	client->adapter = adap;
	client->driver = &m41t00_driver;

	if ((rc = i2c_attach_client(client)) != 0) {
		kfree(client);
		return rc;
	}

	save_client = client;
	return 0;
}

static int
m41t00_attach(struct i2c_adapter *adap)
{
	return i2c_probe(adap, &addr_data, m41t00_probe);
}

static int
m41t00_detach(struct i2c_client *client)
{
	int	rc;

	if ((rc = i2c_detach_client(client)) == 0) {
		kfree(i2c_get_clientdata(client));
		tasklet_kill(&m41t00_tasklet);
	}
	return rc;
}

static struct i2c_driver m41t00_driver = {
	.owner		= THIS_MODULE,
	.name		= M41T00_DRV_NAME,
	.id		= I2C_DRIVERID_STM41T00,
	.flags		= I2C_DF_NOTIFY,
	.attach_adapter	= m41t00_attach,
	.detach_client	= m41t00_detach,
};

static int __init
m41t00_init(void)
{
	return i2c_add_driver(&m41t00_driver);
}

static void __exit
m41t00_exit(void)
{
	i2c_del_driver(&m41t00_driver);
	return;
}

module_init(m41t00_init);
module_exit(m41t00_exit);

MODULE_AUTHOR("Mark A. Greer <mgreer@mvista.com>");
MODULE_DESCRIPTION("ST Microelectronics M41T00 RTC I2C Client Driver");
MODULE_LICENSE("GPL");
