#ifndef _LINUX_HDREG_H
#define _LINUX_HDREG_H

#include <linux/config.h>

/*
 * This file contains some defines for the AT-hd-controller.
 * Various sources. Check out some definitions (see comments with
 * a ques).
 */

/* Hd controller regs. Ref: IBM AT Bios-listing */
/* For a second IDE interface, xor all addresses with 0x80 */
#define HD_DATA		0x1f0	/* _CTL when writing */
#define HD_ERROR	0x1f1	/* see err-bits */
#define HD_NSECTOR	0x1f2	/* nr of sectors to read/write */
#define HD_SECTOR	0x1f3	/* starting sector */
#define HD_LCYL		0x1f4	/* starting cylinder */
#define HD_HCYL		0x1f5	/* high byte of starting cyl */
#define HD_CURRENT	0x1f6	/* 101dhhhh , d=drive, hhhh=head */
#define HD_STATUS	0x1f7	/* see status-bits */
#define HD_FEATURE HD_ERROR	/* same io address, read=error, write=feature */
#define HD_PRECOMP HD_FEATURE	/* obsolete use of this port - predates IDE */
#define HD_COMMAND HD_STATUS	/* same io address, read=status, write=cmd */

#define HD_CMD		0x3f6	/* used for resets */
#define HD_ALTSTATUS	0x3f6	/* same as HD_STATUS but doesn't clear irq */

/* Bits of HD_STATUS */
#define ERR_STAT	0x01
#define INDEX_STAT	0x02
#define ECC_STAT	0x04	/* Corrected error */
#define DRQ_STAT	0x08
#define SEEK_STAT	0x10
#define WRERR_STAT	0x20
#define READY_STAT	0x40
#define BUSY_STAT	0x80

/* Values for HD_COMMAND */
#define WIN_RESTORE		0x10
#define WIN_READ		0x20
#define WIN_WRITE		0x30
#define WIN_VERIFY		0x40
#define WIN_FORMAT		0x50
#define WIN_INIT		0x60
#define WIN_SEEK 		0x70
#define WIN_DIAGNOSE		0x90
#define WIN_SPECIFY		0x91
#define WIN_SETIDLE1		0xE3
#define WIN_SETIDLE2		0x97

#define WIN_PIDENTIFY		0xA1	/* identify ATA-PI device	*/
#define WIN_MULTREAD		0xC4	/* read multiple sectors	*/
#define WIN_MULTWRITE		0xC5	/* write multiple sectors	*/
#define WIN_SETMULT		0xC6	/* enable read multiple		*/
#define WIN_IDENTIFY		0xEC	/* ask drive to identify itself	*/
#define WIN_SETFEATURES		0xEF	/* set special drive features   */

/* Bits for HD_ERROR */
#define MARK_ERR	0x01	/* Bad address mark */
#define TRK0_ERR	0x02	/* couldn't find track 0 */
#define ABRT_ERR	0x04	/* Command aborted */
#define ID_ERR		0x10	/* ID field not found */
#define ECC_ERR		0x40	/* Uncorrectable ECC error */
#define	BBD_ERR		0x80	/* block marked bad */

struct hd_geometry {
      unsigned char heads;
      unsigned char sectors;
      unsigned short cylinders;
      unsigned long start;
};

/* hd/ide ctl's that pass (arg) ptrs to user space are numbered 0x30n/0x31n */
#define HDIO_GETGEO		0x301	/* get device geometry */
#define HDIO_REQ		HDIO_GETGEO	/* obsolete, use HDIO_GETGEO */
#define HDIO_GET_UNMASKINTR	0x302	/* get current unmask setting */
#define HDIO_GET_MULTCOUNT	0x304	/* get current IDE blockmode setting */
#define HDIO_GET_IDENTITY 	0x307	/* get IDE identification info */
#define HDIO_GET_KEEPSETTINGS 	0x308	/* get keep-settings-on-reset flag */
#define HDIO_GET_CHIPSET	0x309	/* get current interface type setting */
#define HDIO_DRIVE_CMD		0x31f	/* execute a special drive command */

/* hd/ide ctl's that pass (arg) non-ptr values are numbered 0x32n/0x33n */
#define HDIO_SET_MULTCOUNT	0x321	/* set IDE blockmode */
#define HDIO_SET_UNMASKINTR	0x322	/* permit other irqs during I/O */
#define HDIO_SET_KEEPSETTINGS	0x323	/* keep ioctl settings on reset */
#define HDIO_SET_CHIPSET	0x324	/* optimise driver for interface type */

/* structure returned by HDIO_GET_IDENTITY, as per ANSI ATA2 rev.2f spec */
struct hd_driveid {
	unsigned short	config;		/* lots of obsolete bit flags */
	unsigned short	cyls;		/* "physical" cyls */
	unsigned short	reserved2;	/* reserved (word 2) */
	unsigned short	heads;		/* "physical" heads */
	unsigned short	track_bytes;	/* unformatted bytes per track */
	unsigned short	sector_bytes;	/* unformatted bytes per sector */
	unsigned short	sectors;	/* "physical" sectors per track */
	unsigned short	vendor0;	/* vendor unique */
	unsigned short	vendor1;	/* vendor unique */
	unsigned short	vendor2;	/* vendor unique */
	unsigned char	serial_no[20];	/* 0 = not_specified */
	unsigned short	buf_type;
	unsigned short	buf_size;	/* 512 byte increments; 0 = not_specified */
	unsigned short	ecc_bytes;	/* for r/w long cmds; 0 = not_specified */
	unsigned char	fw_rev[8];	/* 0 = not_specified */
	unsigned char	model[40];	/* 0 = not_specified */
	unsigned char	max_multsect;	/* 0=not_implemented */
	unsigned char	vendor3;	/* vendor unique */
	unsigned short	dword_io;	/* 0=not_implemented; 1=implemented */
	unsigned char	vendor4;	/* vendor unique */
	unsigned char	capability;	/* bits 0:DMA 1:LBA 2:IORDYsw 3:IORDYsup*/
	unsigned short	reserved50;	/* reserved (word 50) */
	unsigned char	vendor5;	/* vendor unique */
	unsigned char	tPIO;		/* 0=slow, 1=medium, 2=fast */
	unsigned char	vendor6;	/* vendor unique */
	unsigned char	tDMA;		/* 0=slow, 1=medium, 2=fast */
	unsigned short	field_valid;	/* bits 0:cur_ok 1:eide_ok */
	unsigned short	cur_cyls;	/* logical cylinders */
	unsigned short	cur_heads;	/* logical heads */
	unsigned short	cur_sectors;	/* logical sectors per track */
	unsigned short	cur_capacity0;	/* logical total sectors on drive */
	unsigned short	cur_capacity1;	/*  (2 words, misaligned int)     */
	unsigned char	multsect;	/* current multiple sector count */
	unsigned char	multsect_valid;	/* when (bit0==1) multsect is ok */
	unsigned int	lba_capacity;	/* total number of sectors */
	unsigned short	dma_1word;	/* single-word dma info */
	unsigned short	dma_mword;	/* multiple-word dma info */
	unsigned short  eide_pio_modes; /* bits 0:mode3 1:mode4 */
	unsigned short  eide_dma_min;	/* min mword dma cycle time (ns) */
	unsigned short  eide_dma_time;	/* recommended mword dma cycle time (ns) */
	unsigned short  eide_pio;       /* min cycle time (ns), no IORDY  */
	unsigned short  eide_pio_iordy; /* min cycle time (ns), with IORDY */
	unsigned short  reserved69;	/* reserved (word 69) */
	unsigned short  reserved70;	/* reserved (word 70) */
	/* unsigned short reservedxx[57];*/	/* reserved (words 71-127) */
	/* unsigned short vendor7  [32];*/	/* vendor unique (words 128-159) */
	/* unsigned short reservedyy[96];*/	/* reserved (words 160-255) */
};

/*
 * These routines are used for kernel command line parameters from main.c:
 */
#ifdef CONFIG_BLK_DEV_HD
void hd_setup(char *, int *);
#endif	/* CONFIG_BLK_DEV_HD */
#ifdef CONFIG_BLK_DEV_IDE
void ide_setup(char *, int *);
void hda_setup(char *, int *);
void hdb_setup(char *, int *);
void hdc_setup(char *, int *);
void hdd_setup(char *, int *);
#endif	/* CONFIG_BLK_DEV_IDE */

#endif	/* _LINUX_HDREG_H */
