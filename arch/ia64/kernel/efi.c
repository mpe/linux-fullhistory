/*
 * Extensible Firmware Interface
 *
 * Based on Extensible Firmware Interface Specification version 0.9 April 30, 1999
 *
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999 Hewlett-Packard Co.
 * Copyright (C) 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 Stephane Eranian <eranian@hpl.hp.com>
 *
 * All EFI Runtime Services are not implemented yet as EFI only
 * supports physical mode addressing on SoftSDV. This is to be fixed
 * in a future version.  --drummond 1999-07-20
 *
 * Implemented EFI runtime services and virtual mode calls.  --davidm
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/time.h>

#include <asm/efi.h>
#include <asm/io.h>
#include <asm/processor.h>

#define EFI_DEBUG	0

extern efi_status_t efi_call_phys (void *, ...);

struct efi efi;
		    
static efi_runtime_services_t *runtime;

static efi_status_t
phys_get_time (efi_time_t *tm, efi_time_cap_t *tc)
{
	return efi_call_phys(__va(runtime->get_time), __pa(tm), __pa(tc));
}

static efi_status_t
phys_set_time (efi_time_t *tm)
{
	return efi_call_phys(__va(runtime->set_time), __pa(tm));
}

static efi_status_t
phys_get_wakeup_time (efi_bool_t *enabled, efi_bool_t *pending, efi_time_t *tm)
{
	return efi_call_phys(__va(runtime->get_wakeup_time), __pa(enabled), __pa(pending),
			     __pa(tm));
}

static efi_status_t
phys_set_wakeup_time (efi_bool_t enabled, efi_time_t *tm)
{
	return efi_call_phys(__va(runtime->set_wakeup_time), enabled, __pa(tm));
}

static efi_status_t
phys_get_variable (efi_char16_t *name, efi_guid_t *vendor, u32 *attr,
		   unsigned long *data_size, void *data)
{
	return efi_call_phys(__va(runtime->get_variable), __pa(name), __pa(vendor), __pa(attr),
			     __pa(data_size), __pa(data));
}

static efi_status_t
phys_get_next_variable (unsigned long *name_size, efi_char16_t *name, efi_guid_t *vendor)
{
	return efi_call_phys(__va(runtime->get_next_variable), __pa(name_size), __pa(name),
			     __pa(vendor));
}

static efi_status_t
phys_set_variable (efi_char16_t *name, efi_guid_t *vendor, u32 attr,
		   unsigned long data_size, void *data)
{
	return efi_call_phys(__va(runtime->set_variable), __pa(name), __pa(vendor), attr,
			     data_size, __pa(data));
}

static efi_status_t
phys_get_next_high_mono_count (u64 *count)
{
	return efi_call_phys(__va(runtime->get_next_high_mono_count), __pa(count));
}

static void
phys_reset_system (int reset_type, efi_status_t status,
		   unsigned long data_size, efi_char16_t *data)
{
	efi_call_phys(__va(runtime->reset_system), status, data_size, __pa(data));
}

/*
 * Converts Gregorian date to seconds since 1970-01-01 00:00:00.
 * Assumes input in normal date format, i.e. 1980-12-31 23:59:59
 * => year=1980, mon=12, day=31, hour=23, min=59, sec=59.
 *
 * [For the Julian calendar (which was used in Russia before 1917,
 * Britain & colonies before 1752, anywhere else before 1582,
 * and is still in use by some communities) leave out the
 * -year/100+year/400 terms, and add 10.]
 *
 * This algorithm was first published by Gauss (I think).
 *
 * WARNING: this function will overflow on 2106-02-07 06:28:16 on
 * machines were long is 32-bit! (However, as time_t is signed, we
 * will already get problems at other places on 2038-01-19 03:14:08)
 */
static inline unsigned long 
mktime (unsigned int year, unsigned int mon, unsigned int day, unsigned int hour,
	unsigned int min, unsigned int sec)
{
        if (0 >= (int) (mon -= 2)) {    /* 1..12 -> 11,12,1..10 */
                mon += 12;      /* Puts Feb last since it has leap day */
                year -= 1;
        }
        return ((((unsigned long)(year/4 - year/100 + year/400 + 367*mon/12 + day)
		  + year*365 - 719499
		)*24 + hour /* now have hours */
		)*60 + min /* now have minutes */
		)*60 + sec; /* finally seconds */
}

void
efi_gettimeofday (struct timeval *tv)
{
	efi_time_t tm;

	memset(tv, 0, sizeof(tv));
	if ((*efi.get_time)(&tm, 0) != EFI_SUCCESS)
		return;

	tv->tv_sec = mktime(tm.year, tm.month, tm.day, tm.hour, tm.minute, tm.second);
	tv->tv_usec = tm.nanosecond / 1000;
}

/*
 * Walks the EFI memory map and calls CALLBACK once for each EFI
 * memory descriptor that has memory that is available for OS use.
 */
void
efi_memmap_walk (efi_freemem_callback_t callback, void *arg)
{
	int prev_valid = 0;
	struct range {
		u64 start;
		u64 end;
	} prev, curr;
	void *efi_map_start, *efi_map_end, *p;
	efi_memory_desc_t *md;
	u64 efi_desc_size, start, end;

	efi_map_start = __va(ia64_boot_param.efi_memmap);
	efi_map_end   = efi_map_start + ia64_boot_param.efi_memmap_size;
	efi_desc_size = ia64_boot_param.efi_memdesc_size;

	for (p = efi_map_start; p < efi_map_end; p += efi_desc_size) {
		md = p;
		switch (md->type) {
		      case EFI_LOADER_CODE:
		      case EFI_LOADER_DATA:
		      case EFI_BOOT_SERVICES_CODE:
		      case EFI_BOOT_SERVICES_DATA:
		      case EFI_CONVENTIONAL_MEMORY:
			if (md->phys_addr > 1024*1024*1024UL) {
				printk("Warning: ignoring %luMB of memory above 1GB!\n",
				       md->num_pages >> 8);
				md->type = EFI_UNUSABLE_MEMORY;
				continue;
			}

			curr.start = PAGE_OFFSET + md->phys_addr;
			curr.end   = curr.start + (md->num_pages << 12);

			if (!prev_valid) {
				prev = curr;
				prev_valid = 1;
			} else {
				if (curr.start < prev.start)
					printk("Oops: EFI memory table not ordered!\n");

				if (prev.end == curr.start) {
					/* merge two consecutive memory ranges */
					prev.end = curr.end;
				} else {
					start = PAGE_ALIGN(prev.start);
					end = prev.end & PAGE_MASK;
					if ((end > start) && (*callback)(start, end, arg) < 0)
						return;
					prev = curr;
				}
			}
			break;

		      default:
			continue;
		}
	}
	if (prev_valid) {
		start = PAGE_ALIGN(prev.start);
		end = prev.end & PAGE_MASK;
		if (end > start)
			(*callback)(start, end, arg);
	}
}

void __init 
efi_init (void)
{
	void *efi_map_start, *efi_map_end;
	efi_config_table_t *config_tables;
	efi_char16_t *c16;
	u64 efi_desc_size;
	char vendor[100] = "unknown";
	int i;

	efi.systab = __va(ia64_boot_param.efi_systab);

	/*
	 * Verify the EFI Table
 	 */
	if (efi.systab == NULL) 
		panic("Woah! Can't find EFI system table.\n");
	if (efi.systab->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE) 
		panic("Woah! EFI system table signature incorrect\n");
	if ((efi.systab->hdr.revision ^ EFI_SYSTEM_TABLE_REVISION) >> 16 != 0)
		printk("Warning: EFI system table major version mismatch: "
		       "got %d.%02d, expected %d.%02d\n",
		       efi.systab->hdr.revision >> 16, efi.systab->hdr.revision & 0xffff,
		       EFI_SYSTEM_TABLE_REVISION >> 16, EFI_SYSTEM_TABLE_REVISION & 0xffff);

	config_tables = __va(efi.systab->tables);

	/* Show what we know for posterity */
	c16 = __va(efi.systab->fw_vendor);
	if (c16) {
		for (i = 0;i < sizeof(vendor) && *c16; ++i)
			vendor[i] = *c16++;
		vendor[i] = '\0';
	}

	printk("EFI v%u.%.02u by %s:",
	       efi.systab->hdr.revision >> 16, efi.systab->hdr.revision & 0xffff, vendor);

	for (i = 0; i < efi.systab->nr_tables; i++) {
		if (efi_guidcmp(config_tables[i].guid, MPS_TABLE_GUID) == 0) {
			efi.mps = __va(config_tables[i].table);
			printk(" MPS=0x%lx", config_tables[i].table);
		} else if (efi_guidcmp(config_tables[i].guid, ACPI_TABLE_GUID) == 0) {
			efi.acpi = __va(config_tables[i].table);
			printk(" ACPI=0x%lx", config_tables[i].table);
		} else if (efi_guidcmp(config_tables[i].guid, SMBIOS_TABLE_GUID) == 0) {
			efi.smbios = __va(config_tables[i].table);
			printk(" SMBIOS=0x%lx", config_tables[i].table);
		} else if (efi_guidcmp(config_tables[i].guid, SAL_SYSTEM_TABLE_GUID) == 0) {
			efi.sal_systab = __va(config_tables[i].table);
			printk(" SALsystab=0x%lx", config_tables[i].table);
		}
	}
	printk("\n");

	runtime = __va(efi.systab->runtime);
	efi.get_time = phys_get_time;
	efi.set_time = phys_set_time;
	efi.get_wakeup_time = phys_get_wakeup_time;
	efi.set_wakeup_time = phys_set_wakeup_time;
	efi.get_variable = phys_get_variable;
	efi.get_next_variable = phys_get_next_variable;
	efi.set_variable = phys_set_variable;
	efi.get_next_high_mono_count = phys_get_next_high_mono_count;
	efi.reset_system = phys_reset_system;

	efi_map_start = __va(ia64_boot_param.efi_memmap);
	efi_map_end   = efi_map_start + ia64_boot_param.efi_memmap_size;
	efi_desc_size = ia64_boot_param.efi_memdesc_size;

#if EFI_DEBUG
	/* print EFI memory map: */
	{
		efi_memory_desc_t *md = p;
		void *p;

		for (i = 0, p = efi_map_start; p < efi_map_end; ++i, p += efi_desc_size) {
			md = p;
			printk("mem%02u: type=%u, attr=0x%lx, range=[0x%016lx-0x%016lx) (%luMB)\n",
			       i, md->type, md->attribute, md->phys_addr,
			       md->phys_addr + (md->num_pages<<12) - 1, md->num_pages >> 8);
		}
	}
#endif
}

void
efi_enter_virtual_mode (void)
{
	void *efi_map_start, *efi_map_end, *p;
	efi_memory_desc_t *md;
	efi_status_t status;
	u64 efi_desc_size;

	efi_map_start = __va(ia64_boot_param.efi_memmap);
	efi_map_end   = efi_map_start + ia64_boot_param.efi_memmap_size;
	efi_desc_size = ia64_boot_param.efi_memdesc_size;

	for (p = efi_map_start; p < efi_map_end; p += efi_desc_size) {
		md = p;
		if (md->attribute & EFI_MEMORY_RUNTIME) {
			/*
			 * Some descriptors have multiple bits set, so the order of
			 * the tests is relevant.
			 */
			if (md->attribute & EFI_MEMORY_WB) {
				md->virt_addr = (u64) __va(md->phys_addr);
			} else if (md->attribute & EFI_MEMORY_UC) {
				md->virt_addr = (u64) ioremap(md->phys_addr, 0);
			} else if (md->attribute & EFI_MEMORY_WC) {
#if 0
				md->virt_addr = ia64_remap(md->phys_addr, (_PAGE_A | _PAGE_P
									   | _PAGE_D
									   | _PAGE_MA_WC
									   | _PAGE_PL_0
									   | _PAGE_AR_RW));
#else
				printk("EFI_MEMORY_WC mapping\n");
				md->virt_addr = (u64) ioremap(md->phys_addr, 0);
#endif
			} else if (md->attribute & EFI_MEMORY_WT) {
#if 0
				md->virt_addr = ia64_remap(md->phys_addr, (_PAGE_A | _PAGE_P
									   | _PAGE_D | _PAGE_MA_WT
									   | _PAGE_PL_0
									   | _PAGE_AR_RW));
#else
				printk("EFI_MEMORY_WT mapping\n");
				md->virt_addr = (u64) ioremap(md->phys_addr, 0);
#endif
			}
		}
	}

	status = efi_call_phys(__va(runtime->set_virtual_address_map),
			       ia64_boot_param.efi_memmap_size,
			       efi_desc_size, ia64_boot_param.efi_memdesc_version,
			       ia64_boot_param.efi_memmap);
	if (status != EFI_SUCCESS) {
		printk("Warning: unable to switch EFI into virtual mode (status=%lu)\n", status);
		return;
	}

	/*
	 * Now that EFI is in virtual mode, we arrange for EFI functions to be
	 * called directly:
	 */
	efi.get_time = __va(runtime->get_time);
	efi.set_time = __va(runtime->set_time);
	efi.get_wakeup_time = __va(runtime->get_wakeup_time);
	efi.set_wakeup_time = __va(runtime->set_wakeup_time);
	efi.get_variable = __va(runtime->get_variable);
	efi.get_next_variable = __va(runtime->get_next_variable);
	efi.set_variable = __va(runtime->set_variable);
	efi.get_next_high_mono_count = __va(runtime->get_next_high_mono_count);
	efi.reset_system = __va(runtime->reset_system);
}
