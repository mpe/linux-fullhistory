/*
 *   procfs handler for Linux I2O subsystem
 *
 *   Copyright (c) 1999 Intel Corporation
 *   
 *   Originally written by Deepak Saxena(deepak.saxena@intel.com)
 *
 *   This program is free software. You can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation; either version
 *   2 of the License, or (at your option) any later version.
 *
 *   This is an initial test release. The code is based on the design
 *   of the ide procfs system (drivers/block/ide-proc.c). Some code
 *   taken from i2o-core module by Alan Cox.
 *
 *   DISCLAIMER: This code is still under development/test and may cause
 *   your system to behave unpredictably.  Use at your own discretion.
 *
 *   LAN entries by Juha Sievänen(Juha.Sievanen@cs.Helsinki.FI),
 *   University of Helsinki, Department of Computer Science
 *
 */

/*
 * set tabstop=3
 */

/*
 * TODO List
 *
 * - Add support for any version 2.0 spec changes once 2.0 IRTOS is
 *   is available to test with
 * - Clean up code to use official structure definitions 
 */

// FIXME!
#define FMT_U64_HEX "0x%08x%08x"
#define U64_VAL(pu64) *((u32*)(pu64)+1), *((u32*)(pu64))

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/i2o.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>
#include <asm/spinlock.h>


#include "i2o_proc.h"

#include "i2o_lan.h"

/*
 * Structure used to define /proc entries
 */
typedef struct _i2o_proc_entry_t
{
	char *name;						/* entry name */
	mode_t mode;					/* mode */
	read_proc_t *read_proc;		/* read func */
	write_proc_t *write_proc;	/* write func */
} i2o_proc_entry;

static int proc_context = 0;


static int i2o_proc_read_lct(char *, char **, off_t, int, int *, void *);
static int i2o_proc_read_hrt(char *, char **, off_t, int, int *, void *);
static int i2o_proc_read_stat(char *, char **, off_t, int, int *, void *);
static int i2o_proc_read_hw(char *, char **, off_t, int, int *, void *);
static int i2o_proc_read_dev(char *, char **, off_t, int, int *, void *);
static int i2o_proc_read_dev_name(char *, char **, off_t, int, int *, void *);
static int i2o_proc_read_ddm(char *, char **, off_t, int, int *, void *);
static int i2o_proc_read_uinfo(char *, char **, off_t, int, int *, void *);
static int print_serial_number(char *, int, u8 *, int);
static int i2o_proc_create_entries(void *, 
	i2o_proc_entry *p, struct proc_dir_entry *);
static void i2o_proc_remove_entries(i2o_proc_entry *p, 
	struct proc_dir_entry *);
static int i2o_proc_add_controller(struct i2o_controller *, 
	struct proc_dir_entry * );
static void i2o_proc_remove_controller(struct i2o_controller *, 
	struct proc_dir_entry * );
static int create_i2o_procfs(void);
static int destroy_i2o_procfs(void);
static void i2o_proc_reply(struct i2o_handler *, struct i2o_controller *,
			struct i2o_message *);

static int i2o_proc_read_lan_dev_info(char *, char **, off_t, int, int *,
				      void *);
static int i2o_proc_read_lan_mac_addr(char *, char **, off_t, int, int *,
				      void *);
static int i2o_proc_read_lan_curr_addr(char *, char **, off_t, int, int *,
				       void *);
#if 0
static int i2o_proc_read_lan_mcast_addr(char *, char **, off_t, int, int *,
					void *);
#endif
static int i2o_proc_read_lan_batch_control(char *, char **, off_t, int, int *,
					   void *);
static int i2o_proc_read_lan_operation(char *, char **, off_t, int, int *,
				       void *);
static int i2o_proc_read_lan_media_operation(char *, char **, off_t, int,
					     int *, void *);
#if 0
static int i2o_proc_read_lan_alt_addr(char *, char **, off_t, int, int *,
				      void *);
#endif
static int i2o_proc_read_lan_tx_info(char *, char **, off_t, int, int *,
				     void *);
static int i2o_proc_read_lan_rx_info(char *, char **, off_t, int, int *,
				     void *);
static int i2o_proc_read_lan_hist_stats(char *, char **, off_t, int, int *,
					void *);
static int i2o_proc_read_lan_opt_tx_hist_stats(char *, char **, off_t, int,
					       int *, void *);
static int i2o_proc_read_lan_opt_rx_hist_stats(char *, char **, off_t, int,
					       int *, void *);
static int i2o_proc_read_lan_fddi_stats(char *, char **, off_t, int, int *,
					void *);

#if 0
/* Do we really need this??? */

static loff_t i2o_proc_lseek(struct file *file, loff_t off, int whence)
{
	return 0;
}
#endif

static struct proc_dir_entry *i2o_proc_dir_root;

/*
 * Message handler
 */
static struct i2o_handler i2o_proc_handler =
{
	(void *)i2o_proc_reply,
	"I2O procfs Layer",
	0
};

/*
 * IOP specific entries...write field just in case someone 
 * ever wants one.
 */
static i2o_proc_entry generic_iop_entries[] = 
{
	{"hrt", S_IFREG|S_IRUGO, i2o_proc_read_hrt, NULL},
	{"lct", S_IFREG|S_IRUGO, i2o_proc_read_lct, NULL},
	{"stat", S_IFREG|S_IRUGO, i2o_proc_read_stat, NULL},
	{"hw", S_IFREG|S_IRUGO, i2o_proc_read_hw, NULL},
	{NULL, 0, NULL, NULL}
};

/*
 * Device specific entries
 */
static i2o_proc_entry generic_dev_entries[] = 
{
	{"dev_identity", S_IFREG|S_IRUGO, i2o_proc_read_dev, NULL},
	{"ddm_identity", S_IFREG|S_IRUGO, i2o_proc_read_ddm, NULL},
	{"user_info", S_IFREG|S_IRUGO, i2o_proc_read_uinfo, NULL},
	{NULL, 0, NULL, NULL}
};

/*
 *  Storage unit specific entries (SCSI Periph, BS) with device names
 */
static i2o_proc_entry rbs_dev_entries[] = 
{
	{"dev_name", S_IFREG|S_IRUGO, i2o_proc_read_dev_name, NULL},
	{NULL, 0, NULL, NULL}
};

#define SCSI_TABLE_SIZE	13
	static char *scsi_devices[] = 
		{
			"Direct-Access Read/Write",
			"Sequential-Access Storage",
			"Printer",
			"Processor",
			"WORM Device",
			"CD-ROM Device",
			"Scanner Device",
			"Optical Memory Device",
			"Medium Changer Device",
			"Communications Device",
			"Graphics Art Pre-Press Device",
			"Graphics Art Pre-Press Device",
			"Array Controller Device"
		};

/* private */

/*
 * LAN specific entries
 * 
 * Should groups with r/w entries have their own subdirectory?
 *
 */
static i2o_proc_entry lan_entries[] = 
{
	/* LAN param groups 0000h-0008h */
	{"lan_dev_info", S_IFREG|S_IRUGO, i2o_proc_read_lan_dev_info, NULL},
	{"lan_mac_addr", S_IFREG|S_IRUGO, i2o_proc_read_lan_mac_addr, NULL},
#if 0
	{"lan_mcast_addr", S_IFREG|S_IRUGO|S_IWUSR,
	 i2o_proc_read_lan_mcast_addr, NULL},
#endif
	{"lan_batch_ctrl", S_IFREG|S_IRUGO|S_IWUSR,
	 i2o_proc_read_lan_batch_control, NULL},
	{"lan_operation", S_IFREG|S_IRUGO, i2o_proc_read_lan_operation, NULL},
	{"lan_media_operation", S_IFREG|S_IRUGO,
	 i2o_proc_read_lan_media_operation, NULL},
#if 0
	{"lan_alt_addr", S_IFREG|S_IRUGO, i2o_proc_read_lan_alt_addr, NULL},
#endif
	{"lan_tx_info", S_IFREG|S_IRUGO, i2o_proc_read_lan_tx_info, NULL},
	{"lan_rx_info", S_IFREG|S_IRUGO, i2o_proc_read_lan_rx_info, NULL},
	{"lan_stats", S_IFREG|S_IRUGO, i2o_proc_read_lan_hist_stats, NULL},
	{"lan_opt_tx_stats", S_IFREG|S_IRUGO,
	 i2o_proc_read_lan_opt_tx_hist_stats, NULL},
	{"lan_opt_rx_stats", S_IFREG|S_IRUGO,
	 i2o_proc_read_lan_opt_rx_hist_stats, NULL},
	{"lan_fddi_stats", S_IFREG|S_IRUGO, i2o_proc_read_lan_fddi_stats, NULL},
	/* some useful r/w entries, no write yet */
	{"lan_curr_addr", S_IFREG|S_IRUGO|S_IWUSR,
	 i2o_proc_read_lan_curr_addr, NULL},
	{NULL, 0, NULL, NULL}
};

static u32 i2o_proc_token = 0;

static char* bus_strings[] = 
{ 
	"Local Bus", 
	"ISA", 
	"EISA", 
	"MCA", 
	"PCI",
	"PCMCIA", 
	"NUBUS", 
	"CARDBUS"
};

static spinlock_t i2o_proc_lock = SPIN_LOCK_UNLOCKED;

void i2o_proc_reply(struct i2o_handler *phdlr, struct i2o_controller *pctrl,
	struct i2o_message *pmsg)
{
	i2o_proc_token = I2O_POST_WAIT_OK;
}

int i2o_proc_read_hrt(char *buf, char **start, off_t offset, int len, 
	int *eof, void *data)
{
	struct i2o_controller *c = (struct i2o_controller *)data;
	pi2o_hrt hrt;
	u32 msg[6];
	u32 *workspace;
	u32 bus;
	int count;
	int i;
	int token;

	spin_lock(&i2o_proc_lock);

	len = 0;

	workspace = kmalloc(2048, GFP_KERNEL);
	hrt = (pi2o_hrt)workspace;
	if(workspace==NULL)
	{
		len += sprintf(buf, "No free memory for HRT buffer\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	memset(workspace, 0, 2048);

	msg[0]= SIX_WORD_MSG_SIZE| SGL_OFFSET_4;
	msg[1]= I2O_CMD_HRT_GET<<24 | HOST_TID<<12 | ADAPTER_TID;
	msg[2]= (u32)proc_context;
	msg[3]= 0;
	msg[4]= (0xD0000000 | 2048); 
	msg[5]= virt_to_phys(workspace); 

	token = i2o_post_wait(c, ADAPTER_TID, msg, 6*4, &i2o_proc_token,2);
	if(token == I2O_POST_WAIT_TIMEOUT)
	{
		kfree(workspace);
		len += sprintf(buf, "Timeout waiting for HRT\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	if(hrt->hrt_version)
	{
		len += sprintf(buf+len, 
					"HRT table for controller is too new a version.\n");
		return len;
	}

	count = hrt->num_entries;

	if((count * hrt->entry_len + 8) > 2048) {
		printk(KERN_WARNING "i2o_proc: HRT does not fit into buffer\n");
		len += sprintf(buf+len,
			       "HRT table too big to fit in buffer.\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}
	
	len += sprintf(buf+len, "HRT has %d entries of %d bytes each.\n",
		       count, hrt->entry_len);

	for(i = 0; i < count; i++)
	{
		len += sprintf(buf+len, "Entry %d:\n", i);
		len += sprintf(buf+len, "   Adapter ID: %0#10x\n", 
					hrt->hrt_entry[i].adapter_id);
		len += sprintf(buf+len, "   Controlled by: %0#6x\n",
					hrt->hrt_entry[i].parent_tid);
		len += sprintf(buf+len, "   Bus#%d\n", 
					hrt->hrt_entry[i].bus_num);

		if(hrt->hrt_entry[i].bus_type != 0x80)
		{
			bus = hrt->hrt_entry[i].bus_type;
			len += sprintf(buf+len, "   %s Information\n", bus_strings[bus]);

			switch(bus)
			{
				case I2O_BUS_LOCAL:
					len += sprintf(buf+len, "     IOBase: %0#6x,",
								hrt->hrt_entry[i].bus.local_bus.LbBaseIOPort);
					len += sprintf(buf+len, " MemoryBase: %0#10x\n",
								hrt->hrt_entry[i].bus.local_bus.LbBaseMemoryAddress);
					break;

				case I2O_BUS_ISA:
					len += sprintf(buf+len, "     IOBase: %0#6x,",
								hrt->hrt_entry[i].bus.isa_bus.IsaBaseIOPort);
					len += sprintf(buf+len, " MemoryBase: %0#10x,",
								hrt->hrt_entry[i].bus.isa_bus.IsaBaseMemoryAddress);
					len += sprintf(buf+len, " CSN: %0#4x,",
								hrt->hrt_entry[i].bus.isa_bus.CSN);
					break;

				case I2O_BUS_EISA:
					len += sprintf(buf+len, "     IOBase: %0#6x,",
								hrt->hrt_entry[i].bus.eisa_bus.EisaBaseIOPort);
					len += sprintf(buf+len, " MemoryBase: %0#10x,",
								hrt->hrt_entry[i].bus.eisa_bus.EisaBaseMemoryAddress);
					len += sprintf(buf+len, " Slot: %0#4x,",
								hrt->hrt_entry[i].bus.eisa_bus.EisaSlotNumber);
					break;
			 
				case I2O_BUS_MCA:
					len += sprintf(buf+len, "     IOBase: %0#6x,",
								hrt->hrt_entry[i].bus.mca_bus.McaBaseIOPort);
					len += sprintf(buf+len, " MemoryBase: %0#10x,",
								hrt->hrt_entry[i].bus.mca_bus.McaBaseMemoryAddress);
					len += sprintf(buf+len, " Slot: %0#4x,",
								hrt->hrt_entry[i].bus.mca_bus.McaSlotNumber);
					break;

				case I2O_BUS_PCI:
					len += sprintf(buf+len, "     Bus: %0#4x",
								hrt->hrt_entry[i].bus.pci_bus.PciBusNumber);
					len += sprintf(buf+len, " Dev: %0#4x",
								hrt->hrt_entry[i].bus.pci_bus.PciDeviceNumber);
					len += sprintf(buf+len, " Func: %0#4x",
								hrt->hrt_entry[i].bus.pci_bus.PciFunctionNumber);
					len += sprintf(buf+len, " Vendor: %0#6x",
								hrt->hrt_entry[i].bus.pci_bus.PciVendorID);
					len += sprintf(buf+len, " Device: %0#6x\n",
								hrt->hrt_entry[i].bus.pci_bus.PciDeviceID);
					break;

				default:
					len += sprintf(buf+len, "      Unsupported Bus Type\n");
			}
		}
		else
			len += sprintf(buf+len, "   Unknown Bus Type\n");
	}

	kfree(workspace);

	spin_unlock(&i2o_proc_lock);
	
	return len;
}

int i2o_proc_read_lct(char *buf, char **start, off_t offset, int len,
	int *eof, void *data)
{
	struct i2o_controller *c = (struct i2o_controller*)data;
	u32 msg[8];
	u32 *workspace;
	pi2o_lct lct; /* = (pi2o_lct)c->lct; */
	int entries;
	int token;
	int i;

#define BUS_TABLE_SIZE 3
	static char *bus_ports[] =
		{
			"Generic Bus",
			"SCSI Bus",
			"Fibre Channel Bus"
		};

	spin_lock(&i2o_proc_lock);

	len = 0;

	workspace = kmalloc(8192, GFP_KERNEL);
	lct = (pi2o_lct)workspace;
	if(workspace==NULL)
	{
		len += sprintf(buf, "No free memory for LCT buffer\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	memset(workspace, 0, 8192);

	msg[0] = FOUR_WORD_MSG_SIZE|SGL_OFFSET_6;
	msg[1] = I2O_CMD_LCT_NOTIFY<<24 | HOST_TID<<12 | ADAPTER_TID;
	msg[2] = (u32)proc_context;
	msg[3] = 0;
	msg[4] = 0xFFFFFFFF; /* All devices */
	msg[5] = 0x00000000; /* Report now */
	msg[6] = 0xD0000000|8192;
	msg[7] = virt_to_bus(workspace);

	token = i2o_post_wait(c, ADAPTER_TID, msg, 8*4, &i2o_proc_token,2);
	if(token == I2O_POST_WAIT_TIMEOUT)
	{
		kfree(workspace);
		len += sprintf(buf, "Timeout waiting for LCT\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	entries = (lct->table_size - 3)/9;

	len += sprintf(buf, "LCT contains %d %s\n", entries,
						entries == 1 ? "entry" : "entries");
	if(lct->boot_tid)	
		len += sprintf(buf+len, "Boot Device @ ID %d\n", lct->boot_tid);

	for(i = 0; i < entries; i++)
	{
		len += sprintf(buf+len, "Entry %d\n", i);

		len += sprintf(buf+len, "  %s", i2o_get_class_name(lct->lct_entry[i].class_id));
	
		/*
		 *	Classes which we'll print subclass info for
		 */
		switch(lct->lct_entry[i].class_id & 0xFFF)
		{
			case I2O_CLASS_RANDOM_BLOCK_STORAGE:
				switch(lct->lct_entry[i].sub_class)
				{
					case 0x00:
						len += sprintf(buf+len, ": Direct-Access Read/Write");
						break;

					case 0x04:
						len += sprintf(buf+len, ": WORM Drive");
						break;
	
					case 0x05:
						len += sprintf(buf+len, ": CD-ROM Drive");
						break;

					case 0x07:
						len += sprintf(buf+len, ": Optical Memory Device");
						break;

					default:
						len += sprintf(buf+len, ": Unknown");
						break;
				}
				break;

			case I2O_CLASS_LAN:
				switch(lct->lct_entry[i].sub_class & 0xFF)
				{
					case 0x30:
						len += sprintf(buf+len, ": Ethernet");
						break;

					case 0x40:
						len += sprintf(buf+len, ": 100base VG");
						break;

					case 0x50:
						len += sprintf(buf+len, ": IEEE 802.5/Token-Ring");
						break;

					case 0x60:
						len += sprintf(buf+len, ": ANSI X3T9.5 FDDI");
						break;
		
					case 0x70:
						len += sprintf(buf+len, ": Fibre Channel");
						break;

					default:
						len += sprintf(buf+len, ": Unknown Sub-Class");
						break;
				}
				break;

			case I2O_CLASS_SCSI_PERIPHERAL:
				if(lct->lct_entry[i].sub_class < SCSI_TABLE_SIZE)
					len += sprintf(buf+len, ": %s", 
								scsi_devices[lct->lct_entry[i].sub_class]);
				else
					len += sprintf(buf+len, ": Unknown Device Type");
				break;

			case I2O_CLASS_BUS_ADAPTER_PORT:
				if(lct->lct_entry[i].sub_class < BUS_TABLE_SIZE)
					len += sprintf(buf+len, ": %s", 
								bus_ports[lct->lct_entry[i].sub_class]);
				else
					len += sprintf(buf+len, ": Unknown Bus Type");
				break;
		}
		len += sprintf(buf+len, "\n");
		
		len += sprintf(buf+len, "  Local TID: 0x%03x\n", lct->lct_entry[i].tid);
		len += sprintf(buf+len, "  User TID: 0x%03x\n", lct->lct_entry[i].user_tid);
		len += sprintf(buf+len, "  Parent TID: 0x%03x\n", 
					lct->lct_entry[i].parent_tid);
		len += sprintf(buf+len, "  Identity Tag: 0x%x%x%x%x%x%x%x%x\n",
					lct->lct_entry[i].identity_tag[0],
					lct->lct_entry[i].identity_tag[1],
					lct->lct_entry[i].identity_tag[2],
					lct->lct_entry[i].identity_tag[3],
					lct->lct_entry[i].identity_tag[4],
					lct->lct_entry[i].identity_tag[5],
					lct->lct_entry[i].identity_tag[6],
					lct->lct_entry[i].identity_tag[7]);
		len += sprintf(buf+len, "  Change Indicator: %0#10x\n", 
				lct->lct_entry[i].change_ind);
		len += sprintf(buf+len, "  Device Flags: %0#10x\n", 
				lct->lct_entry[i].device_flags);
	}

	kfree(workspace);
	spin_unlock(&i2o_proc_lock);
	
	return len;
}

int i2o_proc_read_stat(char *buf, char **start, off_t offset, int len, 
	int *eof, void *data)
{
	struct i2o_controller *c = (struct i2o_controller*)data;
	u32 *msg;
	u32 m;
	u8 *workspace;
	u16 *work16;
	u32 *work32;
	long time;
	char prodstr[25];
	int version;
	
	spin_lock(&i2o_proc_lock);

	len = 0;

	workspace = (u8*)kmalloc(88, GFP_KERNEL);
	if(!workspace)
	{
		len += sprintf(buf, "No memory for status transfer\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	m = I2O_POST_READ32(c);
	if(m == 0xFFFFFFFF)
	{
		len += sprintf(buf, "Could not get inbound message frame from IOP!\n");	
		kfree(workspace);
		spin_unlock(&i2o_proc_lock);
		return len;
	}
	
	msg = (u32 *)(m+c->mem_offset);

	memset(workspace, 0, 88);
	work32 = (u32*)workspace;
	work16 = (u16*)workspace;

	msg[0] = NINE_WORD_MSG_SIZE|SGL_OFFSET_0;
	msg[1] = I2O_CMD_STATUS_GET<<24|HOST_TID<<12|ADAPTER_TID;
	msg[2] = msg[3] = msg[4] = msg[5] = 0;
	msg[6] = virt_to_phys(workspace);
	msg[7] = 0; /* FIXME: 64-bit */
	msg[8] = 88;

	/* 
	 * hmm...i2o_post_message should just take ptr to message, and 
	 * determine offset on it's own...less work for OSM developers
	 */
	i2o_post_message(c, m);

	time = jiffies;

	while(workspace[87] != 0xFF)
	{
		if(jiffies-time >= 2*HZ)
		{
			len += sprintf(buf, "Timeout waiting for status reply\n");
			kfree(workspace);
			spin_unlock(&i2o_proc_lock);
			return len;
		}
		schedule();
		barrier();
	}

	len += sprintf(buf+len, "Organization ID: %0#6x\n", work16[0]);

	version = workspace[9]&0xF0>>4;
	if(version == 0x02) {
		len += sprintf(buf+len, "Lowest I2O version supported: ");
		switch(workspace[2]) {
		case 0x00:
		case 0x01:
			len += sprintf(buf+len, "1.5\n");
			break;
		case 0x02:
			len += sprintf(buf+len, "2.0\n");
			break;
		}

		len += sprintf(buf+len, "Highest I2O version supported: ");
		switch(workspace[3]) {
		case 0x00:
		case 0x01:
			len += sprintf(buf+len, "1.5\n");
			break;
		case 0x02:
			len += sprintf(buf+len, "2.0\n");
			break;
		}
	}

	len += sprintf(buf+len, "IOP ID: %0#5x\n", work16[2]&0xFFF);
	len += sprintf(buf+len, "Host Unit ID: %0#6x\n", work16[3]);
	len += sprintf(buf+len, "Segment Number: %0#5x\n", work16[4]&0XFFF);

	len += sprintf(buf+len, "I2O Version: ");
	switch(version)
	{
	case 0x00:
	case 0x01:
		len += sprintf(buf+len, "1.5\n");
		break;
	case 0x02:
		len += sprintf(buf+len, "2.0\n");
		break;
	default:
		len += sprintf(buf+len, "Unknown version\n");
	}

	len += sprintf(buf+len, "IOP State: ");
	switch(workspace[10])
	{
		case 0x01:
			len += sprintf(buf+len, "Init\n");
			break;

		case 0x02:
			len += sprintf(buf+len, "Reset\n");
			break;

		case 0x04:
			len += sprintf(buf+len, "Hold\n");
			break;

		case 0x05:
			len += sprintf(buf+len, "Hold\n");
			break;

		case 0x08:
			len += sprintf(buf+len, "Operational\n");
			break;

		case 0x10:
			len += sprintf(buf+len, "FAILED\n");
			break;

		case 0x11:
			len += sprintf(buf+len, "FAULTED\n");
			break;

		default:
			len += sprintf(buf+len, "Unknown\n");
			break;
	}

	/* 0x00 is the only type supported w/spec 1.5 */
	/* Added 2.0 types */
	len += sprintf(buf+len, "Messenger Type: ");
	switch (workspace[11])
	{
	case 0x00:
		len += sprintf(buf+len, "Memory Mapped\n");
		break;
	case 0x01:
		len += sprintf(buf+len, "Memory mapped only\n");
		break;
	case 0x02:
		len += sprintf(buf+len, "Remote only\n");
		break;
	case 0x03:
		len += sprintf(buf+len, "Memory mapped and remote\n");
		break;
	default:
		len += sprintf(buf+len, "Unknown\n");
		break;
	}
	len += sprintf(buf+len, "Inbound Frame Size: %d bytes\n", work16[6]*4);
	len += sprintf(buf+len, "Max Inbound Frames: %d\n", work32[4]);
	len += sprintf(buf+len, "Current Inbound Frames: %d\n", work32[5]);
	len += sprintf(buf+len, "Max Outbound Frames: %d\n", work32[6]);

	/* Spec doesn't say if NULL terminated or not... */
	memcpy(prodstr, work32+7, 24);
	prodstr[24] = '\0';
	len += sprintf(buf+len, "Product ID: %s\n", prodstr);

	len += sprintf(buf+len, "LCT Size: %d\n", work32[13]);

	len += sprintf(buf+len, "Desired Private Memory Space: %d kB\n", 
						work32[15]>>10);
	len += sprintf(buf+len, "Allocated Private Memory Space: %d kB\n", 
						work32[16]>>10);
	len += sprintf(buf+len, "Private Memory Base Address: %0#10x\n", 
						work32[17]);
	len += sprintf(buf+len, "Desired Private I/O Space: %d kB\n", 
						work32[18]>>10);
	len += sprintf(buf+len, "Allocated Private I/O Space: %d kB\n", 
						work32[19]>>10);
	len += sprintf(buf+len, "Private I/O Base Address: %0#10x\n", 
						work32[20]);

	spin_unlock(&i2o_proc_lock);

	return len;
}

int i2o_proc_read_hw(char *buf, char **start, off_t offset, int len, 
	int *eof, void *data)
{
	struct i2o_controller *c = (struct i2o_controller*)data;
	static u32 work32[5];
	static u8 *work8 = (u8*)work32;
	static u16 *work16 = (u16*)work32;
	int token;
	u32 hwcap;

	static char *cpu_table[] =
	{
		"Intel 80960 Series",
		"AMD2900 Series",
		"Motorola 68000 Series",
		"ARM Series",
		"MIPS Series",
		"Sparc Series",
		"PowerPC Series",
		"Intel x86 Series"
	};

	spin_lock(&i2o_proc_lock);

	len = 0;

	token = i2o_query_scalar(c, ADAPTER_TID, proc_context, 
					0,		// ParamGroup 0x0000h
					-1,		// all fields
					&work32,
					sizeof(work32),
					&i2o_proc_token);

	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf, "IOP Hardware Information Table\n");

	len += sprintf(buf+len, "I2O Vendor ID: %0#6x\n", work16[0]);
	len += sprintf(buf+len, "Product ID: %0#6x\n", work16[1]);
	len += sprintf(buf+len, "RAM: %dkB\n", work32[1]>>10);
	len += sprintf(buf+len, "Non-Volatile Storage: %dkB\n", work32[2]>>10);

	hwcap = work32[3];
	len += sprintf(buf+len, "Capabilities:\n");
	if(hwcap&0x00000001)
		len += sprintf(buf+len, "   Self-booting\n");
	if(hwcap&0x00000002)
		len += sprintf(buf+len, "   Upgradable IRTOS\n");
	if(hwcap&0x00000004)
		len += sprintf(buf+len, "   Supports downloading DDMs\n");
	if(hwcap&0x00000008)
		len += sprintf(buf+len, "   Supports installing DDMs\n");
	if(hwcap&0x00000010)
		len += sprintf(buf+len, "   Battery-backed RAM\n");

	len += sprintf(buf+len, "CPU: ");
	if(work8[16] > 8)
		len += sprintf(buf+len, "Unknown\n");
	else
		len += sprintf(buf+len, "%s\n", cpu_table[work8[16]]);
	/* Anyone using ProcessorVersion? */
	
	spin_unlock(&i2o_proc_lock);

	return len;
}

int i2o_proc_read_dev(char *buf, char **start, off_t offset, int len, 
	int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u32 work32[128];		// allow for "stuff" + up to 256 byte (max) serial number
					// == (allow) 512d bytes (max)
	static u16 *work16 = (u16*)work32;
	char sz[17];
	int token;

	spin_lock(&i2o_proc_lock);
	
	len = 0;

	token = i2o_query_scalar(d->controller, d->id, proc_context, 
					0xF100,		// ParamGroup F100h (Device Identity)
					-1,		// all fields
					&work32,
					sizeof(work32),
					&i2o_proc_token);

	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf, "Device Class: %s\n", i2o_get_class_name(work16[0]));

	len += sprintf(buf+len, "Owner TID:    %0#5x\n", work16[2]);
	len += sprintf(buf+len, "Parent TID:   %0#5x\n", work16[3]);

	memcpy(sz, work32+2, 16);
	sz[16] = '\0';
	len += sprintf(buf+len, "Vendor Info:  %s\n", sz);

	memcpy(sz, work32+6, 16);
	sz[16] = '\0';
	len += sprintf(buf+len, "Product Info: %s\n", sz);

	memcpy(sz, work32+10, 16);
	sz[16] = '\0';
	len += sprintf(buf+len, "Description:  %s\n", sz);

	memcpy(sz, work32+14, 8);
	sz[8] = '\0';
	len += sprintf(buf+len, "Product Revision: %s\n", sz);

	len += sprintf(buf+len, "Serial Number: ");
	len = print_serial_number(buf, len,
			(u8*)(work32+16),
							/* allow for SNLen plus
							 * possible trailing '\0'
							 */
			sizeof(work32)-(16*sizeof(u32))-2
				);
	len +=  sprintf(buf+len, "\n");

	spin_unlock(&i2o_proc_lock);

	return len;
}


int i2o_proc_read_dev_name(char *buf, char **start, off_t offset, int len,
	int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;

	if ( d->dev_name[0] == '\0' )
		return 0;

	len = sprintf(buf, "%s\n", d->dev_name);

	return len;
}



int i2o_proc_read_ddm(char *buf, char **start, off_t offset, int len, 
	int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u32 work32[128];
	static u16 *work16 = (u16*)work32;
	int token;
	char mod[25];

	spin_lock(&i2o_proc_lock);
	
	len = 0;

	token = i2o_query_scalar(d->controller, d->id, proc_context, 
					0xF101,		// ParamGroup F101h (DDM Identity)
					-1,		// all fields
					&work32,
					sizeof(work32),
					&i2o_proc_token);

	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf, "Registering DDM TID: 0x%03x\n", work16[0]&0xFFF);

	memcpy(mod, (char*)(work16+1), 24);
	mod[24] = '\0';
	len += sprintf(buf+len, "Module Name: %s\n", mod);

	memcpy(mod, (char*)(work16+13), 8);
	mod[8] = '\0';
	len += sprintf(buf+len, "Module Rev: %s\n", mod);

	len += sprintf(buf+len, "Serial Number: ");
	len = print_serial_number(buf, len,
			(u8*)(work16+17),
							/* allow for SNLen plus
							 * possible trailing '\0'
							 */
			sizeof(work32)-(17*sizeof(u16))-2
				);
	len += sprintf(buf+len, "\n");

	spin_unlock(&i2o_proc_lock);

	return len;
}

int i2o_proc_read_uinfo(char *buf, char **start, off_t offset, int len, 
	int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u32 work32[128];
	int token;
	char sz[65];

	spin_lock(&i2o_proc_lock);
	
	len = 0;

	token = i2o_query_scalar(d->controller, d->id, proc_context, 
					0xF102,		// ParamGroup F102h (User Information)
					-1,		// all fields
					&work32,
					sizeof(work32),
					&i2o_proc_token);

	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	memcpy(sz, (char*)work32, 64);
	sz[64] = '\0';
	len += sprintf(buf, "Device Name: %s\n", sz);

	memcpy(sz, (char*)(work32+16), 64);
	sz[64] = '\0';
	len += sprintf(buf+len, "Service Name: %s\n", sz);

	memcpy(sz, (char*)(work32+32), 64);
	sz[64] = '\0';
	len += sprintf(buf+len, "Physical Name: %s\n", sz);

	memcpy(sz, (char*)(work32+48), 4);
	sz[4] = '\0';
	len += sprintf(buf+len, "Instance Number: %s\n", sz);

	spin_unlock(&i2o_proc_lock);

	return len;
}

static int print_serial_number(char *buff, int pos, u8 *serialno, int max_len)
{
	int i;

	/* 19990419 -sralston
	 *	The I2O v1.5 (and v2.0 so far) "official specification"
	 *	got serial numbers WRONG!
	 *	Apparently, and despite what Section 3.4.4 says and
	 *	Figure 3-35 shows (pg 3-39 in the pdf doc),
	 *	the convention / consensus seems to be:
	 *	  + First byte is SNFormat
	 *	  + Second byte is SNLen (but only if SNFormat==7 (?))
	 *	  + (v2.0) SCSI+BS may use IEEE Registered (64 or 128 bit) format
	 */
	switch(serialno[0])
	{
		case I2O_SNFORMAT_BINARY:		/* Binary */
			pos += sprintf(buff+pos, "0x");
			for(i = 0; i < serialno[1]; i++)
			{
				pos += sprintf(buff+pos, "%02X", serialno[2+i]);
			}
			break;
	
		case I2O_SNFORMAT_ASCII:		/* ASCII */
			if ( serialno[1] < ' ' )	/* printable or SNLen? */
			{
				/* sanity */
				max_len = (max_len < serialno[1]) ? max_len : serialno[1];
				serialno[1+max_len] = '\0';

				/* just print it */
				pos += sprintf(buff+pos, "%s", &serialno[2]);
			}
			else
			{
				/* print chars for specified length */
				for(i = 0; i < serialno[1]; i++)
				{
					pos += sprintf(buff+pos, "%c", serialno[2+i]);
				}
			}
			break;

		case I2O_SNFORMAT_UNICODE:		/* UNICODE */
			pos += sprintf(buff+pos, "UNICODE Format.  Can't Display\n");
			break;

		case I2O_SNFORMAT_LAN48_MAC:		/* LAN-48 MAC Address */
			pos += sprintf(buff+pos, 
						"LAN-48 MAC Address @ %02X:%02X:%02X:%02X:%02X:%02X",
						serialno[2], serialno[3],
						serialno[4], serialno[5],
						serialno[6], serialno[7]);

		case I2O_SNFORMAT_WAN:			/* WAN MAC Address */
			/* FIXME: Figure out what a WAN access address looks like?? */
			pos += sprintf(buff+pos, "WAN Access Address");
			break;


/* plus new in v2.0 */
		case I2O_SNFORMAT_LAN64_MAC:		/* LAN-64 MAC Address */
			/* FIXME: Figure out what a LAN-64 address really looks like?? */
			pos += sprintf(buff+pos, 
						"LAN-64 MAC Address @ [?:%02X:%02X:?] %02X:%02X:%02X:%02X:%02X:%02X",
						serialno[8], serialno[9],
						serialno[2], serialno[3],
						serialno[4], serialno[5],
						serialno[6], serialno[7]);
			break;


		case I2O_SNFORMAT_DDM:			/* I2O DDM */
			pos += sprintf(buff+pos, 
						"DDM: Tid=%03Xh, Rsvd=%04Xh, OrgId=%04Xh",
						*(u16*)&serialno[2],
						*(u16*)&serialno[4],
						*(u16*)&serialno[6]);
			break;

		case I2O_SNFORMAT_IEEE_REG64:		/* IEEE Registered (64-bit) */
		case I2O_SNFORMAT_IEEE_REG128:		/* IEEE Registered (128-bit) */
			/* FIXME: Figure if this is even close?? */
			pos += sprintf(buff+pos, 
						"IEEE NodeName(hi,lo)=(%08Xh:%08Xh), PortName(hi,lo)=(%08Xh:%08Xh)\n",
						*(u32*)&serialno[2],
						*(u32*)&serialno[6],
						*(u32*)&serialno[10],
						*(u32*)&serialno[14]);
			break;


		case I2O_SNFORMAT_UNKNOWN:		/* Unknown 0    */
		case I2O_SNFORMAT_UNKNOWN2:		/* Unknown 0xff */
		default:
			pos += sprintf(buff+pos, "Unknown Data Format");
			break;
	}

	return pos;
}

/* LAN group 0000h - Device info (scalar) */
int i2o_proc_read_lan_dev_info(char *buf, char **start, off_t offset, int len, 
			       int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u32 work32[56];
	static u8 *work8 = (u8*)work32;
	static u16 *work16 = (u16*)work32;
	static u64 *work64 = (u64*)work32;
	int token;

	spin_lock(&i2o_proc_lock);

	len = 0;

	token = i2o_query_scalar(d->controller, d->id, proc_context, 
				 0x0000, -1, &work32, 56*4, &i2o_proc_token);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf, "LAN Type ........... ");
	switch (work16[0])
	{
	case 0x0030:
		len += sprintf(buf+len, "Ethernet, ");
		break;
	case 0x0040:
		len += sprintf(buf+len, "100Base VG, ");
		break;
	case 0x0050:
		len += sprintf(buf+len, "Token Ring, ");
		break;
	case 0x0060:
		len += sprintf(buf+len, "FDDI, ");
		break;
	case 0x0070:
		len += sprintf(buf+len, "Fibre Channel, ");
		break;
	default:
		len += sprintf(buf+len, "Unknown type, ");
		break;
	}

	if (work16[1]&0x00000001)
		len += sprintf(buf+len, "emulated LAN, ");
	else
		len += sprintf(buf+len, "physical LAN port, ");

	if (work16[1]&0x00000002)
		len += sprintf(buf+len, "full duplex\n");
	else
		len += sprintf(buf+len, "simplex\n");

	len += sprintf(buf+len, "Address format:      ");
	switch(work8[4]) {
	case 0x00:
		len += sprintf(buf+len, "IEEE 48bit\n");
		break;
	case 0x01:
		len += sprintf(buf+len, "FC IEEE\n");
		break;
	default:
		len += sprintf(buf+len, "Unknown\n");
		break;
	}

	len += sprintf(buf+len, "State:               ");
	switch(work8[5])
	{
	case 0x00:
		len += sprintf(buf+len, "Unknown\n");
		break;
	case 0x01:
		len += sprintf(buf+len, "Unclaimed\n");
		break;
	case 0x02:
		len += sprintf(buf+len, "Operational\n");
		break;
	case 0x03:
		len += sprintf(buf+len, "Suspended\n");
		break;
	case 0x04:
		len += sprintf(buf+len, "Resetting\n");
		break;
	case 0x05:
		len += sprintf(buf+len, "Error\n");
		break;
	case 0x06:
		len += sprintf(buf+len, "Operational no Rx\n");
		break;
	case 0x07:
		len += sprintf(buf+len, "Suspended no Rx\n");
		break;
	default:
		len += sprintf(buf+len, "Unspecified\n");
		break;
	}

	len += sprintf(buf+len, "Error status:      ");
	if(work16[3]&0x0001)
		len += sprintf(buf+len, "Transmit Control Unit Inoperative ");
	if(work16[3]&0x0002)
		len += sprintf(buf+len, "Receive Control Unit Inoperative\n");
	if(work16[3]&0x0004)
		len += sprintf(buf+len, "Local memory Allocation Error\n");
	len += sprintf(buf+len, "\n");

	len += sprintf(buf+len, "Min Packet size:     %d\n", work32[2]);
	len += sprintf(buf+len, "Max Packet size:     %d\n", work32[3]);
	len += sprintf(buf+len, "HW Address:          "
		       "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
		       work8[16],work8[17],work8[18],work8[19],
		       work8[20],work8[21],work8[22],work8[23]);

	len += sprintf(buf+len, "Max Tx Wire Speed:   " FMT_U64_HEX " bps\n", U64_VAL(&work64[3]));
	len += sprintf(buf+len, "Max Rx Wire Speed:   " FMT_U64_HEX " bps\n", U64_VAL(&work64[4]));

	len += sprintf(buf+len, "Min SDU packet size: 0x%08x\n", work32[10]);
	len += sprintf(buf+len, "Max SDU packet size: 0x%08x\n", work32[11]);

	spin_unlock(&i2o_proc_lock);
	return len;
}

/* LAN group 0001h - MAC address table (scalar) */
int i2o_proc_read_lan_mac_addr(char *buf, char **start, off_t offset, int len, 
			       int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u32 work32[48];
	static u8 *work8 = (u8*)work32;
	int token;

	spin_lock(&i2o_proc_lock);	
	len = 0;

	token = i2o_query_scalar(d->controller, d->id, proc_context, 
				 0x0001, -1, &work32, 48*4, &i2o_proc_token);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf, "Active address: "
		       "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
		       work8[0],work8[1],work8[2],work8[3],
		       work8[4],work8[5],work8[6],work8[7]);
	len += sprintf(buf+len, "Current address: "
		       "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
		       work8[8],work8[9],work8[10],work8[11],
		       work8[12],work8[13],work8[14],work8[15]);
	len += sprintf(buf+len, "Functional address mask: "
		       "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
		       work8[16],work8[17],work8[18],work8[19],
		       work8[20],work8[21],work8[22],work8[23]);

	len += sprintf(buf+len, "Filter mask: 0x%08x\n", work32[6]);
	len += sprintf(buf+len, "HW/DDM capabilities: 0x%08x\n", work32[7]);
	len += sprintf(buf+len, "    Unicast packets %ssupported (%sabled)\n",
		       (work32[7]&0x00000001)?"":"not ",
		       (work32[6]&0x00000001)?"en":"dis");
	len += sprintf(buf+len, "    Promiscuous mode %ssupported (%sabled)\n",
		       (work32[7]&0x00000002)?"":"not",
		       (work32[6]&0x00000002)?"en":"dis");
	len += sprintf(buf+len,
		       "    Multicast promiscuous mode %ssupported (%sabled)\n",
		       (work32[7]&0x00000004)?"":"not ",
		       (work32[6]&0x00000004)?"en":"dis");
	len += sprintf(buf+len,
		       "    Broadcast Reception disabling %ssupported (%sabled)\n",
		       (work32[7]&0x00000100)?"":"not ",
		       (work32[6]&0x00000100)?"en":"dis");
	len += sprintf(buf+len,
		       "    Multicast Reception disabling %ssupported (%sabled)\n",
		       (work32[7]&0x00000200)?"":"not ",
		       (work32[6]&0x00000200)?"en":"dis");
	len += sprintf(buf+len,
		       "    Functional address disabling %ssupported (%sabled)\n",
		       (work32[7]&0x00000400)?"":"not ",
		       (work32[6]&0x00000400)?"en":"dis");
	len += sprintf(buf+len, "    MAC reporting %ssupported\n",
		       (work32[7]&0x00000800)?"":"not ");		       

	len += sprintf(buf+len, "    MAC Reporting mode: ");
	if (work32[6]&0x00000800)
		len += sprintf(buf+len, "Pass only priority MAC packets\n");
	else if (work32[6]&0x00001000)
		len += sprintf(buf+len, "Pass all MAC packets\n");
	else if (work32[6]&0x00001800)
		len += sprintf(buf+len, "Pass all MAC packets (promiscuous)\n");
	else
		len += sprintf(buf+len, "Do not pass MAC packets\n");

	len += sprintf(buf+len, "Number of multicast addesses: %d\n", work32[8]);
	len += sprintf(buf+len, "Perfect filtering for max %d multicast addesses\n",
		       work32[9]);
	len += sprintf(buf+len, "Imperfect filtering for max %d multicast addesses\n",
		       work32[10]);

	spin_unlock(&i2o_proc_lock);

	return len;
}

/* LAN group 0001h, field 1 - Current MAC (scalar) */
int i2o_proc_read_lan_curr_addr(char *buf, char **start, off_t offset, int len,
				int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u32 work32[2];
	static u8 *work8 = (u8*)work32;
	int token;

	spin_lock(&i2o_proc_lock);	
	len = 0;

	token = i2o_query_scalar(d->controller, d->id, proc_context, 
				 0x0001, 2, &work32, 8, &i2o_proc_token);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf, "Current address: "
		       "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
		       work8[0],work8[1],work8[2],work8[3],
		       work8[4],work8[5],work8[6],work8[7]);

	spin_unlock(&i2o_proc_lock);
	return len;
}


#if 0
/* LAN group 0002h - Multicast MAC address table (table) */
int i2o_proc_read_lan_mcast_addr(char *buf, char **start, off_t offset, int len, 
				 int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u8 work8[32];
	static u32 field32[8];
	static u8 *field8 = (u8 *)field32;
	int token;

	spin_lock(&i2o_proc_lock);	
	len = 0;

	token = i2o_query_table_polled(d->controller, d->id, &work8, 32,
				       0x0002, 0, field32, 8);

	switch (token) {
	case -ETIMEDOUT:
		len += sprintf(buf, "Timeout reading table.\n");
		spin_unlock(&i2o_proc_lock);
		return len;
		break;
	case -ENOMEM:
		len += sprintf(buf, "No free memory to read the table.\n");
		spin_unlock(&i2o_proc_lock);
		return len;
		break;
	case -EBADR:
		len += sprintf(buf, "Error reading field.\n");
		spin_unlock(&i2o_proc_lock);
		return len;
		break;
	default:
		break;
	}

	len += sprintf(buf, "Multicast MAC address: "
		       "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
		       field8[0],field8[1],field8[2],field8[3],
		       field8[4],field8[5],field8[6],field8[7]);

	spin_unlock(&i2o_proc_lock);
	return len;
}
#endif

/* LAN group 0003h - Batch Control (scalar) */
int i2o_proc_read_lan_batch_control(char *buf, char **start, off_t offset,
				    int len, int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u32 work32[18];
	int token;

	spin_lock(&i2o_proc_lock);	
	len = 0;

	token = i2o_query_scalar(d->controller, d->id, proc_context, 
				 0x0003, -1, &work32, 72, &i2o_proc_token);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf, "Batch mode ");
	if (work32[0]&0x00000001)
		len += sprintf(buf+len, "disabled");
	else
		len += sprintf(buf+len, "enabled");
	if (work32[0]&0x00000002)
		len += sprintf(buf+len, " (current setting)");
	if (work32[0]&0x00000004)
		len += sprintf(buf+len, ", forced");
	else
		len += sprintf(buf+len, ", toggle");
	len += sprintf(buf+len, "\n");

	if(d->i2oversion == 0x00) { /* Reserved in 1.53 and 2.0 */
		len += sprintf(buf+len, "Rising Load Delay: %d ms\n",
			       work32[1]/10);
		len += sprintf(buf+len, "Rising Load Threshold: %d ms\n",
			       work32[2]/10);
		len += sprintf(buf+len, "Falling Load Delay: %d ms\n",
			       work32[3]/10);
		len += sprintf(buf+len, "Falling Load Threshold: %d ms\n",
			       work32[4]/10);
	}

	len += sprintf(buf+len, "Max Rx Batch Count: %d\n", work32[5]);
	len += sprintf(buf+len, "Max Rx Batch Delay: %d\n", work32[6]);

	if(d->i2oversion == 0x00) {
		len += sprintf(buf+len,
			       "Transmission Completion Reporting Delay: %d ms\n",
			       work32[7]);
	} else {
		len += sprintf(buf+len, "Max Tx Batch Delay: %d\n", work32[7]);
		len += sprintf(buf+len, "Max Tx Batch Count: %d\n", work32[8]);
	}

	spin_unlock(&i2o_proc_lock);
	return len;
}

/* LAN group 0004h - LAN Operation (scalar) */
int i2o_proc_read_lan_operation(char *buf, char **start, off_t offset, int len,
				int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u32 work32[5];
	int token;

	spin_lock(&i2o_proc_lock);	
	len = 0;

	token = i2o_query_scalar(d->controller, d->id, proc_context, 
				 0x0004, -1, &work32, 20, &i2o_proc_token);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf, "Packet prepadding (32b words): %d\n", work32[0]);
	len += sprintf(buf+len, "Transmission error reporting: %s\n",
		       (work32[1]&1)?"on":"off");
	len += sprintf(buf+len, "Bad packet handling: %s\n",
		       (work32[1]&0x2)?"by host":"by DDM");		      
	len += sprintf(buf+len, "Packet orphan limit: %d\n", work32[2]);

	len += sprintf(buf+len, "Tx modes:\n");
	if (work32[3]&0x00000004)
		len += sprintf(buf+len, "    HW CRC supressed\n");
	else
		len += sprintf(buf+len, "    HW CRC\n");
	if (work32[3]&0x00000100)
		len += sprintf(buf+len, "    HW IPv4 checksumming\n");
	if (work32[3]&0x00000200)
		len += sprintf(buf+len, "    HW TCP checksumming\n");
	if (work32[3]&0x00000400)
		len += sprintf(buf+len, "    HW UDP checksumming\n");
	if (work32[3]&0x00000800)
		len += sprintf(buf+len, "    HW RSVP checksumming\n");
	if (work32[3]&0x00001000)
		len += sprintf(buf+len, "    HW ICMP checksumming\n");
	if (work32[3]&0x00002000)
		len += sprintf(buf+len, "    Loopback packet not delivered\n");

	len += sprintf(buf+len, "Rx modes:\n");
	if (work32[4]&0x00000004)
		len += sprintf(buf+len, "    FCS in payload\n");
	if (work32[4]&0x00000100)
		len += sprintf(buf+len, "    HW IPv4 checksum validation\n");
	if (work32[4]&0x00000200)
		len += sprintf(buf+len, "    HW TCP checksum validation\n");
	if (work32[4]&0x00000400)
		len += sprintf(buf+len, "    HW UDP checksum validation\n");
	if (work32[4]&0x00000800)
		len += sprintf(buf+len, "    HW RSVP checksum validation\n");
	if (work32[4]&0x00001000)
		len += sprintf(buf+len, "    HW ICMP checksum validation\n");
 
	spin_unlock(&i2o_proc_lock);
	return len;
}

/* LAN group 0005h - Media operation (scalar) */
int i2o_proc_read_lan_media_operation(char *buf, char **start, off_t offset,
				      int len, int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u32 work32[9];
	static u8 *work8 = (u8*)work32;
        static u64 *work64 = (u64*)work32;
	int token;

	spin_lock(&i2o_proc_lock);	
	len = 0;

	token = i2o_query_scalar(d->controller, d->id, proc_context, 
				 0x0005, -1, &work32, 36, &i2o_proc_token);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf, "Connector type: ");
	switch(work32[0])
	{
	case 0x00000000:
		len += sprintf(buf+len, "OTHER\n");
		break;
	case 0x00000001:
		len += sprintf(buf+len, "UNKNOWN\n");
		break;
	case 0x00000002:
		len += sprintf(buf+len, "AUI\n");
		break;
	case 0x00000003:
		len += sprintf(buf+len, "UTP\n");
		break;
	case 0x00000004:
		len += sprintf(buf+len, "BNC\n");
		break;
	case 0x00000005:
		len += sprintf(buf+len, "RJ45\n");
		break;
	case 0x00000006:
		len += sprintf(buf+len, "STP DB9\n");
		break;
	case 0x00000007:
		len += sprintf(buf+len, "FIBER MIC\n");
		break;
	case 0x00000008:
		len += sprintf(buf+len, "APPLE AUI\n");
		break;
	case 0x00000009:
		len += sprintf(buf+len, "MII\n");
		break;
	case 0x0000000A:
		len += sprintf(buf+len, "DB9\n");
		break;
	case 0x0000000B:
		len += sprintf(buf+len, "HSSDC\n");
		break;
	case 0x0000000C:
		len += sprintf(buf+len, "DUPLEX SC FIBER\n");
		break;
	case 0x0000000D:
		len += sprintf(buf+len, "DUPLEX ST FIBER\n");
		break;
	case 0x0000000E:
		len += sprintf(buf+len, "TNC/BNC\n");
		break;
	case 0xFFFFFFFF:
		len += sprintf(buf+len, "HW DEFAULT\n");
		break;
	}

	len += sprintf(buf+len, "Connection type: ");
	switch(work32[1])
	{
	case I2O_LAN_UNKNOWN:
		len += sprintf(buf+len, "UNKNOWN\n");
		break;
	case I2O_LAN_AUI:
		len += sprintf(buf+len, "AUI\n");
		break;
	case I2O_LAN_10BASE5:
		len += sprintf(buf+len, "10BASE5\n");
		break;
	case I2O_LAN_FIORL:
		len += sprintf(buf+len, "FIORL\n");
		break;
	case I2O_LAN_10BASE2:
		len += sprintf(buf+len, "10BASE2\n");
		break;
	case I2O_LAN_10BROAD36:
		len += sprintf(buf+len, "10BROAD36\n");
		break;
	case I2O_LAN_10BASE_T:
		len += sprintf(buf+len, "10BASE-T\n");
		break;
	case I2O_LAN_10BASE_FP:
		len += sprintf(buf+len, "10BASE-FP\n");
		break;
	case I2O_LAN_10BASE_FB:
		len += sprintf(buf+len, "10BASE-FB\n");
		break;
	case I2O_LAN_10BASE_FL:
		len += sprintf(buf+len, "10BASE-FL\n");
		break;
	case I2O_LAN_100BASE_TX:
		len += sprintf(buf+len, "100BASE-TX\n");
		break;
	case I2O_LAN_100BASE_FX:
		len += sprintf(buf+len, "100BASE-FX\n");
		break;
	case I2O_LAN_100BASE_T4:
		len += sprintf(buf+len, "100BASE-T4\n");
		break;
	case I2O_LAN_1000BASE_SX:
		len += sprintf(buf+len, "1000BASE-SX\n");
		break;
	case I2O_LAN_1000BASE_LX:
		len += sprintf(buf+len, "1000BASE-LX\n");
		break;
	case I2O_LAN_1000BASE_CX:
		len += sprintf(buf+len, "1000BASE-CX\n");
		break;
	case I2O_LAN_1000BASE_T:
		len += sprintf(buf+len, "1000BASE-T\n");
		break;
	case I2O_LAN_100VG_ETHERNET:
		len += sprintf(buf+len, "100VG-ETHERNET\n");
		break;
	case I2O_LAN_100VG_TR:
		len += sprintf(buf+len, "100VG-TOKEN RING\n");
		break;
	case I2O_LAN_4MBIT:
		len += sprintf(buf+len, "4MBIT TOKEN RING\n");
		break;
	case I2O_LAN_16MBIT:
		len += sprintf(buf+len, "16 Mb Token Ring\n");
		break;
	case I2O_LAN_125MBAUD:
		len += sprintf(buf+len, "125 MBAUD FDDI\n");
		break;
	case I2O_LAN_POINT_POINT:
		len += sprintf(buf+len, "Point-to-point\n");
		break;
	case I2O_LAN_ARB_LOOP:
		len += sprintf(buf+len, "Arbitrated loop\n");
		break;
	case I2O_LAN_PUBLIC_LOOP:
		len += sprintf(buf+len, "Public loop\n");
		break;
	case I2O_LAN_FABRIC:
		len += sprintf(buf+len, "Fabric\n");
		break;
	case I2O_LAN_EMULATION:
		len += sprintf(buf+len, "Emulation\n");
		break;
	case I2O_LAN_OTHER:
		len += sprintf(buf+len, "Other\n");
		break;
	case I2O_LAN_DEFAULT:
		len += sprintf(buf+len, "HW default\n");
		break;
	}

	len += sprintf(buf+len, "Current Tx Wire Speed: " FMT_U64_HEX " bps\n",
		       U64_VAL(&work64[1]));
	len += sprintf(buf+len, "Current Rx Wire Speed: " FMT_U64_HEX " bps\n",
		       U64_VAL(&work64[2]));

	len += sprintf(buf+len, "%s duplex\n", (work8[24]&1)?"Full":"Half");

	len += sprintf(buf+len, "Link status: ");
	if(work8[25] == 0x00)
		len += sprintf(buf+len, "Unknown\n");
	else if(work8[25] == 0x01)
		len += sprintf(buf+len, "Normal\n");
	else if(work8[25] == 0x02)
		len += sprintf(buf+len, "Failure\n");
	else if(work8[25] == 0x03)
		len += sprintf(buf+len, "Reset\n");
	else
		len += sprintf(buf+len, "Unspecified\n");

	if (d->i2oversion == 0x00) { /* Reserved in 1.53 and 2.0 */
		len += sprintf(buf+len, "Bad packets handled by: %s\n",
			       (work8[26] == 0xFF)?"host":"DDM");
	}
	if (d->i2oversion != 0x00) {
		len += sprintf(buf+len, "Duplex mode target: ");
		switch (work8[27]) {
		case 0:
			len += sprintf(buf+len, "Half Duplex\n");
			break;
		case 1:
			len += sprintf(buf+len, "Full Duplex\n");
			break;
		default:
			len += sprintf(buf+len, "\n");
			break;
		}

		len += sprintf(buf+len, "Connector type target: ");
		switch(work32[7])
		{
		case 0x00000000:
			len += sprintf(buf+len, "OTHER\n");
			break;
		case 0x00000001:
			len += sprintf(buf+len, "UNKNOWN\n");
			break;
		case 0x00000002:
			len += sprintf(buf+len, "AUI\n");
			break;
		case 0x00000003:
			len += sprintf(buf+len, "UTP\n");
			break;
		case 0x00000004:
			len += sprintf(buf+len, "BNC\n");
			break;
		case 0x00000005:
			len += sprintf(buf+len, "RJ45\n");
			break;
		case 0x00000006:
			len += sprintf(buf+len, "STP DB9\n");
			break;
		case 0x00000007:
			len += sprintf(buf+len, "FIBER MIC\n");
			break;
		case 0x00000008:
			len += sprintf(buf+len, "APPLE AUI\n");
			break;
		case 0x00000009:
			len += sprintf(buf+len, "MII\n");
			break;
		case 0x0000000A:
			len += sprintf(buf+len, "DB9\n");
			break;
		case 0x0000000B:
			len += sprintf(buf+len, "HSSDC\n");
			break;
		case 0x0000000C:
			len += sprintf(buf+len, "DUPLEX SC FIBER\n");
			break;
		case 0x0000000D:
			len += sprintf(buf+len, "DUPLEX ST FIBER\n");
			break;
		case 0x0000000E:
			len += sprintf(buf+len, "TNC/BNC\n");
			break;
		case 0xFFFFFFFF:
			len += sprintf(buf+len, "HW DEFAULT\n");
			break;
		default:
			len += sprintf(buf+len, "\n");
			break;
		}

		len += sprintf(buf+len, "Connection type target: ");
		switch(work32[8])
		{
		case I2O_LAN_UNKNOWN:
			len += sprintf(buf+len, "UNKNOWN\n");
			break;
		case I2O_LAN_AUI:
			len += sprintf(buf+len, "AUI\n");
			break;
		case I2O_LAN_10BASE5:
			len += sprintf(buf+len, "10BASE5\n");
			break;
		case I2O_LAN_FIORL:
			len += sprintf(buf+len, "FIORL\n");
			break;
		case I2O_LAN_10BASE2:
			len += sprintf(buf+len, "10BASE2\n");
			break;
		case I2O_LAN_10BROAD36:
			len += sprintf(buf+len, "10BROAD36\n");
			break;
		case I2O_LAN_10BASE_T:
			len += sprintf(buf+len, "10BASE-T\n");
			break;
		case I2O_LAN_10BASE_FP:
			len += sprintf(buf+len, "10BASE-FP\n");
			break;
		case I2O_LAN_10BASE_FB:
			len += sprintf(buf+len, "10BASE-FB\n");
			break;
		case I2O_LAN_10BASE_FL:
			len += sprintf(buf+len, "10BASE-FL\n");
			break;
		case I2O_LAN_100BASE_TX:
			len += sprintf(buf+len, "100BASE-TX\n");
			break;
		case I2O_LAN_100BASE_FX:
			len += sprintf(buf+len, "100BASE-FX\n");
			break;
		case I2O_LAN_100BASE_T4:
			len += sprintf(buf+len, "100BASE-T4\n");
			break;
		case I2O_LAN_1000BASE_SX:
			len += sprintf(buf+len, "1000BASE-SX\n");
			break;
		case I2O_LAN_1000BASE_LX:
			len += sprintf(buf+len, "1000BASE-LX\n");
			break;
		case I2O_LAN_1000BASE_CX:
			len += sprintf(buf+len, "1000BASE-CX\n");
			break;
		case I2O_LAN_1000BASE_T:
			len += sprintf(buf+len, "1000BASE-T\n");
			break;
		case I2O_LAN_100VG_ETHERNET:
			len += sprintf(buf+len, "100VG-ETHERNET\n");
			break;
		case I2O_LAN_100VG_TR:
			len += sprintf(buf+len, "100VG-TOKEN RING\n");
			break;
		case I2O_LAN_4MBIT:
			len += sprintf(buf+len, "4MBIT TOKEN RING\n");
			break;
		case I2O_LAN_16MBIT:
			len += sprintf(buf+len, "16 Mb Token Ring\n");
			break;
		case I2O_LAN_125MBAUD:
			len += sprintf(buf+len, "125 MBAUD FDDI\n");
			break;
		case I2O_LAN_POINT_POINT:
			len += sprintf(buf+len, "Point-to-point\n");
			break;
		case I2O_LAN_ARB_LOOP:
			len += sprintf(buf+len, "Arbitrated loop\n");
			break;
		case I2O_LAN_PUBLIC_LOOP:
			len += sprintf(buf+len, "Public loop\n");
			break;
		case I2O_LAN_FABRIC:
			len += sprintf(buf+len, "Fabric\n");
			break;
		case I2O_LAN_EMULATION:
			len += sprintf(buf+len, "Emulation\n");
			break;
		case I2O_LAN_OTHER:
			len += sprintf(buf+len, "Other\n");
			break;
		case I2O_LAN_DEFAULT:
			len += sprintf(buf+len, "HW default\n");
			break;
		default:
			len += sprintf(buf+len, "\n");
			break;
		}
	}
	spin_unlock(&i2o_proc_lock);
	return len;
}

#if 0
/* LAN group 0006h - Alternate address (table) */
int i2o_proc_read_lan_alt_addr(char *buf, char **start, off_t offset, int len,
			       int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u8 work8[32];
	static u32 field32[2];
	static u8 *field8 = (u8 *)field32;
	int token;

	spin_lock(&i2o_proc_lock);	
	len = 0;

	token = i2o_query_table_polled(d->controller, d->id, &work8, 32,
				       0x0006, 0, field32, 8);
	switch (token) {
	case -ETIMEDOUT:
		len += sprintf(buf, "Timeout reading table.\n");
		spin_unlock(&i2o_proc_lock);
		return len;
		break;
	case -ENOMEM:
		len += sprintf(buf, "No free memory to read the table.\n");
		spin_unlock(&i2o_proc_lock);
		return len;
		break;
	case -EBADR:
		len += sprintf(buf, "Error reading field.\n");
		spin_unlock(&i2o_proc_lock);
		return len;
		break;
	default:
		break;
	}

	len += sprintf(buf, "Alternate Address: "
		       "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
                       field8[0],field8[1],field8[2],field8[3],
                       field8[4],field8[5],field8[6],field8[7]);

	spin_unlock(&i2o_proc_lock);
	return len;
}
#endif

/* LAN group 0007h - Transmit info (scalar) */
int i2o_proc_read_lan_tx_info(char *buf, char **start, off_t offset, int len, 
				    int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u32 work32[10];
	int token;

	spin_lock(&i2o_proc_lock);	
	len = 0;

	token = i2o_query_scalar(d->controller, d->id, proc_context, 
				 0x0007, -1, &work32, 8, &i2o_proc_token);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf, "Max SG Elements per packet: %d\n", work32[0]);
	len += sprintf(buf+len, "Max SG Elements per chain: %d\n", work32[1]);
	len += sprintf(buf+len, "Max outstanding packets: %d\n", work32[2]);
	len += sprintf(buf+len, "Max packets per request: %d\n", work32[3]);

	len += sprintf(buf+len, "Tx modes:\n");
	if(work32[4]&0x00000002)
		len += sprintf(buf+len, "    No DA in SGL\n");
	if(work32[4]&0x00000004)
		len += sprintf(buf+len, "    CRC suppression\n");
	if(work32[4]&0x00000008)
		len += sprintf(buf+len, "    Loop suppression\n");
	if(work32[4]&0x00000010)
		len += sprintf(buf+len, "    MAC insertion\n");
	if(work32[4]&0x00000020)
		len += sprintf(buf+len, "    RIF insertion\n");
	if(work32[4]&0x00000100)
		len += sprintf(buf+len, "    IPv4 Checksum\n");
	if(work32[4]&0x00000200)
		len += sprintf(buf+len, "    TCP Checksum\n");
	if(work32[4]&0x00000400)
		len += sprintf(buf+len, "    UDP Checksum\n");
	if(work32[4]&0x00000800)
		len += sprintf(buf+len, "    RSVP Checksum\n");
	if(work32[4]&0x00001000)
		len += sprintf(buf+len, "    ICMP Checksum\n");
	if (d->i2oversion == 0x00) {
		if(work32[4]&0x00008000)
			len += sprintf(buf+len, "    Loopback Enabled\n");
		if(work32[4]&0x00010000)
			len += sprintf(buf+len, "    Loopback Suppression Enabled\n");
	} else {
		if(work32[4]&0x00010000)
			len += sprintf(buf+len, "    Loopback Enabled\n");
		if(work32[4]&0x00020000)
			len += sprintf(buf+len, "    Loopback Suppression Enabled\n");
	}

	spin_unlock(&i2o_proc_lock);
	return len;
}

/* LAN group 0008h - Receive info (scalar) */
int i2o_proc_read_lan_rx_info(char *buf, char **start, off_t offset, int len, 
				    int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u32 work32[10];
	int token;

	spin_lock(&i2o_proc_lock);	
	len = 0;

	token = i2o_query_scalar(d->controller, d->id, proc_context, 
				 0x0008, -1, &work32, 8, &i2o_proc_token);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf, "Max size of chain element: %d\n", work32[0]);
	len += sprintf(buf+len, "Max number of buckets: %d\n", work32[1]);

	if (d->i2oversion > 0x00) { /* not in 1.5 */
		len += sprintf(buf+len, "Rx modes: %d\n", work32[2]);
		len += sprintf(buf+len, "RxMaxBucketsReply: %d\n", work32[3]);
		len += sprintf(buf+len, "RxMaxPacketsPerBuckets: %d\n", work32[4]);
		len += sprintf(buf+len, "RxMaxPostBuckets: %d\n", work32[5]);
	}

	spin_unlock(&i2o_proc_lock);
	return len;
}


/* LAN group 0100h - LAN Historical statistics (scalar) */
int i2o_proc_read_lan_hist_stats(char *buf, char **start, off_t offset, int len,
				 int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u64 work64[9];
	int token;

	spin_lock(&i2o_proc_lock);	
	len = 0;

	token = i2o_query_scalar(d->controller, d->id, proc_context, 
				 0x0100, -1, &work64, 9*8, &i2o_proc_token);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf, "Tx packets: " FMT_U64_HEX "\n", U64_VAL(&work64[0]));
	len += sprintf(buf+len, "Tx bytes: " FMT_U64_HEX "\n", U64_VAL(&work64[1]));
	len += sprintf(buf+len, "Rx packets: " FMT_U64_HEX "\n", U64_VAL(&work64[2]));
	len += sprintf(buf+len, "Rx bytes: " FMT_U64_HEX "\n", U64_VAL(&work64[3]));
	len += sprintf(buf+len, "Tx errors: " FMT_U64_HEX "\n", U64_VAL(&work64[4]));
	len += sprintf(buf+len, "Rx errors: " FMT_U64_HEX "\n", U64_VAL(&work64[5]));
	len += sprintf(buf+len, "Rx dropped: " FMT_U64_HEX "\n", U64_VAL(&work64[6]));
	len += sprintf(buf+len, "Adapter resets: " FMT_U64_HEX "\n", U64_VAL(&work64[7]));
	len += sprintf(buf+len, "Adapter suspends: " FMT_U64_HEX "\n", U64_VAL(&work64[8]));

	spin_unlock(&i2o_proc_lock);
	return len;
}


/* LAN group 0182h - Optional Non Media Specific Transmit Historical Statistics
 * (scalar) */
int i2o_proc_read_lan_opt_tx_hist_stats(char *buf, char **start, off_t offset,
					int len, int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u64 work64[9];
	int token;

	spin_lock(&i2o_proc_lock);	

	len = 0;

	token = i2o_query_scalar(d->controller, d->id, proc_context, 
				 0x0182, -1, &work64, 9*8, &i2o_proc_token);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf, "TxRetryCount: " FMT_U64_HEX "\n", U64_VAL(&work64[0]));
	len += sprintf(buf+len, "DirectedBytesTx: " FMT_U64_HEX "\n", U64_VAL(&work64[1]));
	len += sprintf(buf+len, "DirectedPacketsTx: " FMT_U64_HEX "\n", U64_VAL(&work64[2]));
	len += sprintf(buf+len, "MulticastBytesTx: " FMT_U64_HEX "\n", U64_VAL(&work64[3]));
	len += sprintf(buf+len, "MulticastPacketsTx: " FMT_U64_HEX "\n", U64_VAL(&work64[4]));
	len += sprintf(buf+len, "BroadcastBytesTx: " FMT_U64_HEX "\n", U64_VAL(&work64[5]));
	len += sprintf(buf+len, "BroadcastPacketsTx: " FMT_U64_HEX "\n", U64_VAL(&work64[6]));
	len += sprintf(buf+len, "TotalGroupAddrTxCount: " FMT_U64_HEX "\n", U64_VAL(&work64[7]));
	len += sprintf(buf+len, "TotalTxPacketsTooShort: " FMT_U64_HEX "\n", U64_VAL(&work64[8]));

	spin_unlock(&i2o_proc_lock);
	return len;
}

/* LAN group 0183h - Optional Non Media Specific Receive Historical Statistics
 * (scalar) */
int i2o_proc_read_lan_opt_rx_hist_stats(char *buf, char **start, off_t offset,
					int len, int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u64 work64[11];
	int token;

	spin_lock(&i2o_proc_lock);	

	len = 0;

	token = i2o_query_scalar(d->controller, d->id, proc_context, 
				 0x0183, -1, &work64, 11*8, &i2o_proc_token);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf, "ReceiveCRCErrorCount: " FMT_U64_HEX "\n", U64_VAL(&work64[0]));
	len += sprintf(buf+len, "DirectedBytesRx: " FMT_U64_HEX "\n", U64_VAL(&work64[1]));
	len += sprintf(buf+len, "DirectedPacketsRx: " FMT_U64_HEX "\n", U64_VAL(&work64[2]));
	len += sprintf(buf+len, "MulticastBytesRx: " FMT_U64_HEX "\n", U64_VAL(&work64[3]));
	len += sprintf(buf+len, "MulticastPacketsRx: " FMT_U64_HEX "\n", U64_VAL(&work64[4]));
	len += sprintf(buf+len, "BroadcastBytesRx: " FMT_U64_HEX "\n", U64_VAL(&work64[5]));
	len += sprintf(buf+len, "BroadcastPacketsRx: " FMT_U64_HEX "\n", U64_VAL(&work64[6]));
	len += sprintf(buf+len, "TotalGroupAddrRxCount: " FMT_U64_HEX "\n", U64_VAL(&work64[7]));
	len += sprintf(buf+len, "TotalRxPacketsTooShort: " FMT_U64_HEX "\n", U64_VAL(&work64[8]));
	len += sprintf(buf+len, "TotalRxPacketsTooLong: " FMT_U64_HEX "\n", U64_VAL(&work64[9]));
	len += sprintf(buf+len, "TotalRuntPacketsReceived: " FMT_U64_HEX "\n", U64_VAL(&work64[10]));

	spin_unlock(&i2o_proc_lock);
	return len;
}


/* LAN group 0400h - Required FDDI Statistics (scalar) */
int i2o_proc_read_lan_fddi_stats(char *buf, char **start, off_t offset,
				 int len, int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u64 work64[11];
	int token;

	static char *conf_state[] =
	{
		"Isolated",
		"Local a",
		"Local b",
		"Local ab",
		"Local s",
		"Wrap a",
		"Wrap b",
		"Wrap ab",
		"Wrap s",
		"C-Wrap a",
		"C-Wrap b",
		"C-Wrap s",
		"Through",
	};

	static char *ring_state[] =
	{
		"Isolated",
		"Non-op",
		"Rind-op",
		"Detect",
		"Non-op-Dup",
		"Ring-op-Dup",
		"Directed",
		"Trace"
	};

	static char *link_state[] =
	{
		"Off",
		"Break",
		"Trace",
		"Connect",
		"Next",
		"Signal",
		"Join",
		"Verify",
		"Active",
		"Maintenance"
	};

	spin_lock(&i2o_proc_lock);
	
	len = 0;

	token = i2o_query_scalar(d->controller, d->id, proc_context, 
				 0x0400, -1, &work64, 11*8, &i2o_proc_token);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf, "ConfigurationState: %s\n", conf_state[work64[0]]);
	len += sprintf(buf+len, "UpstreamNode: " FMT_U64_HEX "\n", U64_VAL(&work64[1]));
	len += sprintf(buf+len, "DownStreamNode: " FMT_U64_HEX "\n", U64_VAL(&work64[2]));
	len += sprintf(buf+len, "FrameErrors: " FMT_U64_HEX "\n", U64_VAL(&work64[3]));
	len += sprintf(buf+len, "FramesLost: " FMT_U64_HEX "\n", U64_VAL(&work64[4]));
	len += sprintf(buf+len, "RingMgmtState: %s\n", ring_state[work64[5]]);
	len += sprintf(buf+len, "LCTFailures: " FMT_U64_HEX "\n", U64_VAL(&work64[6]));
	len += sprintf(buf+len, "LEMRejects: " FMT_U64_HEX "\n", U64_VAL(&work64[7]));
	len += sprintf(buf+len, "LEMCount: " FMT_U64_HEX "\n", U64_VAL(&work64[8]));
	len += sprintf(buf+len, "LConnectionState: %s\n", link_state[work64[9]]);

	spin_unlock(&i2o_proc_lock);
	return len;
}

static int i2o_proc_create_entries(void *data,
	i2o_proc_entry *pentry, struct proc_dir_entry *parent)
{
	struct proc_dir_entry *ent;
	
	while(pentry->name != NULL)
	{
		ent = create_proc_entry(pentry->name, pentry->mode, parent);
		if(!ent) return -1;

		ent->data = data;
		ent->read_proc = pentry->read_proc;
		ent->write_proc = pentry->write_proc;
		ent->nlink = 1;

		pentry++;
	}

	return 0;
}

static void i2o_proc_remove_entries(i2o_proc_entry *pentry, 
	struct proc_dir_entry *parent)
{
	while(pentry->name != NULL)
	{
		remove_proc_entry(pentry->name, parent);
		pentry++;
	}
}

static int i2o_proc_add_controller(struct i2o_controller *pctrl, 
	struct proc_dir_entry *root )
{
	struct proc_dir_entry *dir, *dir1;
	struct i2o_device *dev;
	char buff[10];

	sprintf(buff, "iop%d", pctrl->unit);

	dir = create_proc_entry(buff, S_IFDIR, root);
	if(!dir)
		return -1;

	pctrl->proc_entry = dir;

	i2o_proc_create_entries(pctrl, generic_iop_entries, dir);
	
	for(dev = pctrl->devices; dev; dev = dev->next)
	{
		sprintf(buff, "%0#5x", dev->id);

		dir1 = create_proc_entry(buff, S_IFDIR, dir);
		dev->proc_entry = dir1;

		if(!dir1)
			printk(KERN_INFO "i2o_proc: Could not allocate proc dir\n");
		
		i2o_proc_create_entries(dev, generic_dev_entries, dir1);

		switch(dev->class)
		{
		case I2O_CLASS_SCSI_PERIPHERAL:
		case I2O_CLASS_RANDOM_BLOCK_STORAGE:
			i2o_proc_create_entries(dev, rbs_dev_entries, dir1);
			break;
		case I2O_CLASS_LAN:
			i2o_proc_create_entries(dev, lan_entries, dir1);
			break;
		default:
			break;
		}
	}

	return 0;
}

static void i2o_proc_remove_controller(struct i2o_controller *pctrl, 
	struct proc_dir_entry *parent)
{
	char buff[10];

	sprintf(buff, "iop%d", pctrl->unit);

	i2o_proc_remove_entries(generic_iop_entries, pctrl->proc_entry);

	remove_proc_entry(buff, parent);

	pctrl->proc_entry = NULL;
}

static int create_i2o_procfs(void)
{
	struct i2o_controller *pctrl = NULL;
	int i;

	i2o_proc_dir_root = create_proc_entry("i2o", S_IFDIR, 0);
	if(!i2o_proc_dir_root)
		return -1;

	for(i = 0; i < MAX_I2O_CONTROLLERS; i++)
	{
		pctrl = i2o_find_controller(i);
		if(pctrl)
			i2o_proc_add_controller(pctrl, i2o_proc_dir_root);
	};

	return 0;
}

static int destroy_i2o_procfs(void)
{
	struct i2o_controller *pctrl = NULL;
	int i;

	if(!i2o_find_controller(0))
		return -1;
	
	for(i = 0; i < MAX_I2O_CONTROLLERS; i++)
	{
		pctrl = i2o_find_controller(i);
		if(pctrl)
			i2o_proc_remove_controller(pctrl, i2o_proc_dir_root);
	};

	remove_proc_entry("i2o", 0);
	return 0;
}
		
#ifdef MODULE

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("I2O procfs Handler");

int init_module(void)
{
	if(create_i2o_procfs())
		return -EBUSY;

	if (i2o_install_handler(&i2o_proc_handler) < 0)
	{
		printk(KERN_ERR "i2o_proc: Unable to install PROC handler.\n");
		return 0;
	}

	proc_context = i2o_proc_handler.context;

	return 0;
}

void cleanup_module(void)
{
	destroy_i2o_procfs();
	i2o_remove_handler(&i2o_proc_handler);
}
#endif
