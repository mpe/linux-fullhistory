/*
 *	ultrastor.c	(C) 1991 David B. Gentzel
 *	Low-level scsi driver for UltraStor 14F
 *	by David B. Gentzel, Whitfield Software Services, Carnegie, PA
 *	    (gentzel@nova.enet.dec.com)
 *	Thanks to UltraStor for providing the necessary documentation
 */

/* ??? Caveats:
   This driver is VERY stupid.  It takes no advantage of much of the power of
   the UltraStor controller.  We just sit-and-spin while waiting for commands
   to complete.  I hope to go back and beat it into shape, but PLEASE, anyone
   else who would like to, please make improvements! */

#include <linux/config.h>

#ifdef CONFIG_SCSI_ULTRASTOR

#include <stddef.h>

#include <linux/string.h>
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/hdreg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#include "ultrastor.h"
#include "scsi.h"
#include "hosts.h"

#define VERSION "1.0 alpha"

#define ARRAY_SIZE(arr) (sizeof (arr) / sizeof (arr)[0])
#define BYTE(num, n) ((unsigned char)((unsigned int)(num) >> ((n) * 8)))

/* Simply using "unsigned long" in these structures won't work as it causes
   alignment.  Perhaps the "aligned" attribute may be used in GCC 2.0 to get
   around this, but for now I use this hack. */
typedef struct {
    unsigned char bytes[4];
} Longword;

/* Used to store configuration info read from config i/o registers.  Most of
   this is not used yet, but might as well save it. */
struct config {
    struct {
	unsigned char bios_segment: 3;
	unsigned char reserved: 1;
	unsigned char interrupt: 2;
	unsigned char dma_channel: 2;
    } config_1;
    struct {
	unsigned char ha_scsi_id: 3;
	unsigned char mapping_mode: 2;
	unsigned char bios_drive_number: 1;
	unsigned char tfr_port: 2;
    } config_2;
};

/* MailBox SCSI Command Packet.  Basic command structure for communicating
   with controller. */
struct mscp {
    unsigned char opcode: 3;		/* type of command */
    unsigned char xdir: 2;		/* data transfer direction */
    unsigned char dcn: 1;		/* disable disconnect */
    unsigned char ca: 1;		/* use cache (if available) */
    unsigned char sg: 1;		/* scatter/gather operation */
    unsigned char target_id: 3;		/* target SCSI id */
    unsigned char ch_no: 2;		/* SCSI channel (always 0 for 14f) */
    unsigned char lun: 3;		/* logical unit number */
    Longword transfer_data;		/* transfer data pointer */
    Longword transfer_data_length;	/* length in bytes */
    Longword command_link;		/* for linking command chains */
    unsigned char scsi_command_link_id;	/* identifies command in chain */
    unsigned char number_of_sg_list;	/* (if sg is set) 8 bytes per list */
    unsigned char length_of_sense_byte;
    unsigned char length_of_scsi_cdbs;	/* 6, 10, or 12 */
    unsigned char scsi_cdbs[12];	/* SCSI commands */
    unsigned char adapter_status;	/* non-zero indicates HA error */
    unsigned char target_status;	/* non-zero indicates target error */
    Longword sense_data;
};

/* Allowed BIOS base addresses for 14f (NULL indicates reserved) */
static const void *const bios_segment_table[8] = {
    NULL,	     (void *)0xC4000, (void *)0xC8000, (void *)0xCC000,
    (void *)0xD0000, (void *)0xD4000, (void *)0xD8000, (void *)0xDC000,
};

/* Allowed IRQs for 14f */
static const unsigned char interrupt_table[4] = { 15, 14, 11, 10 };

/* Allowed DMA channels for 14f (0 indicates reserved) */
static const unsigned char dma_channel_table[4] = { 5, 6, 7, 0 };

#if 0	/* Not currently used, head/sector mappings allowed by 14f */
static const struct {
    unsigned char heads;
    unsigned char sectors;
} mapping_table[4] = { { 16, 63 }, { 64, 32 }, { 64, 63 }, { 0, 0 } };
#endif

/* Config info */
static struct config config;

/* Our index in the host adapter array maintained by higher-level driver */
static int host_number;

/* PORT_ADDRESS is first port address used for i/o of messages. */
#ifdef PORT_OVERRIDE
# define PORT_ADDRESS PORT_OVERRIDE
#else
static unsigned short port_address = 0;
# define PORT_ADDRESS port_address
#endif

static volatile int aborted = 0;

#ifndef PORT_OVERRIDE
static const unsigned short ultrastor_ports[] = {
    0x330, 0x340, 0x310, 0x230, 0x240, 0x210, 0x130, 0x140,
};
#endif

static const struct {
    const char *signature;
    size_t offset;
    size_t length;
} signatures[] = {
    { "SBIOS 1.01 COPYRIGHT (C) UltraStor Corporation,1990-1992.", 0x10, 57 },
};

int ultrastor_14f_detect(int hostnum)
{
    size_t i;
    unsigned char in_byte;
    const void *base_address;

#ifdef DEBUG
    printk("ultrastor_14f_detect: called\n");
#endif

#ifndef PORT_OVERRIDE
/* ??? This is easy to implement, but I'm not sure how "friendly" it is to
   go off and read random i/o ports. */
# error Not implemented!
#endif

    if (!PORT_ADDRESS) {
#ifdef DEBUG
	printk("ultrastor_14f_detect: no port address found!\n");
#endif
	return FALSE;
    }

#ifdef DEBUG
    printk("ultrastor_14f_detect: port address = %X\n", PORT_ADDRESS);
#endif

    in_byte = inb(PRODUCT_ID(PORT_ADDRESS + 0));
    if (in_byte != US14F_PRODUCT_ID_0) {
#ifdef DEBUG
	printk("ultrastor_14f_detect: unknown product ID 0 - %02X\n", in_byte);
#endif
	return FALSE;
    }
    in_byte = inb(PRODUCT_ID(PORT_ADDRESS + 1));
    /* Only upper nibble is defined for Product ID 1 */
    if ((in_byte & 0xF0) != US14F_PRODUCT_ID_1) {
#ifdef DEBUG
	printk("ultrastor_14f_detect: unknown product ID 1 - %02X\n", in_byte);
#endif
	return FALSE;
    }

    /* All above tests passed, must be the right thing.  Get some useful
       info. */
    *(char *)&config.config_1 = inb(CONFIG(PORT_ADDRESS + 0));
    *(char *)&config.config_2 = inb(CONFIG(PORT_ADDRESS + 1));

    /* To verify this card, we simply look for the UltraStor SCSI from the
       BIOS version notice. */
    base_address = bios_segment_table[config.config_1.bios_segment];
    if (base_address != NULL) {
	int found = 0;

	for (i = 0; !found && i < ARRAY_SIZE(signatures); i++)
	    if (memcmp((char *)base_address + signatures[i].offset,
		       signatures[i].signature, signatures[i].length))
		found = 1;
	if (!found)
	    base_address = NULL;
    }
    if (!base_address) {
#ifdef DEBUG
	printk("ultrastor_14f_detect: not detected.\n");
#endif
	return FALSE;
    }

    /* Final consistancy check, verify previous info. */
    if (!dma_channel_table[config.config_1.dma_channel]
	|| !(config.config_2.tfr_port & 0x2)) {
#ifdef DEBUG
	printk("ultrastor_14f_detect: consistancy check failed\n");
#endif
	return FALSE;
    }

    /* If we were TRULY paranoid, we could issue a host adapter inquiry
       command here and verify the data returned.  But frankly, I'm
       exhausted! */

    /* Finally!  Now I'm satisfied... */
#ifdef DEBUG
    printk("ultrastor_14f_detect: detect succeeded\n"
	   "  BIOS segment: %05X\n"
	   "  Interrupt: %d\n"
	   "  DMA channel: %d\n"
	   "  H/A SCSI ID: %d\n",
	   base_address, interrupt_table[config.config_1.interrupt],
	   dma_channel_table[config.config_1.dma_channel],
	   config.config_2.ha_scsi_id);
#endif
    host_number = hostnum;
    scsi_hosts[hostnum].this_id = config.config_2.ha_scsi_id;
    return TRUE;
}

const char *ultrastor_14f_info(void)
{
    return "UltraStor 14F SCSI driver version "
	   VERSION
	   " by David B. Gentzel\n";
}

#if 0
int ultrastor_14f_queuecommand(unsigned char target, const void *cmnd,
			       void *buff, int bufflen, void (*done)(int, int))
#else
int ultrastor_14f_command(unsigned char target, const void *cmnd,
			  void *buff, int bufflen)
#endif
{
    struct mscp mscp = {
	OP_SCSI, DTD_SCSI, FALSE, TRUE, FALSE,
	target, 0, 0 /* LUN??? */,
	*(Longword *)&buff,
	*(Longword *)&bufflen,
	{ 0, 0, 0, 0 },
	0,
	0,
	0,
	((*(char *)cmnd <= 0x1F) ? 6 : 10),
	{ 0 },	/* Filled in via memcpy below */
	0,
	0,
	{ 0, 0, 0, 0 }
    };
    unsigned char in_byte;

    memcpy(mscp.scsi_cdbs, cmnd, mscp.length_of_scsi_cdbs);

    /* Find free OGM slot (OGMINT bit is 0) */
    do
	in_byte = inb(LCL_DOORBELL_INTR(PORT_ADDRESS));
    while (!aborted && (in_byte & 1));
    if (aborted)
	/* ??? is this right? */
	return (aborted << 16);

    /* Store pointer in OGM address bytes */
    outb(BYTE(&mscp, 0), OGM_DATA_PTR(PORT_ADDRESS + 0));
    outb(BYTE(&mscp, 1), OGM_DATA_PTR(PORT_ADDRESS + 1));
    outb(BYTE(&mscp, 2), OGM_DATA_PTR(PORT_ADDRESS + 2));
    outb(BYTE(&mscp, 3), OGM_DATA_PTR(PORT_ADDRESS + 3));

    /* Issue OGM interrupt */
    outb(0x1, LCL_DOORBELL_INTR(PORT_ADDRESS));

    /* Wait for ICM interrupt */
    do
	in_byte = inb(SYS_DOORBELL_INTR(PORT_ADDRESS));
    while (!aborted && !(in_byte & 1));
    if (aborted)
	/* ??? is this right? */
	return (aborted << 16);

    /* Clean ICM slot (set ICMINT bit to 0) */
    outb(0x1, SYS_DOORBELL_INTR(PORT_ADDRESS));

    /* ??? not right, but okay for now? */
    return (mscp.adapter_status << 16) | mscp.target_status;
}

int ultrastor_14f_abort(int code)
{
    aborted = (code ? code : DID_ABORT);
    return 0;
}

int ultrastor_14f_reset(void)
{
    unsigned char in_byte;

#ifdef DEBUG
    printk("ultrastor_14f_reset: called\n");
#endif

    /* Issue SCSI BUS reset */
    outb(0x20, LCL_DOORBELL_INTR(PORT_ADDRESS));
    /* Wait for completion... */
    do
	in_byte = inb(LCL_DOORBELL_INTR(PORT_ADDRESS));
    while (in_byte & 0x20);

    aborted = DID_RESET;

#ifdef DEBUG
    printk("ultrastor_14f_reset: returning\n");
#endif
    return 0;
}

#endif
