  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/blk.h>
#include <linux/genhd.h>
#include <asm/pgtable.h>
#include <asm/ap1000/apreg.h>
#include <asm/ap1000/DdvReqTable.h>


#define GENDISK_STRUCT ddv_gendisk

struct RequestTable *RTable=NULL;
struct OPrintBufArray *PrintBufs=NULL;
struct OAlignBufArray *AlignBufs=NULL;
struct DiskInfo *DiskInfo=NULL;

extern int ddv_length[];

int ddv_mlist_available(void)
{
	int start = RTable->start_mtable;
	int end = RTable->end_mtable;
	
	if (start >= end)
		return (MTABLE_SIZE - start);
	return (end+1) - start;
}


int ddv_get_mlist(unsigned mptr[],int bnum)
{
	int available = ddv_mlist_available();
	int i;
	int start = RTable->start_mtable;
	
	if (available < bnum) {
		return -1;
	}
	
	for (i = 0; i < bnum; i++) {    
		unsigned phys = (unsigned)mmu_v2p((unsigned)mptr[i]);
		if (phys == -1)
			panic("bad address %x in ddv_get_mlist\n",mptr[i]);
		RTable->mtable[RTable->start_mtable] = phys;
		RTable->start_mtable = INC_ML(RTable->start_mtable);
	}
	
	return start;
}



void ddv_load_kernel(char *opcodep)
{
	int tsize;
	char *p;
	struct exec *mhead;  
  
	mhead = (struct exec *)opcodep;
	p = opcodep + sizeof(*mhead);
	
	tsize = (mhead->a_text + (PAGE_SIZE-1)) & ~(PAGE_SIZE-1);
	memcpy((char *)OPIBUS_BASE+mhead->a_entry,p,mhead->a_text);
	memcpy((char *)OPIBUS_BASE+mhead->a_entry+tsize,
	       p+mhead->a_text,mhead->a_data);
	memset((char *)OPIBUS_BASE+mhead->a_entry+tsize+mhead->a_data,0,
	       mhead->a_bss+PAGE_SIZE);
	
#ifdef DDV_DEBUG  
	printk("CELL(%d) loaded opiu kernel of size %ld %ld %ld (%ld)\n",
	       ap_getcid(),
	       mhead->a_text,mhead->a_data,mhead->a_bss,mhead->a_entry);
#endif
}


int ddv_restart_cpu(void)
{ 
	unsigned long timeout;

	OPT_IO(OPIU_OP) = OPIU_RESET; 
	OPT_IO(PRST) = PRST_IRST; 
	if (OPT_IO(PRST) != PRST_IRST) { 
		printk("_iu_load reset release error.\n"); 
		return(-1);
	}
	for (timeout=jiffies + 10; 
	     time_before(jiffies, timeout) || (OPT_IO(PBUF0) == 0);
	     ) /* wait */ ;
	if (OPT_IO(PBUF0) == 0) {
		printk("WARNING: option kernel didn't startup\n");
		return(-1);
	} else {
		printk("option kernel IU running\n");
		DiskInfo = (struct DiskInfo *)(OPT_IO(PBUF0) + OPIBUS_BASE);
		RTable = (struct RequestTable *)(DiskInfo->ptrs[0]+OPIBUS_BASE);
		PrintBufs = (struct OPrintBufArray *)(DiskInfo->ptrs[1]+OPIBUS_BASE);
		AlignBufs = (struct OAlignBufArray *)(DiskInfo->ptrs[2]+OPIBUS_BASE);
		
		printk("Disk capacity: %d blocks of size %d\n",
		       (int)DiskInfo->blocks,(int)DiskInfo->blk_size);
		
		OPT_IO(PBUF0) = 0;
	}
	return(0);
}



