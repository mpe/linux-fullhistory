/*
 * lm63.c - driver for the National Semiconductor LM63 temperature sensor
 *          with integrated fan control
 * Copyright (C) 2004  Jean Delvare <khali@linux-fr.org>
 * Based on the lm90 driver.
 *
 * The LM63 is a sensor chip made by National Semiconductor. It measures
 * two temperatures (its own and one external one) and the speed of one
 * fan, those speed it can additionally control. Complete datasheet can be
 * obtained from National's website at:
 *   http://www.national.com/pf/LM/LM63.html
 *
 * The LM63 is basically an LM86 with fan speed monitoring and control
 * capabilities added. It misses some of the LM86 features though:
 *  - No low limit for local temperature.
 *  - No critical limit for local temperature.
 *  - Critical limit for remote temperature can be changed only once. We
 *    will consider that the critical limit is read-only.
 *
 * The datasheet isn't very clear about what the tachometer reading is.
 * I had a explanation from National Semiconductor though. The two lower
 * bits of the read value have to be masked out. The value is still 16 bit
 * in width.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/i2c-sensor.h>

/*
 * Addresses to scan
 * Address is fully defined internally and cannot be changed.
 */

static unsigned short normal_i2c[] = { 0x4c, I2C_CLIENT_END };
static unsigned int normal_isa[] = { I2C_CLIENT_ISA_END };

/*
 * Insmod parameters
 */

SENSORS_INSMOD_1(lm63);

/*
 * The LM63 registers
 */

#define LM63_REG_CONFIG1		0x03
#define LM63_REG_CONFIG2		0xBF
#define LM63_REG_CONFIG_FAN		0x4A

#define LM63_REG_TACH_COUNT_MSB		0x47
#define LM63_REG_TACH_COUNT_LSB		0x46
#define LM63_REG_TACH_LIMIT_MSB		0x49
#define LM63_REG_TACH_LIMIT_LSB		0x48

#define LM63_REG_PWM_VALUE		0x4C
#define LM63_REG_PWM_FREQ		0x4D

#define LM63_REG_LOCAL_TEMP		0x00
#define LM63_REG_LOCAL_HIGH		0x05

#define LM63_REG_REMOTE_TEMP_MSB	0x01
#define LM63_REG_REMOTE_TEMP_LSB	0x10
#define LM63_REG_REMOTE_OFFSET_MSB	0x11
#define LM63_REG_REMOTE_OFFSET_LSB	0x12
#define LM63_REG_REMOTE_HIGH_MSB	0x07
#define LM63_REG_REMOTE_HIGH_LSB	0x13
#define LM63_REG_REMOTE_LOW_MSB		0x08
#define LM63_REG_REMOTE_LOW_LSB		0x14
#define LM63_REG_REMOTE_TCRIT		0x19
#define LM63_REG_REMOTE_TCRIT_HYST	0x21

#define LM63_REG_ALERT_STATUS		0x02
#define LM63_REG_ALERT_MASK		0x16

#define LM63_REG_MAN_ID			0xFE
#define LM63_REG_CHIP_ID		0xFF

/*
 * Conversions and various macros
 * For tachometer counts, the LM63 uses 16-bit values.
 * For local temperature and high limit, remote critical limit and hysteresis
 * value, it uses signed 8-bit values with LSB = 1 degree Celcius.
 * For remote temperature, low and high limits, it uses signed 11-bit values
 * with LSB = 0.125 degree Celcius, left-justified in 16-bit registers.
 */

#define FAN_FROM_REG(reg)	((reg) == 0xFFFC || (reg) == 0 ? 0 : \
				 5400000 / (reg))
#define FAN_TO_REG(val)		((val) <= 82 ? 0xFFFC : \
				 (5400000 / (val)) & 0xFFFC)
#define TEMP8_FROM_REG(reg)	((reg) * 1000)
#define TEMP8_TO_REG(val)	((val) <= -128000 ? -128 : \
				 (val) >= 127000 ? 127 : \
				 (val) < 0 ? ((val) - 500) / 1000 : \
				 ((val) + 500) / 1000)
#define TEMP11_FROM_REG(reg)	((reg) / 32 * 125)
#define TEMP11_TO_REG(val)	((val) <= -128000 ? 0x8000 : \
				 (val) >= 127875 ? 0x7FE0 : \
				 (val) < 0 ? ((val) - 62) / 125 * 32 : \
				 ((val) + 62) / 125 * 32)
#define HYST_TO_REG(val)	((val) <= 0 ? 0 : \
				 (val) >= 127000 ? 127 : \
				 ((val) + 500) / 1000)

/*
 * Functions declaration
 */

static int lm63_attach_adapter(struct i2c_adapter *adapter);
static int lm63_detach_client(struct i2c_client *client);

static struct lm63_data *lm63_update_device(struct device *dev);

static int lm63_detect(struct i2c_adapter *adapter, int address, int kind);
static void lm63_init_client(struct i2c_client *client);

/*
 * Driver data (common to all clients)
 */

static struct i2c_driver lm63_driver = {
	.owner		= THIS_MODULE,
	.name		= "lm63",
	.flags		= I2C_DF_NOTIFY,
	.attach_adapter	= lm63_attach_adapter,
	.detach_client	= lm63_detach_client,
};

/*
 * Client data (each client gets its own)
 */

struct lm63_data {
	struct i2c_client client;
	struct semaphore update_lock;
	char valid; /* zero until following fields are valid */
	unsigned long last_updated; /* in jiffies */

	/* registers values */
	u8 config, config_fan;
	u16 fan1_input;
	u16 fan1_low;
	u8 pwm1_freq;
	u8 pwm1_value;
	s8 temp1_input;
	s8 temp1_high;
	s16 temp2_input;
	s16 temp2_high;
	s16 temp2_low;
	s8 temp2_crit;
	u8 temp2_crit_hyst;
	u8 alarms;
};

/*
 * Sysfs callback functions and files
 */

#define show_fan(value) \
static ssize_t show_##value(struct device *dev, char *buf) \
{ \
	struct lm63_data *data = lm63_update_device(dev); \
	return sprintf(buf, "%d\n", FAN_FROM_REG(data->value)); \
}
show_fan(fan1_input);
show_fan(fan1_low);

static ssize_t set_fan1_low(struct device *dev, const char *buf,
	size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm63_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10);
	data->fan1_low = FAN_TO_REG(val);
	i2c_smbus_write_byte_data(client, LM63_REG_TACH_LIMIT_LSB,
				  data->fan1_low & 0xFF);
	i2c_smbus_write_byte_data(client, LM63_REG_TACH_LIMIT_MSB,
				  data->fan1_low >> 8);
	return count;
}

static ssize_t show_pwm1(struct device *dev, char *buf)
{
	struct lm63_data *data = lm63_update_device(dev);
	return sprintf(buf, "%d\n", data->pwm1_value >= 2 * data->pwm1_freq ?
		       255 : (data->pwm1_value * 255 + data->pwm1_freq) /
		       (2 * data->pwm1_freq));
}

static ssize_t set_pwm1(struct device *dev, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm63_data *data = i2c_get_clientdata(client);
	unsigned long val;
	
	if (!(data->config_fan & 0x20)) /* register is read-only */
		return -EPERM;

	val = simple_strtoul(buf, NULL, 10);
	data->pwm1_value = val <= 0 ? 0 :
			   val >= 255 ? 2 * data->pwm1_freq :
			   (val * data->pwm1_freq * 2 + 127) / 255;
	i2c_smbus_write_byte_data(client, LM63_REG_PWM_VALUE, data->pwm1_value);
	return count;
}

static ssize_t show_pwm1_enable(struct device *dev, char *buf)
{
	struct lm63_data *data = lm63_update_device(dev);
	return sprintf(buf, "%d\n", data->config_fan & 0x20 ? 1 : 2);
}

#define show_temp8(value) \
static ssize_t show_##value(struct device *dev, char *buf) \
{ \
	struct lm63_data *data = lm63_update_device(dev); \
	return sprintf(buf, "%d\n", TEMP8_FROM_REG(data->value)); \
}
#define show_temp11(value) \
static ssize_t show_##value(struct device *dev, char *buf) \
{ \
	struct lm63_data *data = lm63_update_device(dev); \
	return sprintf(buf, "%d\n", TEMP11_FROM_REG(data->value)); \
}
show_temp8(temp1_input);
show_temp8(temp1_high);
show_temp11(temp2_input);
show_temp11(temp2_high);
show_temp11(temp2_low);
show_temp8(temp2_crit);

#define set_temp8(value, reg) \
static ssize_t set_##value(struct device *dev, const char *buf, \
	size_t count) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct lm63_data *data = i2c_get_clientdata(client); \
	long val = simple_strtol(buf, NULL, 10); \
	data->value = TEMP8_TO_REG(val); \
	i2c_smbus_write_byte_data(client, reg, data->value); \
	return count; \
}
#define set_temp11(value, reg_msb, reg_lsb) \
static ssize_t set_##value(struct device *dev, const char *buf, \
	size_t count) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct lm63_data *data = i2c_get_clientdata(client); \
	long val = simple_strtol(buf, NULL, 10); \
	data->value = TEMP11_TO_REG(val); \
	i2c_smbus_write_byte_data(client, reg_msb, data->value >> 8); \
	i2c_smbus_write_byte_data(client, reg_lsb, data->value & 0xff); \
	return count; \
}
set_temp8(temp1_high, LM63_REG_LOCAL_HIGH);
set_temp11(temp2_high, LM63_REG_REMOTE_HIGH_MSB, LM63_REG_REMOTE_HIGH_LSB);
set_temp11(temp2_low, LM63_REG_REMOTE_LOW_MSB, LM63_REG_REMOTE_LOW_LSB);

/* Hysteresis register holds a relative value, while we want to present
   an absolute to user-space */
static ssize_t show_temp2_crit_hyst(struct device *dev, char *buf)
{
	struct lm63_data *data = lm63_update_device(dev);
	return sprintf(buf, "%d\n", TEMP8_FROM_REG(data->temp2_crit)
		       - TEMP8_FROM_REG(data->temp2_crit_hyst));
}

/* And now the other way around, user-space provides an absolute
   hysteresis value and we have to store a relative one */
static ssize_t set_temp2_crit_hyst(struct device *dev, const char *buf,
	size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm63_data *data = i2c_get_clientdata(client);
	int hyst = TEMP8_FROM_REG(data->temp2_crit) -
		   simple_strtol(buf, NULL, 10);
	i2c_smbus_write_byte_data(client, LM63_REG_REMOTE_TCRIT_HYST,
				  HYST_TO_REG(hyst));
	return count;
}

static ssize_t show_alarms(struct device *dev, char *buf)
{
	struct lm63_data *data = lm63_update_device(dev);
	return sprintf(buf, "%u\n", data->alarms);
}

static DEVICE_ATTR(fan1_input, S_IRUGO, show_fan1_input, NULL);
static DEVICE_ATTR(fan1_min, S_IWUSR | S_IRUGO, show_fan1_low,
	set_fan1_low);

static DEVICE_ATTR(pwm1, S_IWUSR | S_IRUGO, show_pwm1, set_pwm1);
static DEVICE_ATTR(pwm1_enable, S_IRUGO, show_pwm1_enable, NULL);

static DEVICE_ATTR(temp1_input, S_IRUGO, show_temp1_input, NULL);
static DEVICE_ATTR(temp1_max, S_IWUSR | S_IRUGO, show_temp1_high,
	set_temp1_high);

static DEVICE_ATTR(temp2_input, S_IRUGO, show_temp2_input, NULL);
static DEVICE_ATTR(temp2_min, S_IWUSR | S_IRUGO, show_temp2_low,
	set_temp2_low);
static DEVICE_ATTR(temp2_max, S_IWUSR | S_IRUGO, show_temp2_high,
	set_temp2_high);
static DEVICE_ATTR(temp2_crit, S_IRUGO, show_temp2_crit, NULL);
static DEVICE_ATTR(temp2_crit_hyst, S_IWUSR | S_IRUGO, show_temp2_crit_hyst,
	set_temp2_crit_hyst);

static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);

/*
 * Real code
 */

static int lm63_attach_adapter(struct i2c_adapter *adapter)
{
	if (!(adapter->class & I2C_CLASS_HWMON))
		return 0;
	return i2c_detect(adapter, &addr_data, lm63_detect);
}

/*
 * The following function does more than just detection. If detection
 * succeeds, it also registers the new chip.
 */
static int lm63_detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *new_client;
	struct lm63_data *data;
	int err = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		goto exit;

	if (!(data = kmalloc(sizeof(struct lm63_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}
	memset(data, 0, sizeof(struct lm63_data));

	/* The common I2C client data is placed right before the
	   LM63-specific data. */
	new_client = &data->client;
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	new_client->adapter = adapter;
	new_client->driver = &lm63_driver;
	new_client->flags = 0;

	/* Default to an LM63 if forced */
	if (kind == 0)
		kind = lm63;

	if (kind < 0) { /* must identify */
		u8 man_id, chip_id, reg_config1, reg_config2;
		u8 reg_alert_status, reg_alert_mask;

		man_id = i2c_smbus_read_byte_data(new_client,
			 LM63_REG_MAN_ID);
		chip_id = i2c_smbus_read_byte_data(new_client,
			  LM63_REG_CHIP_ID);
		reg_config1 = i2c_smbus_read_byte_data(new_client,
			      LM63_REG_CONFIG1);
		reg_config2 = i2c_smbus_read_byte_data(new_client,
			      LM63_REG_CONFIG2);
		reg_alert_status = i2c_smbus_read_byte_data(new_client,
				   LM63_REG_ALERT_STATUS);
		reg_alert_mask = i2c_smbus_read_byte_data(new_client,
				 LM63_REG_ALERT_MASK);

		if (man_id == 0x01 /* National Semiconductor */
		 && chip_id == 0x41 /* LM63 */
		 && (reg_config1 & 0x18) == 0x00
		 && (reg_config2 & 0xF8) == 0x00
		 && (reg_alert_status & 0x20) == 0x00
		 && (reg_alert_mask & 0xA4) == 0xA4) {
			kind = lm63;
		} else { /* failed */
			dev_dbg(&adapter->dev, "Unsupported chip "
				"(man_id=0x%02X, chip_id=0x%02X).\n",
				man_id, chip_id);
			goto exit_free;
		}
	}

	strlcpy(new_client->name, "lm63", I2C_NAME_SIZE);
	data->valid = 0;
	init_MUTEX(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto exit_free;

	/* Initialize the LM63 chip */
	lm63_init_client(new_client);

	/* Register sysfs hooks */
	if (data->config & 0x04) { /* tachometer enabled */
		device_create_file(&new_client->dev, &dev_attr_fan1_input);
		device_create_file(&new_client->dev, &dev_attr_fan1_min);
	}
	device_create_file(&new_client->dev, &dev_attr_pwm1);
	device_create_file(&new_client->dev, &dev_attr_pwm1_enable);
	device_create_file(&new_client->dev, &dev_attr_temp1_input);
	device_create_file(&new_client->dev, &dev_attr_temp2_input);
	device_create_file(&new_client->dev, &dev_attr_temp2_min);
	device_create_file(&new_client->dev, &dev_attr_temp1_max);
	device_create_file(&new_client->dev, &dev_attr_temp2_max);
	device_create_file(&new_client->dev, &dev_attr_temp2_crit);
	device_create_file(&new_client->dev, &dev_attr_temp2_crit_hyst);
	device_create_file(&new_client->dev, &dev_attr_alarms);

	return 0;

exit_free:
	kfree(data);
exit:
	return err;
}

/* Idealy we shouldn't have to initialize anything, since the BIOS
   should have taken care of everything */
static void lm63_init_client(struct i2c_client *client)
{
	struct lm63_data *data = i2c_get_clientdata(client);

	data->config = i2c_smbus_read_byte_data(client, LM63_REG_CONFIG1);
	data->config_fan = i2c_smbus_read_byte_data(client,
						    LM63_REG_CONFIG_FAN);

	/* Start converting if needed */
	if (data->config & 0x40) { /* standby */
		dev_dbg(&client->dev, "Switching to operational mode");
		data->config &= 0xA7;
		i2c_smbus_write_byte_data(client, LM63_REG_CONFIG1,
					  data->config);
	}

	/* We may need pwm1_freq before ever updating the client data */
	data->pwm1_freq = i2c_smbus_read_byte_data(client, LM63_REG_PWM_FREQ);
	if (data->pwm1_freq == 0)
		data->pwm1_freq = 1;

	/* Show some debug info about the LM63 configuration */
	dev_dbg(&client->dev, "Alert/tach pin configured for %s\n",
		(data->config & 0x04) ? "tachometer input" :
		"alert output");
	dev_dbg(&client->dev, "PWM clock %s kHz, output frequency %u Hz\n",
		(data->config_fan & 0x08) ? "1.4" : "360",
		((data->config_fan & 0x08) ? 700 : 180000) / data->pwm1_freq);
	dev_dbg(&client->dev, "PWM output active %s, %s mode\n",
		(data->config_fan & 0x10) ? "low" : "high",
		(data->config_fan & 0x20) ? "manual" : "auto");
}

static int lm63_detach_client(struct i2c_client *client)
{
	int err;

	if ((err = i2c_detach_client(client))) {
		dev_err(&client->dev, "Client deregistration failed, "
			"client not detached\n");
		return err;
	}

	kfree(i2c_get_clientdata(client));
	return 0;
}

static struct lm63_data *lm63_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm63_data *data = i2c_get_clientdata(client);

	down(&data->update_lock);

	if ((jiffies - data->last_updated > HZ) ||
	    (jiffies < data->last_updated) ||
	    !data->valid) {
		if (data->config & 0x04) { /* tachometer enabled  */
			/* order matters for fan1_input */
			data->fan1_input = i2c_smbus_read_byte_data(client,
					   LM63_REG_TACH_COUNT_LSB) & 0xFC;
			data->fan1_input |= i2c_smbus_read_byte_data(client,
					    LM63_REG_TACH_COUNT_MSB) << 8;
			data->fan1_low = (i2c_smbus_read_byte_data(client,
					  LM63_REG_TACH_LIMIT_LSB) & 0xFC)
				       | (i2c_smbus_read_byte_data(client,
					  LM63_REG_TACH_LIMIT_MSB) << 8);
		}

		data->pwm1_freq = i2c_smbus_read_byte_data(client,
				  LM63_REG_PWM_FREQ);
		if (data->pwm1_freq == 0)
			data->pwm1_freq = 1;
		data->pwm1_value = i2c_smbus_read_byte_data(client,
				   LM63_REG_PWM_VALUE);

		data->temp1_input = i2c_smbus_read_byte_data(client,
				    LM63_REG_LOCAL_TEMP);
		data->temp1_high = i2c_smbus_read_byte_data(client,
				   LM63_REG_LOCAL_HIGH);

		/* order matters for temp2_input */
		data->temp2_input = i2c_smbus_read_byte_data(client,
				    LM63_REG_REMOTE_TEMP_MSB) << 8;
		data->temp2_input |= i2c_smbus_read_byte_data(client,
				     LM63_REG_REMOTE_TEMP_LSB);
		data->temp2_high = (i2c_smbus_read_byte_data(client,
				   LM63_REG_REMOTE_HIGH_MSB) << 8)
				 | i2c_smbus_read_byte_data(client,
				   LM63_REG_REMOTE_HIGH_LSB);
		data->temp2_low = (i2c_smbus_read_byte_data(client,
				  LM63_REG_REMOTE_LOW_MSB) << 8)
				| i2c_smbus_read_byte_data(client,
				  LM63_REG_REMOTE_LOW_LSB);
		data->temp2_crit = i2c_smbus_read_byte_data(client,
				   LM63_REG_REMOTE_TCRIT);
		data->temp2_crit_hyst = i2c_smbus_read_byte_data(client,
					LM63_REG_REMOTE_TCRIT_HYST);

		data->alarms = i2c_smbus_read_byte_data(client,
			       LM63_REG_ALERT_STATUS) & 0x7F;

		data->last_updated = jiffies;
		data->valid = 1;
	}

	up(&data->update_lock);

	return data;
}

static int __init sensors_lm63_init(void)
{
	return i2c_add_driver(&lm63_driver);
}

static void __exit sensors_lm63_exit(void)
{
	i2c_del_driver(&lm63_driver);
}

MODULE_AUTHOR("Jean Delvare <khali@linux-fr.org>");
MODULE_DESCRIPTION("LM63 driver");
MODULE_LICENSE("GPL");

module_init(sensors_lm63_init);
module_exit(sensors_lm63_exit);
