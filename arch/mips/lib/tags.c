/*
 *  linux/arch/mips/lib/tags.c
 *
 *  Copyright (C) 1996  Stoned Elipot
 */
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/addrspace.h>
#include <asm/bootinfo.h>

/*
 * Parse the tags present in upper memory to find out
 * a pecular one.
 *
 * Parameter: type - tag type to find
 * 
 * returns  : NULL  - failure
 *            !NULL - pointer on the tag structure found 
 */
tag *
bi_TagFind(enum bi_tag type)
{
	tag* t = (tag*)(mips_memory_upper - sizeof(tag));

	while((t->tag != tag_dummy) && (t->tag != type))
		t = (tag*)(NEXTTAGPTR(t));

	if (t->tag == tag_dummy)		/* tag not found */
		return (tag*)NULL;

	return t;
}

/*
 * Snarf from the tag list in memory end some tags needed
 * before the kernel reachs setup_arch()
 *
 * add yours here if you want to, but *beware*: the kernel var
 * that will hold the values you want to snarf have to be
 * in .data section of the kernel, so initialized in to whatever
 * value in the kernel's sources.
 */
void bi_EarlySnarf(void)
{
	tag* atag;
  
	/* for wire_mappings() */
	atag = bi_TagFind(tag_machgroup);
	if (atag)
		memcpy(&mips_machgroup, TAGVALPTR(atag), atag->size);
	else {
		/* useless for boxes without text video mode but....*/
		panic("machine group not specified by bootloader");
	}

	atag = bi_TagFind(tag_machtype);
	if (atag)
		memcpy(&mips_machtype, TAGVALPTR(atag), atag->size);
	else {
		/* useless for boxes without text video mode but....*/
		panic("machine type not specified by bootloader");
	}

	/* for tlbflush() */
	atag = bi_TagFind(tag_tlb_entries);
	if (atag)
		memcpy(&mips_tlb_entries, TAGVALPTR(atag), atag->size);
	else {
		/* useless for boxes without text video mode but....*/
		panic("number of TLB entries not specified by bootloader");
	}

	return;
}
