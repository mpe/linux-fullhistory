/*
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1997, 2001 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 2000, 2001 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include <linux/config.h>
#include <asm/mmu_context.h>
#include <asm/bootinfo.h>
#include <asm/cpu.h>

extern void except_vec0_sb1(void);
extern void except_vec1_sb1(void);

/* Dump the current entry* and pagemask registers */
static inline void dump_cur_tlb_regs(void)
{
	unsigned int entryhihi, entryhilo, entrylo0hi, entrylo0lo, entrylo1hi;
	unsigned int entrylo1lo, pagemask;

	__asm__ __volatile__ (
		".set push             \n"
		".set noreorder        \n"
		".set mips64           \n"
		".set noat             \n"
		"     tlbr             \n"
		"     dmfc0  $1, $10   \n"
		"     dsrl32 %0, $1, 0 \n"
		"     sll    %1, $1, 0 \n"
		"     dmfc0  $1, $2    \n"
		"     dsrl32 %2, $1, 0 \n"
		"     sll    %3, $1, 0 \n"
		"     dmfc0  $1, $3    \n"
		"     dsrl32 %4, $1, 0 \n"
		"     sll    %5, $1, 0 \n"
		"     mfc0   %6, $5    \n"
		".set pop              \n"
		: "=r" (entryhihi), "=r" (entryhilo),
		  "=r" (entrylo0hi), "=r" (entrylo0lo),
		  "=r" (entrylo1hi), "=r" (entrylo1lo),
		  "=r" (pagemask));

	printk("%08X%08X %08X%08X %08X%08X %08X",
	       entryhihi, entryhilo,
	       entrylo0hi, entrylo0lo,
	       entrylo1hi, entrylo1lo,
	       pagemask);
}

void sb1_dump_tlb(void)
{
	unsigned long old_ctx;
	unsigned long flags;
	int entry;
	local_irq_save(flags);
	old_ctx = read_c0_entryhi();
	printk("Current TLB registers state:\n"
	       "      EntryHi       EntryLo0          EntryLo1     PageMask  Index\n"
	       "--------------------------------------------------------------------\n");
	dump_cur_tlb_regs();
	printk(" %08X\n", read_c0_index());
	printk("\n\nFull TLB Dump:\n"
	       "Idx      EntryHi       EntryLo0          EntryLo1     PageMask\n"
	       "--------------------------------------------------------------\n");
	for (entry = 0; entry < current_cpu_data.tlbsize; entry++) {
		write_c0_index(entry);
		printk("\n%02i ", entry);
		dump_cur_tlb_regs();
	}
	printk("\n");
	write_c0_entryhi(old_ctx);
	local_irq_restore(flags);
}

void local_flush_tlb_all(void)
{
	unsigned long flags;
	unsigned long old_ctx;
	int entry;

	local_irq_save(flags);
	/* Save old context and create impossible VPN2 value */
	old_ctx = read_c0_entryhi() & ASID_MASK;
	write_c0_entrylo0(0);
	write_c0_entrylo1(0);
	for (entry = 0; entry < current_cpu_data.tlbsize; entry++) {
		write_c0_entryhi(KSEG0 + (PAGE_SIZE << 1) * entry);
		write_c0_index(entry);
		tlb_write_indexed();
	}
	write_c0_entryhi(old_ctx);
	local_irq_restore(flags);
}


/*
 * Use a bogus region of memory (starting at 0) to sanitize the TLB's.
 * Use increments of the maximum page size (16MB), and check for duplicate
 * entries before doing a given write.  Then, when we're safe from collisions
 * with the firmware, go back and give all the entries invalid addresses with
 * the normal flush routine.
 */
void sb1_sanitize_tlb(void)
{
	int entry;
	long addr = 0;

	long inc = 1<<24;  /* 16MB */
	/* Save old context and create impossible VPN2 value */
	write_c0_entrylo0(0);
	write_c0_entrylo1(0);
	for (entry = 0; entry < current_cpu_data.tlbsize; entry++) {
		do {
			addr += inc;
			write_c0_entryhi(addr);
			tlb_probe();
		} while ((int)(read_c0_index()) >= 0);
		write_c0_index(entry);
		tlb_write_indexed();
	}
	/* Now that we know we're safe from collisions, we can safely flush
	   the TLB with the "normal" routine. */
	local_flush_tlb_all();
}

void local_flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
	unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	int cpu;

	local_irq_save(flags);
	cpu = smp_processor_id();
	if (cpu_context(cpu, mm) != 0) {
		int size;
		size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		size = (size + 1) >> 1;
		if (size <= (current_cpu_data.tlbsize/2)) {
			int oldpid = read_c0_entryhi() & ASID_MASK;
			int newpid = cpu_asid(cpu, mm);

			start &= (PAGE_MASK << 1);
			end += ((PAGE_SIZE << 1) - 1);
			end &= (PAGE_MASK << 1);
			while (start < end) {
				int idx;

				write_c0_entryhi(start | newpid);
				start += (PAGE_SIZE << 1);
				tlb_probe();
				idx = read_c0_index();
				write_c0_entrylo0(0);
				write_c0_entrylo1(0);
				write_c0_entryhi(KSEG0 + (idx << (PAGE_SHIFT+1)));
				if (idx < 0)
					continue;
				tlb_write_indexed();
			}
			write_c0_entryhi(oldpid);
		} else {
			drop_mmu_context(mm, cpu);
		}
	}
	local_irq_restore(flags);
}

void local_flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	unsigned long flags;
	int size;

	size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
	size = (size + 1) >> 1;

	local_irq_save(flags);
	if (size <= (current_cpu_data.tlbsize/2)) {
		int pid = read_c0_entryhi();

		start &= (PAGE_MASK << 1);
		end += ((PAGE_SIZE << 1) - 1);
		end &= (PAGE_MASK << 1);

		while (start < end) {
			int idx;

			write_c0_entryhi(start);
			start += (PAGE_SIZE << 1);
			tlb_probe();
			idx = read_c0_index();
			write_c0_entrylo0(0);
			write_c0_entrylo1(0);
			write_c0_entryhi(KSEG0 + (idx << (PAGE_SHIFT+1)));
			if (idx < 0)
				continue;
			tlb_write_indexed();
		}
		write_c0_entryhi(pid);
	} else {
		local_flush_tlb_all();
	}
	local_irq_restore(flags);
}

void local_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	unsigned long flags;
	int cpu = smp_processor_id();

	local_irq_save(flags);
	if (cpu_context(cpu, vma->vm_mm) != 0) {
		int oldpid, newpid, idx;
		newpid = cpu_asid(cpu, vma->vm_mm);
		page &= (PAGE_MASK << 1);
		oldpid = read_c0_entryhi() & ASID_MASK;
		write_c0_entryhi(page | newpid);
		tlb_probe();
		idx = read_c0_index();
		write_c0_entrylo0(0);
		write_c0_entrylo1(0);
		if(idx < 0)
			goto finish;
		/* Make sure all entries differ. */
		write_c0_entryhi(KSEG0+(idx<<(PAGE_SHIFT+1)));
		tlb_write_indexed();
	finish:
		write_c0_entryhi(oldpid);
	}
	local_irq_restore(flags);
}

/*
 * This one is only used for pages with the global bit set so we don't care
 * much about the ASID.
 */
void local_flush_tlb_one(unsigned long page)
{
	unsigned long flags;
	int oldpid, idx;

	local_irq_save(flags);
	page &= (PAGE_MASK << 1);
	oldpid = read_c0_entryhi() & ASID_MASK;
	write_c0_entryhi(page);
	tlb_probe();
	idx = read_c0_index();
	write_c0_entrylo0(0);
	write_c0_entrylo1(0);
	if (idx >= 0) {
		/* Make sure all entries differ. */
		write_c0_entryhi(KSEG0+(idx<<(PAGE_SHIFT+1)));
		tlb_write_indexed();
	}
	write_c0_entryhi(oldpid);

	local_irq_restore(flags);
}

/* All entries common to a mm share an asid.  To effectively flush
   these entries, we just bump the asid. */
void local_flush_tlb_mm(struct mm_struct *mm)
{
	int cpu = smp_processor_id();
	if (cpu_context(cpu, mm) != 0) {
		drop_mmu_context(mm, cpu);
	}
}

/* Stolen from mips32 routines */

void __update_tlb(struct vm_area_struct *vma, unsigned long address, pte_t pte)
{
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	int idx, pid;

	/*
	 * Handle debugger faulting in for debugee.
	 */
	if (current->active_mm != vma->vm_mm)
		return;

	local_irq_save(flags);

	pid = read_c0_entryhi() & ASID_MASK;
	address &= (PAGE_MASK << 1);
	write_c0_entryhi(address | (pid));
	pgdp = pgd_offset(vma->vm_mm, address);
	tlb_probe();
	pmdp = pmd_offset(pgdp, address);
	idx = read_c0_index();
	ptep = pte_offset_map(pmdp, address);
	write_c0_entrylo0(pte_val(*ptep++) >> 6);
	write_c0_entrylo1(pte_val(*ptep) >> 6);
	if (idx < 0) {
		tlb_write_random();
	} else {
		tlb_write_indexed();
	}
	local_irq_restore(flags);
}

/*
 * This is called from loadmmu.c.  We have to set up all the
 * memory management function pointers, as well as initialize
 * the caches and tlbs
 */
void sb1_tlb_init(void)
{
	write_c0_pagemask(PM_4K);

	/*
	 * We don't know what state the firmware left the TLB's in, so this is
	 * the ultra-conservative way to flush the TLB's and avoid machine
	 * check exceptions due to duplicate TLB entries
	 */
	sb1_sanitize_tlb();

#ifdef CONFIG_MIPS32
	memcpy((void *)KSEG0, except_vec0_sb1, 0x80);
	flush_icache_range(KSEG0, KSEG0 + 0x80);
#endif
#ifdef CONFIG_MIPS64
	memcpy((void *)KSEG0 + 0x80, except_vec1_sb1, 0x80);
	flush_icache_range(KSEG0 + 0x80, KSEG0 + 0x100);
#endif
}
