/*
    amd756.c - Part of lm_sensors, Linux kernel modules for hardware
              monitoring

    Copyright (c) 1999-2002 Merlin Hughes <merlin@merlin.org>

    Shamelessly ripped from i2c-piix4.c:

    Copyright (c) 1998, 1999  Frodo Looijaard <frodol@dds.nl> and
    Philip Edelbrock <phil@netroedge.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
    2002-04-08: Added nForce support. (Csaba Halasz)
    2002-10-03: Fixed nForce PnP I/O port. (Michael Steil)
*/

/*
   Supports AMD756, AMD766, AMD768 and nVidia nForce
   Note: we assume there can only be one device, with one SMBus interface.
*/

#include <linux/version.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/i2c.h>
#include <linux/init.h>

struct sd {
    const unsigned short vendor;
    const unsigned short device;
    const unsigned short function;
    const char* name;
    int amdsetup:1;
};

static struct sd supported[] = {
    {PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_VIPER_740B, 3, "AMD756", 1},
    {PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_VIPER_7413, 3, "AMD766", 1},
    {PCI_VENDOR_ID_AMD, 0x7443, 3, "AMD768", 1},
    {PCI_VENDOR_ID_NVIDIA, 0x01B4, 1, "nVidia nForce", 0},
    {0, 0, 0}
};

/* AMD756 SMBus address offsets */
#define SMB_ADDR_OFFSET        0xE0
#define SMB_IOSIZE             16
#define SMB_GLOBAL_STATUS      (0x0 + amd756_smba)
#define SMB_GLOBAL_ENABLE      (0x2 + amd756_smba)
#define SMB_HOST_ADDRESS       (0x4 + amd756_smba)
#define SMB_HOST_DATA          (0x6 + amd756_smba)
#define SMB_HOST_COMMAND       (0x8 + amd756_smba)
#define SMB_HOST_BLOCK_DATA    (0x9 + amd756_smba)
#define SMB_HAS_DATA           (0xA + amd756_smba)
#define SMB_HAS_DEVICE_ADDRESS (0xC + amd756_smba)
#define SMB_HAS_HOST_ADDRESS   (0xE + amd756_smba)
#define SMB_SNOOP_ADDRESS      (0xF + amd756_smba)

/* PCI Address Constants */

/* address of I/O space */
#define SMBBA     0x058		/* mh */
#define SMBBANFORCE     0x014

/* general configuration */
#define SMBGCFG   0x041		/* mh */

/* silicon revision code */
#define SMBREV    0x008

/* Other settings */
#define MAX_TIMEOUT 500

/* AMD756 constants */
#define AMD756_QUICK        0x00
#define AMD756_BYTE         0x01
#define AMD756_BYTE_DATA    0x02
#define AMD756_WORD_DATA    0x03
#define AMD756_PROCESS_CALL 0x04
#define AMD756_BLOCK_DATA   0x05

/* insmod parameters */

int __init i2c_amd756_init(void);
void __exit i2c_amd756_exit(void);
static int amd756_cleanup(void);

static int amd756_setup(void);
static s32 amd756_access(struct i2c_adapter *adap, u16 addr,
			 unsigned short flags, char read_write,
			 u8 command, int size, union i2c_smbus_data *data);
static void amd756_do_pause(unsigned int amount);
static void amd756_abort(void);
static int amd756_transaction(void);
static void amd756_inc(struct i2c_adapter *adapter);
static void amd756_dec(struct i2c_adapter *adapter);
static u32 amd756_func(struct i2c_adapter *adapter);

static struct i2c_algorithm smbus_algorithm = {
	/* name */ "Non-I2C SMBus adapter",
	/* id */ I2C_ALGO_SMBUS,
	/* master_xfer */ NULL,
	/* smbus_access */ amd756_access,
	/* slave;_send */ NULL,
	/* slave_rcv */ NULL,
	/* algo_control */ NULL,
	/* functionality */ amd756_func,
};

static struct i2c_adapter amd756_adapter = {
	"unset",
	I2C_ALGO_SMBUS | I2C_HW_SMBUS_AMD756,
	&smbus_algorithm,
	NULL,
	amd756_inc,
	amd756_dec,
	NULL,
	NULL,
};

static int __initdata amd756_initialized;
static struct sd *amd756_sd = NULL;
static unsigned short amd756_smba = 0;

int amd756_setup(void)
{
	unsigned char temp;
	struct sd *currdev;
	struct pci_dev *AMD756_dev = NULL;

	if (pci_present() == 0) {
		return -ENODEV;
	}

	/* Look for a supported chip */
	for(currdev = supported; currdev->vendor; ) {
		AMD756_dev = pci_find_device(currdev->vendor,
						currdev->device, AMD756_dev);
		if (AMD756_dev != NULL)	{
			if (PCI_FUNC(AMD756_dev->devfn) == currdev->function)
				break;
		} else {
		    currdev++;
		}
	}

	if (AMD756_dev == NULL) {
		printk
		    ("i2c-amd756.o: Error: No AMD756 or compatible device detected!\n");
		return -ENODEV;
	}
	printk(KERN_INFO "i2c-amd756.o: Found %s SMBus controller.\n", currdev->name);

	if (currdev->amdsetup)
	{
		pci_read_config_byte(AMD756_dev, SMBGCFG, &temp);
		if ((temp & 128) == 0) {
			printk("i2c-amd756.o: Error: SMBus controller I/O not enabled!\n");
			return -ENODEV;
		}

		/* Determine the address of the SMBus areas */
		/* Technically it is a dword but... */
		pci_read_config_word(AMD756_dev, SMBBA, &amd756_smba);
		amd756_smba &= 0xff00;
		amd756_smba += SMB_ADDR_OFFSET;
	} else {
		pci_read_config_word(AMD756_dev, SMBBANFORCE, &amd756_smba);
		amd756_smba &= 0xfffc;
	}
	if(amd756_smba == 0) {
		printk(KERN_ERR "i2c-amd756.o: Error: SMB base address uninitialized\n");
		return -ENODEV;
	}
	if (check_region(amd756_smba, SMB_IOSIZE)) {
		printk
		    ("i2c-amd756.o: SMB region 0x%x already in use!\n",
		     amd756_smba);
		return -ENODEV;
	}

	/* Everything is happy, let's grab the memory and set things up. */
	request_region(amd756_smba, SMB_IOSIZE, "amd756-smbus");

#ifdef DEBUG
	pci_read_config_byte(AMD756_dev, SMBREV, &temp);
	printk("i2c-amd756.o: SMBREV = 0x%X\n", temp);
	printk("i2c-amd756.o: AMD756_smba = 0x%X\n", amd756_smba);
#endif				/* DEBUG */

	/* store struct sd * for future reference */
        amd756_sd = currdev;

	return 0;
}

/* 
  SMBUS event = I/O 28-29 bit 11
     see E0 for the status bits and enabled in E2
     
*/

/* Internally used pause function */
void amd756_do_pause(unsigned int amount)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(amount);
}

#define GS_ABRT_STS (1 << 0)
#define GS_COL_STS (1 << 1)
#define GS_PRERR_STS (1 << 2)
#define GS_HST_STS (1 << 3)
#define GS_HCYC_STS (1 << 4)
#define GS_TO_STS (1 << 5)
#define GS_SMB_STS (1 << 11)

#define GS_CLEAR_STS (GS_ABRT_STS | GS_COL_STS | GS_PRERR_STS | \
  GS_HCYC_STS | GS_TO_STS )

#define GE_CYC_TYPE_MASK (7)
#define GE_HOST_STC (1 << 3)
#define GE_ABORT (1 << 5)

void amd756_abort(void)
{
	printk("i2c-amd756.o: Sending abort.\n");
	outw_p(inw(SMB_GLOBAL_ENABLE) | GE_ABORT, SMB_GLOBAL_ENABLE);
	amd756_do_pause(100);
	outw_p(GS_CLEAR_STS, SMB_GLOBAL_STATUS);
}

int amd756_transaction(void)
{
	int temp;
	int result = 0;
	int timeout = 0;

#ifdef DEBUG
	printk
	    ("i2c-amd756.o: Transaction (pre): GS=%04x, GE=%04x, ADD=%04x, DAT=%04x\n",
	     inw_p(SMB_GLOBAL_STATUS), inw_p(SMB_GLOBAL_ENABLE),
	     inw_p(SMB_HOST_ADDRESS), inb_p(SMB_HOST_DATA));
#endif

	/* Make sure the SMBus host is ready to start transmitting */
	if ((temp = inw_p(SMB_GLOBAL_STATUS)) & (GS_HST_STS | GS_SMB_STS)) {
#ifdef DEBUG
		printk
		    ("i2c-amd756.o: SMBus busy (%04x). Waiting... \n", temp);
#endif
		do {
			amd756_do_pause(1);
			temp = inw_p(SMB_GLOBAL_STATUS);
		} while ((temp & (GS_HST_STS | GS_SMB_STS)) &&
		         (timeout++ < MAX_TIMEOUT));
		/* If the SMBus is still busy, we give up */
		if (timeout >= MAX_TIMEOUT) {
			printk("i2c-amd756.o: Busy wait timeout! (%04x)\n", temp);
			amd756_abort();
			return -1;
		}
		timeout = 0;
	}

	/* start the transaction by setting the start bit */
	outw_p(inw(SMB_GLOBAL_ENABLE) | GE_HOST_STC, SMB_GLOBAL_ENABLE);

	/* We will always wait for a fraction of a second! */
	do {
		amd756_do_pause(1);
		temp = inw_p(SMB_GLOBAL_STATUS);
	} while ((temp & GS_HST_STS) && (timeout++ < MAX_TIMEOUT));

	/* If the SMBus is still busy, we give up */
	if (timeout >= MAX_TIMEOUT) {
		printk("i2c-amd756.o: Completion timeout!\n");
		amd756_abort ();
		return -1;
	}

	if (temp & GS_PRERR_STS) {
		result = -1;
#ifdef DEBUG
		printk("i2c-amd756.o: SMBus Protocol error (no response)!\n");
#endif
	}

	if (temp & GS_COL_STS) {
		result = -1;
		printk("i2c-amd756.o: SMBus collision!\n");
	}

	if (temp & GS_TO_STS) {
		result = -1;
#ifdef DEBUG
		printk("i2c-amd756.o: SMBus protocol timeout!\n");
#endif
	}
#ifdef DEBUG
	if (temp & GS_HCYC_STS) {
		printk("i2c-amd756.o: SMBus protocol success!\n");
	}
#endif

	outw_p(GS_CLEAR_STS, SMB_GLOBAL_STATUS);

#ifdef DEBUG
	if (((temp = inw_p(SMB_GLOBAL_STATUS)) & GS_CLEAR_STS) != 0x00) {
		printk
		    ("i2c-amd756.o: Failed reset at end of transaction (%04x)\n",
		     temp);
	}
	printk
	    ("i2c-amd756.o: Transaction (post): GS=%04x, GE=%04x, ADD=%04x, DAT=%04x\n",
	     inw_p(SMB_GLOBAL_STATUS), inw_p(SMB_GLOBAL_ENABLE),
	     inw_p(SMB_HOST_ADDRESS), inb_p(SMB_HOST_DATA));
#endif

	return result;
}

/* Return -1 on error. */
s32 amd756_access(struct i2c_adapter * adap, u16 addr,
		  unsigned short flags, char read_write,
		  u8 command, int size, union i2c_smbus_data * data)
{
	int i, len;

  /** TODO: Should I supporte the 10-bit transfers? */
	switch (size) {
	case I2C_SMBUS_PROC_CALL:
		printk
		    ("i2c-amd756.o: I2C_SMBUS_PROC_CALL not supported!\n");
		/* TODO: Well... It is supported, I'm just not sure what to do here... */
		return -1;
	case I2C_SMBUS_QUICK:
		outw_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMB_HOST_ADDRESS);
		size = AMD756_QUICK;
		break;
	case I2C_SMBUS_BYTE:
		outw_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMB_HOST_ADDRESS);
		/* TODO: Why only during write? */
		if (read_write == I2C_SMBUS_WRITE)
			outb_p(command, SMB_HOST_COMMAND);
		size = AMD756_BYTE;
		break;
	case I2C_SMBUS_BYTE_DATA:
		outw_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMB_HOST_ADDRESS);
		outb_p(command, SMB_HOST_COMMAND);
		if (read_write == I2C_SMBUS_WRITE)
			outw_p(data->byte, SMB_HOST_DATA);
		size = AMD756_BYTE_DATA;
		break;
	case I2C_SMBUS_WORD_DATA:
		outw_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMB_HOST_ADDRESS);
		outb_p(command, SMB_HOST_COMMAND);
		if (read_write == I2C_SMBUS_WRITE)
			outw_p(data->word, SMB_HOST_DATA);	/* TODO: endian???? */
		size = AMD756_WORD_DATA;
		break;
	case I2C_SMBUS_BLOCK_DATA:
		outw_p(((addr & 0x7f) << 1) | (read_write & 0x01),
		       SMB_HOST_ADDRESS);
		outb_p(command, SMB_HOST_COMMAND);
		if (read_write == I2C_SMBUS_WRITE) {
			len = data->block[0];
			if (len < 0)
				len = 0;
			if (len > 32)
				len = 32;
			outw_p(len, SMB_HOST_DATA);
			/* i = inw_p(SMBHSTCNT); Reset SMBBLKDAT */
			for (i = 1; i <= len; i++)
				outb_p(data->block[i],
				       SMB_HOST_BLOCK_DATA);
		}
		size = AMD756_BLOCK_DATA;
		break;
	}

	/* How about enabling interrupts... */
	outw_p(size & GE_CYC_TYPE_MASK, SMB_GLOBAL_ENABLE);

	if (amd756_transaction())	/* Error in transaction */
		return -1;

	if ((read_write == I2C_SMBUS_WRITE) || (size == AMD756_QUICK))
		return 0;


	switch (size) {
	case AMD756_BYTE:
		data->byte = inw_p(SMB_HOST_DATA);
		break;
	case AMD756_BYTE_DATA:
		data->byte = inw_p(SMB_HOST_DATA);
		break;
	case AMD756_WORD_DATA:
		data->word = inw_p(SMB_HOST_DATA);	/* TODO: endian???? */
		break;
	case AMD756_BLOCK_DATA:
		data->block[0] = inw_p(SMB_HOST_DATA) & 0x3f;
		if(data->block[0] > 32)
			data->block[0] = 32;
		/* i = inw_p(SMBHSTCNT); Reset SMBBLKDAT */
		for (i = 1; i <= data->block[0]; i++)
			data->block[i] = inb_p(SMB_HOST_BLOCK_DATA);
		break;
	}

	return 0;
}

void amd756_inc(struct i2c_adapter *adapter)
{
	MOD_INC_USE_COUNT;
}

void amd756_dec(struct i2c_adapter *adapter)
{

	MOD_DEC_USE_COUNT;
}

u32 amd756_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
	    I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA |
	    I2C_FUNC_SMBUS_BLOCK_DATA | I2C_FUNC_SMBUS_PROC_CALL;
}

int __init i2c_amd756_init(void)
{
	int res;
#ifdef DEBUG
/* PE- It might be good to make this a permanent part of the code! */
	if (amd756_initialized) {
		printk
		    ("i2c-amd756.o: Oops, amd756_init called a second time!\n");
		return -EBUSY;
	}
#endif
	amd756_initialized = 0;
	if ((res = amd756_setup())) {
		printk
		    ("i2c-amd756.o: AMD756 or compatible device not detected, module not inserted.\n");
		amd756_cleanup();
		return res;
	}
	amd756_initialized++;
	sprintf(amd756_adapter.name, "SMBus %s adapter at %04x",
		amd756_sd->name, amd756_smba);
	if ((res = i2c_add_adapter(&amd756_adapter))) {
		printk
		    ("i2c-amd756.o: Adapter registration failed, module not inserted.\n");
		amd756_cleanup();
		return res;
	}
	amd756_initialized++;
	printk("i2c-amd756.o: %s bus detected and initialized\n",
               amd756_sd->name);
	return 0;
}

void __exit i2c_amd756_exit(void)
{
	amd756_cleanup();
}

static int amd756_cleanup(void)
{
	int res;
	if (amd756_initialized >= 2) {
		if ((res = i2c_del_adapter(&amd756_adapter))) {
			printk
			    ("i2c-amd756.o: i2c_del_adapter failed, module not removed\n");
			return res;
		} else
			amd756_initialized--;
	}
	if (amd756_initialized >= 1) {
		release_region(amd756_smba, SMB_IOSIZE);
		amd756_initialized--;
	}
			return 0;
}

EXPORT_NO_SYMBOLS;

#ifdef MODULE

MODULE_AUTHOR("Merlin Hughes <merlin@merlin.org>");
MODULE_DESCRIPTION("AMD756/766/768/nVidia nForce SMBus driver");

#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

#endif				/* MODULE */

module_init(i2c_amd756_init)
module_exit(i2c_amd756_exit)
