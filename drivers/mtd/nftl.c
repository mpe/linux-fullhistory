
/* Linux driver for NAND Flash Translation Layer      */
/* (c) 1999 Machine Vision Holdings, Inc.             */
/* Author: David Woodhouse <dwmw2@infradead.org>      */
/* $Id: nftl.c,v 1.35 2000/07/06 14:35:01 dwmw2 Exp $ */

/*
  The contents of this file are distributed under the GNU Public
  Licence version 2 ("GPL"). The legal note below refers only to the
  _use_ of the code in some jurisdictions, and does not in any way
  affect the copying, distribution and modification of this code,
  which is permitted under the terms of the GPL.

  Section 0 of the GPL says:

 "Activities other than copying, distribution and modification are not
  covered by this License; they are outside its scope."

  You may copy, distribute and modify this code to your hearts'
  content - it's just that in some jurisdictions, you may only _use_
  it under the terms of the licence below. This puts it in a similar
  situation to the ISDN code, which you may need telco approval to
  use, and indeed any code which has uses that may be restricted in
  law. For example, certain malicious uses of the networking stack
  may be illegal, but that doesn't prevent the networking code from
  being under GPL.

  In fact the ISDN case is worse than this, because modification of
  the code automatically invalidates its approval. Modificiation,
  unlike usage, _is_ one of the rights which is protected by the
  GPL. Happily, the law in those places where approval is required
  doesn't actually prevent you from modifying the code - it's just
  that you may not be allowed to _use_ it once you've done so - and
  because usage isn't addressed by the GPL, that's just fine.

  dwmw2@infradead.org
  6/7/0

  LEGAL NOTE: The NFTL format is patented by M-Systems.  They have
  granted a licence for its use with their DiskOnChip products:

    "M-Systems grants a royalty-free, non-exclusive license under
    any presently existing M-Systems intellectual property rights
    necessary for the design and development of NFTL-compatible
    drivers, file systems and utilities to use the data formats with, 
    and solely to support, M-Systems' DiskOnChip products"

  A signed copy of this agreement from M-Systems is kept on file by
  Red Hat UK Limited. In the unlikely event that you need access to it,
  please contact dwmw2@redhat.com for assistance.  */

#define PRERELEASE

#ifdef NFTL_DEBUG
#define DEBUGLVL debug
#endif

#include <linux/config.h>
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
#include <linux/mtd/mtd.h>
#include <linux/mtd/nftl.h>
#include <linux/mtd/compatmac.h>

#undef WE_KNOW_WTF_THIS_DOES_NOT_WORK

/* NFTL block device stuff */
#define MAJOR_NR NFTL_MAJOR
#define DEVICE_REQUEST nftl_request
#define DEVICE_OFF(device)
#ifdef WE_KNOW_WTF_THIS_DOES_NOT_WORK
#define LOCAL_END_REQUEST
#endif
#include <linux/blk.h>
#include <linux/hdreg.h>


#ifdef WE_KNOW_WTF_THIS_DOES_NOT_WORK

static void nftl_end_request(struct request *req, int res)
{
	req->sector += req->current_nr_sectors;
	req->nr_sectors -= req->current_nr_sectors;

	if (end_that_request_first( req, res, "nftl" ))
                return;
        end_that_request_last( req );
}
#endif

#ifdef NFTL_DEBUG
static int debug = NFTL_DEBUG;
MODULE_PARM(debug, "i");
#endif



/* Linux-specific block device functions */

/* I _HATE_ the Linux block device setup more than anything else I've ever
 *  encountered, except ...
 */

static int nftl_sizes[256]={0,};
static int nftl_blocksizes[256] = {0,};

/* .. for the Linux partition table handling. */

struct hd_struct part_table[256] = {{0,0},};

#if LINUX_VERSION_CODE < 0x20328
static void dummy_init (struct gendisk *crap)
{}
#endif

static struct gendisk nftl_gendisk = {
        NFTL_MAJOR,     /* Major number */      
        "nftl",         /* Major name */
        4,              /* Bits to shift to get real from partition */
        15,             /* Number of partitions per real */
#if LINUX_VERSION_CODE < 0x20328
        MAX_NFTLS,      /* maximum number of real */
        dummy_init,     /* init function */
#endif
        part_table,     /* hd struct */
        nftl_sizes,     /* block sizes */
        0,              /* number */
        NULL,           /* internal use, not presently used */
        NULL            /* next */
};


struct NFTLrecord *NFTLs[MAX_NFTLS] = {NULL};

static void NFTL_setup(struct mtd_info *mtd, unsigned long ofs, 
		struct NFTLMediaHeader *hdr)
{
	int i;
	struct NFTLrecord *thisNFTL;
	unsigned long temp;
	int firstfree = -1;

	DEBUG(1,"NFTL_setup\n");

	for (i=0; i < MAX_NFTLS; i++) {
		if (!NFTLs[i] && firstfree==-1)
			firstfree = i;
		else if (NFTLs[i] && NFTLs[i]->mtd == mtd && 
			 NFTLs[i]->MediaHdr.FirstPhysicalEUN == hdr->FirstPhysicalEUN) {
			/* This is a Spare Media Header for an NFTL we've already found */
			DEBUG(1, "Spare Media Header for NFTL %d found at %lx\n",i, ofs);
			NFTLs[i]->SpareMediaUnit = ofs / mtd->erasesize;
			return;
		}
	}
	
	
	/* OK, it's a new one. Set up all the data structures. */
#ifdef PSYCHO_DEBUG	
	printk("Found new NFTL nftl%c at offset %lx\n",firstfree + 'a', ofs);
#endif
	if (hdr->UnitSizeFactor != 0xff) {
		printk("Sorry, we don't support UnitSizeFactor of != 1 yet\n");
		return;
	}
	
	thisNFTL = kmalloc(sizeof(struct NFTLrecord), GFP_KERNEL);
	if (!thisNFTL) {
		printk(KERN_WARNING "Out of memory for NFTL data structures\n");
		return;
	}
	init_MUTEX(&thisNFTL->mutex);
	thisNFTL->EraseSize = mtd->erasesize;
	memcpy(&thisNFTL->MediaHdr, hdr, sizeof(*hdr));
	thisNFTL->mtd = mtd;
	thisNFTL->MediaUnit = ofs / mtd->erasesize;
	thisNFTL->SpareMediaUnit = 0xffff;
	thisNFTL->numvunits = le32_to_cpu(thisNFTL->MediaHdr.FormattedSize) / 8192;
	thisNFTL->nr_sects = thisNFTL->numvunits  * (thisNFTL->EraseSize / 512);
	thisNFTL->usecount = 0;

	thisNFTL->cylinders = 1024;
	thisNFTL->heads = 16;

	temp = thisNFTL->cylinders * thisNFTL->heads;
	thisNFTL->sectors = thisNFTL->nr_sects / temp;

	if (thisNFTL->nr_sects % temp) {

		thisNFTL->sectors++;
		temp = thisNFTL->cylinders * thisNFTL->sectors;
		thisNFTL->heads = thisNFTL->nr_sects / temp;

		if (thisNFTL->nr_sects & temp) {
			thisNFTL->heads++;
			temp = thisNFTL->heads * thisNFTL->sectors;

			thisNFTL->cylinders = thisNFTL->nr_sects / temp;
		}
	}
	if (thisNFTL->nr_sects != thisNFTL->heads * thisNFTL->cylinders *
	    thisNFTL->sectors) {
		printk(KERN_WARNING "Cannot calculate an NFTL geometry to match size of 0x%lx.\n", thisNFTL->nr_sects);
		printk(KERN_WARNING "Using C:%d H:%d S:%d (== %lx sects)\n", 
		       thisNFTL->cylinders, thisNFTL->heads , 
		       thisNFTL->sectors, 
		       (long)thisNFTL->cylinders * (long)thisNFTL->heads * 
		       (long)thisNFTL->sectors );

		/* Oh no we don't 
		 * thisNFTL->nr_sects = thisNFTL->heads * thisNFTL->cylinders * thisNFTL->sectors;
		 */
	}

	
	thisNFTL->EUNtable = kmalloc( 2 * thisNFTL->numvunits,
				      GFP_KERNEL);
	if (!thisNFTL->EUNtable) {
		printk("ENOMEM\n");
		kfree(thisNFTL);
		return;
	}
	memset(thisNFTL->EUNtable, 0xff, 2 * thisNFTL->numvunits);
	
	thisNFTL->VirtualUnitTable = kmalloc( 2 * le16_to_cpu(thisNFTL->MediaHdr.NumEraseUnits) , GFP_KERNEL);
	if (!thisNFTL->VirtualUnitTable) {
		printk("ENOMEM\n");
		kfree(thisNFTL->EUNtable);
		kfree(thisNFTL);
		return;
	}
	memset(thisNFTL->VirtualUnitTable, 0xff, 2 * le16_to_cpu(thisNFTL->MediaHdr.NumEraseUnits));
	
	thisNFTL->ReplUnitTable = kmalloc( 2 * le16_to_cpu(thisNFTL->MediaHdr.NumEraseUnits) , GFP_KERNEL);
	if (!thisNFTL->ReplUnitTable) {
		printk("ENOMEM\n");
		kfree(thisNFTL->VirtualUnitTable);
		kfree(thisNFTL->EUNtable);
		kfree(thisNFTL);
		return;
	}
	memset(thisNFTL->ReplUnitTable, 0xff, 2 *le16_to_cpu(thisNFTL->MediaHdr.NumEraseUnits) );
	
	/* Ought to check the media header for bad blocks */
	thisNFTL->lastEUN = le16_to_cpu(thisNFTL->MediaHdr.NumEraseUnits) + 
					le16_to_cpu(thisNFTL->MediaHdr.FirstPhysicalEUN) - 1;
	thisNFTL->numfreeEUNs = 0;

	/* Scan each physical Erase Unit for validity and to find the 
	   Virtual Erase Unit Chain to which it belongs */
	
	for (i=le16_to_cpu(thisNFTL->MediaHdr.FirstPhysicalEUN); 
	     i <= thisNFTL->lastEUN; i++) {
		
		union nftl_uci uci;
		unsigned long ofs;
		size_t retlen;
		ofs = i * thisNFTL->EraseSize;
		
		MTD_READOOB(mtd, (i * thisNFTL->EraseSize) + 512 + 8, 8, &retlen, (char *)&uci);
		
		if (uci.b.EraseMark != cpu_to_le16(0x3c69) || 
		    uci.b.EraseMark1 != cpu_to_le16(0x3c69)) {
			printk("EUN %d: EraseMark not 0x3c69 (0x%4.4x 0x%4.4x instead)\n",
			       i, le16_to_cpu(uci.b.EraseMark), le16_to_cpu(uci.b.EraseMark1));
			thisNFTL->VirtualUnitTable[i] = 0x7fff;
			thisNFTL->ReplUnitTable[i] = 0xffff;
			continue;
		}
		
		MTD_READOOB(mtd, (i * thisNFTL->EraseSize) + 8, 8, &retlen, (u_char *)&uci);
		
		if (uci.a.VirtUnitNum != uci.a.SpareVirtUnitNum)
			printk("EUN %d: VirtualUnitNumber (%x) != SpareVirtualUnitNumber (%x)\n",
			       i, le16_to_cpu(uci.a.VirtUnitNum), 
			       le16_to_cpu(uci.a.SpareVirtUnitNum));
		
		if (uci.a.ReplUnitNum != uci.a.SpareReplUnitNum)
			printk("EUN %d: ReplacementUnitNumber (%x) != SpareReplacementUnitNumber (%x)\n",
			       i, le16_to_cpu(uci.a.ReplUnitNum), 
			       le16_to_cpu(uci.a.SpareReplUnitNum));
		
		/* We don't actually _do_ anything about the above, just whinge */
		
		thisNFTL->VirtualUnitTable[i] = le16_to_cpu(uci.a.VirtUnitNum);
		thisNFTL->ReplUnitTable[i] = le16_to_cpu(uci.a.ReplUnitNum);
		
		/* if (!(VUN & 0x8000) && VUN < (arraybounds)).. optimises to: */
		if (le16_to_cpu(uci.a.VirtUnitNum) < thisNFTL->numvunits) 
			thisNFTL->EUNtable[le16_to_cpu(uci.a.VirtUnitNum) & 0x7fff] = i;

		if (uci.a.VirtUnitNum == 0xffff) {
			/* Free block */
			thisNFTL->LastFreeEUN = i;
			thisNFTL->numfreeEUNs++;
		}
		
	} 
	NFTLs[firstfree] = thisNFTL;
	thisNFTL->LastFreeEUN = le16_to_cpu(thisNFTL->MediaHdr.FirstPhysicalEUN);
	
	//#define PSYCHO_DEBUG	
#ifdef PSYCHO_DEBUG
	for (i=0; i < 10/* thisNFTL->numvunits*/; i++) {
		u16 curEUN = thisNFTL->EUNtable[i];
		int sillycount=100;
		
		printk("Virtual Unit #%d: ",i);
		if (!curEUN || curEUN == 0xffff) {
			printk("Not present\n");
			continue;
		}
		printk("%d", curEUN);
		
		while ((curEUN = thisNFTL->ReplUnitTable[curEUN]) != 0xffff && --sillycount) {
			printk(", %d", curEUN & 0xffff);
			
		}
		printk("\n");
	}
#endif

	/* OK. Now we deal with the fact that we're in the real world. Sometimes 
	   things don't actually happen the way they're supposed to. Find, fix,
	   and whinge about the most common deviations from spec that we have
	   been known to encounter.
	*/
	/* Except that I haven't implemented that bit yet :) */

	/* Finally, set up the block device sizes */
	nftl_sizes[firstfree * 16]=thisNFTL->nr_sects;
//	nftl_blocksizes[firstfree*16] = 512;
	part_table[firstfree * 16].nr_sects = thisNFTL->nr_sects;
#if LINUX_VERSION_CODE < 0x20328
	resetup_one_dev(&nftl_gendisk, firstfree);
#else
	grok_partitions(&nftl_gendisk, firstfree, 1<<4, thisNFTL->nr_sects);
#endif

}


static void NFTL_unsetup(int i)
{
	struct NFTLrecord *thisNFTL = NFTLs[i];

	DEBUG(1, "NFTL_unsetup %d\n", i);
	
	NFTLs[i] = NULL;
	
	if (thisNFTL->VirtualUnitTable)
		kfree(thisNFTL->VirtualUnitTable);
	if (thisNFTL->ReplUnitTable)
		kfree(thisNFTL->ReplUnitTable);
	if (thisNFTL->EUNtable)
		kfree(thisNFTL->EUNtable);
		      
	kfree(thisNFTL);
}




/* Search the MTD device for NFTL partitions */
static void NFTL_notify_add(struct mtd_info *mtd)
{
	int i;
	unsigned long ofs;
	struct NFTLMediaHeader hdr;

	DEBUG(1, "NFTL_notify_add for %s\n", mtd->name);

	if (mtd) {
		if (!mtd->read_oob) /* If this MTD doesn't have out-of-band data,
				       then there's no point continuing */
		{
			DEBUG(1, "No OOB data, quitting\n");
			return;
		}
		DEBUG(3, "mtd->read = %p,size = %d, erasesize = %d\n", 
					mtd->read, mtd->size, mtd->erasesize);	
		for (ofs = 0; ofs < mtd->size ; ofs += mtd->erasesize) {
			size_t retlen = 0;
			MTD_READ(mtd, ofs, sizeof(hdr), &retlen, (u_char *)&hdr);

			if (retlen < sizeof(hdr))
			{	
				continue;
			}
			
			if (!strncmp(hdr.DataOrgID, "ANAND", 6)) {
				DEBUG(2, "Valid NFTL partition at ofs %ld\n", ofs);	
				NFTL_setup(mtd, ofs, &hdr);
			}
			else {
				DEBUG(3,"No valid NFTL Partition at ofs %d\n", ofs);
				for(i = 0; i < 6; i++) {
				    DEBUG(3,"%x, ", hdr.DataOrgID[i]);
				}
				DEBUG(3," = %s\n", hdr.DataOrgID);
				DEBUG(3,"%d, %d, %d, %d\n", hdr.NumEraseUnits, hdr.FirstPhysicalEUN,
					hdr.FormattedSize, hdr.UnitSizeFactor);

			}
		}
		return;
	}
}

static void NFTL_notify_remove(struct mtd_info *mtd)
{
	int i;

	for (i=0; i< MAX_NFTLS; i++) {
		if (NFTLs[i] && NFTLs[i]->mtd == mtd)
			NFTL_unsetup(i);
	}
}


#ifdef CONFIG_NFTL_RW

/* Actual NFTL access routines */


static u16 NFTL_findfreeblock( struct NFTLrecord *thisNFTL, int desperate )
{
	/* For a given Virtual Unit Chain: find or create a free block and
	   add it to the chain */
	/* We're passed the number of the last EUN in the chain, to save us from
	   having to look it up again */
	
	u16 pot = thisNFTL->LastFreeEUN;
	int silly = -1;

	/* Normally, we force a fold to happen before we run out of free blocks completely */

	if (!desperate && thisNFTL->numfreeEUNs < 2) {
		//		printk("NFTL_findfreeblock: there are too few free EUNs\n");
		return 0xffff;
	}

	/* Scan for a free block */

	do {
		if (thisNFTL->VirtualUnitTable[pot] == 0xffff) {
			thisNFTL->LastFreeEUN = pot;
			thisNFTL->numfreeEUNs--;
			return pot;
		}

		if (++pot > thisNFTL->lastEUN)
			pot = le16_to_cpu(thisNFTL->MediaHdr.FirstPhysicalEUN);

		if (!silly--) {
			printk("Tell Dave he fucked up. LastFreeEUN = %d, FirstEUN = %d\n",
			       thisNFTL->LastFreeEUN, le16_to_cpu(thisNFTL->MediaHdr.FirstPhysicalEUN));
			return 0xffff;
		}
			
	} while (pot != thisNFTL->LastFreeEUN);

	return 0xffff;
}





static u16 NFTL_foldchain (struct NFTLrecord *thisNFTL, u16 thisVUC, unsigned pendingblock )
{
	u16 BlockMap[thisNFTL->EraseSize / 512];
	unsigned char BlockLastState[thisNFTL->EraseSize / 512];
	unsigned char BlockFreeFound[thisNFTL->EraseSize / 512];
	u16 thisEUN;
	int block;
	int silly = -1;
	u16 targetEUN = 0xffff;
	struct nftl_oob oob;
	int inplace = 1;

	memset(BlockMap, 0xff, sizeof(BlockMap));
	memset(BlockFreeFound, 0, sizeof(BlockFreeFound));

	thisEUN = thisNFTL->EUNtable[thisVUC];

	if (thisEUN == 0xffff) {
		printk(KERN_WARNING "Trying to fold non-existent Virtual Unit Chain %d!\n", thisVUC);
		return 0xffff;
	}
	
	/* Scan to find the Erase Unit which holds the actual data for each
	   512-byte block within the Chain.
	*/

	while( thisEUN <= thisNFTL->lastEUN ) {
		size_t retlen;
		
		targetEUN = thisEUN;

		for (block = 0 ; block < thisNFTL->EraseSize / 512; block ++) {

			MTD_READOOB(thisNFTL->mtd, (thisEUN * thisNFTL->EraseSize) + (block * 512),16 , &retlen, (char *)&oob);

			if (block == 2) {
				if (oob.u.c.WriteInh != 0xffffffff) {
					printk("Write Inhibited on EUN %d\n", thisEUN);
					inplace = 0;
				} else {
					/* There's no other reason not to do inplace,
					   except ones that come later. So we don't need
					   to preserve inplace */
					inplace = 1;
				}
			}

			BlockLastState[block] = (unsigned char) oob.b.Status & 0xff;

			switch(oob.b.Status) {
			case __constant_cpu_to_le16(BLOCK_FREE):
				BlockFreeFound[block]=1;
				break;

			case __constant_cpu_to_le16(BLOCK_USED):
				if (!BlockFreeFound[block])
					BlockMap[block] = thisEUN;
				else
					printk(KERN_WARNING "BLOCK_USED found after BLOCK_FREE in Virtual Unit Chain %d for block %d\n", thisVUC, block);
				break;
			case __constant_cpu_to_le16(BLOCK_IGNORE):
			case __constant_cpu_to_le16(BLOCK_DELETED):
				break;
			default:
				printk("Unknown status for block %d in EUN %d: %x\n",block,thisEUN, oob.b.Status);
			}
		}

		if (!silly--) {
			printk(KERN_WARNING "Infinite loop in Virtual Unit Chain 0x%x\n", thisVUC);
			return 0xffff;
		}
		
		thisEUN = thisNFTL->ReplUnitTable[thisEUN] & 0x7fff;
	}

	if (inplace) {
		/* We're being asked to be a fold-in-place. Check
		   that all blocks are either present or BLOCK_FREE
		   in the target block. If not, we're going to have
		   to fold out-of-place anyway.
		*/

		for (block = 0; block < thisNFTL->EraseSize / 512 ; block++) {
		
			if (BlockLastState[block] != (unsigned char) (cpu_to_le16(BLOCK_FREE) & 0xff) &&
			    BlockMap[block] != targetEUN) {
				DEBUG(1, "Setting inplace to 0. VUC %d, block %d was %x lastEUN, and is in EUN %d (%s) %d\n",
				     thisVUC, block, BlockLastState[block], BlockMap[block] , BlockMap[block]==targetEUN?"==":"!=", targetEUN);

				inplace = 0;
				break;
			}
		}

		if ( pendingblock >= (thisVUC * (thisNFTL->EraseSize / 512)) &&
		     pendingblock < ((thisVUC + 1)* (thisNFTL->EraseSize / 512)) &&
		     BlockLastState[ pendingblock - (thisVUC * (thisNFTL->EraseSize / 512))] != 
		     (unsigned char) (cpu_to_le16(BLOCK_FREE) & 0xff)) {
			DEBUG(1, "Pending write not free in EUN %d. Folding out of place.\n", targetEUN);
			inplace = 0;
		}

	}
	
	if (!inplace) {
		DEBUG(1, "Cannot fold Virtual Unit Chain %d in place. Trying out-of-place\n", thisVUC);
		/* We need to find a targetEUN to fold into. */
		targetEUN = NFTL_findfreeblock(thisNFTL, 1);
		if (targetEUN == 0xffff) {
				/* Ouch. Now we're screwed. We need to do a 
				   fold-in-place of another chain to make room
				   for this one. We need a better way of selecting
				   which chain to fold, because makefreeblock will 
				   only ask us to fold the same one again.
				*/
			printk(KERN_WARNING"NFTL_findfreeblock(desperate) returns 0xffff.\n");
			return 0xffff;
		}
		
	} 


	/* OK. We now know the location of every block in the Virtual Unit Chain,
	   and the Erase Unit into which we are supposed to be copying.
	   Go for it.
	*/

	DEBUG(1,"Folding chain %d into unit %d\n", thisVUC, targetEUN);

	for (block = 0; block < thisNFTL->EraseSize / 512 ; block++) {
		unsigned char movebuf[512];
		struct nftl_oob oob;
		size_t retlen;

		memset(&oob, 0xff, sizeof(oob));

		/* If it's in the target EUN already, or if it's pending write, do nothing */
		if (BlockMap[block] == targetEUN ||(pendingblock == (thisVUC * (thisNFTL->EraseSize / 512) + block))) {
			/* Except if it's the first block, in which case we have to
			   set the UnitNumbers */
			if (block == 0) { 
				
				thisNFTL->mtd->read_oob(thisNFTL->mtd, (thisNFTL->EraseSize * targetEUN) ,
						16, &retlen, (char *)&oob);

				//				printk("Setting VirtUnitNum on EUN %d to %x, was %x\n", targetEUN, thisVUC, 
				//			       le16_to_cpu(oob.u.a.VirtUnitNum));

				oob.u.a.VirtUnitNum = oob.u.a.SpareVirtUnitNum = cpu_to_le16(thisVUC & 0x7fff);

				thisNFTL->mtd->write_oob(thisNFTL->mtd, (thisNFTL->EraseSize * targetEUN) ,
							 16, &retlen, (char *)&oob);
			}
			continue;
		}

		oob.b.Status = BLOCK_USED;

		switch(block) {
		case 0:
			oob.u.a.VirtUnitNum = oob.u.a.SpareVirtUnitNum = cpu_to_le16(thisVUC & 0x7fff);
			//		printk("Setting VirtUnitNum on EUN %d to %x\n", targetEUN, thisVUC);
			
			oob.u.a.ReplUnitNum = oob.u.a.SpareReplUnitNum = 0xffff;
			break;
				
		case 1:
			oob.u.b.WearInfo = cpu_to_le32(3); // We don't use this, but M-Systems' drivers do
			oob.u.b.EraseMark = oob.u.b.EraseMark1 = cpu_to_le16(0x3c69);
			break;
			
		case 2:
		default:
			oob.u.c.WriteInh = 0xffffffff;
			oob.u.c.unused = 0xffffffff;
		}
		if (thisNFTL->mtd->read_ecc(thisNFTL->mtd, (thisNFTL->EraseSize * BlockMap[block]) + (block * 512),
					    512, &retlen, movebuf, (char *)&oob) == -EIO) {
			if (thisNFTL->mtd->read_ecc(thisNFTL->mtd, (thisNFTL->EraseSize * BlockMap[block]) + (block * 512),
						    512, &retlen, movebuf, (char *)&oob) != -EIO) 
				printk("Error went away on retry.\n");
		}			

		thisNFTL->mtd->write_ecc(thisNFTL->mtd, (thisNFTL->EraseSize * targetEUN) + (block * 512),
					 512, &retlen, movebuf, (char *)&oob);

		
		/* FIXME: Add some error checking.... */
		thisNFTL->mtd->write_oob(thisNFTL->mtd, (thisNFTL->EraseSize * targetEUN) + (block * 512), 
					 16, &retlen, (char *)&oob);

	}

	/* OK. We've moved the whole lot into the new block. Now we have to free the original blocks. */

	/* At this point, we have two different chains for this Virtual Unit, and no way to tell 
	   them apart. If we crash now, we get confused. However, both contain the same data, so we
	   shouldn't actually lose data in this case. It's just that when we load up on a medium which
	   has duplicate chains, we need to free one of the chains because it's not necessary any more.
	*/
	

	thisEUN = thisNFTL->EUNtable[thisVUC];

	DEBUG(1,"Want to erase\n");
	/* For each block in the old chain (except the targetEUN of course), 
	   free it and make it available for future use */

	while( thisEUN <= thisNFTL->lastEUN && thisEUN != targetEUN) {
		size_t retlen;
		struct erase_info *instr;
		u16 EUNtmp;

		instr = kmalloc(sizeof(struct erase_info), GFP_KERNEL);
		if (!instr) {
			printk(KERN_WARNING "Out of memory for struct erase_info\n");

			EUNtmp = thisEUN;

			thisEUN = thisNFTL->ReplUnitTable[EUNtmp] & 0x7fff;
			thisNFTL->VirtualUnitTable[EUNtmp] = 0x7fff;
			thisNFTL->ReplUnitTable[EUNtmp] = 0xffff;
		} else {
			memset(instr, 0, sizeof(struct erase_info));
			instr->addr = thisEUN * thisNFTL->EraseSize;
			instr->len = thisNFTL->EraseSize;

			MTD_ERASE(thisNFTL->mtd,  instr);
			/* This is an async interface. Or will be. At which point
			   this code will break. */
			
#if 0
			MTD_READOOB(thisNFTL->mtd, (thisEUN * thisNFTL->EraseSize) + 512, 16, &retlen, (char *)&oob);

			printk("After erasing, EUN %d contains: %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X\n", 
			       thisEUN, oob.b.ECCSig[0],
			       oob.b.ECCSig[1],
			       oob.b.ECCSig[2],
			       oob.b.ECCSig[3],
			       oob.b.ECCSig[4],
			       oob.b.ECCSig[5]);
#endif
			memset(&oob, 0xff, sizeof(oob));
			oob.u.b.WearInfo = cpu_to_le32(3);
			oob.u.b.EraseMark = oob.u.b.EraseMark1 = cpu_to_le16(0x3c69);

			MTD_WRITEOOB(thisNFTL->mtd, (thisEUN * thisNFTL->EraseSize) + 512, 16, &retlen, (char *)&oob);

			EUNtmp = thisEUN;

			thisEUN = thisNFTL->ReplUnitTable[EUNtmp] & 0x7fff;
			thisNFTL->VirtualUnitTable[EUNtmp] = 0xffff;
			thisNFTL->ReplUnitTable[EUNtmp] = 0xffff;

			thisNFTL->numfreeEUNs++;

		}
		
		// shifted upwards:	thisEUN = thisNFTL->ReplUnitTable[thisEUN] & 0x7fff;

	}
	
	/* Make this the new start of chain for thisVUC */
	thisNFTL->VirtualUnitTable[targetEUN] = thisVUC;
	thisNFTL->ReplUnitTable[targetEUN] = 0xffff;

	thisNFTL->EUNtable[thisVUC] = targetEUN;
	return targetEUN;
	
}

u16 NFTL_makefreeblock( struct NFTLrecord *thisNFTL , unsigned pendingblock)
{
	/* This is the part that needs some cleverness applied. 
	   For now, I'm doing the minimum applicable to actually
	   get the thing to work.
	   Wear-levelling and other clever stuff needs to be implemented
	   and we also need to do some assessment of the results when
	   the system loses power half-way through the routine.
	*/

	u16 LongestChain = 0;
	u16 ChainLength = 0, thislen;
	u16 chain, EUN;


	for (chain=0; chain < thisNFTL->MediaHdr.FormattedSize / thisNFTL->EraseSize; chain++) {
		EUN = thisNFTL->EUNtable[chain];

		thislen = 0;

		while (EUN <= thisNFTL->lastEUN) {
			thislen++;
			//			printk("VUC %d reaches len %d with EUN %d\n", chain, thislen, EUN);
			EUN = thisNFTL->ReplUnitTable[EUN] & 0x7fff;
			if (thislen > 0xff00) {
				printk("Endless loop in Virtual Chain %d: Unit %x\n", chain, EUN);
			}
			if (thislen > 0xff10) {
				/* Actually, don't return failure. Just ignore this chain and
				   get on with it. */
				thislen = 0;
				break;
			}
				
		}


		if (thislen > ChainLength) {
			//			printk("New longest chain is %d with length %d\n", chain, thislen);
			ChainLength = thislen;
			LongestChain = chain;
		}
	}		

	if (ChainLength < 2) {
		printk(KERN_WARNING "No Virtual Unit Chains available for folding. Failing request\n");
		return 0xffff;
	}
		
	return NFTL_foldchain (thisNFTL, LongestChain, pendingblock);
}

/* NFTL_findwriteunit: Return the unit number into which we can write 
                       for this block. Make it available if it isn't already
*/

static inline u16 NFTL_findwriteunit(struct NFTLrecord *thisNFTL, unsigned block)
{
	u16 lastEUN;
	u16 thisVUC = block / (thisNFTL->EraseSize / 512);
	u16 writeEUN;
	unsigned long blockofs = (block * 512) & (thisNFTL->EraseSize -1);
	size_t retlen;
	int silly = 0x10000, silly2 = 3;
	struct nftl_oob oob;
	int debug=0;

	do {

		/* Scan the media to find a unit in the VUC which has
		   a free space for the block in question.
		*/

		/* This condition catches the 0x[7f]fff cases, as well as 
		   being a sanity check for past-end-of-media access
		*/
		lastEUN = 0xffff;
		writeEUN = thisNFTL->EUNtable[thisVUC];

		while(writeEUN <= thisNFTL->lastEUN) {
			struct nftl_bci bci;
			size_t retlen;
			
			lastEUN = writeEUN;
			
			MTD_READOOB(thisNFTL->mtd, (writeEUN * thisNFTL->EraseSize) 
				    + blockofs,8, &retlen, (char *)&bci);
			
			if (debug) 
				printk("Status of block %d in EUN %d is %x\n", block , writeEUN, le16_to_cpu(bci.Status));

			switch(bci.Status) {
			case __constant_cpu_to_le16(BLOCK_FREE):
				return writeEUN;

			case __constant_cpu_to_le16(BLOCK_DELETED):
			case __constant_cpu_to_le16(BLOCK_USED):
			case __constant_cpu_to_le16(BLOCK_IGNORE):
				break;
			default:
				// Invalid block. Don't use it any more. Must implement.
				break;			
			}
			
			if (!silly--) { 
				printk(KERN_WARNING "Infinite loop in Virtual Unit Chain 0x%x\n", thisVUC);
				return 0xffff;
			}

			/* Skip to next block in chain */

			writeEUN = thisNFTL->ReplUnitTable[writeEUN] & 0x7fff;
		}

		/* OK. We didn't find one in the existing chain, or there 
		   is no existing chain. */

		/* Try to find an already-free block */

		writeEUN = NFTL_findfreeblock(thisNFTL, 0);

		if (writeEUN == 0xffff) {
			/* That didn't work - there were no free blocks just
			   waiting to be picked up. We're going to have to fold
			   a chain to make room.
			*/

			/* First remember the start of this chain */
			//			u16 startEUN = thisNFTL->EUNtable[thisVUC];
			
			//printk("Write to VirtualUnitChain %d, calling makefreeblock()\n", thisVUC);
			writeEUN = NFTL_makefreeblock(thisNFTL, block);
			
			if (writeEUN == 0xffff) {
				/* Ouch. This should never happen - we should
				   always be able to make some room somehow. 
				   If we get here, we've allocated more storage 
				   space than actual media, or our makefreeblock
				   routine is missing something.
				*/
				printk(KERN_WARNING "Cannot make free space.\n");
				return 0xffff;
			}			
			//			printk("Restarting scan\n");
			lastEUN = 0xffff;
			//			debug = 1;
			continue;
#if 0
			if (startEUN != thisNFTL->EUNtable[thisVUC]) {
				/* The fold operation has moved the chain 
				   that we're looking at. Start the scan again.
				*/
				continue;
			}
#endif
		}

		/* We've found a free block. Insert it into the chain. */
		
		if (lastEUN != 0xffff) {
			/* Addition to an existing chain. Make the previous
			   last block in the chain point to this one.
			*/

			//printk("Linking EUN %d to EUN %d in VUC %d\n", 
			//			       lastEUN, writeEUN, thisVUC);
			/* Both in our cache... */
			thisNFTL->ReplUnitTable[lastEUN] = writeEUN;


			/* ... and on the flash itself */
			MTD_READOOB(thisNFTL->mtd, (lastEUN * thisNFTL->EraseSize), 16, &retlen,
				    (char *)&oob);

			oob.u.a.ReplUnitNum = oob.u.a.SpareReplUnitNum = cpu_to_le16(writeEUN);

			MTD_WRITEOOB(thisNFTL->mtd, (lastEUN * thisNFTL->EraseSize), 16, &retlen,
			     (char *)&oob);

			thisVUC |= 0x8000; /* It's a replacement block */
		} else {
			/* The first block in a new chain */
			thisNFTL->EUNtable[thisVUC] = writeEUN;
		}

		/* Now set up the actual EUN we're writing into */

			/* Both in our cache... */
		thisNFTL->VirtualUnitTable[writeEUN] = thisVUC;
		thisNFTL->ReplUnitTable[writeEUN] = 0xffff;

			/* ... and on the flash itself */
		MTD_READOOB(thisNFTL->mtd, writeEUN * thisNFTL->EraseSize, 16,
			    &retlen, (char *)&oob);

		oob.u.a.VirtUnitNum = oob.u.a.SpareVirtUnitNum = cpu_to_le16(thisVUC);

		MTD_WRITEOOB(thisNFTL->mtd, writeEUN * thisNFTL->EraseSize, 16,
			    &retlen, (char *)&oob);

		return writeEUN;

	} while (silly2--);

	printk(KERN_WARNING "Error folding to make room for Virtual Unit Chain 0x%x\n", thisVUC);
	return 0xffff;
}

static int NFTL_writeblock(struct NFTLrecord *thisNFTL, unsigned block,
			   char *buffer)
{
	u16 writeEUN;
	unsigned long blockofs = (block * 512) & (thisNFTL->EraseSize -1);
	size_t retlen;
	u16 eccbuf[8];

	//	if (thisEUN == 0xffff) thisEUN = 0;

	writeEUN = NFTL_findwriteunit(thisNFTL, block);

//	printk("writeblock(%d): Write to Unit %d\n", block, writeEUN);

	if (writeEUN == 0xffff) {
		printk(KERN_WARNING "NFTL_writeblock(): Cannot find block to write to\n");
		/* If we _still_ haven't got a block to use, we're screwed */
		return 1;
	}
//		printk("Writing block %lx to EUN %x\n",block, writeEUN);


	thisNFTL->mtd->write_ecc(thisNFTL->mtd, 
				(writeEUN * thisNFTL->EraseSize) + blockofs,
				512, &retlen, (char *)buffer, (char *)eccbuf);
	eccbuf[3] = BLOCK_USED;
	eccbuf[4] = eccbuf[5] = eccbuf[6] = eccbuf[7] = 0xffff;

	thisNFTL->mtd->write_oob(thisNFTL->mtd,
				 (writeEUN * thisNFTL->EraseSize) + blockofs,
				 16, &retlen, (char *)eccbuf);

	return 0;
}

#endif /* CONFIG_NFTL_RW */

static int NFTL_readblock(struct NFTLrecord *thisNFTL, 
			  unsigned block, char *buffer)
{
	u16 lastgoodEUN = 0xffff;
	u16 thisEUN = thisNFTL->EUNtable[block / (thisNFTL->EraseSize / 512)];
	unsigned long blockofs = (block * 512) & (thisNFTL->EraseSize -1);

	int silly = -1;

	if (thisEUN == 0xffff) thisEUN = 0;
	
	while(thisEUN && (thisEUN & 0x7fff) != 0x7fff) {
		struct nftl_bci bci;
		size_t retlen;
		
		MTD_READOOB(thisNFTL->mtd, (thisEUN * thisNFTL->EraseSize) + blockofs,8, &retlen, (char *)&bci);
		
		switch(bci.Status) {
		case __constant_cpu_to_le16(BLOCK_FREE):
			thisEUN = 0;
			break;
		case __constant_cpu_to_le16(BLOCK_USED):
			lastgoodEUN = thisEUN;
			break;
		case __constant_cpu_to_le16(BLOCK_IGNORE):
		case __constant_cpu_to_le16(BLOCK_DELETED):
			break;
		default:
			printk("Unknown status for block %d in EUN %d: %x\n",block,thisEUN, bci.Status);
		}

		if (!silly--) {
			printk(KERN_WARNING "Infinite loop in Virtual Unit Chain 0x%x\n",block / (thisNFTL->EraseSize / 512));
			return 1;
		}
		if (thisEUN)
			thisEUN = thisNFTL->ReplUnitTable[thisEUN] & 0x7fff;
	}
	if (lastgoodEUN == 0xffff) {
		memset(buffer, 0, 512);
	} else {
		loff_t ptr = (lastgoodEUN * thisNFTL->EraseSize) + blockofs;
		size_t retlen;
		u_char eccbuf[6];
		thisNFTL->mtd->read_ecc(thisNFTL->mtd, ptr, 512, &retlen, buffer, eccbuf);
	}
	return 0;
}


static int nftl_ioctl(struct inode * inode, struct file * file,
		      unsigned int cmd, unsigned long arg)
{
	struct NFTLrecord *thisNFTL;

	thisNFTL = NFTLs[MINOR(inode->i_rdev) / 16];

	if (!thisNFTL) return -EINVAL;


	switch (cmd) {
	case HDIO_GETGEO: {
		struct hd_geometry g;

		g.heads = thisNFTL->heads;
		g.sectors = thisNFTL->sectors;
		g.cylinders = thisNFTL->cylinders;
		g.start = part_table[MINOR(inode->i_rdev)].start_sect;
		return copy_to_user((void *)arg, &g, sizeof g) ? -EFAULT : 0;
	}
	case BLKGETSIZE:   /* Return device size */
		if (!arg)  return -EINVAL;
		return put_user(part_table[MINOR(inode->i_rdev)].nr_sects,
                                (long *) arg);
		
	case BLKFLSBUF:
		if(!capable(CAP_SYS_ADMIN))  return -EACCES;
		fsync_dev(inode->i_rdev);
		invalidate_buffers(inode->i_rdev);
		if (thisNFTL->mtd->sync)
			thisNFTL->mtd->sync(thisNFTL->mtd);
		return 0;

	case BLKRRPART:
		if (!capable(CAP_SYS_ADMIN)) return -EACCES;
		if (thisNFTL->usecount > 1) {
			//			printk("Use count %d\n", thisNFTL->usecount);
			return -EBUSY;
		}
#if LINUX_VERSION_CODE < 0x20328
		resetup_one_dev(&nftl_gendisk, MINOR(inode->i_dev) / 16);
#else
		grok_partitions(&nftl_gendisk, MINOR(inode->i_dev) / 16, 1<<4, thisNFTL->nr_sects);
#endif
		return 0;
		
		//        RO_IOCTLS(inode->i_rdev, arg);  /* ref. linux/blk.h */
	default:
		return -EINVAL;
	}
}


void nftl_request(RQFUNC_ARG)
{
	unsigned int dev, block, nsect;
	struct NFTLrecord *thisNFTL;
	char *buffer;
	struct request *req;
	int res;

	while (1) {
		INIT_REQUEST;	/* blk.h */
		
		req = CURRENT;
#ifdef WE_KNOW_WTF_THIS_DOES_NOT_WORK
		blkdev_dequeue_request(req);
		spin_unlock_irq(&io_request_lock);
#else
		req = CURRENT;
#endif		
		
		DEBUG(2,"NFTL_request\n");
		DEBUG(3,"NFTL %d request, %lx, %lx", req->cmd, 
		       req->sector, req->current_nr_sectors);
		dev = MINOR(req->rq_dev);
		block = req->sector;
		nsect = req->current_nr_sectors;
		buffer = req->buffer;
		res = 1; /* succeed */

		if (dev >= MAX_NFTLS * 16) {
			printk("fl: bad minor number: device=%s\n",
			       kdevname(req->rq_dev));
			res = 0; /* fail */
			goto repeat;
		}
		
		thisNFTL = NFTLs[dev / 16];
		DEBUG(3,"Waiting for mutex\n");
		down(&thisNFTL->mutex);
		DEBUG(3,"Got mutex\n");
		
		if (block + nsect >= part_table[dev].nr_sects) {
			printk("nftl%c%d: bad access: block=%d, count=%d\n",
			       (MINOR(req->rq_dev)>>6)+'a', dev & 0xf, block, nsect);
			up(&thisNFTL->mutex);
			res = 0; /* fail */
			goto repeat;
		}
		
		block += part_table[dev].start_sect;
		
		if (req->cmd == READ) {
			DEBUG(2,"NFTL read\n");	
			for ( ; nsect > 0; nsect-- , block++, buffer+= 512) {
				/* Read a single sector to req->buffer + (512 * i) */
				
				if (NFTL_readblock(thisNFTL, block, buffer)) {
					DEBUG(2,"NFTL read request failed\n");
					up(&thisNFTL->mutex);
					res = 0;
					goto repeat;
				}
			}
			DEBUG(2,"NFTL read request completed OK\n");
			up(&thisNFTL->mutex);
			goto repeat;
		}
		else if (req->cmd == WRITE) {
			DEBUG(2,"NFTL write request of 0x%x sectors @ %x (req->nr_sectors == %lx\n",nsect, block, req->nr_sectors);
#ifdef CONFIG_NFTL_RW
			for ( ; nsect > 0; nsect-- , block++, buffer+= 512) {
				/* Read a single sector to req->buffer + (512 * i) */
				
				if (NFTL_writeblock(thisNFTL, block, buffer)) {
					DEBUG(1,"NFTL write request failed\n");
					
					up(&thisNFTL->mutex);
					res = 0;
					goto repeat;
				}
			}
			DEBUG(2,"NFTL write request completed OK\n");
#else
			res=0; /* Writes always fail */
#endif /* CONFIG_NFTL_RW */
			up(&thisNFTL->mutex);
			goto repeat;
		}
		else {
			DEBUG(0,"NFTL ??? request\n");
			up(&thisNFTL->mutex);
			res = 0;
			goto repeat;
		}
	repeat: 
		DEBUG(3,"end_request(%d)\n", res);
#ifdef WE_KNOW_WTF_THIS_DOES_NOT_WORK
		spin_lock_irq(&io_request_lock);
		nftl_end_request(req, res);
#else
		end_request(res);
#endif
	}
}

static int nftl_open(struct inode *ip, struct file *fp)
{
	struct NFTLrecord *thisNFTL;
	thisNFTL = NFTLs[MINOR(ip->i_rdev) / 16];

	DEBUG(2,"NFTL_open\n");

	if (!thisNFTL) {
		DEBUG(2,"ENODEV: thisNFTL = %d, minor = %d, ip = %p, fp = %p\n", 
		      MINOR(ip->i_rdev) / 16,ip->i_rdev,ip, fp);
		return -ENODEV;
	}
#ifndef CONFIG_NFTL_RW
	if (fp->f_mode & FMODE_WRITE)
	    return -EROFS;
#endif /* !CONFIG_NFTL_RW */
	thisNFTL->usecount++;
	MOD_INC_USE_COUNT;
	if (!get_mtd_device(thisNFTL->mtd, -1)) {
		MOD_DEC_USE_COUNT;
		return /* -E'SBUGGEREDOFF */ -ENXIO;
	}

	return 0;
}

static int nftl_release(struct inode *inode, struct file *fp)
{
	struct super_block *sb = get_super(inode->i_rdev);
	struct NFTLrecord *thisNFTL;

	thisNFTL = NFTLs[MINOR(inode->i_rdev) / 16];

	DEBUG(2, "NFTL_release\n");
	
	fsync_dev(inode->i_rdev);
	if (sb)
		invalidate_inodes(sb);
	invalidate_buffers(inode->i_rdev);

	if (thisNFTL->mtd->sync)
		thisNFTL->mtd->sync(thisNFTL->mtd);
	thisNFTL->usecount--;
	MOD_DEC_USE_COUNT;

	put_mtd_device(thisNFTL->mtd);

	return 0;
}
#if LINUX_VERSION_CODE < 0x20326
static struct file_operations nftl_fops = {
        NULL,           /* lseek - default */
        block_read,     /* read - block dev read */
        block_write,    /* write - block dev write */
        NULL,           /* readdir - not here! */
        NULL,           /* select */
        nftl_ioctl,     /* ioctl */
        NULL,           /* mmap */
        nftl_open,           /* open */
        NULL,           /* flush */
        nftl_release,           /* no special release code... */
        block_fsync     /* fsync */
};
#else
static struct block_device_operations nftl_fops = 
{
	open: nftl_open,
	release: nftl_release,
	ioctl: nftl_ioctl
};
#endif



/****************************************************************************
 *
 * Module stuff
 *
 ****************************************************************************/

#if LINUX_VERSION_CODE < 0x20300
#ifdef MODULE
#define init_nftl init_module
#define cleanup_nftl cleanup_module
#endif
#define __exit
#endif

static struct mtd_notifier nftl_notifier = {NFTL_notify_add, NFTL_notify_remove, NULL};


/* static int __init init_nftl(void) */
int __init init_nftl(void)
{
	int i;

	printk(KERN_NOTICE "M-Systems NAND Flash Translation Layer driver. (C) 1999 MVHI\n");
#ifdef PRERELEASE 
	printk(KERN_INFO"$Id: nftl.c,v 1.35 2000/07/06 14:35:01 dwmw2 Exp $\n");
#endif

	if (register_blkdev(NFTL_MAJOR, "nftl", &nftl_fops)){
		printk("unable to register NFTL block device\n");
	} else {
#if LINUX_VERSION_CODE < 0x20320
	  blk_dev[MAJOR_NR].request_fn = nftl_request;
#else
	  blk_init_queue(BLK_DEFAULT_QUEUE(MAJOR_NR), &nftl_request);
#endif
		for (i=0; i < 256 ; i++) {
			nftl_blocksizes[i] = 1024;
		}
		blksize_size[NFTL_MAJOR] = nftl_blocksizes;
		nftl_gendisk.next = gendisk_head;
		gendisk_head = &nftl_gendisk;
	}
	
	register_mtd_user(&nftl_notifier);

	return 0;
}

static void __exit cleanup_nftl(void)
{
  struct gendisk *gd, **gdp;

  unregister_mtd_user(&nftl_notifier);

  unregister_blkdev(NFTL_MAJOR, "nftl");
#if LINUX_VERSION_CODE < 0x20320
  blk_dev[MAJOR_NR].request_fn = 0;
#else
  blk_cleanup_queue(BLK_DEFAULT_QUEUE(MAJOR_NR));
#endif	
  for (gdp = &gendisk_head; *gdp; gdp = &((*gdp)->next))
    if (*gdp == &nftl_gendisk) {
      gd = *gdp; *gdp = gd->next;
      break;
    }
  
}

#if LINUX_VERSION_CODE > 0x20300
module_init(init_nftl);
module_exit(cleanup_nftl);
#endif
