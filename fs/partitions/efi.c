/************************************************************
 * EFI GUID Partition Table handling
 * Per Intel EFI Specification v1.02
 * http://developer.intel.com/technology/efi/efi.htm
 * efi.[ch] by Matt Domsch <Matt_Domsch@dell.com>
 *   Copyright 2000,2001,2002 Dell Computer Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 * TODO:
 *
 * Changelog:
 * Mon  Jan 14 2002 Matt Domsch <Matt_Domsch@dell.com>
 * - Ported to 2.5.2-pre11 + library crc32 patch Linus applied
 *
 * Thu Dec 6 2001 Matt Domsch <Matt_Domsch@dell.com>
 * - Added compare_gpts().
 * - moved le_efi_guid_to_cpus() back into this file.  GPT is the only
 *   thing that keeps EFI GUIDs on disk.
 * - Changed gpt structure names and members to be simpler and more Linux-like.
 * 
 * Wed Oct 17 2001 Matt Domsch <Matt_Domsch@dell.com>
 * - Removed CONFIG_DEVFS_VOLUMES_UUID code entirely per Martin Wilck
 *
 * Wed Oct 10 2001 Matt Domsch <Matt_Domsch@dell.com>
 * - Changed function comments to DocBook style per Andreas Dilger suggestion.
 *
 * Mon Oct 08 2001 Matt Domsch <Matt_Domsch@dell.com>
 * - Change read_lba() to use the page cache per Al Viro's work.
 * - print u64s properly on all architectures
 * - fixed debug_printk(), now Dprintk()
 *
 * Mon Oct 01 2001 Matt Domsch <Matt_Domsch@dell.com>
 * - Style cleanups
 * - made most functions static
 * - Endianness addition
 * - remove test for second alternate header, as it's not per spec,
 *   and is unnecessary.  There's now a method to read/write the last
 *   sector of an odd-sized disk from user space.  No tools have ever
 *   been released which used this code, so it's effectively dead.
 * - Per Asit Mallick of Intel, added a test for a valid PMBR.
 * - Added kernel command line option 'gpt' to override valid PMBR test.
 *
 * Wed Jun  6 2001 Martin Wilck <Martin.Wilck@Fujitsu-Siemens.com>
 * - added devfs volume UUID support (/dev/volumes/uuids) for
 *   mounting file systems by the partition GUID. 
 *
 * Tue Dec  5 2000 Matt Domsch <Matt_Domsch@dell.com>
 * - Moved crc32() to linux/lib, added efi_crc32().
 *
 * Thu Nov 30 2000 Matt Domsch <Matt_Domsch@dell.com>
 * - Replaced Intel's CRC32 function with an equivalent
 *   non-license-restricted version.
 *
 * Wed Oct 25 2000 Matt Domsch <Matt_Domsch@dell.com>
 * - Fixed the last_lba() call to return the proper last block
 *
 * Thu Oct 12 2000 Matt Domsch <Matt_Domsch@dell.com>
 * - Thanks to Andries Brouwer for his debugging assistance.
 * - Code works, detects all the partitions.
 *
 ************************************************************/
#include <linux/config.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/blk.h>
#include <linux/blkpg.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/crc32.h>
#include <asm/system.h>
#include <asm/byteorder.h>
#include "check.h"
#include "efi.h"

#if CONFIG_BLK_DEV_MD
extern void md_autodetect_dev(kdev_t dev);
#endif

/* Handle printing of 64-bit values */
/* Borrowed from /usr/include/inttypes.h */
# if BITS_PER_LONG == 64 
#  define __PRI64_PREFIX	"l"
#  define __PRIPTR_PREFIX	"l"
# else
#  define __PRI64_PREFIX	"ll"
#  define __PRIPTR_PREFIX
# endif
# define PRIx64		__PRI64_PREFIX "x"


#undef EFI_DEBUG
#ifdef EFI_DEBUG
#define Dprintk(x...) printk(KERN_DEBUG x)
#else
#define Dprintk(x...)
#endif

/* This allows a kernel command line option 'gpt' to override
 * the test for invalid PMBR.  Not __initdata because reloading
 * the partition tables happens after init too.
 */
static int forcegpt;
static int __init
force_gpt(char *str)
{
	forcegpt = 1;
	return 1;
}

__setup("gpt", force_gpt);

/**
 * le_efi_guid_to_cpus()
 * @guid
 *
 * Description: modifies @guid in situ
 *
 * This function converts a little endian efi_guid_t to the
 * native cpu representation.  The EFI Spec. declares that all 
 * on-disk structures are stored in little endian format.
 */
static void
le_efi_guid_to_cpus(efi_guid_t *guid)
{
	le32_to_cpus(guid->data1);
	le16_to_cpus(guid->data2);
	le16_to_cpus(guid->data3);
	/* no need to change the rest. It's already an array of chars */
	return;
}

/**
 * efi_crc32() - EFI version of crc32 function
 * @buf: buffer to calculate crc32 of
 * @len - length of buf
 *
 * Description: Returns EFI-style CRC32 value for @buf
 * 
 * This function uses the little endian Ethernet polynomial
 * but seeds the function with ~0, and xor's with ~0 at the end.
 * Note, the EFI Specification, v1.02, has a reference to
 * Dr. Dobbs Journal, May 1994 (actually it's in May 1992).
 */
static inline u32
efi_crc32(const void *buf, unsigned long len)
{
	return (crc32(~0L, buf, len) ^ ~0L);
}

/**
 * le_part_attributes_to_cpus(): converts LE attributes to CPU type in situ
 * @attributes - ptr to partition attributes
 * 
 * Description:  modifies attributes in situ, returns nothing.
 * Converts a little endian partition attributes struct to the
 * native cpu representation.  Good for reading attributes off of a disk.
 */
static void
le_part_attributes_to_cpus(gpt_entry_attributes * a)
{
	u64 *b = (u64 *) a;
	*b = le64_to_cpu(*b);
}

/**
 * is_pmbr_valid(): test Protective MBR for validity
 * @mbr: pointer to a legacy mbr structure
 *
 * Description: Returns 1 if PMBR is valid, 0 otherwise.
 * Validity depends on two things:
 *  1) MSDOS signature is in the last two bytes of the MBR
 *  2) One partition of type 0xEE is found
 */
static int
is_pmbr_valid(legacy_mbr *mbr)
{
	int i, found = 0, signature = 0;
	if (!mbr)
		return 0;
	signature = (le16_to_cpu(mbr->signature) == MSDOS_MBR_SIGNATURE);
	for (i = 0; signature && i < 4; i++) {
		if (mbr->partition_record[i].sys_ind == EFI_PMBR_OSTYPE_EFI_GPT) {
			found = 1;
			break;
		}
	}
	return (signature && found);
}

/**
 * last_lba(): return number of last logical block of device
 * @hd: gendisk with partition list
 * @bdev: block device
 * 
 * Description: Returns last LBA value on success, 0 on error.
 * This is stored (by sd and ide-geometry) in
 *  the part[0] entry for this disk, and is the number of
 *  physical sectors available on the disk.
 */
static u64
last_lba(struct gendisk *hd, struct block_device *bdev)
{
	if (!hd || !hd->part || !bdev)
		return 0;
	return hd->part[minor(to_kdev_t(bdev->bd_dev))].nr_sects - 1;
}

/**
 * read_lba(): Read bytes from disk, starting at given LBA
 * @hd
 * @bdev
 * @lba
 * @buffer
 * @size_t
 *
 * Description:  Reads @count bytes from @bdev into @buffer.
 * Returns number of bytes read on success, 0 on error.
 */
static size_t
read_lba(struct gendisk *hd, struct block_device *bdev, u64 lba,
	 u8 * buffer, size_t count)
{

	size_t totalreadcount = 0, bytesread = 0;
	unsigned long blocksize;
	int i;
	Sector sect;
	unsigned char *data = NULL;

	if (!hd || !bdev || !buffer || !count)
		return 0;

	blocksize = get_hardsect_size(to_kdev_t(bdev->bd_dev));
	if (!blocksize)
		blocksize = 512;

	for (i = 0; count > 0; i++) {
		data = read_dev_sector(bdev, lba, &sect);
		if (!data)
			return totalreadcount;

		bytesread =
		    PAGE_CACHE_SIZE - (data -
				       (unsigned char *) page_address(sect.v));
		bytesread = min(bytesread, count);
		memcpy(buffer, data, bytesread);
		put_dev_sector(sect);

		buffer += bytesread;
		totalreadcount += bytesread;
		count -= bytesread;
		lba += (bytesread / blocksize);
	}
	return totalreadcount;
}

/**
 * print_gpt_header(): unparses gpt header to console
 * @gpt: gpt header
 */
static void
print_gpt_header(gpt_header *gpt)
{
	Dprintk("GUID Partition Table Header\n");
	if (!gpt)
		return;
	Dprintk("signature                   : %" PRIx64 "\n", gpt->signature);
	Dprintk("revision                    : %x\n", gpt->revision);
	Dprintk("header_size                 : %x\n", gpt->header_size);
	Dprintk("header_crc32                : %x\n", gpt->header_crc32);
	Dprintk("my_lba                      : %" PRIx64 "\n", gpt->my_lba);
	Dprintk("alternate_lba               : %" PRIx64 "\n",
		gpt->alternate_lba);
	Dprintk("first_usable_lba            : %" PRIx64 "\n",
		gpt->first_usable_lba);
	Dprintk("last_usable_lba             : %" PRIx64 "\n",
		gpt->last_usable_lba);
	Dprintk("partition_entry_lba         : %" PRIx64 "\n",
		gpt->partition_entry_lba);
	Dprintk("num_partition_entries       : %x\n",
		gpt->num_partition_entries);
	Dprintk("sizeof_partition_entry      : %x\n",
		gpt->sizeof_partition_entry);
	Dprintk("partition_entry_array_crc32 : %x\n",
		gpt->partition_entry_array_crc32);
	return;
}

/**
 * alloc_read_gpt_entries(): reads partition entries from disk
 * @hd
 * @bdev
 * @gpt - GPT header
 * 
 * Description: Returns ptes on success,  NULL on error.
 * Allocates space for PTEs based on information found in @gpt.
 * Notes: remember to free pte when you're done!
 */
static gpt_entry *
alloc_read_gpt_entries(struct gendisk *hd,
		       struct block_device *bdev, gpt_header *gpt)
{
	u32 i, j;
	size_t count;
	gpt_entry *pte;
	if (!hd || !bdev || !gpt)
		return NULL;

	count = gpt->num_partition_entries * gpt->sizeof_partition_entry;
	if (!count)
		return NULL;
	pte = kmalloc(count, GFP_KERNEL);
	if (!pte)
		return NULL;
	memset(pte, 0, count);

	if (read_lba(hd, bdev, gpt->partition_entry_lba, (u8 *) pte,
		     count) < count) {
		kfree(pte);
		return NULL;
	}
	/* Fixup endianness */
	for (i = 0; i < gpt->num_partition_entries; i++) {
		le_efi_guid_to_cpus(&pte[i].partition_type_guid);
		le_efi_guid_to_cpus(&pte[i].unique_partition_guid);
		le64_to_cpus(pte[i].starting_lba);
		le64_to_cpus(pte[i].ending_lba);
		le_part_attributes_to_cpus(&pte[i].attributes);
		for (j = 0; j < (72 / sizeof (efi_char16_t)); j++) {
			le16_to_cpus((u16) (pte[i].partition_name[j]));
		}
	}

	return pte;
}

/**
 * alloc_read_gpt_header(): Allocates GPT header, reads into it from disk
 * @hd
 * @bdev
 * @lba is the Logical Block Address of the partition table
 * 
 * Description: returns GPT header on success, NULL on error.   Allocates
 * and fills a GPT header starting at @ from @bdev.
 * Note: remember to free gpt when finished with it.
 */
static gpt_header *
alloc_read_gpt_header(struct gendisk *hd, struct block_device *bdev, u64 lba)
{
	gpt_header *gpt;
	if (!hd || !bdev)
		return NULL;

	gpt = kmalloc(sizeof (gpt_header), GFP_KERNEL);
	if (!gpt)
		return NULL;
	memset(gpt, 0, sizeof (gpt_header));

	if (read_lba(hd, bdev, lba, (u8 *) gpt,
		     sizeof (gpt_header)) < sizeof (gpt_header)) {
		kfree(gpt);
		return NULL;
	}

	/* Fixup endianness */
	le64_to_cpus(gpt->signature);
	le32_to_cpus(gpt->revision);
	le32_to_cpus(gpt->header_size);
	le32_to_cpus(gpt->header_crc32);
	le32_to_cpus(gpt->reserved1);
	le64_to_cpus(gpt->my_lba);
	le64_to_cpus(gpt->alternate_lba);
	le64_to_cpus(gpt->first_usable_lba);
	le64_to_cpus(gpt->last_usable_lba);
	le_efi_guid_to_cpus(&gpt->disk_guid);
	le64_to_cpus(gpt->partition_entry_lba);
	le32_to_cpus(gpt->num_partition_entries);
	le32_to_cpus(gpt->sizeof_partition_entry);
	le32_to_cpus(gpt->partition_entry_array_crc32);

	print_gpt_header(gpt);

	return gpt;
}

/**
 * is_gpt_valid() - tests one GPT header and PTEs for validity
 * @hd
 * @bdev
 * @lba is the logical block address of the GPT header to test
 * @gpt is a GPT header ptr, filled on return.
 * @ptes is a PTEs ptr, filled on return.
 *
 * Description: returns 1 if valid,  0 on error.
 * If valid, returns pointers to newly allocated GPT header and PTEs.
 */
static int
is_gpt_valid(struct gendisk *hd, struct block_device *bdev, u64 lba,
	     gpt_header **gpt, gpt_entry **ptes)
{
	u32 crc, origcrc;

	if (!hd || !bdev || !gpt || !ptes)
		return 0;
	if (!(*gpt = alloc_read_gpt_header(hd, bdev, lba)))
		return 0;

	/* Check the GUID Partition Table signature */
	if ((*gpt)->signature != GPT_HEADER_SIGNATURE) {
		Dprintk("GUID Partition Table Header signature is wrong: %"
			PRIx64 " != %" PRIx64 "\n", (*gpt)->signature,
			GPT_HEADER_SIGNATURE);
		kfree(*gpt);
		*gpt = NULL;
		return 0;
	}

	/* Check the GUID Partition Table CRC */
	origcrc = (*gpt)->header_crc32;
	(*gpt)->header_crc32 = 0;
	crc = efi_crc32((const unsigned char *) (*gpt), (*gpt)->header_size);

	if (crc != origcrc) {
		Dprintk
		    ("GUID Partition Table Header CRC is wrong: %x != %x\n",
		     (*gpt)->header_crc32, origcrc);
		kfree(*gpt);
		*gpt = NULL;
		return 0;
	}
	(*gpt)->header_crc32 = origcrc;

	/* Check that the my_lba entry points to the LBA that contains
	 * the GUID Partition Table */
	if ((*gpt)->my_lba != lba) {
		Dprintk("GPT my_lba incorrect: %" PRIx64 " != %" PRIx64 "\n",
			(*gpt)->my_lba, lba);
		kfree(*gpt);
		*gpt = NULL;
		return 0;
	}

	if (!(*ptes = alloc_read_gpt_entries(hd, bdev, *gpt))) {
		kfree(*gpt);
		*gpt = NULL;
		return 0;
	}

	/* Check the GUID Partition Entry Array CRC */
	crc = efi_crc32((const unsigned char *) (*ptes),
			(*gpt)->num_partition_entries *
			(*gpt)->sizeof_partition_entry);

	if (crc != (*gpt)->partition_entry_array_crc32) {
		Dprintk("GUID Partitition Entry Array CRC check failed.\n");
		kfree(*gpt);
		*gpt = NULL;
		kfree(*ptes);
		*ptes = NULL;
		return 0;
	}

	/* We're done, all's well */
	return 1;
}

/**
 * compare_gpts() - Search disk for valid GPT headers and PTEs
 * @pgpt is the primary GPT header
 * @agpt is the alternate GPT header
 * @lastlba is the last LBA number
 * Description: Returns nothing.  Sanity checks pgpt and agpt fields
 * and prints warnings on discrepancies.
 * 
 */
static void
compare_gpts(gpt_header *pgpt, gpt_header *agpt, u64 lastlba)
{
	int error_found = 0;
	if (!pgpt || !agpt)
		return;
	if (pgpt->my_lba != agpt->alternate_lba) {
		printk(KERN_WARNING
		       "GPT:Primary header LBA != Alt. header alternate_lba\n");
		printk(KERN_WARNING "GPT:%" PRIx64 " != %" PRIx64 "\n",
		       pgpt->my_lba, agpt->alternate_lba);
		error_found++;
	}
	if (pgpt->alternate_lba != agpt->my_lba) {
		printk(KERN_WARNING
		       "GPT:Primary header alternate_lba != Alt. header my_lba\n");
		printk(KERN_WARNING "GPT:%" PRIx64 " != %" PRIx64 "\n",
		       pgpt->alternate_lba, agpt->my_lba);
		error_found++;
	}
	if (pgpt->first_usable_lba != agpt->first_usable_lba) {
		printk(KERN_WARNING "GPT:first_usable_lbas don't match.\n");
		printk(KERN_WARNING "GPT:%" PRIx64 " != %" PRIx64 "\n",
		       pgpt->first_usable_lba, agpt->first_usable_lba);
		error_found++;
	}
	if (pgpt->last_usable_lba != agpt->last_usable_lba) {
		printk(KERN_WARNING "GPT:last_usable_lbas don't match.\n");
		printk(KERN_WARNING "GPT:%" PRIx64 " != %" PRIx64 "\n",
		       pgpt->last_usable_lba, agpt->last_usable_lba);
		error_found++;
	}
	if (efi_guidcmp(pgpt->disk_guid, agpt->disk_guid)) {
		printk(KERN_WARNING "GPT:disk_guids don't match.\n");
		error_found++;
	}
	if (pgpt->num_partition_entries != agpt->num_partition_entries) {
		printk(KERN_WARNING "GPT:num_partition_entries don't match: "
		       "0x%x != 0x%x\n",
		       pgpt->num_partition_entries,
		       agpt->num_partition_entries);
		error_found++;
	}
	if (pgpt->sizeof_partition_entry != agpt->sizeof_partition_entry) {
		printk(KERN_WARNING
		       "GPT:sizeof_partition_entry values don't match: "
		       "0x%x != 0x%x\n", pgpt->sizeof_partition_entry,
		       agpt->sizeof_partition_entry);
		error_found++;
	}
	if (pgpt->partition_entry_array_crc32 !=
	    agpt->partition_entry_array_crc32) {
		printk(KERN_WARNING
		       "GPT:partition_entry_array_crc32 values don't match: "
		       "0x%x != 0x%x\n", pgpt->partition_entry_array_crc32,
		       agpt->partition_entry_array_crc32);
		error_found++;
	}
	if (pgpt->alternate_lba != lastlba) {
		printk(KERN_WARNING
		       "GPT:Primary header thinks Alt. header is not at the end of the disk.\n");
		printk(KERN_WARNING "GPT:%" PRIx64 " != %" PRIx64 "\n",
		       pgpt->alternate_lba, lastlba);
		error_found++;
	}

	if (agpt->my_lba != lastlba) {
		printk(KERN_WARNING
		       "GPT:Alternate GPT header not at the end of the disk.\n");
		printk(KERN_WARNING "GPT:%" PRIx64 " != %" PRIx64 "\n",
		       agpt->my_lba, lastlba);
		error_found++;
	}

	if (error_found)
		printk(KERN_WARNING
		       "GPT: Use GNU Parted to correct GPT errors.\n");
	return;
}

/**
 * find_valid_gpt() - Search disk for valid GPT headers and PTEs
 * @hd
 * @bdev
 * @gpt is a GPT header ptr, filled on return.
 * @ptes is a PTEs ptr, filled on return.
 * Description: Returns 1 if valid, 0 on error.
 * If valid, returns pointers to newly allocated GPT header and PTEs.
 * Validity depends on finding either the Primary GPT header and PTEs valid,
 * or the Alternate GPT header and PTEs valid, and the PMBR valid.
 */
static int
find_valid_gpt(struct gendisk *hd, struct block_device *bdev,
	       gpt_header **gpt, gpt_entry **ptes)
{
	int good_pgpt = 0, good_agpt = 0, good_pmbr = 0;
	gpt_header *pgpt = NULL, *agpt = NULL;
	gpt_entry *pptes = NULL, *aptes = NULL;
	legacy_mbr *legacymbr = NULL;
	u64 lastlba;
	if (!hd || !bdev || !gpt || !ptes)
		return 0;

	lastlba = last_lba(hd, bdev);
	/* Check the Primary GPT */
	good_pgpt = is_gpt_valid(hd, bdev, GPT_PRIMARY_PARTITION_TABLE_LBA,
				 &pgpt, &pptes);
	if (good_pgpt) {
		/* Primary GPT is OK, check the alternate and warn if bad */
		good_agpt = is_gpt_valid(hd, bdev, pgpt->alternate_lba,
					 &agpt, &aptes);
		if (!good_agpt) {
			printk(KERN_WARNING
			       "Alternate GPT is invalid, using primary GPT.\n");
		}

		compare_gpts(pgpt, agpt, lastlba);

		*gpt = pgpt;
		*ptes = pptes;
		if (agpt) {
			kfree(agpt);
			agpt = NULL;
		}
		if (aptes) {
			kfree(aptes);
			aptes = NULL;
		}
	} /* if primary is valid */
	else {
		/* Primary GPT is bad, check the Alternate GPT */
		good_agpt = is_gpt_valid(hd, bdev, lastlba, &agpt, &aptes);
		if (good_agpt) {
			/* Primary is bad, alternate is good.
			   Return values from the alternate and warn.
			 */
			printk(KERN_WARNING
			       "Primary GPT is invalid, using alternate GPT.\n");
			*gpt = agpt;
			*ptes = aptes;
		}
	}

	/* Now test for valid PMBR */
	/* This will be added to the EFI Spec. per Intel after v1.02. */
	if (good_pgpt || good_agpt) {
		legacymbr = kmalloc(sizeof (*legacymbr), GFP_KERNEL);
		if (legacymbr) {
			memset(legacymbr, 0, sizeof (*legacymbr));
			read_lba(hd, bdev, 0, (u8 *) legacymbr,
				 sizeof (*legacymbr));
			good_pmbr = is_pmbr_valid(legacymbr);
			kfree(legacymbr);
		}
		if (good_pmbr)
			return 1;
		if (!forcegpt) {
			printk
			    (" Warning: Disk has a valid GPT signature but invalid PMBR.\n");
			printk(KERN_WARNING
			       "  Assuming this disk is *not* a GPT disk anymore.\n");
			printk(KERN_WARNING
			       "  Use gpt kernel option to override.  Use GNU Parted to correct disk.\n");
		} else {
			printk(KERN_WARNING
			       "  Warning: Disk has a valid GPT signature but invalid PMBR.\n");
			printk(KERN_WARNING
			       "  Use GNU Parted to correct disk.\n");
			printk(KERN_WARNING
			       "  gpt option taken, disk treated as GPT.\n");
			return 1;
		}
	}

	/* Both primary and alternate GPTs are bad, and/or PMBR is invalid.
	 * This isn't our disk, return 0.
	 */
	if (pgpt) {
		kfree(pgpt);
		pgpt = NULL;
	}
	if (agpt) {
		kfree(agpt);
		agpt = NULL;
	}
	if (pptes) {
		kfree(pptes);
		pptes = NULL;
	}
	if (aptes) {
		kfree(aptes);
		aptes = NULL;
	}
	*gpt = NULL;
	*ptes = NULL;
	return 0;
}

/**
 * add_gpt_partitions(struct gendisk *hd, struct block_device *bdev,
 * @hd
 * @bdev
 *
 * Description: Create devices for each entry in the GUID Partition Table
 * Entries.
 *
 * We do not create a Linux partition for GPT, but
 * only for the actual data partitions.
 * Returns:
 * -1 if unable to read the partition table
 *  0 if this isn't our partition table
 *  1 if successful
 *
 */
static int
add_gpt_partitions(struct gendisk *hd, struct block_device *bdev, int nextminor)
{
	gpt_header *gpt = NULL;
	gpt_entry *ptes = NULL;
	u32 i, nummade = 0;
	int max_p; 

	efi_guid_t unusedGuid = UNUSED_ENTRY_GUID;
#if CONFIG_BLK_DEV_MD
	efi_guid_t raidGuid = PARTITION_LINUX_RAID_GUID;
#endif

	if (!hd || !bdev)
		return -1;

	if (!find_valid_gpt(hd, bdev, &gpt, &ptes) || !gpt || !ptes) {
		if (gpt)
			kfree(gpt);
		if (ptes)
			kfree(ptes);
		return 0;
	}

	Dprintk("GUID Partition Table is valid!  Yea!\n");

	max_p = (1 << hd->minor_shift) - 1;
	for (i = 0; i < gpt->num_partition_entries && nummade < max_p; i++) {
		if (!efi_guidcmp(unusedGuid, ptes[i].partition_type_guid))
			continue;

		add_gd_partition(hd, nextminor, ptes[i].starting_lba,
				 (ptes[i].ending_lba - ptes[i].starting_lba +
				  1));

		/* If there's this is a RAID volume, tell md */
#if CONFIG_BLK_DEV_MD
		if (!efi_guidcmp(raidGuid, ptes[i].partition_type_guid)) {
			md_autodetect_dev(mk_kdev(hd->major,
                                                  nextminor));
		}
#endif
		nummade++;
		nextminor++;

	}
	kfree(ptes);
	kfree(gpt);
	printk("\n");
	return 1;
}

/**
 * efi_partition(): EFI GPT partition handling entry function
 * @hd
 * @bdev
 * @first_sector: unused
 * @first_part_minor: minor number assigned to first GPT partition found
 *
 * Description: called from check.c, if the disk contains GPT
 * partitions, sets up partition entries in the kernel.
 *
 * If the first block on the disk is a legacy MBR,
 * it will get handled by msdos_partition().
 * If it's a Protective MBR, we'll handle it here.
 *
 * set_blocksize() calls are necessary to be able to read
 * a disk with an odd number of 512-byte sectors, as the
 * default BLOCK_SIZE of 1024 bytes won't let that last
 * sector be read otherwise.
 *
 * Returns:
 * -1 if unable to read the partition table
 *  0 if this isn't our partitoin table
 *  1 if successful
 */
int
efi_partition(struct gendisk *hd, struct block_device *bdev,
	      unsigned long first_sector, int first_part_minor)
{

	kdev_t dev = to_kdev_t(bdev->bd_dev);
	int hardblocksize = get_hardsect_size(dev);
	int orig_blksize_size = BLOCK_SIZE;
	int rc = 0;

	/* Need to change the block size that the block layer uses */
	if (blksize_size[major(dev)]) {
		orig_blksize_size = blksize_size[major(dev)][minor(dev)];
	}

	if (orig_blksize_size != hardblocksize)
		set_blocksize(dev, hardblocksize);

	rc = add_gpt_partitions(hd, bdev, first_part_minor);

	/* change back */
	if (orig_blksize_size != hardblocksize)
		set_blocksize(dev, orig_blksize_size);

	return rc;
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
