/*
 *   procfs handler for Linux I2O subsystem
 *
 *   (c) Copyright 1999 Deepak Saxena
 *   
 *   Originally written by Deepak Saxena(deepak@plexity.net)
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
 *   LAN entries by Juha Sievänen (Juha.Sievanen@cs.Helsinki.FI),
 *		    Auvo Häkkinen (Auvo.Hakkinen@cs.Helsinki.FI)
 *   University of Helsinki, Department of Computer Science
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

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/i2o.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/spinlock.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>


#include "i2o_proc.h"

#include "i2o_lan.h"

/*
 * Structure used to define /proc entries
 */
typedef struct _i2o_proc_entry_t
{
	char *name;			/* entry name */
	mode_t mode;			/* mode */
	read_proc_t *read_proc;		/* read func */
	write_proc_t *write_proc;	/* write func */
} i2o_proc_entry;

static int proc_context = 0;


static int i2o_proc_read_lct(char *, char **, off_t, int, int *, void *);
static int i2o_proc_read_hrt(char *, char **, off_t, int, int *, void *);
static int i2o_proc_read_stat(char *, char **, off_t, int, int *, void *);
static int i2o_proc_read_hw(char *, char **, off_t, int, int *, void *);
static int i2o_proc_read_dst(char *, char **, off_t, int, int *, void *);
static int i2o_proc_read_ddm_table(char *, char **, off_t, int, int *, void *);
static int i2o_proc_read_ds(char *, char **, off_t, int, int *, void *);
static int i2o_proc_read_groups(char *, char **, off_t, int, int *, void *);
static int i2o_proc_read_priv_msgs(char *, char **, off_t, int, int *, void *);
static int i2o_proc_read_dev(char *, char **, off_t, int, int *, void *);
static int i2o_proc_read_dev_name(char *, char **, off_t, int, int *, void *);
static int i2o_proc_read_ddm(char *, char **, off_t, int, int *, void *);
static int i2o_proc_read_uinfo(char *, char **, off_t, int, int *, void *);
static int i2o_proc_read_sgl_limits(char *, char **, off_t, int, int *, void *);
static int print_serial_number(char *, int, u8 *, int);
static int i2o_proc_create_entries(void *, 
				   i2o_proc_entry *, struct proc_dir_entry *);
static void i2o_proc_remove_entries(i2o_proc_entry *, struct proc_dir_entry *);
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
static int i2o_proc_read_lan_mcast_addr(char *, char **, off_t, int, int *,
					void *);
static int i2o_proc_read_lan_batch_control(char *, char **, off_t, int, int *,
					   void *);
static int i2o_proc_read_lan_operation(char *, char **, off_t, int, int *,
				       void *);
static int i2o_proc_read_lan_media_operation(char *, char **, off_t, int,
					     int *, void *);
static int i2o_proc_read_lan_alt_addr(char *, char **, off_t, int, int *,
				      void *);
static int i2o_proc_read_lan_tx_info(char *, char **, off_t, int, int *,
				     void *);
static int i2o_proc_read_lan_rx_info(char *, char **, off_t, int, int *,
				     void *);
static int i2o_proc_read_lan_hist_stats(char *, char **, off_t, int, int *,
					void *);
static int i2o_proc_read_lan_supp_opt_stats(char *, char **, off_t, int, int *,
					    void *);
static int i2o_proc_read_lan_opt_tx_hist_stats(char *, char **, off_t, int,
					       int *, void *);
static int i2o_proc_read_lan_opt_rx_hist_stats(char *, char **, off_t, int,
					       int *, void *);
static int i2o_proc_read_lan_eth_stats(char *, char **, off_t, int,
				       int *, void *);
static int i2o_proc_read_lan_supp_eth_stats(char *, char **, off_t, int, int *,
					    void *);
static int i2o_proc_read_lan_opt_eth_stats(char *, char **, off_t, int, int *,
					   void *);
static int i2o_proc_read_lan_tr_stats(char *, char **, off_t, int, int *,
				      void *);
static int i2o_proc_read_lan_fddi_stats(char *, char **, off_t, int, int *,
					void *);

static struct proc_dir_entry *i2o_proc_dir_root;

/*
 * Message handler
 */
static struct i2o_handler i2o_proc_handler =
{
	(void *)i2o_proc_reply,
	"I2O procfs Layer",
	0,
	0xffffffff	// All classes
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
	{"dst", S_IFREG|S_IRUGO, i2o_proc_read_dst, NULL},
	{"ddm_table", S_IFREG|S_IRUGO, i2o_proc_read_ddm_table, NULL},
	{"ds", S_IFREG|S_IRUGO, i2o_proc_read_ds, NULL},
	{NULL, 0, NULL, NULL}
};

/*
 * Device specific entries
 */
static i2o_proc_entry generic_dev_entries[] = 
{
	{"groups", S_IFREG|S_IRUGO, i2o_proc_read_groups, NULL},
	{"priv_msgs", S_IFREG|S_IRUGO, i2o_proc_read_priv_msgs, NULL},
	{"dev_identity", S_IFREG|S_IRUGO, i2o_proc_read_dev, NULL},
	{"ddm_identity", S_IFREG|S_IRUGO, i2o_proc_read_ddm, NULL},
	{"sgl_limits", S_IFREG|S_IRUGO, i2o_proc_read_sgl_limits, NULL},
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
 * Generic LAN specific entries
 * 
 * Should groups with r/w entries have their own subdirectory?
 *
 */
static i2o_proc_entry lan_entries[] = 
{
	/* LAN param groups 0000h-0008h */
	{"lan_dev_info", S_IFREG|S_IRUGO, i2o_proc_read_lan_dev_info, NULL},
	{"lan_mac_addr", S_IFREG|S_IRUGO, i2o_proc_read_lan_mac_addr, NULL},
	{"lan_mcast_addr", S_IFREG|S_IRUGO|S_IWUSR,
	 i2o_proc_read_lan_mcast_addr, NULL},
	{"lan_batch_ctrl", S_IFREG|S_IRUGO|S_IWUSR,
	 i2o_proc_read_lan_batch_control, NULL},
	{"lan_operation", S_IFREG|S_IRUGO, i2o_proc_read_lan_operation, NULL},
	{"lan_media_operation", S_IFREG|S_IRUGO,
	 i2o_proc_read_lan_media_operation, NULL},
	{"lan_alt_addr", S_IFREG|S_IRUGO, i2o_proc_read_lan_alt_addr, NULL},
	{"lan_tx_info", S_IFREG|S_IRUGO, i2o_proc_read_lan_tx_info, NULL},
	{"lan_rx_info", S_IFREG|S_IRUGO, i2o_proc_read_lan_rx_info, NULL},
	/* LAN param groups 0100h, 0180h, 0182h, 0183h */
	{"lan_stats", S_IFREG|S_IRUGO, i2o_proc_read_lan_hist_stats, NULL},
	{"lan_supp_opt_stats", S_IFREG|S_IRUGO,
	 i2o_proc_read_lan_supp_opt_stats, NULL},
	{"lan_opt_tx_stats", S_IFREG|S_IRUGO,
	 i2o_proc_read_lan_opt_tx_hist_stats, NULL},
	{"lan_opt_rx_stats", S_IFREG|S_IRUGO,
	 i2o_proc_read_lan_opt_rx_hist_stats, NULL},
	/* TODO: LAN param group 0184h */
	{NULL, 0, NULL, NULL}
};

/*
 * Ethernet specific LAN entries
 * 
 */
static i2o_proc_entry lan_eth_entries[] = 
{
	/* LAN param groups 0200h, 0280h, 0281h */
	{"lan_eth_stat", S_IFREG|S_IRUGO, i2o_proc_read_lan_eth_stats, NULL},
        {"lan_supp_eth_stats", S_IFREG|S_IRUGO,
	 i2o_proc_read_lan_supp_eth_stats, NULL},
        {"lan_opt_eth_stats", S_IFREG|S_IRUGO,
	 i2o_proc_read_lan_opt_eth_stats, NULL},
	{NULL, 0, NULL, NULL}
};

/*
 * Token Ring specific LAN entries
 * 
 */
static i2o_proc_entry lan_tr_entries[] = 
{
	/* LAN param group 0300h */
	{"lan_tr_stats", S_IFREG|S_IRUGO,
	 i2o_proc_read_lan_tr_stats, NULL},
	/* TODO: LAN param group 0380h, 0381h */
	{NULL, 0, NULL, NULL}
};

/*
 * FDDI specific LAN entries
 * 
 */
static i2o_proc_entry lan_fddi_entries[] = 
{
	/* LAN param group 0400h */
	{"lan_fddi_stats", S_IFREG|S_IRUGO,
	 i2o_proc_read_lan_fddi_stats, NULL},
	/* TODO: LAN param group 0480h, 0481h */
	{NULL, 0, NULL, NULL}
};


static u32 i2o_proc_token = 0;

static char *chtostr(u8 *chars, int n)
{
	char tmp[256];
	tmp[0] = 0;
        return strncat(tmp, (char *)chars, n);
}

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
	pi2o_hrt hrt = (pi2o_hrt)c->hrt;
	u32 bus;
	int count;
	int i;

	spin_lock(&i2o_proc_lock);

	len = 0;

	if(hrt->hrt_version)
	{
		len += sprintf(buf+len, 
			       "HRT table for controller is too new a version.\n");
		spin_unlock(&i2o_proc_lock);
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

	spin_unlock(&i2o_proc_lock);
	
	return len;
}

int i2o_proc_read_lct(char *buf, char **start, off_t offset, int len,
	int *eof, void *data)
{
	struct i2o_controller *c = (struct i2o_controller*)data;
	pi2o_lct lct = (pi2o_lct)c->lct;
	int entries;
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
						len += sprintf(buf+len, ": Unknown (0x%02x)",
							       lct->lct_entry[i].sub_class);
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
						len += sprintf(buf+len, ": Unknown Sub-Class (0x%02x)",
							       lct->lct_entry[i].sub_class & 0xFF);
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
			len += sprintf(buf+len, "1.0\n");
			break;
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
			len += sprintf(buf+len, "1.0\n");
			break;
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

	len += sprintf(buf+len, "I2O version: ");
	switch(version)
	{
	case 0x00:
		len += sprintf(buf+len, "1.0\n");
		break;
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
		len += sprintf(buf+len, "Memory mapped\n");
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

	len += sprintf(buf+len, "Desired private memory space: %d kB\n", 
						work32[15]>>10);
	len += sprintf(buf+len, "Allocated private memory space: %d kB\n", 
						work32[16]>>10);
	len += sprintf(buf+len, "Private memory base address: %0#10x\n", 
						work32[17]);
	len += sprintf(buf+len, "Desired private I/O space: %d kB\n", 
						work32[18]>>10);
	len += sprintf(buf+len, "Allocated private I/O space: %d kB\n", 
						work32[19]>>10);
	len += sprintf(buf+len, "Private I/O base address: %0#10x\n", 
						work32[20]);

	kfree(workspace);
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
		"Intel 80960 series",
		"AMD2900 series",
		"Motorola 68000 series",
		"ARM series",
		"MIPS series",
		"Sparc series",
		"PowerPC series",
		"Intel x86 series"
	};

	spin_lock(&i2o_proc_lock);

	len = 0;

	token = i2o_query_scalar(c, ADAPTER_TID,
					0,		// ParamGroup 0x0000h
					-1,		// all fields
					&work32,
					sizeof(work32));

	if(token < 0)
	{
		len += sprintf(buf, "Error waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf, "IOP Hardware Information Table (group = 0x0000)\n");

	len += sprintf(buf+len, "I2O Vendor ID        : %0#6x\n", work16[0]);
	len += sprintf(buf+len, "Product ID           : %0#6x\n", work16[1]);
	len += sprintf(buf+len, "RAM                  : %dkB\n", work32[1]>>10);
	len += sprintf(buf+len, "Non-Volatile Storage : %dkB\n", work32[2]>>10);

	hwcap = work32[3];
	len += sprintf(buf+len, "Capabilities :\n");
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

	len += sprintf(buf+len, "CPU                 : ");
	if(work8[16] > 8)
		len += sprintf(buf+len, "Unknown\n");
	else
		len += sprintf(buf+len, "%s\n", cpu_table[work8[16]]);
	/* Anyone using ProcessorVersion? */
	
	spin_unlock(&i2o_proc_lock);

	return len;
}


/* Executive group 0003h - Executing DDM List (table) */
int i2o_proc_read_ddm_table(char *buf, char **start, off_t offset, int len, 
			    int *eof, void *data)
{
	struct i2o_controller *c = (struct i2o_controller*)data;
	int token;
	int i;

	typedef struct _i2o_exec_execute_ddm_table {
		u16 ddm_tid;
		u8  module_type;
		u8  reserved;
		u16 i2o_vendor_id;
		u16 module_id;
		u8  module_name[24];
		u8  module_version[4];
		u32 data_size;
		u32 code_size;
	} i2o_exec_execute_ddm_table, *pi2o_exec_execute_ddm_table;

	struct
	{
		u16 result_count;
		u16 pad;
		u16 block_size;
		u8  block_status;
		u8  error_info_size;
		u16 row_count;
		u16 more_flag;
		i2o_exec_execute_ddm_table ddm_table[MAX_I2O_MODULES];
	} result;

	i2o_exec_execute_ddm_table ddm_table;

	spin_lock(&i2o_proc_lock);
	len = 0;

	token = i2o_query_table(I2O_PARAMS_TABLE_GET,
				c, ADAPTER_TID, 
				0x0003, -1,
				NULL, 0,
				&result, sizeof(result));

	if (token<0)
		switch (token)
		{
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
		default:
			len += sprintf(buf, "Error reading group. BlockStatus %d\n",
				       token);
			spin_unlock(&i2o_proc_lock);
			return len;
		}

	len += sprintf(buf+len, "Tid   Type            Vendor Id     Name                     Vrs  Data_size Code_size\n");
	ddm_table=result.ddm_table[0];

	for(i=0; i < result.row_count; ddm_table=result.ddm_table[++i])
	{
		len += sprintf(buf+len, "0x%03x ", ddm_table.ddm_tid & 0xFFF);

		switch(ddm_table.module_type)
		{
		case 0x01:
			len += sprintf(buf+len, "Downloaded DDM  ");
			break;			
		case 0x22:
			len += sprintf(buf+len, "Embedded DDM    ");
			break;
		default:
			len += sprintf(buf+len, "                ");
		}

		len += sprintf(buf+len, "%0#7x", ddm_table.i2o_vendor_id);
		len += sprintf(buf+len, "%0#7x", ddm_table.module_id);
		len += sprintf(buf+len, "%-25s", chtostr(ddm_table.module_name, 24));
		len += sprintf(buf+len, "%-6s", chtostr(ddm_table.module_version,4));
		len += sprintf(buf+len, "%8d  ", ddm_table.data_size);
		len += sprintf(buf+len, "%8d", ddm_table.code_size);

		len += sprintf(buf+len, "\n");
	}

	spin_unlock(&i2o_proc_lock);

	return len;
}


/* Executive group 0004h - Driver Store (scalar) */
int i2o_proc_read_ds(char *buf, char **start, off_t offset, int len, 
		     int *eof, void *data)
{
	struct i2o_controller *c = (struct i2o_controller*)data;
	u32 work32[8];

	spin_lock(&i2o_proc_lock);

	len = 0;

	if(i2o_query_scalar(c, ADAPTER_TID, 0x0004, -1, &work32,
		sizeof(work32)) < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf+len, "Module limit  : %d\n"
				"Module count  : %d\n"
				"Current space : %d kB\n"
				"Free space    : %d kB\n", 
			work32[0], work32[1], work32[2]>>10, work32[3]>>10);

	spin_unlock(&i2o_proc_lock);

	return len;
}


/* Executive group 0005h - Driver Store Table (table) */
int i2o_proc_read_dst(char *buf, char **start, off_t offset, int len, 
		      int *eof, void *data)
{
	typedef struct _i2o_driver_store {
		u16 stored_ddm_index;
		u8  module_type;
		u8  reserved;
		u16 i2o_vendor_id;
		u16 module_id;
		u8  module_name_version[28];
		u8  date[8];
		u32 module_size;
		u32 mpb_size;
		u32 module_flags;
	} i2o_driver_store_table;

	struct i2o_controller *c = (struct i2o_controller*)data;
	int token;
	int i;

	struct
	{
		u16 result_count;
		u16 pad;
		u16 block_size;
		u8  block_status;
		u8  error_info_size;
		u16 row_count;
		u16 more_flag;
		i2o_driver_store_table dst[MAX_I2O_MODULES];
	} result;

	i2o_driver_store_table dst;

	spin_lock(&i2o_proc_lock);

	len = 0;

	token = i2o_query_table(I2O_PARAMS_TABLE_GET,
				c, ADAPTER_TID,
				0x0005, -1,
				NULL, 0,
				&result, sizeof(result));

	if (token<0)
		switch (token)
		{
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
		default:
			len += sprintf(buf, "Error reading group. "
					"BlockStatus %d\n",token);
			spin_unlock(&i2o_proc_lock);
			return len;
		}

	len += sprintf(buf+len, "#  Type            Vendor Id      Name                    Vrs  Date     Mod_size Par_size Flags\n");	

	for(i=0, dst=result.dst[0]; i < result.row_count; dst=result.dst[++i])
	{
		len += sprintf(buf+len, "%-3d", dst.stored_ddm_index);
		switch(dst.module_type)
		{
		case 0x01:
			len += sprintf(buf+len, "Downloaded DDM  ");
			break;			
		case 0x22:
			len += sprintf(buf+len, "Embedded DDM    ");
			break;
		default:
			len += sprintf(buf+len, "                ");
		}

#if 0
		if(c->i2oversion == 0x02)
			len += sprintf(buf+len, "%-d", dst.module_state);
#endif

		len += sprintf(buf+len, "%#7x", dst.i2o_vendor_id);
		len += sprintf(buf+len, "%#8x", dst.module_id);
		len += sprintf(buf+len, "%-29s", chtostr(dst.module_name_version,28));
		len += sprintf(buf+len, "%-9s", chtostr(dst.date,8));
		len += sprintf(buf+len, "%8d ", dst.module_size);
		len += sprintf(buf+len, "%8d ", dst.mpb_size);
		len += sprintf(buf+len, "0x%04x", dst.module_flags);
#if 0
		if(c->i2oversion == 0x02)
			len += sprintf(buf+len, "%d",
				       dst.notification_level);
#endif
		len += sprintf(buf+len, "\n");
	}

	spin_unlock(&i2o_proc_lock);

	return len;
}


/* Generic group F000h - Params Descriptor (table) */
int i2o_proc_read_groups(char *buf, char **start, off_t offset, int len, 
			 int *eof, void *data)
{
	struct i2o_controller *c = (struct i2o_controller*)data;
	int token;
	int i;
	int rows;
	u16 work16[2048];
	u16 *group=work16;
	int more;
	
	spin_lock(&i2o_proc_lock);

	len = 0;

	token = i2o_query_table(I2O_PARAMS_TABLE_GET,
				c, ADAPTER_TID,
				0xF000, -1,
				NULL, 0,
				&work16, sizeof(work16));

	if (token<0)
		switch (token)
		{
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
		default:
			len += sprintf(buf, "Error reading table. BlockStatus %d\n",
				       token);
			spin_unlock(&i2o_proc_lock);
			return len;
		}

	rows=work16[4];
	more=work16[5];

	len += sprintf(buf+len, "\nPARAMS DESCRIPTOR TABLE:\n\n");
	len += sprintf(buf+len, "#  Group   FieldCount RowCount Type   Add Del Clear\n");

	group+=64;

	for(i=0; i < rows; i++,	group+=16)
	{
		len += sprintf(buf+len, "%-3d", i);

		len += sprintf(buf+len, "%#6x ", group[0]);
		len += sprintf(buf+len, "%10d ", group[1]);
		len += sprintf(buf+len, "%8d ", group[2]);

		if(group[3]&0x1)
			len += sprintf(buf+len, "Table  ");
		else
			len += sprintf(buf+len, "Scalar ");
		if(group[3]&0x2)
			len += sprintf(buf+len, "x   ");
		else
			len += sprintf(buf+len, "    ");
		if(group[3]&0x4)
			len += sprintf(buf+len, "x   ");
		else
			len += sprintf(buf+len, "    ");
		if(group[3]&0x8)
			len += sprintf(buf+len, "x   ");
		else
			len += sprintf(buf+len, "    ");

		len += sprintf(buf+len, "\n");
	}

	if(more)
		len += sprintf(buf+len, "There is more...\n");

	spin_unlock(&i2o_proc_lock);

	return len;
}


/* Generic group F005h - Private message extensions (table) */
int i2o_proc_read_priv_msgs(char *buf, char **start, off_t offset, int len, 
			    int *eof, void *data)
{
	struct i2o_controller *c = (struct i2o_controller*)data;
	int token;
	int i;
	int rows;
	int more;
	u16 work16[1024];
	u16 *field=work16;

	spin_lock(&i2o_proc_lock);

	len = 0;

	token = i2o_query_table(I2O_PARAMS_TABLE_GET,
				c, ADAPTER_TID,
				0xF000, -1,
				NULL, 0,
				&work16, sizeof(work16));

	if (token<0)
		switch (token)
		{
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
		default:
			len += sprintf(buf, "Error reading field. BlockStatus %d\n",
				       token);
			spin_unlock(&i2o_proc_lock);
			return len;
		}

	rows=work16[4];
	more=work16[5];
	
	len += sprintf(buf+len, "Instance#  OrgId  FunctionCode\n");

	field+=64;
	for(i=0; i < rows; i++,	field+=16)
	{
		len += sprintf(buf+len, "%0#9x ", field[0]);
		len += sprintf(buf+len, "%0#6x ", work16[1]);
		len += sprintf(buf+len, "%0#6x", work16[2]);

		len += sprintf(buf+len, "\n");
	}

	if(more)
		len += sprintf(buf+len, "There is more...\n");

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
	int token;

	spin_lock(&i2o_proc_lock);
	
	len = 0;

	token = i2o_query_scalar(d->controller, d->lct_data->tid,
					0xF100,		// ParamGroup F100h (Device Identity)
					-1,		// all fields
					&work32,
					sizeof(work32));

	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}
	
	len += sprintf(buf,     "Device Class  : %s\n", i2o_get_class_name(work16[0]));
	len += sprintf(buf+len, "Owner TID     : %0#5x\n", work16[2]);
	len += sprintf(buf+len, "Parent TID    : %0#5x\n", work16[3]);
	len += sprintf(buf+len, "Vendor info   : %s\n", chtostr((u8 *)(work32+2), 16));
	len += sprintf(buf+len, "Product info  : %s\n", chtostr((u8 *)(work32+6), 16));
	len += sprintf(buf+len, "Description   : %s\n", chtostr((u8 *)(work32+10), 16));
	len += sprintf(buf+len, "Product rev.  : %s\n", chtostr((u8 *)(work32+14), 8));

	len += sprintf(buf+len, "Serial number : ");
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

	spin_lock(&i2o_proc_lock);
	
	len = 0;

	token = i2o_query_scalar(d->controller, d->lct_data->tid, 
					0xF101,		// ParamGroup F101h (DDM Identity)
					-1,		// all fields
					&work32,
					sizeof(work32));

	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf,     "Registering DDM TID : 0x%03x\n", work16[0]&0xFFF);
	len += sprintf(buf+len, "Module name         : %s\n", chtostr((u8 *)(work16+1), 24));
	len += sprintf(buf+len, "Module revision     : %s\n", chtostr((u8 *)(work16+13), 8));

	len += sprintf(buf+len, "Serial number       : ");
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
	static u32 work32[256];
	int token;
 
	spin_lock(&i2o_proc_lock);
	
	len = 0;

	token = i2o_query_scalar(d->controller, d->lct_data->tid,
					0xF102,		// ParamGroup F102h (User Information)
					-1,		// all fields
					&work32,
					sizeof(work32));

	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf,     "Device name     : %s\n", chtostr((u8 *)work32, 64));
	len += sprintf(buf+len, "Service name    : %s\n", chtostr((u8 *)(work32+16), 64));
	len += sprintf(buf+len, "Physical name   : %s\n", chtostr((u8 *)(work32+32), 64));
	len += sprintf(buf+len, "Instance number : %s\n", chtostr((u8 *)(work32+48), 4));
        
	spin_unlock(&i2o_proc_lock);

	return len;
}


int i2o_proc_read_sgl_limits(char *buf, char **start, off_t offset, int len, 
			     int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u32 work32[12];
	static u16 *work16 = (u16 *)work32;
	static u8 *work8 = (u8 *)work32;
	int token;

	spin_lock(&i2o_proc_lock);
	
	len = 0;

	token = i2o_query_scalar(d->controller, d->lct_data->tid, 
				 0xF103,	// ParamGroup F103h (SGL Operating Limits)
				 -1,		// all fields
				 &work32,
				 sizeof(work32));

	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf,     "SGL chain size        : %d\n", work32[0]);
	len += sprintf(buf+len, "Max SGL chain size    : %d\n", work32[1]);
	len += sprintf(buf+len, "SGL chain size target : %d\n", work32[2]);
	len += sprintf(buf+len, "SGL frag count        : %d\n", work16[6]);
	len += sprintf(buf+len, "Max SGL frag count    : %d\n", work16[7]);
	len += sprintf(buf+len, "SGL frag count target : %d\n", work16[8]);

	if (d->i2oversion == 0x02)
	{
		len += sprintf(buf+len, "SGL data alignment    : %d\n", work16[8]);
		len += sprintf(buf+len, "SGL addr limit        : %d\n", work8[20]);
		len += sprintf(buf+len, "SGL addr sizes supported : ");
		if (work8[21] & 0x01)
			len += sprintf(buf+len, "32 bit ");
		if (work8[21] & 0x02)
			len += sprintf(buf+len, "64 bit ");
		if (work8[21] & 0x04)
			len += sprintf(buf+len, "96 bit ");
		if (work8[21] & 0x08)
			len += sprintf(buf+len, "128 bit ");
		len += sprintf(buf+len, "\n");
	}

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
						"LAN-48 MAC address @ %02X:%02X:%02X:%02X:%02X:%02X",
						serialno[2], serialno[3],
						serialno[4], serialno[5],
						serialno[6], serialno[7]);
			break;

		case I2O_SNFORMAT_WAN:			/* WAN MAC Address */
			/* FIXME: Figure out what a WAN access address looks like?? */
			pos += sprintf(buff+pos, "WAN Access Address");
			break;

/* plus new in v2.0 */
		case I2O_SNFORMAT_LAN64_MAC:		/* LAN-64 MAC Address */
			/* FIXME: Figure out what a LAN-64 address really looks like?? */
			pos += sprintf(buff+pos, 
						"LAN-64 MAC address @ [?:%02X:%02X:?] %02X:%02X:%02X:%02X:%02X:%02X",
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
			pos += sprintf(buff+pos, "Unknown data format (0x%02x)",
				       serialno[0]);
			break;
	}

	return pos;
}

const char * i2o_get_connector_type(int conn)
{
	int idx = 16;
	static char *i2o_connector_type[] = {
		"OTHER",
		"UNKNOWN",
		"AUI",
		"UTP",
		"BNC",
		"RJ45",
		"STP DB9",
		"FIBER MIC",
		"APPLE AUI",
		"MII",
		"DB9",
		"HSSDC",
		"DUPLEX SC FIBER",
		"DUPLEX ST FIBER",
		"TNC/BNC",
		"HW DEFAULT"
	};

	switch(conn)
	{
	case 0x00000000:
		idx = 0;
		break;
	case 0x00000001:
		idx = 1;
		break;
	case 0x00000002:
		idx = 2;
		break;
	case 0x00000003:
		idx = 3;
		break;
	case 0x00000004:
		idx = 4;
		break;
	case 0x00000005:
		idx = 5;
		break;
	case 0x00000006:
		idx = 6;
		break;
	case 0x00000007:
		idx = 7;
		break;
	case 0x00000008:
		idx = 8;
		break;
	case 0x00000009:
		idx = 9;
		break;
	case 0x0000000A:
		idx = 10;
		break;
	case 0x0000000B:
		idx = 11;
		break;
	case 0x0000000C:
		idx = 12;
		break;
	case 0x0000000D:
		idx = 13;
		break;
	case 0x0000000E:
		idx = 14;
		break;
	case 0xFFFFFFFF:
		idx = 15;
		break;
	}

	return i2o_connector_type[idx];
}


const char * i2o_get_connection_type(int conn)
{
	int idx = 0;
	static char *i2o_connection_type[] = {
		"Unknown",
		"AUI",
		"10BASE5",
		"FIORL",
		"10BASE2",
		"10BROAD36",
		"10BASE-T",
		"10BASE-FP",
		"10BASE-FB",
		"10BASE-FL",
		"100BASE-TX",
		"100BASE-FX",
		"100BASE-T4",
		"1000BASE-SX",
		"1000BASE-LX",
		"1000BASE-CX",
		"1000BASE-T",
		"100VG-ETHERNET",
		"100VG-TOKEN RING",
		"4MBIT TOKEN RING",
		"16 Mb Token Ring",
		"125 MBAUD FDDI",
		"Point-to-point",
		"Arbitrated loop",
		"Public loop",
		"Fabric",
		"Emulation",
		"Other",
		"HW default"
	};

	switch(conn)
	{
	case I2O_LAN_UNKNOWN:
		idx = 0;
		break;
	case I2O_LAN_AUI:
		idx = 1;
		break;
	case I2O_LAN_10BASE5:
		idx = 2;
		break;
	case I2O_LAN_FIORL:
		idx = 3;
		break;
	case I2O_LAN_10BASE2:
		idx = 4;
		break;
	case I2O_LAN_10BROAD36:
		idx = 5;
		break;
	case I2O_LAN_10BASE_T:
		idx = 6;
		break;
	case I2O_LAN_10BASE_FP:
		idx = 7;
		break;
	case I2O_LAN_10BASE_FB:
		idx = 8;
		break;
	case I2O_LAN_10BASE_FL:
		idx = 9;
		break;
	case I2O_LAN_100BASE_TX:
		idx = 10;
		break;
	case I2O_LAN_100BASE_FX:
		idx = 11;
		break;
	case I2O_LAN_100BASE_T4:
		idx = 12;
		break;
	case I2O_LAN_1000BASE_SX:
		idx = 13;
		break;
	case I2O_LAN_1000BASE_LX:
		idx = 14;
		break;
	case I2O_LAN_1000BASE_CX:
		idx = 15;
		break;
	case I2O_LAN_1000BASE_T:
		idx = 16;
		break;
	case I2O_LAN_100VG_ETHERNET:
		idx = 17;
		break;
	case I2O_LAN_100VG_TR:
		idx = 18;
		break;
	case I2O_LAN_4MBIT:
		idx = 19;
		break;
	case I2O_LAN_16MBIT:
		idx = 20;
		break;
	case I2O_LAN_125MBAUD:
		idx = 21;
		break;
	case I2O_LAN_POINT_POINT:
		idx = 22;
		break;
	case I2O_LAN_ARB_LOOP:
		idx = 23;
		break;
	case I2O_LAN_PUBLIC_LOOP:
		idx = 24;
		break;
	case I2O_LAN_FABRIC:
		idx = 25;
		break;
	case I2O_LAN_EMULATION:
		idx = 26;
		break;
	case I2O_LAN_OTHER:
		idx = 27;
		break;
	case I2O_LAN_DEFAULT:
		idx = 28;
		break;
	}

	return i2o_connection_type[idx];
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

	token = i2o_query_scalar(d->controller, d->lct_data->tid,
				 0x0000, -1, &work32, 56*4);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf, "LAN Type            : ");
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
		len += sprintf(buf+len, "Unknown type (0x%04x), ", work16[0]);
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

	len += sprintf(buf+len, "Address format      : ");
	switch(work8[4]) {
	case 0x00:
		len += sprintf(buf+len, "IEEE 48bit\n");
		break;
	case 0x01:
		len += sprintf(buf+len, "FC IEEE\n");
		break;
	default:
		len += sprintf(buf+len, "Unknown (0x%02x)\n", work8[4]);
		break;
	}

	len += sprintf(buf+len, "State               : ");
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
		len += sprintf(buf+len, "ERROR: ");
		if(work16[3]&0x0001)
			len += sprintf(buf+len, "TxCU inoperative ");
		if(work16[3]&0x0002)
			len += sprintf(buf+len, "RxCU inoperative ");
		if(work16[3]&0x0004)
			len += sprintf(buf+len, "Local mem alloc ");
		len += sprintf(buf+len, "\n");
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

	len += sprintf(buf+len, "Min packet size     : %d\n", work32[2]);
	len += sprintf(buf+len, "Max packet size     : %d\n", work32[3]);
	len += sprintf(buf+len, "HW address          : "
		       "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
		       work8[16],work8[17],work8[18],work8[19],
		       work8[20],work8[21],work8[22],work8[23]);

	len += sprintf(buf+len, "Max Tx wire speed   : %d bps\n", (int)work64[3]);
	len += sprintf(buf+len, "Max Rx wire speed   : %d bps\n", (int)work64[4]);

	len += sprintf(buf+len, "Min SDU packet size : 0x%08x\n", work32[10]);
	len += sprintf(buf+len, "Max SDU packet size : 0x%08x\n", work32[11]);

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

	token = i2o_query_scalar(d->controller, d->lct_data->tid,
				 0x0001, -1, &work32, 48*4);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf,     "Active address          : "
		       "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
		       work8[0],work8[1],work8[2],work8[3],
		       work8[4],work8[5],work8[6],work8[7]);
	len += sprintf(buf+len, "Current address         : "
		       "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
		       work8[8],work8[9],work8[10],work8[11],
		       work8[12],work8[13],work8[14],work8[15]);
	len += sprintf(buf+len, "Functional address mask : "
		       "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
		       work8[16],work8[17],work8[18],work8[19],
		       work8[20],work8[21],work8[22],work8[23]);

	len += sprintf(buf+len, "HW/DDM capabilities : 0x%08x\n", work32[7]);
	len += sprintf(buf+len, "    Unicast packets %ssupported\n",
		       (work32[7]&0x00000001)?"":"not ");
	len += sprintf(buf+len, "    Promiscuous mode %ssupported\n",
		       (work32[7]&0x00000002)?"":"not");
	len += sprintf(buf+len, "    Promiscuous multicast mode %ssupported\n",
		       (work32[7]&0x00000004)?"":"not ");
	len += sprintf(buf+len,"    Broadcast reception disabling %ssupported\n",
		       (work32[7]&0x00000100)?"":"not ");
	len += sprintf(buf+len,"    Multicast reception disabling %ssupported\n",
		       (work32[7]&0x00000200)?"":"not ");
	len += sprintf(buf+len,"    Functional address disabling %ssupported\n",
		       (work32[7]&0x00000400)?"":"not ");
	len += sprintf(buf+len, "    MAC reporting %ssupported\n",
		       (work32[7]&0x00000800)?"":"not ");

	len += sprintf(buf+len, "Filter mask : 0x%08x\n", work32[6]);
	len += sprintf(buf+len, "    Unicast packets %s\n",
		(work32[6]&0x00000001)?"rejected":"enabled");
	len += sprintf(buf+len, "    Promiscuous mode %s\n",
		(work32[6]&0x00000002)?"enabled":"disabled");
	len += sprintf(buf+len, "    Promiscuous multicast mode %s\n",
		(work32[6]&0x00000004)?"enabled":"disabled");	
	len += sprintf(buf+len, "    Broadcast packets %s\n",
		(work32[6]&0x00000100)?"rejected":"enabled");
	len += sprintf(buf+len, "    Multicast packets %s\n",
		(work32[6]&0x00000200)?"rejected":"enabled");
	len += sprintf(buf+len, "    Functional address %s\n",
		       (work32[6]&0x00000400)?"ignored":"enabled");
		       
	if (work32[7]&0x00000800)
	{		       
		len += sprintf(buf+len, "    MAC reporting mode : ");
		if (work32[6]&0x00000800)
			len += sprintf(buf+len, "Pass only priority MAC packets to user\n");
		else if (work32[6]&0x00001000)
			len += sprintf(buf+len, "Pass all MAC packets to user\n");
		else if (work32[6]&0x00001800)
			len += sprintf(buf+len, "Pass all MAC packets (promiscuous) to user\n");
		else
			len += sprintf(buf+len, "Do not pass MAC packets to user\n");
	}
	len += sprintf(buf+len, "Number of multicast addresses : %d\n", work32[8]);
	len += sprintf(buf+len, "Perfect filtering for max %d multicast addresses\n",
		       work32[9]);
	len += sprintf(buf+len, "Imperfect filtering for max %d multicast addresses\n",
		       work32[10]);

	spin_unlock(&i2o_proc_lock);

	return len;
}

/* LAN group 0002h - Multicast MAC address table (table) */
int i2o_proc_read_lan_mcast_addr(char *buf, char **start, off_t offset,
				 int len, int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u32 field32[64];
	static u8 *field8 = (u8 *)field32;
	static u16 *field16 = (u16 *)field32;
	int token;
	int i;

	spin_lock(&i2o_proc_lock);	
	len = 0;

	token = i2o_query_table(I2O_PARAMS_TABLE_GET,
			d->controller, d->lct_data->tid, 0x0002, -1, 
			NULL, 0, &field32, sizeof(field32));

	if (token<0)
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
		default:
			len += sprintf(buf, "Error reading field. BlockStatus %d\n",
				       token);
			spin_unlock(&i2o_proc_lock);
			return len;
		}

	len += sprintf(buf, "RowCount=%d, MoreFlag=%d\n", 
		       field16[0], field16[1]);

	field8=(u8 *)&field16[2];

	for(i=0; i<field16[0]; i++, field8+=8)
	{
		len += sprintf(buf+len, "MC MAC address[%d]: "
			       "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
			       i,
			       field8[0], field8[1], field8[2],
			       field8[3], field8[4], field8[5],
			       field8[6], field8[7]);
	}

	spin_unlock(&i2o_proc_lock);
	return len;
}

/* LAN group 0003h - Batch Control (scalar) */
int i2o_proc_read_lan_batch_control(char *buf, char **start, off_t offset,
				    int len, int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u32 work32[9];
	int token;

	spin_lock(&i2o_proc_lock);	
	len = 0;

	token = i2o_query_scalar(d->controller, d->lct_data->tid,
				 0x0003, -1, &work32, 9*4);
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
		len += sprintf(buf+len, "Rising load delay      : %d ms\n",
			       work32[1]/10);
		len += sprintf(buf+len, "Rising load threshold  : %d ms\n",
			       work32[2]/10);
		len += sprintf(buf+len, "Falling load delay     : %d ms\n",
			       work32[3]/10);
		len += sprintf(buf+len, "Falling load threshold : %d ms\n",
			       work32[4]/10);
	}

	len += sprintf(buf+len, "Max Rx batch count : %d\n", work32[5]);
	len += sprintf(buf+len, "Max Rx batch delay : %d\n", work32[6]);

	if(d->i2oversion == 0x00) {
		len += sprintf(buf+len,
			       "Transmission completion reporting delay : %d ms\n",
			       work32[7]);
	} else {
		len += sprintf(buf+len, "Max Tx batch delay : %d\n", work32[7]);
		len += sprintf(buf+len, "Max Tx batch count : %d\n", work32[8]);
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

	token = i2o_query_scalar(d->controller, d->lct_data->tid,
				 0x0004, -1, &work32, 20);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf, "Packet prepadding (32b words) : %d\n", work32[0]);
	len += sprintf(buf+len, "Transmission error reporting  : %s\n",
		       (work32[1]&1)?"on":"off");
	len += sprintf(buf+len, "Bad packet handling           : %s\n",
		       (work32[1]&0x2)?"by host":"by DDM");		      
	len += sprintf(buf+len, "Packet orphan limit           : %d\n", work32[2]);

	len += sprintf(buf+len, "Tx modes :\n");
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

	len += sprintf(buf+len, "Rx modes :\n");
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

	token = i2o_query_scalar(d->controller, d->lct_data->tid,
				 0x0005, -1, &work32, 36);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf, "Connector type         : %s\n",
		       i2o_get_connector_type(work32[0]));
	len += sprintf(buf+len, "Connection type        : %s\n",
		       i2o_get_connection_type(work32[1]));

	len += sprintf(buf+len, "Current Tx wire speed  : %d bps\n", (int)work64[1]);
	len += sprintf(buf+len, "Current Rx wire speed  : %d bps\n", (int)work64[2]);

	len += sprintf(buf+len, "Duplex mode            : %s duplex\n", 
			(work8[24]&1)?"Full":"Half");
	len += sprintf(buf+len, "Link status            : ");
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
		len += sprintf(buf+len, "Bad packets handled by : %s\n",
			       (work8[26] == 0xFF)?"host":"DDM");
	}
	if (d->i2oversion != 0x00) {
		len += sprintf(buf+len, "Duplex mode target     : ");
		switch (work8[27]) {
		case 0:
			len += sprintf(buf+len, "Half duplex\n");
			break;
		case 1:
			len += sprintf(buf+len, "Full duplex\n");
			break;
		default:
			len += sprintf(buf+len, "\n");
			break;
		}

		len += sprintf(buf+len, "Connector type target  : %s\n",
			       i2o_get_connector_type(work32[7]));
		len += sprintf(buf+len, "Connection type target : %s\n",
			       i2o_get_connection_type(work32[8]));
	}

	spin_unlock(&i2o_proc_lock);
	return len;
}

/* LAN group 0006h - Alternate address (table) */
int i2o_proc_read_lan_alt_addr(char *buf, char **start, off_t offset, int len,
			       int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u32 field32[64];
	static u8 *field8 = (u8 *)field32;
	static u16 *field16 = (u16 *)field32;
	int token;
	int i;

	spin_lock(&i2o_proc_lock);	
	len = 0;

	token = i2o_query_table(I2O_PARAMS_TABLE_GET,
			d->controller, d->lct_data->tid, 0x0006, -1, 
			NULL, 0, &field32, sizeof(field32));

	if (token<0)
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
		default:
			len += sprintf(buf, "Error reading field. BlockStatus %d\n",
				       token);
			spin_unlock(&i2o_proc_lock);
			return len;
		}

	len += sprintf(buf,"RowCount=%d, MoreFlag=%d\n", field16[0],
		       field16[1]);

	field8=(u8 *)&field16[2];

	for(i=0; i<field16[0]; i++, field8+=8)
	{
		len += sprintf(buf+len, "Alternate address[%d]: "
			       "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
			       i,
			       field8[0], field8[1], field8[2],
			       field8[3], field8[4], field8[5],
			       field8[6], field8[7]);
	}

	spin_unlock(&i2o_proc_lock);
	return len;
}


/* LAN group 0007h - Transmit info (scalar) */
int i2o_proc_read_lan_tx_info(char *buf, char **start, off_t offset, int len, 
			      int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u32 work32[8];
	int token;

	spin_lock(&i2o_proc_lock);	
	len = 0;

	token = i2o_query_scalar(d->controller, d->lct_data->tid,
				 0x0007, -1, &work32, 8*4);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf,     "Max SG Elements per packet : %d\n", work32[0]);
	len += sprintf(buf+len, "Max SG Elements per chain  : %d\n", work32[1]);
	len += sprintf(buf+len, "Max outstanding packets    : %d\n", work32[2]);
	len += sprintf(buf+len, "Max packets per request    : %d\n", work32[3]);

	len += sprintf(buf+len, "Tx modes :\n");
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
		len += sprintf(buf+len, "    IPv4 checksum\n");
	if(work32[4]&0x00000200)
		len += sprintf(buf+len, "    TCP checksum\n");
	if(work32[4]&0x00000400)
		len += sprintf(buf+len, "    UDP checksum\n");
	if(work32[4]&0x00000800)
		len += sprintf(buf+len, "    RSVP checksum\n");
	if(work32[4]&0x00001000)
		len += sprintf(buf+len, "    ICMP checksum\n");
	if (d->i2oversion == 0x00) {
		if(work32[4]&0x00008000)
			len += sprintf(buf+len, "    Loopback enabled\n");
		if(work32[4]&0x00010000)
			len += sprintf(buf+len, "    Loopback suppression enabled\n");
	} else {
		if(work32[4]&0x00010000)
			len += sprintf(buf+len, "    Loopback enabled\n");
		if(work32[4]&0x00020000)
			len += sprintf(buf+len, "    Loopback suppression enabled\n");
	}

	spin_unlock(&i2o_proc_lock);
	return len;
}

/* LAN group 0008h - Receive info (scalar) */
int i2o_proc_read_lan_rx_info(char *buf, char **start, off_t offset, int len, 
			      int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u32 work32[8];
	int token;

	spin_lock(&i2o_proc_lock);	
	len = 0;

	token = i2o_query_scalar(d->controller, d->lct_data->tid,
				 0x0008, -1, &work32, 8*4);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf,"Max size of chain element : %d\n", work32[0]);
	len += sprintf(buf+len, "Max number of buckets     : %d\n", work32[1]);

	if (d->i2oversion > 0x00) { /* not in 1.5 */
		len += sprintf(buf+len, "RxModes                   : %d\n", work32[2]);
		len += sprintf(buf+len, "RxMaxBucketsReply         : %d\n", work32[3]);
		len += sprintf(buf+len, "RxMaxPacketsPerBuckets    : %d\n", work32[4]);
		len += sprintf(buf+len, "RxMaxPostBuckets          : %d\n", work32[5]);
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

	token = i2o_query_scalar(d->controller, d->lct_data->tid,
				 0x0100, -1, &work64, 9*8);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf,     "Tx packets       : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[0]));
	len += sprintf(buf+len, "Tx bytes         : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[1]));
	len += sprintf(buf+len, "Rx packets       : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[2]));
	len += sprintf(buf+len, "Rx bytes         : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[3]));
	len += sprintf(buf+len, "Tx errors        : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[4]));
	len += sprintf(buf+len, "Rx errors        : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[5]));
	len += sprintf(buf+len, "Rx dropped       : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[6]));
	len += sprintf(buf+len, "Adapter resets   : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[7]));
	len += sprintf(buf+len, "Adapter suspends : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[8]));

	spin_unlock(&i2o_proc_lock);
	return len;
}


/* LAN group 0180h - Supported Optional Historical Statistics (scalar) */
int i2o_proc_read_lan_supp_opt_stats(char *buf, char **start, off_t offset,
				     int len, int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u64 work64[4];
	int token;

	spin_lock(&i2o_proc_lock);	

	len = 0;

	token = i2o_query_scalar(d->controller, d->lct_data->tid,
				 0x0180, -1, &work64, 4*8);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	if (d->i2oversion == 0x00)
		len += sprintf(buf, "Supported stats : " FMT_U64_HEX " \n",
			       U64_VAL(&work64[0]));
	else
	{
		len += sprintf(buf, "Supported stats (0182h) : " FMT_U64_HEX " \n",
			       U64_VAL(&work64[1]));
		len += sprintf(buf, "Supported stats (0183h) : " FMT_U64_HEX " \n",
			       U64_VAL(&work64[2]));
		len += sprintf(buf, "Supported stats (0184h) : " FMT_U64_HEX " \n",
			       U64_VAL(&work64[3]));
	}

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

	token = i2o_query_scalar(d->controller, d->lct_data->tid, 
				 0x0182, -1, &work64, 9*8);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf,     "TxRetryCount           : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[0]));
	len += sprintf(buf+len, "DirectedBytesTx        : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[1]));
	len += sprintf(buf+len, "DirectedPacketsTx      : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[2]));
	len += sprintf(buf+len, "MulticastBytesTx       : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[3]));
	len += sprintf(buf+len, "MulticastPacketsTx     : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[4]));
	len += sprintf(buf+len, "BroadcastBytesTx       : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[5]));
	len += sprintf(buf+len, "BroadcastPacketsTx     : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[6]));
	len += sprintf(buf+len, "TotalGroupAddrTxCount  : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[7]));
	len += sprintf(buf+len, "TotalTxPacketsTooShort : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[8]));

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

	token = i2o_query_scalar(d->controller, d->lct_data->tid,
				 0x0183, -1, &work64, 11*8);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf,     "ReceiveCRCErrorCount     : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[0]));
	len += sprintf(buf+len, "DirectedBytesRx          : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[1]));
	len += sprintf(buf+len, "DirectedPacketsRx        : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[2]));
	len += sprintf(buf+len, "MulticastBytesRx         : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[3]));
	len += sprintf(buf+len, "MulticastPacketsRx       : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[4]));
	len += sprintf(buf+len, "BroadcastBytesRx         : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[5]));
	len += sprintf(buf+len, "BroadcastPacketsRx       : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[6]));
	len += sprintf(buf+len, "TotalGroupAddrRxCount    : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[7]));
	len += sprintf(buf+len, "TotalRxPacketsTooShort   : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[8]));
	len += sprintf(buf+len, "TotalRxPacketsTooLong    : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[9]));
	len += sprintf(buf+len, "TotalRuntPacketsReceived : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[10]));

	spin_unlock(&i2o_proc_lock);
	return len;
}

/* LAN group 0200h - Required Ethernet Statistics (scalar) */
int i2o_proc_read_lan_eth_stats(char *buf, char **start, off_t offset,
				int len, int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u64 work64[8];
	int token;


	spin_lock(&i2o_proc_lock);
	
	len = 0;

	token = i2o_query_scalar(d->controller, d->lct_data->tid, 
				 0x0200, -1, &work64, 8*8);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf,     "Rx alignment errors    : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[0]));
	len += sprintf(buf+len, "Tx one collisions      : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[1]));
	len += sprintf(buf+len, "Tx multicollisions     : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[2]));
	len += sprintf(buf+len, "Tx deferred            : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[3]));
	len += sprintf(buf+len, "Tx late collisions     : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[4]));
	len += sprintf(buf+len, "Tx max collisions      : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[5]));
	len += sprintf(buf+len, "Tx carrier lost        : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[6]));
	len += sprintf(buf+len, "Tx excessive deferrals : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[7]));

	spin_unlock(&i2o_proc_lock);
	return len;
}

/* LAN group 0280h - Supported Ethernet Historical Statistics (scalar) */
int i2o_proc_read_lan_supp_eth_stats(char *buf, char **start, off_t offset,
				     int len, int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u64 work64[1];
	int token;

	spin_lock(&i2o_proc_lock);	

	len = 0;

	token = i2o_query_scalar(d->controller, d->lct_data->tid, 
				 0x0280, -1, &work64, 8);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf, "Supported stats : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[0]));

	spin_unlock(&i2o_proc_lock);
	return len;
}

/* LAN group 0281h - Optional Ethernet Historical Statistics (scalar) */
int i2o_proc_read_lan_opt_eth_stats(char *buf, char **start, off_t offset,
				    int len, int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u64 work64[3];
	int token;

	spin_lock(&i2o_proc_lock);	

	len = 0;

	token = i2o_query_scalar(d->controller, d->lct_data->tid,
				 0x0281, -1, &work64, 3*8);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf, "Rx overrun           : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[0]));
	len += sprintf(buf, "Tx underrun          : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[1]));
	len += sprintf(buf, "Tx heartbeat failure : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[2]));

	spin_unlock(&i2o_proc_lock);
	return len;
}

/* LAN group 0300h - Required Token Ring Statistics (scalar) */
int i2o_proc_read_lan_tr_stats(char *buf, char **start, off_t offset,
			       int len, int *eof, void *data)
{
	struct i2o_device *d = (struct i2o_device*)data;
	static u64 work64[13];
	int token;

	static char *ring_status[] =
	{
		"",
		"",
		"",
		"",
		"",
		"Ring Recovery",
		"Single Station",
		"Counter Overflow",
		"Remove Received",
		"",
		"Auto-Removal Error 1",
		"Lobe Wire Fault",
		"Transmit Beacon",
		"Soft Error",
		"Hard Error",
		"Signal Loss"
	};

	spin_lock(&i2o_proc_lock);
	
	len = 0;

	token = i2o_query_scalar(d->controller, d->lct_data->tid, 
				 0x0300, -1, &work64, 13*8);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf,     "LineErrors          : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[0]));
	len += sprintf(buf+len, "LostFrames          : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[1]));
	len += sprintf(buf+len, "ACError             : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[2]));
	len += sprintf(buf+len, "TxAbortDelimiter    : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[3]));
	len += sprintf(buf+len, "BursErrors          : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[4]));
	len += sprintf(buf+len, "FrameCopiedErrors   : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[5]));
	len += sprintf(buf+len, "FrequencyErrors     : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[6]));
	len += sprintf(buf+len, "InternalErrors      : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[7]));
	len += sprintf(buf+len, "LastRingStatus      : %s\n", ring_status[work64[8]]);
	len += sprintf(buf+len, "TokenError          : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[9]));
	len += sprintf(buf+len, "UpstreamNodeAddress : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[10]));
	len += sprintf(buf+len, "LastRingID          : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[11]));
	len += sprintf(buf+len, "LastBeaconType      : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[12]));

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

	token = i2o_query_scalar(d->controller, d->lct_data->tid,
				 0x0400, -1, &work64, 11*8);
	if(token < 0)
	{
		len += sprintf(buf, "Timeout waiting for reply from IOP\n");
		spin_unlock(&i2o_proc_lock);
		return len;
	}

	len += sprintf(buf,     "ConfigurationState : %s\n", conf_state[work64[0]]);
	len += sprintf(buf+len, "UpstreamNode       : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[1]));
	len += sprintf(buf+len, "DownStreamNode     : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[2]));
	len += sprintf(buf+len, "FrameErrors        : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[3]));
	len += sprintf(buf+len, "FramesLost         : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[4]));
	len += sprintf(buf+len, "RingMgmtState      : %s\n", ring_state[work64[5]]);
	len += sprintf(buf+len, "LCTFailures: " FMT_U64_HEX "\n",
		       U64_VAL(&work64[6]));
	len += sprintf(buf+len, "LEMRejects         : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[7]));
	len += sprintf(buf+len, "LEMCount           : " FMT_U64_HEX "\n",
		       U64_VAL(&work64[8]));
	len += sprintf(buf+len, "LConnectionState   : %s\n",
		       link_state[work64[9]]);

	spin_unlock(&i2o_proc_lock);
	return len;
}

static int i2o_proc_create_entries(void *data, i2o_proc_entry *pentry,
				   struct proc_dir_entry *parent)
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
		sprintf(buff, "%0#5x", dev->lct_data->tid);

		dir1 = create_proc_entry(buff, S_IFDIR, dir);
		dev->proc_entry = dir1;

		if(!dir1)
			printk(KERN_INFO "i2o_proc: Could not allocate proc dir\n");
		
		i2o_proc_create_entries(dev, generic_dev_entries, dir1);

		switch(dev->lct_data->class_id)
		{
		case I2O_CLASS_SCSI_PERIPHERAL:
		case I2O_CLASS_RANDOM_BLOCK_STORAGE:
			i2o_proc_create_entries(dev, rbs_dev_entries, dir1);
			break;
		case I2O_CLASS_LAN:
			i2o_proc_create_entries(dev, lan_entries, dir1);
			switch(dev->lct_data->sub_class)
			{
			case I2O_LAN_ETHERNET:
				i2o_proc_create_entries(dev, lan_eth_entries,
							dir1);
				break;
			case I2O_LAN_FDDI:
				i2o_proc_create_entries(dev, lan_fddi_entries,
							dir1);
				break;
			case I2O_LAN_TR:
				i2o_proc_create_entries(dev, lan_tr_entries,
							dir1);
				break;
			default:
				break;
			}
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
	char dev_id[10];
	struct proc_dir_entry *de;
	struct i2o_device *dev;

	/* Remove unused device entries */
	for(dev=pctrl->devices; dev; dev=dev->next)
	{
		de=dev->proc_entry;
		sprintf(dev_id, "%0#5x", dev->lct_data->tid);

		/* Would it be safe to remove _files_ even if they are in use? */
		if((de) && (!de->count))
		{
			i2o_proc_remove_entries(generic_dev_entries, de);

			switch(dev->lct_data->class_id)
			{
			case I2O_CLASS_SCSI_PERIPHERAL:
			case I2O_CLASS_RANDOM_BLOCK_STORAGE:
				i2o_proc_remove_entries(rbs_dev_entries, de);
				break;
			case I2O_CLASS_LAN:
				i2o_proc_remove_entries(lan_entries, de);
				switch(dev->lct_data->sub_class)
				{
				case I2O_LAN_ETHERNET:
					i2o_proc_remove_entries(lan_eth_entries, de);
					break;
				case I2O_LAN_FDDI:
					i2o_proc_remove_entries(lan_fddi_entries, de);
					break;
				case I2O_LAN_TR:
					i2o_proc_remove_entries(lan_tr_entries, de);
					break;
				}
			}
			remove_proc_entry(dev_id, parent);
		}
	}

	if(!pctrl->proc_entry->count)
	{
		sprintf(buff, "iop%d", pctrl->unit);

		i2o_proc_remove_entries(generic_iop_entries, pctrl->proc_entry);

		remove_proc_entry(buff, parent);
		pctrl->proc_entry = NULL;
	}
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
		{
			i2o_proc_add_controller(pctrl, i2o_proc_dir_root);
			i2o_unlock_controller(pctrl);
		}
	};

	return 0;
}

static int destroy_i2o_procfs(void)
{
	struct i2o_controller *pctrl = NULL;
	int i;

	for(i = 0; i < MAX_I2O_CONTROLLERS; i++)
	{
		pctrl = i2o_find_controller(i);
		if(pctrl)
		{
			i2o_proc_remove_controller(pctrl, i2o_proc_dir_root);
			i2o_unlock_controller(pctrl);
		}
	}

	if(!i2o_proc_dir_root->count)
		remove_proc_entry("i2o", 0);
	else
		return -1;

	return 0;
}
		
#ifdef MODULE
#define i2o_proc_init init_module
#endif

int __init i2o_proc_init(void)
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

#ifdef MODULE


MODULE_AUTHOR("Deepak Saxena");
MODULE_DESCRIPTION("I2O procfs Handler");

void cleanup_module(void)
{
	destroy_i2o_procfs();
	i2o_remove_handler(&i2o_proc_handler);
}
#endif
