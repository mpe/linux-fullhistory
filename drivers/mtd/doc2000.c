
/* Linux driver for Disk-On-Chip 2000       */
/* (c) 1999 Machine Vision Holdings, Inc.   */
/* Author: David Woodhouse <dwmw2@mvhi.com> */
/* $Id: doc2000.c,v 1.23 2000/07/03 10:01:38 dwmw2 Exp $ */

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

//#define PRERELEASE

static int doc_read (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf);
static int doc_write (struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen, const u_char *buf);
static int doc_read_ecc (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf, u_char *eecbuf);
static int doc_write_ecc (struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen, const u_char *buf, u_char *eccbuf);
static int doc_read_oob(struct mtd_info *mtd, loff_t ofs, size_t len, size_t *retlen, u_char *buf);
static int doc_write_oob(struct mtd_info *mtd, loff_t ofs, size_t len, size_t *retlen, const u_char *buf);
static int doc_erase (struct mtd_info *mtd, struct erase_info *instr);


static struct mtd_info *doc2klist = NULL;

/* DOC_WaitReady: Wait for RDY line to be asserted by the flash chip */

int _DoC_WaitReady (unsigned long docptr)
{
	//long c=-1;
	short c=-1;

	DEBUG(2,"_DoC_WaitReady called for out-of-line wait\n");

	/* Out-of-line routine to wait for chip response */
	while (!(ReadDOC(docptr, CDSNControl) & CDSN_CTRL_FR_B) && --c)
		;

	if (c == 0)
		DEBUG(2, "_DoC_WaitReady timed out.\n");
	
	return (c==0);
}

static inline int DoC_WaitReady(unsigned long docptr) 
{
	/* This is inline, to optimise the common case, where it's ready instantly */
	volatile char dummy;
	int ret = 0;

        /* Out-of-line routine to wait for chip response */
        /* TPW: Add 4 reads - see Software Requirement 2.3.2 */
        dummy = ReadDOC(docptr, CDSNControl);
        dummy = ReadDOC(docptr, CDSNControl);
        dummy = ReadDOC(docptr, CDSNControl);
        dummy = ReadDOC(docptr, CDSNControl);
	
	if (!(ReadDOC(docptr, CDSNControl) & CDSN_CTRL_FR_B))
		ret =  _DoC_WaitReady(docptr); /* Call the out-of-line routine to wait */
	
        /* TPW: Add 2 reads - see Software Requirement 2.3.2 */
        dummy = ReadDOC(docptr, CDSNControl);
        dummy = ReadDOC(docptr, CDSNControl);

	return ret;
}


/* DoC_Command: Send a flash command to the flash chip */

static inline int DoC_Command(unsigned long docptr, unsigned char command, unsigned char xtraflags)
{
	/* Assert the CLE (Command Latch Enable) line to the flash chip */
 	WriteDOC( CDSN_CTRL_FLASH_IO | xtraflags | CDSN_CTRL_CLE | CDSN_CTRL_CE,
		 docptr, CDSNControl);

	/* Send the command */
	WriteDOC(command, docptr, 2k_CDSN_IO);
 
	/* Lower the CLE line */
	WriteDOC( CDSN_CTRL_FLASH_IO | xtraflags | CDSN_CTRL_CE, docptr, CDSNControl);

	/* Wait for the chip to respond */
	return DoC_WaitReady(docptr);
}

/* DoC_Address: Set the current address for the flash chip */

static inline int DoC_Address (unsigned long docptr, int numbytes, unsigned long ofs,
			       unsigned char xtraflags1, unsigned char xtraflags2)
{
	/* Assert the ALE (Address Latch Enable line to the flash chip */
 	WriteDOC( CDSN_CTRL_FLASH_IO | xtraflags1 | CDSN_CTRL_ALE | CDSN_CTRL_CE,
		 docptr, CDSNControl);

	/* Send the address */
	/* Three cases:
	   numbytes == 1: Send single byte, bits 0-7.
	   numbytes == 2: Send bits 9-16 followed by 17-23
	   numbytes == 3: Send 0-7, 9-16, then 17-23 
	*/
	if (numbytes != 2)
		WriteDOC(ofs & 0xff, docptr, 2k_CDSN_IO);
	
	if (numbytes != 1) {
		WriteDOC((ofs >> 9) & 0xff, docptr, 2k_CDSN_IO);
		WriteDOC((ofs >> 17) & 0xff, docptr, 2k_CDSN_IO);
	}
	/* Lower the ALE line */
	WriteDOC( CDSN_CTRL_FLASH_IO | xtraflags1 | xtraflags2 | CDSN_CTRL_CE, docptr, CDSNControl);
	
	/* Wait for the chip to respond */
	return DoC_WaitReady(docptr);
}

/* DoC_SelectChip: Select a given flash chip within the current floor */

static inline int DoC_SelectChip(unsigned long docptr, int chip)
{
	/* Select the individual flash chip requested */
	WriteDOC( chip, docptr, CDSNDeviceSelect);
	
	/* Wait for it to be ready */
	return DoC_WaitReady(docptr);
}

/* DoC_SelectFloor: Select a given floor (bank of flash chips) */

static inline int DoC_SelectFloor(unsigned long docptr, int floor)
{
	/* Select the floor (bank) of chips required */
	WriteDOC( floor, docptr, FloorSelect);

	/* Wait for the chip to be ready */
	return DoC_WaitReady(docptr);
}
  
/* DoC_IdentChip: Identify a given NAND chip given {floor,chip} */

int DoC_IdentChip(struct DiskOnChip *doc, int floor, int chip)
{
	int mfr, id, chipshift=0;
	char *mfrname=NULL, *idname=NULL;

	/* Page in the required floor/chip */
	DoC_SelectFloor(doc->virtadr, floor);
	DoC_SelectChip(doc->virtadr, chip);

	/* Reset the chip */
	if (DoC_Command(doc->virtadr, NAND_CMD_RESET, CDSN_CTRL_WP)) {
		DEBUG(2, "DoC_Command (reset) for %d,%d returned true\n", floor,chip);
		return 0;
	}
  
	/* Read the NAND chip ID: 1. Send ReadID command */ 
	if(DoC_Command(doc->virtadr, NAND_CMD_READID, CDSN_CTRL_WP)) {
		DEBUG(2,"DoC_Command (ReadID) for %d,%d returned true\n", floor,chip);
		return 0;
	}

	/* Read the NAND chip ID: 2. Send address byte zero 
	 */ 
	DoC_Address(doc->virtadr, 1, 0, CDSN_CTRL_WP, 0);
 
	/* Read the manufacturer and device id codes from the device */
	mfr = ReadDOC(doc->virtadr, 2k_CDSN_IO);
	id = ReadDOC(doc->virtadr, 2k_CDSN_IO);
	
	/* No response - return failure */
	if (mfr == 0xff || mfr == 0)
		return 0;
	
	/* Check it's the same as the first chip we identified. 
	 * M-Systems say that any given DiskOnChip device should only
	 * contain _one_ type of flash part, although that's not a 
	 * hardware restriction. */
	if (doc->mfr) {
		if (doc->mfr == mfr && doc->id == id)
			return 1; /* This is another the same the first */
		else
			printk(KERN_WARNING "Flash chip at floor %d, chip %d is different:\n",
			       floor, chip);
	}
	
	/* Print (and store if first time) the manufacturer and ID codes. */
  
	switch(mfr) {
	case NAND_MFR_TOSHIBA: /* Toshiba */
		mfrname = "Toshiba";
		
		switch(id) { 
		case 0x64:
			idname = "TC5816BDC";
			chipshift = 21;
			break;
			
		case 0x6b:
			idname = "TC5832DC";
			chipshift = 22;
			break;
			
		case 0x73: 
			idname = "TH58V128DC";
			chipshift = 24;
			break;
			
		case 0x75: 
			idname = "TC58256FT/DC";
			chipshift = 25;
			break;
			
		case 0xe5:
			idname = "TC58V32DC";
			chipshift = 22;
			break;
			
		case 0xe6: 
			idname = "TC58V64DC";
			chipshift = 23;
			break;
			
		case 0xea:
			idname = "TC58V16BDC";
			chipshift = 21;
			break;
		}
		break; /* End of Toshiba parts */
		
	case NAND_MFR_SAMSUNG: /* Samsung */
		mfrname = "Samsung";
		
		switch(id) {
		case 0x64:
			idname = "KM29N16000";
			chipshift = 21;
			
		case 0x73:
			idname = "KM29U128T";
			chipshift = 24;
			break;
			
		case 0x75:
			idname = "KM29U256T";
			chipshift = 25;
			break;

		case 0xe3:
			idname = "KM29W32000";
			chipshift = 22;
			break;
			
		case 0xe6:
			idname = "KM29U64000";
			chipshift = 23;
			break;
			
		case 0xea:
			idname = "KM29W16000";
			chipshift = 21;
			break;
		}
		break; /* End of Samsung parts */
	}
	
	/* If we've identified it fully, print the full names */
	if (idname) {
#ifdef PRERELEASE
		DEBUG(1, "Flash chip found: %2.2X %2.2X (%s %s)\n", 
			mfr,id,mfrname,idname);
#endif
		/* If this is the first chip, store the id codes */
		if (!doc->mfr) {
			doc->mfr = mfr;
			doc->id = id;
			doc->chipshift = chipshift;
			return 1;
		}
		return 0;
	}

	/* We haven't fully identified the chip. Print as much as we know. */
	if (mfrname)
		printk(KERN_WARNING "Unknown %s flash chip found: %2.2X %2.2X\n", mfrname,
		       id, mfr);
	else
		printk(KERN_WARNING "Unknown flash chip found: %2.2X %2.2X\n", id, mfr);
	
	printk(KERN_WARNING "Please report to David.Woodhouse@mvhi.com\n");
	return 0;
}     

/* DoC_ScanChips: Find all NAND chips present in a DiskOnChip, and identify them */

void DoC_ScanChips(struct DiskOnChip *this)
{
	int floor, chip;
	int numchips[MAX_FLOORS];
	int ret = 1;
	
	this->numchips = 0;
	this->mfr = 0;
	this->id = 0;
	
	/* For each floor, find the number of valid chips it contains */
	for (floor = 0 ; floor < MAX_FLOORS ; floor++) {
		ret = 1;
		numchips[floor]=0;
		for (chip = 0 ; chip < MAX_CHIPS && ret != 0; chip++ ) {
			
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
	
	ret = 0;
	
	/* Fill out the chip array with {floor, chipno} for each 
	 * detected chip in the device. */
	for (floor = 0; floor < MAX_FLOORS; floor++) {
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

	printk(KERN_INFO "%d flash chips found. Total DiskOnChip size: %ld Mb\n", this->numchips ,
	       this->totlen >> 20);
}


static int DoC2k_is_alias(struct DiskOnChip *doc1, struct DiskOnChip *doc2)
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


void DoC2k_init(struct mtd_info *mtd)
{
	struct DiskOnChip *this = (struct DiskOnChip *)mtd->priv;
	struct DiskOnChip *old = NULL;

	/* We must avoid being called twice for the same device. */

	if (doc2klist)
		old = (struct DiskOnChip *)doc2klist->priv;

	while (old) {
		if (DoC2k_is_alias(old, this)) {
			printk(KERN_NOTICE "Ignoring DiskOnChip 2000 at 0x%lX - already configured\n",
			       this->physadr);
			iounmap((void *)this->virtadr);
			kfree(mtd);
			return;
		}
		if (old->nextdoc)
			old = (struct DiskOnChip *)old->nextdoc->priv;
		else
			old = NULL;
	}
			
	
	mtd->name = "DiskOnChip 2000";
	printk(KERN_NOTICE "DiskOnChip 2000 found at address 0x%lX\n",this->physadr);

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
		this->nextdoc = doc2klist;
		doc2klist = mtd;
		mtd->size  = this->totlen;
		add_mtd_device(mtd);
		return;
	}
}


EXPORT_SYMBOL(DoC2k_init);

static int doc_read (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf)
{
	/* Just a special case of doc_read_ecc */
	return doc_read_ecc(mtd, from, len, retlen, buf, NULL);
}

static int doc_read_ecc (struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen, u_char *buf, u_char *eccbuf)
{
	struct DiskOnChip *this = (struct DiskOnChip *)mtd->priv;
	int di=0; /* Yes, DI is a hangover from when I was disassembling the binary driver */
	unsigned long docptr;
	struct Nand *mychip;

	docptr = this->virtadr;

	/* Don't allow read past end of device */
	if (from >= this->totlen)
		return -EINVAL;
	
	/* Don't allow a single read to cross a 512-byte block boundary */
	if (from + len > ( (from | 0x1ff) + 1)) 
		len = ((from | 0x1ff) + 1) - from;

	/* Find the chip which is to be used and select it */
	mychip = &this->chips[from >> (this->chipshift)];
	
	if (this->curfloor != mychip->floor) {
		DoC_SelectFloor(docptr, mychip->floor);
		DoC_SelectChip(docptr, mychip->chip);
	}
	else if (this->curchip != mychip->chip) {
		DoC_SelectChip(docptr, mychip->chip);
	}
	
	this->curfloor = mychip->floor;
	this->curchip = mychip->chip;
	

	if (eccbuf) {
		/* Prime the ECC engine */
		WriteDOC ( DOC_ECC_RESET, docptr, ECCConf);
		WriteDOC ( DOC_ECC_EN, docptr, ECCConf);
	}

	DoC_Command(docptr, (from >> 8) & 1, CDSN_CTRL_WP);
	DoC_Address(docptr, 3, from, CDSN_CTRL_WP , CDSN_CTRL_ECC_IO);

	for (di=0; di < len ; di++) {
		buf[di] = ReadDOC(docptr, 2k_CDSN_IO);
	}

	/* Let the caller know we completed it */
	*retlen = len;

	if (eccbuf) {
		/* Read the ECC data through the DiskOnChip ECC logic */
		for (di=0; di<6; di++) {
			eccbuf[di] = ReadDOC(docptr, 2k_CDSN_IO);
		}
		
		/* Flush the pipeline */
		(void) ReadDOC(docptr, 2k_ECCStatus);
		(void) ReadDOC(docptr, 2k_ECCStatus);
		
		/* Check the ECC Status */
		if (ReadDOC(docptr, 2k_ECCStatus) & 0x80) {
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
			       (long)from, eccbuf[0], eccbuf[1], eccbuf[2], eccbuf[3], eccbuf[4],
			       eccbuf[5]);
#endif
		
		/* Reset the ECC engine */
		WriteDOC(DOC_ECC_RESV, docptr , ECCConf);
		
	}

	return 0;
}

static int doc_write (struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen, const u_char *buf)
{
	static char as[6];
	return doc_write_ecc(mtd, to, len, retlen, buf, as);
}

static int doc_write_ecc (struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen, const u_char *buf, u_char *eccbuf)
{
	struct DiskOnChip *this = (struct DiskOnChip *)mtd->priv;
	int di=0; 
	unsigned long docptr;
	struct Nand *mychip;

	docptr = this->virtadr;

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
	mychip = &this->chips[to >> (this->chipshift)];
	
	if (this->curfloor != mychip->floor) {
		DoC_SelectFloor(docptr, mychip->floor);
		DoC_SelectChip(docptr, mychip->chip);
	}
	else if (this->curchip != mychip->chip) {
		DoC_SelectChip(docptr, mychip->chip);
	}
	
	this->curfloor = mychip->floor;
	this->curchip = mychip->chip;
	
	/* Set device to main plane of flash */
	DoC_Command(docptr, NAND_CMD_RESET, CDSN_CTRL_WP);
	DoC_Command(docptr, NAND_CMD_READ0, CDSN_CTRL_WP);

	if (eccbuf) {
		/* Prime the ECC engine */
		WriteDOC ( DOC_ECC_RESET, docptr, ECCConf);
		WriteDOC ( DOC_ECC_EN | DOC_ECC_RW, docptr, ECCConf);
	}

	DoC_Command(docptr, NAND_CMD_SEQIN, 0);
	DoC_Address(docptr, 3, to, 0, CDSN_CTRL_ECC_IO);

	for (di=0; di < len ; di++) {
		WriteDOC(buf[di], docptr, 2k_CDSN_IO);
	}


	if (eccbuf) {
		WriteDOC( CDSN_CTRL_ECC_IO | CDSN_CTRL_CE , docptr, CDSNControl );
		
#if 1
		/* eduardp@m-sys.com says this shouldn't be necessary,
		 * but it doesn't actually work without it, so I've
		 * left it in for now. dwmw2.
		 */
		 
		WriteDOC( 0, docptr, 2k_CDSN_IO);
		WriteDOC( 0, docptr, 2k_CDSN_IO);
		WriteDOC( 0, docptr, 2k_CDSN_IO);
#endif
		/* Read the ECC data through the DiskOnChip ECC logic */
		for (di=0; di<6; di++) {
			eccbuf[di] = ReadDOC(docptr, ECCSyndrome0 + di);
		}
#ifdef PSYCHO_DEBUG
		printk("OOB data at %lx is %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X\n",
		       (long) to, eccbuf[0], eccbuf[1], eccbuf[2],
		       eccbuf[3], eccbuf[4], eccbuf[5]       );
#endif
		/* Reset the ECC engine */
		WriteDOC(DOC_ECC_RESV, docptr , ECCConf);
		
	}

	DoC_Command(docptr, NAND_CMD_PAGEPROG, 0);

	DoC_Command(docptr, NAND_CMD_STATUS, CDSN_CTRL_WP);
	/* There's an implicit DoC_WaitReady() in DoC_Command */

	if (ReadDOC(docptr, 2k_CDSN_IO) & 1) {
		printk("Error programming flash\n");
		/* Error in programming */
		*retlen = 0;
		return -EIO;
	}

	/* Let the caller know we completed it */
	*retlen = len;

	return 0;
}



static int doc_read_oob(struct mtd_info *mtd, loff_t ofs, size_t len, size_t *retlen, u_char *buf)
{
	struct DiskOnChip *this = (struct DiskOnChip *)mtd->priv;
	int i;
	unsigned long docptr;
	struct Nand *mychip;
	
	docptr = this->virtadr;
	
	mychip = &this->chips[ofs >> this->chipshift];
	
	if (this->curfloor != mychip->floor) {
		DoC_SelectFloor(docptr, mychip->floor);
		DoC_SelectChip(docptr, mychip->chip);
	}
	else if (this->curchip != mychip->chip) {
		DoC_SelectChip(docptr, mychip->chip);
	}
	this->curfloor = mychip->floor;
	this->curchip = mychip->chip;
	
	
	
	DoC_Command(docptr, NAND_CMD_READOOB, CDSN_CTRL_WP);
	DoC_Address(docptr, 3, ofs, CDSN_CTRL_WP, 0);
	
	for (i=0; i<len; i++)
		buf[i] = ReadDOC(docptr, 2k_CDSN_IO);
	
	*retlen = len;
	return 0;

}

static int doc_write_oob(struct mtd_info *mtd, loff_t ofs, size_t len, size_t *retlen, const u_char *buf)
{
	struct DiskOnChip *this = (struct DiskOnChip *)mtd->priv;
	int i;
	unsigned long docptr;
	struct Nand *mychip;

	//	printk("doc_write_oob(%lx, %d): %2.2X %2.2X %2.2X %2.2X ... %2.2X %2.2X .. %2.2X %2.2X\n",(long)ofs, len,
	//   buf[0], buf[1], buf[2], buf[3], buf[8], buf[9], buf[14],buf[15]);

	docptr = this->virtadr;
	
	mychip = &this->chips[ofs >> this->chipshift];
	
	if (this->curfloor != mychip->floor) {
		DoC_SelectFloor(docptr, mychip->floor);
		DoC_SelectChip(docptr, mychip->chip);
	}
	else if (this->curchip != mychip->chip) {
		DoC_SelectChip(docptr, mychip->chip);
	}
	this->curfloor = mychip->floor;
	this->curchip = mychip->chip;
	
	DoC_Command(docptr, NAND_CMD_RESET, CDSN_CTRL_WP);
	DoC_Command(docptr, NAND_CMD_READOOB, CDSN_CTRL_WP);

	DoC_Command(docptr, NAND_CMD_SEQIN, 0);
	DoC_Address(docptr, 3, ofs, 0, 0);
	
	for (i=0; i<len; i++)
		WriteDOC(buf[i], docptr, 2k_CDSN_IO);

	DoC_Command(docptr, NAND_CMD_PAGEPROG, 0);
	DoC_Command(docptr, NAND_CMD_STATUS, 0);
	/* DoC_WaitReady() is implicit in DoC_Command */

	if (ReadDOC(docptr, 2k_CDSN_IO) & 1) {
		printk("Error programming oob data\n");
		/* There was an error */
		*retlen = 0;
		return -EIO;
	}

	*retlen = len;
	return 0;

}


int doc_erase (struct mtd_info *mtd, struct erase_info *instr)
{
	struct DiskOnChip *this = (struct DiskOnChip *)mtd->priv;
	unsigned long ofs = instr->addr;
	unsigned long len = instr->len;
	unsigned long docptr;
	struct Nand *mychip;
	
	if(len != mtd->erasesize) 
		printk(KERN_WARNING "Erase not right size (%lx != %lx)n", len, mtd->erasesize);
		

	docptr = this->virtadr;
	
	mychip = &this->chips[ofs >> this->chipshift];
	
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

	DoC_Command(docptr, NAND_CMD_ERASE1, 0);
	DoC_Address(docptr, 2, ofs, 0, 0);
	DoC_Command(docptr, NAND_CMD_ERASE2, 0);

	instr->state = MTD_ERASING;

	DoC_Command(docptr, NAND_CMD_STATUS, CDSN_CTRL_WP);

	if (ReadDOC(docptr, 2k_CDSN_IO) & 1) {
		printk("Error writing\n");
		/* There was an error */
		instr->state = MTD_ERASE_FAILED;
	}
	else
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

#if LINUX_VERSION_CODE < 0x20300
#ifdef MODULE
#define cleanup_doc2000 cleanup_module
#endif
#define __exit
#endif


static void __exit cleanup_doc2000(void)
{
	struct mtd_info *mtd;
	struct DiskOnChip *this;

	while((mtd=doc2klist)) {
		this = (struct DiskOnChip *)mtd->priv;
		doc2klist = this->nextdoc;
			
		del_mtd_device(mtd);
			
		iounmap((void *)this->virtadr);
		kfree(this->chips);
		kfree(mtd);
	}

}

#if LINUX_VERSION_CODE > 0x20300
module_exit(cleanup_doc2000);
#endif

