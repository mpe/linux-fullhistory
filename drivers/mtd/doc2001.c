/* Linux driver for Disk-On-Chip Millennium */
/* (c) 1999 Machine Vision Holdings, Inc.   */
/* Author: David Woodhouse <dwmw2@mvhi.com> */
/* $Id: doc2001.c,v 1.7 2000/07/13 10:41:39 dwmw2 Exp $ */

#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/errno.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/malloc.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/types.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/doc2000.h>

static struct {
	char * name;
	int manufacture_id;
	int model_id;
	int chipshift;
} nand_flash_ids[] = {
	{"Toshiba TC5816BDC",    NAND_MFR_TOSHIBA, 0x64, 21},
	{"Toshiba TC5832DC",     NAND_MFR_TOSHIBA, 0x6b, 22},
	{"Toshiba TH58V128DC",   NAND_MFR_TOSHIBA, 0x73, 24},
	{"Toshiba TC58256FT/DC", NAND_MFR_TOSHIBA, 0x75, 25},
	{"Toshiba TC58V32DC",    NAND_MFR_TOSHIBA, 0xe5, 22},
	{"Toshiba TC58V64DC",    NAND_MFR_TOSHIBA, 0xe6, 23},
	{"Toshiba TC58V16BDC",   NAND_MFR_TOSHIBA, 0xea, 21},
	{"Samsung KM29N16000",   NAND_MFR_SAMSUNG, 0x64, 21},
	{"Samsung KM29U128T",    NAND_MFR_SAMSUNG, 0x73, 24},
	{"Samsung KM29U256T",    NAND_MFR_SAMSUNG, 0x75, 25},
	{"Samsung KM29W32000",   NAND_MFR_SAMSUNG, 0xe3, 22},
	{"Samsung KM29U64000",   NAND_MFR_SAMSUNG, 0xe6, 23},
	{"Samsung KM29W16000",   NAND_MFR_SAMSUNG, 0xea, 21},
	{NULL,}
};

static int doc_read (struct mtd_info *mtd, loff_t from, size_t len,
		     size_t *retlen, u_char *buf);
static int doc_write (struct mtd_info *mtd, loff_t to, size_t len,
		      size_t *retlen, const u_char *buf);
static int doc_read_ecc (struct mtd_info *mtd, loff_t from, size_t len,
			 size_t *retlen, u_char *buf, u_char *eecbuf);
static int doc_write_ecc (struct mtd_info *mtd, loff_t to, size_t len,
			  size_t *retlen, const u_char *buf, u_char *eccbuf);
static int doc_read_oob(struct mtd_info *mtd, loff_t ofs, size_t len,
			size_t *retlen, u_char *buf);
static int doc_write_oob(struct mtd_info *mtd, loff_t ofs, size_t len,
			 size_t *retlen, const u_char *buf);
static int doc_erase (struct mtd_info *mtd, struct erase_info *instr);

static struct mtd_info *docmillist = NULL;

static void DoC_Delay(unsigned long docptr, unsigned short cycles)
{
	volatile char dummy;
	int i;

	for (i = 0; i < cycles; i++)
		dummy = ReadDOC(docptr, NOP);
}

/* DOC_WaitReady: Wait for RDY line to be asserted by the flash chip */
static int _DoC_WaitReady(unsigned long docptr)
{
	unsigned short c = 0xffff;

	/* Out-of-line routine to wait for chip response */
	while (!(ReadDOC(docptr, CDSNControl) & CDSN_CTRL_FR_B) && --c)
		;

	return (c == 0);
}

static __inline__ int DoC_WaitReady(unsigned long docptr) 
{
	/* This is inline, to optimise the common case, where it's ready instantly */
	int ret = 0;

	/* 4 read form NOP register should be issued in prior to the read from CDSNControl
	   see Software Requirement 11.4 item 2. */
	DoC_Delay(docptr, 4);

	if (!(ReadDOC(docptr, CDSNControl) & CDSN_CTRL_FR_B))
		/* Call the out-of-line routine to wait */
		ret = _DoC_WaitReady(docptr);

	/* issue 2 read from NOP register after reading from CDSNControl register
	   see Software Requirement 11.4 item 2. */
	DoC_Delay(docptr, 2);

	return ret;
}

/* DoC_Command: Send a flash command to the flash chip through the CDSN Slow IO register to bypass
   the internal pipeline. Each of 4 delay cycles (read from the NOP register) is required after
   writing to CDSN Control register, see Software Requirement 11.4 item 3. */
static __inline__ void DoC_Command(unsigned long docptr, unsigned char command,
				   unsigned char xtraflags)
{
	/* Assert the CLE (Command Latch Enable) line to the flash chip */
 	WriteDOC(xtraflags | CDSN_CTRL_CLE | CDSN_CTRL_CE, docptr, CDSNControl);
	DoC_Delay(docptr, 4);

	/* Send the command */
	WriteDOC(command, docptr, CDSNSlowIO);
	WriteDOC(command, docptr, Mil_CDSN_IO);
 
	/* Lower the CLE line */
	WriteDOC(xtraflags | CDSN_CTRL_CE, docptr, CDSNControl);
	DoC_Delay(docptr, 4);
}

/* DoC_Address: Set the current address for the flash chip through the CDSN Slow IO register to bypass
   the internal pipeline. Each of 4 delay cycles (read from the NOP register) is required after
   writing to CDSN Control register, see Software Requirement 11.4 item 3. */
static __inline__ void DoC_Address (unsigned long docptr, int numbytes, unsigned long ofs,
			       unsigned char xtraflags1, unsigned char xtraflags2)
{
	/* Assert the ALE (Address Latch Enable line to the flash chip */
 	WriteDOC(xtraflags1 | CDSN_CTRL_ALE | CDSN_CTRL_CE, docptr, CDSNControl);
	DoC_Delay(docptr, 4);

	/* Send the address */
	switch (numbytes)
	    {
	    case 1:
		/* Send single byte, bits 0-7. */
		WriteDOC(ofs & 0xff, docptr, CDSNSlowIO);
		WriteDOC(ofs & 0xff, docptr, Mil_CDSN_IO);
		break;
	    case 2:
		/* Send bits 9-16 followed by 17-23 */
		WriteDOC((ofs >> 9)  & 0xff, docptr, CDSNSlowIO);
		WriteDOC((ofs >> 9)  & 0xff, docptr, Mil_CDSN_IO);
		WriteDOC((ofs >> 17) & 0xff, docptr, CDSNSlowIO);
		WriteDOC((ofs >> 17) & 0xff, docptr, Mil_CDSN_IO);
		break;
	    case 3:
		/* Send 0-7, 9-16, then 17-23 */
		WriteDOC(ofs & 0xff, docptr, CDSNSlowIO);
		WriteDOC(ofs & 0xff, docptr, Mil_CDSN_IO);
		WriteDOC((ofs >> 9)  & 0xff, docptr, CDSNSlowIO);
		WriteDOC((ofs >> 9)  & 0xff, docptr, Mil_CDSN_IO);
		WriteDOC((ofs >> 17) & 0xff, docptr, CDSNSlowIO);
		WriteDOC((ofs >> 17) & 0xff, docptr, Mil_CDSN_IO);
		break;
	    default:
		return;
	    }

	/* Lower the ALE line */
	WriteDOC(xtraflags1 | xtraflags2 | CDSN_CTRL_CE, docptr, CDSNControl);
	DoC_Delay(docptr, 4);
}

/* DoC_SelectChip: Select a given flash chip within the current floor */
static int DoC_SelectChip(unsigned long docptr, int chip)
{
	/* Select the individual flash chip requested */
	WriteDOC(chip, docptr, CDSNDeviceSelect);
	DoC_Delay(docptr, 4);

	/* Wait for it to be ready */
	return DoC_WaitReady(docptr);
}

/* DoC_SelectFloor: Select a given floor (bank of flash chips) */
static int DoC_SelectFloor(unsigned long docptr, int floor)
{
	/* Select the floor (bank) of chips required */
	WriteDOC(floor, docptr, FloorSelect);

	/* Wait for the chip to be ready */
	return DoC_WaitReady(docptr);
}

/* DoC_IdentChip: Identify a given NAND chip given {floor,chip} */
static int DoC_IdentChip(struct DiskOnChip *doc, int floor, int chip)
{
	int mfr, id, i;
	volatile char dummy;

	/* Page in the required floor/chip
	   FIXME: is this supported by Millennium ?? */
	DoC_SelectFloor(doc->virtadr, floor);
	DoC_SelectChip(doc->virtadr, chip);

	/* Reset the chip, see Software Requirement 11.4 item 1. */
	DoC_Command(doc->virtadr, NAND_CMD_RESET, CDSN_CTRL_WP);
	DoC_WaitReady(doc->virtadr);

	/* Read the NAND chip ID: 1. Send ReadID command */ 
	DoC_Command(doc->virtadr, NAND_CMD_READID, CDSN_CTRL_WP);

	/* Read the NAND chip ID: 2. Send address byte zero */ 
	DoC_Address(doc->virtadr, 1, 0x00, CDSN_CTRL_WP, 0x00);

	/* Read the manufacturer and device id codes of the flash device through
	   CDSN Slow IO register see Software Requirement 11.4 item 5.*/
	dummy = ReadDOC(doc->virtadr, CDSNSlowIO);
	DoC_Delay(doc->virtadr, 2);
	mfr = ReadDOC(doc->virtadr, Mil_CDSN_IO);

	dummy = ReadDOC(doc->virtadr, CDSNSlowIO);
	DoC_Delay(doc->virtadr, 2);
	id  = ReadDOC(doc->virtadr, Mil_CDSN_IO);

	/* No response - return failure */
	if (mfr == 0xff || mfr == 0)
		return 0;

	/* FIXME: to deal with mulit-flash on multi-Millennium case more carefully */
	for (i = 0; nand_flash_ids[i].name != NULL; i++) {
		if (mfr == nand_flash_ids[i].manufacture_id &&
		    id == nand_flash_ids[i].model_id) {
			printk(KERN_INFO "Flash chip found: Manufacture ID: %2.2X, "
			       "Chip ID: %2.2X (%s)\n",
			       mfr, id, nand_flash_ids[i].name);
			doc->mfr = mfr;
			doc->id = id;
			doc->chipshift = nand_flash_ids[i].chipshift;
			break;
		}
	}

	if (nand_flash_ids[i].name == NULL)
		return 0;
	else
		return 1;
}     

/* DoC_ScanChips: Find all NAND chips present in a DiskOnChip, and identify them */
static void DoC_ScanChips(struct DiskOnChip *this)
{
	int floor, chip;
	int numchips[MAX_FLOORS_MIL];
	int ret;
	
	this->numchips = 0;
	this->mfr = 0;
	this->id = 0;
	
	/* For each floor, find the number of valid chips it contains */
	for (floor = 0,ret = 1; floor < MAX_FLOORS_MIL; floor++) {
		numchips[floor] = 0;
		for (chip = 0; chip < MAX_CHIPS_MIL && ret != 0; chip++) {
			ret = DoC_IdentChip(this, floor, chip);
			if (ret) {
				numchips[floor]++;
				this->numchips++;
			}
		}
	}
	/* If there are none at all that we recognise, bail */
	if (!this->numchips) {
		printk("No flash chips recognised.\n");
		return;
	}
	
	/* Allocate an array to hold the information for each chip */
	this->chips = kmalloc(sizeof(struct Nand) * this->numchips, GFP_KERNEL);
	if (!this->chips){
		printk("No memory for allocating chip info structures\n");
		return;
	}
	
	/* Fill out the chip array with {floor, chipno} for each 
	 * detected chip in the device. */
	for (floor = 0, ret = 0; floor < MAX_FLOORS_MIL; floor++) {
		for (chip = 0 ; chip < numchips[floor] ; chip++) {
			this->chips[ret].floor = floor;
			this->chips[ret].chip = chip;
			this->chips[ret].curadr = 0;
			this->chips[ret].curmode = 0x50;
			ret++;
		}
	}

	/* Calculate and print the total size of the device */
	this->totlen = this->numchips * (1 << this->chipshift);
	printk(KERN_INFO "%d flash chips found. Total DiskOnChip size: %ld Mbytes\n",
	       this->numchips ,this->totlen >> 20);
}

static int DoCMil_is_alias(struct DiskOnChip *doc1, struct DiskOnChip *doc2)
{
	int tmp1, tmp2, retval;

	if (doc1->physadr == doc2->physadr)
		return 1;

	/* Use the alias resolution register which was set aside for this
	 * purpose. If it's value is the same on both chips, they might
	 * be the same chip, and we write to one and check for a change in
	 * the other. It's unclear if this register is usuable in the
	 * DoC 2000 (it's in the Millenium docs), but it seems to work. */
	tmp1 = ReadDOC(doc1->virtadr, AliasResolution);
	tmp2 = ReadDOC(doc2->virtadr, AliasResolution);
	if (tmp1 != tmp2)
		return 0;
	
	WriteDOC((tmp1+1) % 0xff, doc1->virtadr, AliasResolution);
	tmp2 = ReadDOC(doc2->virtadr, AliasResolution);
	if (tmp2 == (tmp1+1) % 0xff)
		retval = 1;
	else
		retval = 0;

	/* Restore register contents.  May not be necessary, but do it just to
	 * be safe. */
	WriteDOC(tmp1, doc1->virtadr, AliasResolution);
	
	return retval;
}

static const char im_name[] = "DoCMil_init";

/* This routine is made available to other mtd code via
 * inter_module_register.  It must only be accessed through
 * inter_module_get which will bump the use count of this module.  The
 * addresses passed back in mtd are valid as long as the use count of
 * this module is non-zero, i.e. between inter_module_get and
 * inter_module_put.  Keith Owens <kaos@ocs.com.au> 29 Oct 2000.
 */
void DoCMil_init(struct mtd_info *mtd)
{
	struct DiskOnChip *this = (struct DiskOnChip *)mtd->priv;
	struct DiskOnChip *old = NULL;

	/* We must avoid being called twice for the same device. */
	if (docmillist)
		old = (struct DiskOnChip *)docmillist->priv;

	while (old) {
		if (DoCMil_is_alias(this, old)) {
			printk(KERN_NOTICE "Ignoring DiskOnChip Millennium at "
			       "0x%lX - already configured\n", this->physadr);
			iounmap((void *)this->virtadr);
			kfree(mtd);
			return;
		}
		if (old->nextdoc)
			old = (struct DiskOnChip *)old->nextdoc->priv;
		else
			old = NULL;
	}

	mtd->name = "DiskOnChip Millennium";
	printk(KERN_NOTICE "DiskOnChip Millennium found at address 0x%lX\n",
		this->physadr);

	mtd->type = MTD_NANDFLASH;
	mtd->flags = MTD_CAP_NANDFLASH;
	mtd->size = 0;
	mtd->erasesize = 0x2000;
	mtd->oobblock = 512;
	mtd->oobsize = 16;
	mtd->module = THIS_MODULE;
	mtd->erase = doc_erase;
	mtd->point = NULL;
	mtd->unpoint = NULL;
	mtd->read = doc_read;
	mtd->write = doc_write;
	mtd->read_ecc = doc_read_ecc;
	mtd->write_ecc = doc_write_ecc;
	mtd->read_oob = doc_read_oob;
	mtd->write_oob = doc_write_oob;
	mtd->sync = NULL;
	
	this->totlen = 0;
	this->numchips = 0;	
	this->curfloor = -1;
	this->curchip = -1;
	
	/* Ident all the chips present. */
	DoC_ScanChips(this);
	
	if (!this->totlen) {
		kfree(mtd);
		iounmap((void *)this->virtadr);
	} else {
		this->nextdoc = docmillist;
		docmillist = mtd;
		mtd->size  = this->totlen;
		add_mtd_device(mtd);
		return;
	}
}

static int doc_read (struct mtd_info *mtd, loff_t from, size_t len,
		     size_t *retlen, u_char *buf)
{
	/* Just a special case of doc_read_ecc */
	return doc_read_ecc(mtd, from, len, retlen, buf, NULL);
}

static int doc_read_ecc (struct mtd_info *mtd, loff_t from, size_t len,
			 size_t *retlen, u_char *buf, u_char *eccbuf)
{
	int i;
	volatile char dummy;
	struct DiskOnChip *this = (struct DiskOnChip *)mtd->priv;
	unsigned long docptr = this->virtadr;
	struct Nand *mychip = &this->chips[from >> (this->chipshift)];

	/* Don't allow read past end of device */
	if (from >= this->totlen)
		return -EINVAL;

	/* Don't allow a single read to cross a 512-byte block boundary */
	if (from + len > ((from | 0x1ff) + 1)) 
		len = ((from | 0x1ff) + 1) - from;

	/* Find the chip which is to be used and select it */
	if (this->curfloor != mychip->floor) {
		DoC_SelectFloor(docptr, mychip->floor);
		DoC_SelectChip(docptr, mychip->chip);
	} else if (this->curchip != mychip->chip) {
		DoC_SelectChip(docptr, mychip->chip);
	}
	this->curfloor = mychip->floor;
	this->curchip = mychip->chip;

	if (eccbuf) {
		/* init the ECC engine, see Reed-Solomon EDC/ECC 11.1 .*/
		WriteDOC (DOC_ECC_RESET, docptr, ECCConf);
		WriteDOC (DOC_ECC_EN, docptr, ECCConf);
	} else {
		/* disable the ECC engine, FIXME: is this correct ?? */
		WriteDOC (DOC_ECC_RESET, docptr, ECCConf);
		WriteDOC (DOC_ECC_DIS, docptr, ECCConf);	
	}

	/* issue the Read0 or Read1 command depend on which half of the page
	   we are accessing. Polling the Flash Ready bit after issue 3 bytes
	   address in Sequence Read Mode, see Software Requirement 11.4 item 1.*/
	DoC_Command(docptr, (from >> 8) & 1, CDSN_CTRL_WP);
	DoC_Address(docptr, 3, from, CDSN_CTRL_WP, 0x00);
	DoC_WaitReady(docptr);

	/* Read the data via the internal pipeline through CDSN IO register,
	   see Pipelined Read Operations 11.3 */
	dummy = ReadDOC(docptr, ReadPipeInit);
	for (i = 0; i < len-1; i++) {
		buf[i] = ReadDOC(docptr, Mil_CDSN_IO);
	}
	buf[i] = ReadDOC(docptr, LastDataRead);

	/* Let the caller know we completed it */
	*retlen = len;

	if (eccbuf) {
		/* FIXME: are we reading the ECC from the ECC logic of DOC or
		   the spare data space on the flash chip i.e. How do we
		   control the Spare Area Enable bit of the flash ?? */
		/* Read the ECC data through the DiskOnChip ECC logic
		   see Reed-Solomon EDC/ECC 11.1 */
		dummy = ReadDOC(docptr, ReadPipeInit);
		for (i = 0; i < 5; i++) {
			eccbuf[i] = ReadDOC(docptr, Mil_CDSN_IO);
		}
		eccbuf[i] = ReadDOC(docptr, LastDataRead);

		/* Flush the pipeline */
		dummy = ReadDOC(docptr, ECCConf);
		dummy = ReadDOC(docptr, ECCConf);

		/* Check the ECC Status */
		if (ReadDOC(docptr, ECCConf) & 0x80) {
			/* There was an ECC error */
			printk("DiskOnChip ECC Error: Read at %lx\n", (long)from);

			/* FIXME: Implement ECC error correction, don't just whinge */

			/* We return error, but have actually done the read. Not that
			   this can be told to user-space, via sys_read(), but at least
			   MTD-aware stuff can know about it by checking *retlen */
			return -EIO;
		}
#ifdef PSYCHO_DEBUG
		else
			printk("ECC OK at %lx: %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X\n",
			       (long)from, eccbuf[0], eccbuf[1], eccbuf[2], eccbuf[3],
			       eccbuf[4], eccbuf[5]);
#endif
		/* Reset the ECC engine */
		WriteDOC(DOC_ECC_RESV, docptr , ECCConf);
	}

	return 0;
}

static int doc_write (struct mtd_info *mtd, loff_t to, size_t len,
		      size_t *retlen, const u_char *buf)
{
	static char as[6];
	return doc_write_ecc(mtd, to, len, retlen, buf, as);
}

static int doc_write_ecc (struct mtd_info *mtd, loff_t to, size_t len,
			  size_t *retlen, const u_char *buf, u_char *eccbuf)
{
	int i;
	volatile char dummy;
	struct DiskOnChip *this = (struct DiskOnChip *)mtd->priv;
	unsigned long docptr = this->virtadr;
	struct Nand *mychip = &this->chips[to >> (this->chipshift)];

	/* Don't allow write past end of device */
	if (to >= this->totlen)
		return -EINVAL;

#if 0
	/* Don't allow a single write to cross a 512-byte block boundary */
	if (to + len > ( (to | 0x1ff) + 1)) 
		len = ((to | 0x1ff) + 1) - to;
#else
	/* Don't allow writes which aren't exactly one block */
	if (to & 0x1ff || len != 0x200)
		return -EINVAL;
#endif

	/* Find the chip which is to be used and select it */
	if (this->curfloor != mychip->floor) {
		DoC_SelectFloor(docptr, mychip->floor);
		DoC_SelectChip(docptr, mychip->chip);
	}
	else if (this->curchip != mychip->chip) {
		DoC_SelectChip(docptr, mychip->chip);
	}
	this->curfloor = mychip->floor;
	this->curchip = mychip->chip;

	/* Reset the chip, see Software Requirement 11.4 item 1. */
	DoC_Command(docptr, NAND_CMD_RESET, CDSN_CTRL_WP);
	DoC_WaitReady(docptr);
	/* Set device to main plane of flash */
	DoC_Command(docptr, NAND_CMD_READ0, CDSN_CTRL_WP);

	if (eccbuf) {
		/* init the ECC engine, see Reed-Solomon EDC/ECC 11.1 .*/
		WriteDOC (DOC_ECC_RESET, docptr, ECCConf);
		WriteDOC (DOC_ECC_EN | DOC_ECC_RW, docptr, ECCConf);
	} else {
		/* disable the ECC engine, FIXME: is this correct ?? */
		WriteDOC (DOC_ECC_RESET, docptr, ECCConf);
		WriteDOC (DOC_ECC_DIS, docptr, ECCConf);
	}

	/* issue the Serial Data In command to initial the Page Program process */
	DoC_Command(docptr, NAND_CMD_SEQIN, 0x00);
	DoC_Address(docptr, 3, to, 0x00, 0x00);

	/* Write the data via the internal pipeline through CDSN IO register,
	   see Pipelined Write Operations 11.2 */
	for (i = 0; i < len; i++) {
		WriteDOC(buf[i], docptr, Mil_CDSN_IO);
	}
	WriteDOC(0x00, docptr, WritePipeTerm);

	if (eccbuf) {
		/* Write ECC data to flash, the ECC info is generated by the DiskOnChip DECC logic
		   see Reed-Solomon EDC/ECC 11.1 */
		WriteDOC(0, docptr, NOP);
		WriteDOC(0, docptr, NOP);
		WriteDOC(0, docptr, NOP);

		/* Read the ECC data through the DiskOnChip ECC logic */
		for (i = 0; i < 6; i++) {
			eccbuf[i] = ReadDOC(docptr, ECCSyndrome0 + i);
		}

		/* Write the ECC data to flash */
		for (i = 0; i < 6; i++) {
			WriteDOC(eccbuf[i], docptr, Mil_CDSN_IO);
		}
		WriteDOC(0x00, docptr, WritePipeTerm);

#ifdef PSYCHO_DEBUG
		printk("OOB data at %lx is %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X\n",
		       (long) to, eccbuf[0], eccbuf[1], eccbuf[2], eccbuf[3],
		       eccbuf[4], eccbuf[5]);
#endif

		/* Reset the ECC engine */
		WriteDOC(DOC_ECC_RESV, docptr , ECCConf);
	}

	/* Commit the Page Program command and wait for ready
	   see Software Requirement 11.4 item 1.*/
	DoC_Command(docptr, NAND_CMD_PAGEPROG, 0x00);
	DoC_WaitReady(docptr);

	/* Read the status of the flash device through CDSN Slow IO register
	   see Software Requirement 11.4 item 5.*/
	DoC_Command(docptr, NAND_CMD_STATUS, 0x00);
	dummy = ReadDOC(docptr, CDSNSlowIO);
	DoC_Delay(docptr, 2);
	if (ReadDOC(docptr, Mil_CDSN_IO) & 1) {
		printk("Error programming flash\n");
		/* Error in programming */
		*retlen = 0;
		return -EIO;
	}

	/* Let the caller know we completed it */
	*retlen = len;

	return 0;
}

static int doc_read_oob(struct mtd_info *mtd, loff_t ofs, size_t len,
			size_t *retlen, u_char *buf)
{
	volatile char dummy;
	int i;
	struct DiskOnChip *this = (struct DiskOnChip *)mtd->priv;
	unsigned long docptr = this->virtadr;
	struct Nand *mychip = &this->chips[ofs >> this->chipshift];

	/* FIXME: should we restrict the access between 512 to 527 ?? */

	/* Find the chip which is to be used and select it */
	if (this->curfloor != mychip->floor) {
		DoC_SelectFloor(docptr, mychip->floor);
		DoC_SelectChip(docptr, mychip->chip);
	}
	else if (this->curchip != mychip->chip) {
		DoC_SelectChip(docptr, mychip->chip);
	}
	this->curfloor = mychip->floor;
	this->curchip = mychip->chip;

	/* FIXME: should we disable ECC engine in this way ?? */
	/* disable the ECC engine, FIXME: is this correct ?? */
	WriteDOC (DOC_ECC_RESET, docptr, ECCConf);
	WriteDOC (DOC_ECC_DIS, docptr, ECCConf);

	/* issue the Read2 command to read the Spare Data Area.
	   Polling the Flash Ready bit after issue 3 bytes address in
	   Sequence Read Mode, see Software Requirement 11.4 item 1.*/
	DoC_Command(docptr, NAND_CMD_READOOB, CDSN_CTRL_WP);
	DoC_Address(docptr, 3, ofs, CDSN_CTRL_WP, 0x00);
	DoC_WaitReady(docptr);

	/* Read the data out via the internal pipeline through CDSN IO register,
	   see Pipelined Read Operations 11.3 */
	dummy = ReadDOC(docptr, ReadPipeInit);
	for (i = 0; i < len-1; i++) {
		buf[i] = ReadDOC(docptr, Mil_CDSN_IO);
	}
	buf[i] = ReadDOC(docptr, LastDataRead);

	*retlen = len;

	return 0;
}

static int doc_write_oob(struct mtd_info *mtd, loff_t ofs, size_t len,
			 size_t *retlen, const u_char *buf)
{
	int i;
	volatile char dummy;
	struct DiskOnChip *this = (struct DiskOnChip *)mtd->priv;
	unsigned long docptr = this->virtadr;
	struct Nand *mychip = &this->chips[ofs >> this->chipshift];

	/* Find the chip which is to be used and select it */
 	if (this->curfloor != mychip->floor) {
		DoC_SelectFloor(docptr, mychip->floor);
		DoC_SelectChip(docptr, mychip->chip);
	}
	else if (this->curchip != mychip->chip) {
		DoC_SelectChip(docptr, mychip->chip);
	}
	this->curfloor = mychip->floor;
	this->curchip = mychip->chip;

	/* FIXME: should we disable ECC engine in this way ?? */
	/* disable the ECC engine, FIXME: is this correct ?? */
	WriteDOC (DOC_ECC_RESET, docptr, ECCConf);
	WriteDOC (DOC_ECC_DIS, docptr, ECCConf);

	/* Reset the chip, see Software Requirement 11.4 item 1. */
	DoC_Command(docptr, NAND_CMD_RESET, CDSN_CTRL_WP);
	DoC_WaitReady(docptr);
	/* issue the Read2 command to read the Spare Data Area. */
	DoC_Command(docptr, NAND_CMD_READOOB, CDSN_CTRL_WP);

	/* issue the Serial Data In command to initial the Page Program process */
	DoC_Command(docptr, NAND_CMD_SEQIN, 0x00);
	DoC_Address(docptr, 3, ofs, 0x00, 0x00);

	/* Write the data via the internal pipeline through CDSN IO register,
	   see Pipelined Write Operations 11.2 */
	for (i = 0; i < len; i++)
		WriteDOC(buf[i], docptr, Mil_CDSN_IO);
	WriteDOC(0x00, docptr, WritePipeTerm);

	/* Commit the Page Program command and wait for ready
	   see Software Requirement 11.4 item 1.*/
	DoC_Command(docptr, NAND_CMD_PAGEPROG, 0x00);
	DoC_WaitReady(docptr);

	/* Read the status of the flash device through CDSN Slow IO register
	   see Software Requirement 11.4 item 5.*/
	DoC_Command(docptr, NAND_CMD_STATUS, 0x00);
	dummy = ReadDOC(docptr, CDSNSlowIO);
	DoC_Delay(docptr, 2);
	if (ReadDOC(docptr, Mil_CDSN_IO) & 1) {
		printk("Error programming oob data\n");
		*retlen = 0;
		return -EIO;
	}

	*retlen = len;

	return 0;
}

int doc_erase (struct mtd_info *mtd, struct erase_info *instr)
{
	volatile char dummy;
	struct DiskOnChip *this = (struct DiskOnChip *)mtd->priv;
	unsigned long ofs = instr->addr;
	unsigned long len = instr->len;
	unsigned long docptr = this->virtadr;
	struct Nand *mychip = &this->chips[ofs >> this->chipshift];

	if (len != mtd->erasesize) 
		printk(KERN_WARNING "Erase not right size (%lx != %lx)n",
		       len, mtd->erasesize);

	/* Find the chip which is to be used and select it */
	if (this->curfloor != mychip->floor) {
		DoC_SelectFloor(docptr, mychip->floor);
		DoC_SelectChip(docptr, mychip->chip);
	}
	else if (this->curchip != mychip->chip) {
		DoC_SelectChip(docptr, mychip->chip);
	}
	this->curfloor = mychip->floor;
	this->curchip = mychip->chip;
	
	instr->state = MTD_ERASE_PENDING;

	/* issue the Erase Setup command */
	DoC_Command(docptr, NAND_CMD_ERASE1, 0x00);
	DoC_Address(docptr, 2, ofs, 0x00, 0x00);

	/* Commit the Erase Start command and wait for ready
	   see Software Requirement 11.4 item 1.*/
	DoC_Command(docptr, NAND_CMD_ERASE2, 0x00);
	DoC_WaitReady(docptr);

	instr->state = MTD_ERASING;

	/* Read the status of the flash device through CDSN Slow IO register
	   see Software Requirement 11.4 item 5.*/
	DoC_Command(docptr, NAND_CMD_STATUS, CDSN_CTRL_WP);
	dummy = ReadDOC(docptr, CDSNSlowIO);
	DoC_Delay(docptr, 2);
	if (ReadDOC(docptr, Mil_CDSN_IO) & 1) {
		printk("Error Erasing\n");
		/* There was an error */
		instr->state = MTD_ERASE_FAILED;
	} else
		instr->state = MTD_ERASE_DONE;

	if (instr->callback) 
		instr->callback(instr);

	return 0;
}

/****************************************************************************
 *
 * Module stuff
 *
 ****************************************************************************/

static int __init init_doc2001(void)
{
	inter_module_register(im_name, THIS_MODULE, &DoCMil_init);
	return 0;
}

#if LINUX_VERSION_CODE < 0x20300
#ifdef MODULE
#define cleanup_doc2001 cleanup_module
#endif
#define __exit
#endif


static void __exit cleanup_doc2001(void)
{
	struct mtd_info *mtd;
	struct DiskOnChip *this;

	while((mtd=docmillist)) {
		this = (struct DiskOnChip *)mtd->priv;
		docmillist = this->nextdoc;
			
		del_mtd_device(mtd);
			
		iounmap((void *)this->virtadr);
		kfree(this->chips);
		kfree(mtd);
	}
	inter_module_unregister(im_name);

}

module_init(init_doc2001);

#if LINUX_VERSION_CODE > 0x20300
module_exit(cleanup_doc2001);
#endif
