/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999,2001-2004 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * Module to export the system's Firmware Interface Tables, including
 * PROM revision numbers and banners, in /proc
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/sn2/addrs.h>

MODULE_DESCRIPTION("PROM version reporting for /proc");
MODULE_AUTHOR("Chad Talbott");
MODULE_LICENSE("GPL");

/* Standard Intel FIT entry types */
#define FIT_ENTRY_FIT_HEADER	0x00	/* FIT header entry */
#define FIT_ENTRY_PAL_B		0x01	/* PAL_B entry */
/* Entries 0x02 through 0x0D reserved by Intel */
#define FIT_ENTRY_PAL_A_PROC	0x0E	/* Processor-specific PAL_A entry */
#define FIT_ENTRY_PAL_A		0x0F	/* PAL_A entry, same as... */
#define FIT_ENTRY_PAL_A_GEN	0x0F	/* ...Generic PAL_A entry */
#define FIT_ENTRY_UNUSED	0x7F	/* Unused (reserved by Intel?) */
/* OEM-defined entries range from 0x10 to 0x7E. */
#define FIT_ENTRY_SAL_A		0x10	/* SAL_A entry */
#define FIT_ENTRY_SAL_B		0x11	/* SAL_B entry */
#define FIT_ENTRY_SALRUNTIME	0x12	/* SAL runtime entry */
#define FIT_ENTRY_EFI		0x1F	/* EFI entry */
#define FIT_ENTRY_FPSWA		0x20	/* embedded fpswa entry */
#define FIT_ENTRY_VMLINUX	0x21	/* embedded vmlinux entry */

#define FIT_MAJOR_SHIFT	(32 + 8)
#define FIT_MAJOR_MASK	((1 << 8) - 1)
#define FIT_MINOR_SHIFT	32
#define FIT_MINOR_MASK	((1 << 8) - 1)

#define FIT_MAJOR(q)	\
	((unsigned) ((q) >> FIT_MAJOR_SHIFT) & FIT_MAJOR_MASK)
#define FIT_MINOR(q)	\
	((unsigned) ((q) >> FIT_MINOR_SHIFT) & FIT_MINOR_MASK)

#define FIT_TYPE_SHIFT	(32 + 16)
#define FIT_TYPE_MASK	((1 << 7) - 1)

#define FIT_TYPE(q)	\
	((unsigned) ((q) >> FIT_TYPE_SHIFT) & FIT_TYPE_MASK)

struct fit_type_map_t {
	unsigned char	type;
	const char	*name;
};

static const struct fit_type_map_t fit_entry_types[] = {
	{ FIT_ENTRY_FIT_HEADER, "FIT Header" },
	{ FIT_ENTRY_PAL_A_GEN,  "Generic PAL_A" },
	{ FIT_ENTRY_PAL_A_PROC, "Processor-specific PAL_A" },
	{ FIT_ENTRY_PAL_A,      "PAL_A" },
	{ FIT_ENTRY_PAL_B,      "PAL_B" },
	{ FIT_ENTRY_SAL_A,      "SAL_A" },
	{ FIT_ENTRY_SAL_B,      "SAL_B" },
	{ FIT_ENTRY_SALRUNTIME, "SAL runtime" },
	{ FIT_ENTRY_EFI,	"EFI" },
	{ FIT_ENTRY_VMLINUX,    "Embedded Linux" },
	{ FIT_ENTRY_FPSWA,      "Embedded FPSWA" },
	{ FIT_ENTRY_UNUSED,     "Unused" },
	{ 0xff,                 "Error" },
};

static const char *
fit_type_name(unsigned char type)
{
	struct fit_type_map_t const*mapp;

	for (mapp = fit_entry_types; mapp->type != 0xff; mapp++)
		if (type == mapp->type)
			return mapp->name;

	if ((type > FIT_ENTRY_PAL_A) && (type < FIT_ENTRY_UNUSED))
		return "OEM type";
	if ((type > FIT_ENTRY_PAL_B) && (type < FIT_ENTRY_PAL_A))
		return "Reserved";

	return "Unknown type";
}


/* ============ BEGIN temp til old PROMs are no longer supported =============
 *
 * The OS should not make direct access to the PROM flash memory. Access to
 * this region must be serialized with a PROM lock. If SAL on one cpu is
 * updating the FLASH error log at the same time another cpu is accessing the
 * PROM, data corruption will occur.
 *
 * To solve the problem, all flash PROM access has been moved to SAL. Because
 * not all systems will have instant PROM updates, we need to support a new OS
 * running on a system with old PROMs.
 *
 * This code should be deleted after 1 OS/PROM release has occurred & the OS
 * no longer supports downrev PROMs. (PROM support should be in the 3.50
 * PROMs).
 */
#define SUPPORT_OLD_PROMS
#ifdef SUPPORT_OLD_PROMS


#define FIT_SIGNATURE		0x2020205f5449465ful

/* Sub-regions determined by bits in Node Offset */
#define	LB_PROM_SPACE		0x0000000700000000ul /* Local LB PROM */

/* Offset of PROM banner pointers in SAL A and SAL B */
#define SAL_A_BANNER_OFFSET	(1 * 16)
#define SAL_B_BANNER_OFFSET	(3 * 16)

/* Architected IA64 firmware space */
#define FW_BASE                 0x00000000FF000000
#define FW_TOP                  0x0000000100000000

static unsigned long
convert_fw_addr(nasid_t nasid, unsigned long addr)
{
	/* snag just the node-relative offset */
	addr &= ~0ul >> (63-35);
	/* the pointer to SAL A is relative to IA-64 compatibility
	 * space.  However, the PROM is mapped at a different offset
	 * in MMR space (both local and global)
	 */
	addr += 0x700000000;
	return GLOBAL_MMR_ADDR(nasid, addr);
}

static int
valid_fw_addr(unsigned long addr)
{
	addr &= ~(1ul << 63); /* Clear cached/uncached bit */
	return (addr >= FW_BASE && addr < FW_TOP);
}

static unsigned long *
lookup_fit(int nasid)
{
	unsigned long *fitp;
	unsigned long fit_paddr;
	unsigned long *fit_vaddr;

	fitp = (void *)GLOBAL_MMR_ADDR(nasid, LB_PROM_SPACE - 32);
	fit_paddr = readq(fitp);
	fit_vaddr = (unsigned long *) convert_fw_addr(nasid, fit_paddr);
	return fit_vaddr;
}
#endif /* SUPPORT_OLD_PROMS */
/* ============ END temp til old PROMs are no longer supported ============= */

static int
get_fit_entry(unsigned long nasid, int index, unsigned long *fentry,
	      char *banner, int banlen)
{
	int ret;

	ret = ia64_sn_get_fit_compt(nasid, index, fentry, banner, banlen);

#ifdef SUPPORT_OLD_PROMS
	/* The following is hack is temporary until PROMs are updated */
	if (ret == SALRET_NOT_IMPLEMENTED) {
		unsigned long *fitadr = lookup_fit(nasid);
		int nentries;

		if (readq(fitadr) != FIT_SIGNATURE) {
			printk(KERN_WARNING "Unrecognized FIT signature");
			return -2;
		}

		nentries = (unsigned int) (readq(fitadr + 1) & 0xffffff);
		if (index >= nentries)
			return -2;

		fentry[0] = readq(fitadr + 2 * index);
		fentry[1] = readq(fitadr + 2 * index + 1);
		ret = 0;

		if (banner && FIT_TYPE(fentry[1]) == FIT_ENTRY_SAL_A) {
			unsigned long i, qw, *bwp, *qwp;

			banner[0] = '\0';
			qw = fentry[0];	/* Address of SAL A */
			if (!valid_fw_addr(qw))
				return 0;

			qw += SAL_A_BANNER_OFFSET;
			qw = convert_fw_addr(nasid, qw);

			qw = readq(qw);			/* Address of banner */
			if (!valid_fw_addr(qw))
				return 0;
			qw = convert_fw_addr(nasid, qw);
			qwp = (unsigned long *) qw;
			bwp = (unsigned long *) banner;
			for (i=0; i<banlen/8; i++)
				bwp[i] = qwp[i];
		}
	}
#endif /* SUPPORT_OLD_PROMS */
	return ret;
}


/*
 * These two routines display the FIT table for each node.
 */
static int
dump_fit_entry(char *page, unsigned long *fentry)
{
	unsigned type;

	type = FIT_TYPE(fentry[1]);
	return sprintf(page, "%02x %-25s %x.%02x %016lx %u\n",
		       type,
		       fit_type_name(type),
		       FIT_MAJOR(fentry[1]), FIT_MINOR(fentry[1]),
		       fentry[0],
		       /* mult by sixteen to get size in bytes */
		       (unsigned)(fentry[1] & 0xffffff) * 16);
}


/*
 * We assume that the fit table will be small enough that we can print
 * the whole thing into one page.  (This is true for our default 16kB
 * pages -- each entry is about 60 chars wide when printed.)  I read
 * somewhere that the maximum size of the FIT is 128 entries, so we're
 * OK except for 4kB pages (and no one is going to do that on SN
 * anyway).
 */
static int
dump_fit(char *page, unsigned long nasid)
{
	unsigned long fentry[2];
	int index;
	char *p;

	p = page;
	for (index=0;;index++) {
		BUG_ON(index * 60 > PAGE_SIZE);
		if (get_fit_entry(nasid, index, fentry, NULL, 0))
			break;
		p += dump_fit_entry(p, fentry);
	}

	return p - page;
}

static int
dump_version(char *page, unsigned long nasid)
{
	unsigned long fentry[2];
	char banner[128];
	int index;
	int len;

	for (index = 0; ; index++) {
		if (get_fit_entry(nasid, index, fentry, banner,
				  sizeof(banner)))
			return 0;
		if (FIT_TYPE(fentry[1]) == FIT_ENTRY_SAL_A)
			break;
	}

	len = sprintf(page, "%x.%02x\n", FIT_MAJOR(fentry[1]),
		      FIT_MINOR(fentry[1]));
	page += len;

	if (banner[0])
		len += snprintf(page, PAGE_SIZE-len, "%s\n", banner);

	return len;
}

/* same as in proc_misc.c */
static int
proc_calc_metrics(char *page, char **start, off_t off, int count, int *eof,
		  int len)
{
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;
	return len;
}

static int
read_version_entry(char *page, char **start, off_t off, int count, int *eof,
		   void *data)
{
	int len = 0;

	/* data holds the NASID of the node */
	len = dump_version(page, (unsigned long)data);
	len = proc_calc_metrics(page, start, off, count, eof, len);
	return len;
}

static int
read_fit_entry(char *page, char **start, off_t off, int count, int *eof,
	       void *data)
{
	int len = 0;

	/* data holds the NASID of the node */
	len = dump_fit(page, (unsigned long)data);
	len = proc_calc_metrics(page, start, off, count, eof, len);

	return len;
}

/* module entry points */
int __init prominfo_init(void);
void __exit prominfo_exit(void);

module_init(prominfo_init);
module_exit(prominfo_exit);

static struct proc_dir_entry **proc_entries;
static struct proc_dir_entry *sgi_prominfo_entry;

#define NODE_NAME_LEN 11

int __init
prominfo_init(void)
{
	struct proc_dir_entry **entp;
	struct proc_dir_entry *p;
	cnodeid_t cnodeid;
	unsigned long nasid;
	char name[NODE_NAME_LEN];

	if (!ia64_platform_is("sn2"))
		return 0;

	proc_entries = kmalloc(numnodes * sizeof(struct proc_dir_entry *),
			       GFP_KERNEL);

	sgi_prominfo_entry = proc_mkdir("sgi_prominfo", NULL);

	for (cnodeid = 0, entp = proc_entries;
	     cnodeid < numnodes;
	     cnodeid++, entp++) {
		sprintf(name, "node%d", cnodeid);
		*entp = proc_mkdir(name, sgi_prominfo_entry);
		nasid = cnodeid_to_nasid(cnodeid);
		p = create_proc_read_entry(
			"fit", 0, *entp, read_fit_entry,
			(void *)nasid);
		if (p)
			p->owner = THIS_MODULE;
		p = create_proc_read_entry(
			"version", 0, *entp, read_version_entry,
			(void *)nasid);
		if (p)
			p->owner = THIS_MODULE;
	}

	return 0;
}

void __exit
prominfo_exit(void)
{
	struct proc_dir_entry **entp;
	unsigned cnodeid;
	char name[NODE_NAME_LEN];

	for (cnodeid = 0, entp = proc_entries;
	     cnodeid < numnodes;
	     cnodeid++, entp++) {
		remove_proc_entry("fit", *entp);
		remove_proc_entry("version", *entp);
		sprintf(name, "node%d", cnodeid);
		remove_proc_entry(name, sgi_prominfo_entry);
	}
	remove_proc_entry("sgi_prominfo", NULL);
	kfree(proc_entries);
}
