/*
 * arch/arm/mm/mm-rpc.c
 *
 * Extra MM routines for RiscPC architecture
 *
 * Copyright (C) 1998 Russell King
 */

#include <linux/sched.h>
#include <linux/slab.h>

#include <asm/pgtable.h>
#include <asm/setup.h>
#include <asm/io.h>
#include <asm/proc/mm-init.h>
#include <asm/arch/mm-init.h>

#define NR_DRAM_BANKS	4
#define NR_VRAM_BANKS	1

#define NR_BANKS	(NR_DRAM_BANKS + NR_VRAM_BANKS)

#define FIRST_BANK	0
#define FIRST_DRAM_BANK	0
#define FIRST_VRAM_BANK	NR_DRAM_BANKS

#define BANK_SHIFT	26
#define FIRST_DRAM_ADDR	0x10000000

#define PHYS_TO_BANK(x)	(((x) >> BANK_SHIFT) & (NR_DRAM_BANKS - 1))
#define BANK_TO_PHYS(x)	((FIRST_DRAM_ADDR) + \
			    (((x) - FIRST_DRAM_BANK) << BANK_SHIFT))

struct ram_bank {
	unsigned int virt_addr;		/* virtual address of the *end* of this bank + 1 */
	  signed int phys_offset;	/* offset to physical address of this bank */
};

static struct ram_bank rambank[NR_BANKS];

/*
 * Return the physical (0x10000000 -> 0x20000000) address of
 * the virtual (0xc0000000 -> 0xd0000000) address
 */
unsigned long __virt_to_phys(unsigned long vpage)
{
	unsigned int bank = FIRST_BANK;

	while (vpage >= rambank[bank].virt_addr && bank < NR_BANKS)
		bank ++;

	return vpage - rambank[bank].phys_offset;
}

/*
 * Return the virtual (0xc0000000 -> 0xd0000000) address of
 * the physical (0x10000000 -> 0x20000000) address
 */
unsigned long __phys_to_virt(unsigned long phys)
{
	unsigned int bank;

	if (phys > FIRST_DRAM_ADDR)
		bank = PHYS_TO_BANK(phys);
	else
		bank = FIRST_VRAM_BANK;

	return phys + rambank[bank].phys_offset;
}

void init_dram_banks(struct param_struct *params)
{
	unsigned int bank;
	unsigned int bytes = 0;

	for (bank = FIRST_DRAM_BANK; bank < NR_DRAM_BANKS; bank++) {
		rambank[bank].phys_offset = PAGE_OFFSET + bytes
						- BANK_TO_PHYS(bank);

		bytes += params->u1.s.pages_in_bank[bank - FIRST_DRAM_BANK] * PAGE_SIZE;

		rambank[bank].virt_addr   = PAGE_OFFSET + bytes;
	}

	rambank[FIRST_VRAM_BANK].phys_offset = 0xd6000000;
	rambank[FIRST_VRAM_BANK].virt_addr   = 0xd8000000;

	current->tss.memmap = __virt_to_phys((unsigned long)swapper_pg_dir);
}

static struct mapping {
	unsigned long virtual;
	unsigned long physical;
	unsigned long length;
} io_mapping[] = {
	{ SCREEN2_BASE,	SCREEN_START,	2*1048576	},	/* VRAM		*/
	{ IO_BASE,	IO_START,	IO_SIZE		}	/* IO space	*/
};

#define SIZEOFIO (sizeof(io_mapping) / sizeof(io_mapping[0]))

/* map in IO */
unsigned long setup_io_pagetables(unsigned long start_mem)
{
	struct mapping *mp;
	int i;

	for (i = 0, mp = io_mapping; i < SIZEOFIO; i++, mp++) {
		while ((mp->virtual & 1048575 || mp->physical & 1048575) && mp->length >= PAGE_SIZE) {
			alloc_init_page(&start_mem, mp->virtual, mp->physical, DOMAIN_IO,
					PTE_AP_WRITE);

			mp->length -= PAGE_SIZE;
			mp->virtual += PAGE_SIZE;
			mp->physical += PAGE_SIZE;
		}

		while (mp->length >= 1048576) {
			alloc_init_section(&start_mem, mp->virtual, mp->physical, DOMAIN_IO,
					   PMD_SECT_AP_WRITE);
			mp->length -= 1048576;
			mp->virtual += 1048576;
			mp->physical += 1048576;
		}

		while (mp->length >= PAGE_SIZE) {
			alloc_init_page(&start_mem, mp->virtual, mp->physical, DOMAIN_IO,
					PTE_AP_WRITE);

			mp->length -= PAGE_SIZE;
			mp->virtual += PAGE_SIZE;
			mp->physical += PAGE_SIZE;
		}
	}

	return start_mem;
}
