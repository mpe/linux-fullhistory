#ifndef _LINUX_HDREG_H
#define _LINUX_HDREG_H

/*
 * This file contains some defines for the AT-hd-controller.
 * Various sources. Check out some definitions (see comments with
 * a ques).
 */

/* Hd controller regs. Ref: IBM AT Bios-listing */
#define HD_DATA		0x1f0	/* _CTL when writing */
#define HD_ERROR	0x1f1	/* see err-bits */
#define HD_NSECTOR	0x1f2	/* nr of sectors to read/write */
#define HD_SECTOR	0x1f3	/* starting sector */
#define HD_LCYL		0x1f4	/* starting cylinder */
#define HD_HCYL		0x1f5	/* high byte of starting cyl */
#define HD_CURRENT	0x1f6	/* 101dhhhh , d=drive, hhhh=head */
#define HD_STATUS	0x1f7	/* see status-bits */
#define HD_PRECOMP HD_ERROR	/* same io address, read=error, write=precomp */
#define HD_COMMAND HD_STATUS	/* same io address, read=status, write=cmd */

#define HD_CMD		0x3f6

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


/* HDIO_GETGEO is the preferred choice - HDIO_REQ will be removed at some
   later date */
#define HDIO_REQ 0x301
#define HDIO_GETGEO 0x301
struct hd_geometry {
      unsigned char heads;
      unsigned char sectors;
      unsigned short cylinders;
      unsigned long start;
};
#define HDIO_GETUNMASKINTR	0x302
#define HDIO_SETUNMASKINTR	0x303
#define HDIO_GETMULTCOUNT	0x304
#define HDIO_SETMULTCOUNT	0x305
#define HDIO_SETFEATURE  	0x306
#define HDIO_GETIDENTITY 	0x307
#endif

/* structure returned by HDIO_GETIDENTITY, as per ASC X3T9.2 rev 4a */
struct hd_driveid {
	unsigned short	config;		/* lots of obsolete bit flags */
	unsigned short	cyls;		/* "physical" cyls */
	unsigned short	reserved0;	/* reserved */
	unsigned short	heads;		/* "physical" heads */
	unsigned short	track_bytes;	/* unformatted bytes per track */
	unsigned short	sector_bytes;	/* unformatted bytes per sector */
	unsigned short	sectors;	/* "physical" sectors per track */
	unsigned short	vendor0;	/* vendor unique */
	unsigned short	vendor1;	/* vendor unique */
	unsigned short	vendor2;	/* vendor unique */
	unsigned char	serial_no[20];	/* big_endian; 0 = not_specified */
	unsigned short	buf_type;
	unsigned short	buf_size;	/* 512 byte increments; 0 = not_specified */
	unsigned short	ecc_bytes;	/* for r/w long cmds; 0 = not_specified */
	unsigned char	fw_rev[8];	/* big_endian; 0 = not_specified */
	unsigned char	model[40];	/* big_endian; 0 = not_specified */
	unsigned char	max_multsect;	/* 0=not_implemented */
	unsigned char	vendor3;	/* vendor unique */
	unsigned short	dword_io;	/* 0=not_implemented; 1=implemented */
	unsigned char	vendor4;	/* vendor unique */
	unsigned char	capability;	/* bit0:DMA, bit1:LBA */
	unsigned short	reserved1;	/* reserved */
	unsigned char	vendor5;	/* vendor unique */
	unsigned char	tPIO;		/* 0=slow, 1=medium, 2=fast */
	unsigned char	vendor6;	/* vendor unique */
	unsigned char	tDMA;		/* 0=slow, 1=medium, 2=fast */
	unsigned short	cur_valid;	/* when (bit0==1) use logical geom */
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
	/* unsigned short reserved2[64];*/	/* reserved */
	/* unsigned short vendor7  [32];*/	/* vendor unique */
	/* unsigned short reserved3[96];*/	/* reserved */
};
