/*
 * pSeries_lpar.c
 * Copyright (C) 2001 Todd Inglett, IBM Corporation
 *
 * pSeries LPAR support.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#define DEBUG

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/machdep.h>
#include <asm/abs_addr.h>
#include <asm/mmu_context.h>
#include <asm/ppcdebug.h>
#include <asm/iommu.h>
#include <asm/naca.h>
#include <asm/tlbflush.h>
#include <asm/tlb.h>
#include <asm/prom.h>
#include <asm/abs_addr.h>
#include <asm/cputable.h>
#include <asm/plpar_wrappers.h>

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

/* in pSeries_hvCall.S */
EXPORT_SYMBOL(plpar_hcall);
EXPORT_SYMBOL(plpar_hcall_4out);
EXPORT_SYMBOL(plpar_hcall_norets);
EXPORT_SYMBOL(plpar_hcall_8arg_2ret);

extern void fw_feature_init(void);
extern void pSeries_find_serial_port(void);


int vtermno;	/* virtual terminal# for udbg  */

static void udbg_putcLP(unsigned char c)
{
	char buf[16];
	unsigned long rc;

	if (c == '\n')
		udbg_putcLP('\r');

	buf[0] = c;
	do {
		rc = plpar_put_term_char(vtermno, 1, buf);
	} while(rc == H_Busy);
}

/* Buffered chars getc */
static long inbuflen;
static long inbuf[2];	/* must be 2 longs */

static int udbg_getc_pollLP(void)
{
	/* The interface is tricky because it may return up to 16 chars.
	 * We save them statically for future calls to udbg_getc().
	 */
	char ch, *buf = (char *)inbuf;
	int i;
	long rc;
	if (inbuflen == 0) {
		/* get some more chars. */
		inbuflen = 0;
		rc = plpar_get_term_char(vtermno, &inbuflen, buf);
		if (rc != H_Success)
			inbuflen = 0;	/* otherwise inbuflen is garbage */
	}
	if (inbuflen <= 0 || inbuflen > 16) {
		/* Catch error case as well as other oddities (corruption) */
		inbuflen = 0;
		return -1;
	}
	ch = buf[0];
	for (i = 1; i < inbuflen; i++)	/* shuffle them down. */
		buf[i-1] = buf[i];
	inbuflen--;
	return ch;
}

static unsigned char udbg_getcLP(void)
{
	int ch;
	for (;;) {
		ch = udbg_getc_pollLP();
		if (ch == -1) {
			/* This shouldn't be needed...but... */
			volatile unsigned long delay;
			for (delay=0; delay < 2000000; delay++)
				;
		} else {
			return ch;
		}
	}
}

/* call this from early_init() for a working debug console on
 * vterm capable LPAR machines
 */
void udbg_init_debug_lpar(void)
{
	vtermno = 0;
	ppc_md.udbg_putc = udbg_putcLP;
	ppc_md.udbg_getc = udbg_getcLP;
	ppc_md.udbg_getc_poll = udbg_getc_pollLP;
}

/* returns 0 if couldn't find or use /chosen/stdout as console */
int find_udbg_vterm(void)
{
	struct device_node *stdout_node;
	u32 *termno;
	char *name;
	int found = 0;

	/* find the boot console from /chosen/stdout */
	if (!of_chosen)
		return 0;
	name = (char *)get_property(of_chosen, "linux,stdout-path", NULL);
	if (name == NULL)
		return 0;
	stdout_node = of_find_node_by_path(name);
	if (!stdout_node)
		return 0;

	/* now we have the stdout node; figure out what type of device it is. */
	name = (char *)get_property(stdout_node, "name", NULL);
	if (!name) {
		printk(KERN_WARNING "stdout node missing 'name' property!\n");
		goto out;
	}

	if (strncmp(name, "vty", 3) == 0) {
		if (device_is_compatible(stdout_node, "hvterm1")) {
			termno = (u32 *)get_property(stdout_node, "reg", NULL);
			if (termno) {
				vtermno = termno[0];
				ppc_md.udbg_putc = udbg_putcLP;
				ppc_md.udbg_getc = udbg_getcLP;
				ppc_md.udbg_getc_poll = udbg_getc_pollLP;
				found = 1;
			}
		} else {
			/* XXX implement udbg_putcLP_vtty for hvterm-protocol1 case */
			printk(KERN_WARNING "%s doesn't speak hvterm1; "
					"can't print udbg messages\n",
			       stdout_node->full_name);
		}
	} else if (strncmp(name, "serial", 6)) {
		/* XXX fix ISA serial console */
		printk(KERN_WARNING "serial stdout on LPAR ('%s')! "
				"can't print udbg messages\n",
		       stdout_node->full_name);
	} else {
		printk(KERN_WARNING "don't know how to print to stdout '%s'\n",
		       stdout_node->full_name);
	}

out:
	of_node_put(stdout_node);
	return found;
}


long pSeries_lpar_hpte_insert(unsigned long hpte_group,
			      unsigned long va, unsigned long prpn,
			      int secondary, unsigned long hpteflags,
			      int bolted, int large)
{
	unsigned long arpn = physRpn_to_absRpn(prpn);
	unsigned long lpar_rc;
	unsigned long flags;
	unsigned long slot;
	HPTE lhpte;
	unsigned long dummy0, dummy1;

	/* Fill in the local HPTE with absolute rpn, avpn and flags */
	lhpte.dw1.dword1      = 0;
	lhpte.dw1.dw1.rpn     = arpn;
	lhpte.dw1.flags.flags = hpteflags;

	lhpte.dw0.dword0      = 0;
	lhpte.dw0.dw0.avpn    = va >> 23;
	lhpte.dw0.dw0.h       = secondary;
	lhpte.dw0.dw0.bolted  = bolted;
	lhpte.dw0.dw0.v       = 1;

	if (large) {
		lhpte.dw0.dw0.l = 1;
		lhpte.dw0.dw0.avpn &= ~0x1UL;
	}

	/* Now fill in the actual HPTE */
	/* Set CEC cookie to 0         */
	/* Zero page = 0               */
	/* I-cache Invalidate = 0      */
	/* I-cache synchronize = 0     */
	/* Exact = 0                   */
	flags = 0;

	/* XXX why is this here? - Anton */
	if (hpteflags & (_PAGE_GUARDED|_PAGE_NO_CACHE))
		lhpte.dw1.flags.flags &= ~_PAGE_COHERENT;

	lpar_rc = plpar_hcall(H_ENTER, flags, hpte_group, lhpte.dw0.dword0,
			      lhpte.dw1.dword1, &slot, &dummy0, &dummy1);

	if (unlikely(lpar_rc == H_PTEG_Full))
		return -1;

	/*
	 * Since we try and ioremap PHBs we don't own, the pte insert
	 * will fail. However we must catch the failure in hash_page
	 * or we will loop forever, so return -2 in this case.
	 */
	if (unlikely(lpar_rc != H_Success))
		return -2;

	/* Because of iSeries, we have to pass down the secondary
	 * bucket bit here as well
	 */
	return (slot & 7) | (secondary << 3);
}

static spinlock_t pSeries_lpar_tlbie_lock = SPIN_LOCK_UNLOCKED;

static long pSeries_lpar_hpte_remove(unsigned long hpte_group)
{
	unsigned long slot_offset;
	unsigned long lpar_rc;
	int i;
	unsigned long dummy1, dummy2;

	/* pick a random slot to start at */
	slot_offset = mftb() & 0x7;

	for (i = 0; i < HPTES_PER_GROUP; i++) {

		/* don't remove a bolted entry */
		lpar_rc = plpar_pte_remove(H_ANDCOND, hpte_group + slot_offset,
					   (0x1UL << 4), &dummy1, &dummy2);

		if (lpar_rc == H_Success)
			return i;

		BUG_ON(lpar_rc != H_Not_Found);

		slot_offset++;
		slot_offset &= 0x7;
	}

	return -1;
}

static void pSeries_lpar_hptab_clear(void)
{
	unsigned long size_bytes = 1UL << naca->pftSize;
	unsigned long hpte_count = size_bytes >> 4;
	unsigned long dummy1, dummy2;
	int i;

	/* TODO: Use bulk call */
	for (i = 0; i < hpte_count; i++)
		plpar_pte_remove(0, i, 0, &dummy1, &dummy2);
}

/*
 * NOTE: for updatepp ops we are fortunate that the linux "newpp" bits and
 * the low 3 bits of flags happen to line up.  So no transform is needed.
 * We can probably optimize here and assume the high bits of newpp are
 * already zero.  For now I am paranoid.
 */
static long pSeries_lpar_hpte_updatepp(unsigned long slot, unsigned long newpp,
				       unsigned long va, int large, int local)
{
	unsigned long lpar_rc;
	unsigned long flags = (newpp & 7) | H_AVPN;
	unsigned long avpn = va >> 23;

	if (large)
		avpn &= ~0x1UL;

	lpar_rc = plpar_pte_protect(flags, slot, (avpn << 7));

	if (lpar_rc == H_Not_Found)
		return -1;

	BUG_ON(lpar_rc != H_Success);

	return 0;
}

static unsigned long pSeries_lpar_hpte_getword0(unsigned long slot)
{
	unsigned long dword0;
	unsigned long lpar_rc;
	unsigned long dummy_word1;
	unsigned long flags;

	/* Read 1 pte at a time                        */
	/* Do not need RPN to logical page translation */
	/* No cross CEC PFT access                     */
	flags = 0;

	lpar_rc = plpar_pte_read(flags, slot, &dword0, &dummy_word1);

	BUG_ON(lpar_rc != H_Success);

	return dword0;
}

static long pSeries_lpar_hpte_find(unsigned long vpn)
{
	unsigned long hash;
	unsigned long i, j;
	long slot;
	union {
		unsigned long dword0;
		Hpte_dword0 dw0;
	} hpte_dw0;
	Hpte_dword0 dw0;

	hash = hpt_hash(vpn, 0);

	for (j = 0; j < 2; j++) {
		slot = (hash & htab_data.htab_hash_mask) * HPTES_PER_GROUP;
		for (i = 0; i < HPTES_PER_GROUP; i++) {
			hpte_dw0.dword0 = pSeries_lpar_hpte_getword0(slot);
			dw0 = hpte_dw0.dw0;

			if ((dw0.avpn == (vpn >> 11)) && dw0.v &&
			    (dw0.h == j)) {
				/* HPTE matches */
				if (j)
					slot = -slot;
				return slot;
			}
			++slot;
		}
		hash = ~hash;
	}

	return -1;
} 

static void pSeries_lpar_hpte_updateboltedpp(unsigned long newpp,
					     unsigned long ea)
{
	unsigned long lpar_rc;
	unsigned long vsid, va, vpn, flags;
	long slot;

	vsid = get_kernel_vsid(ea);
	va = (vsid << 28) | (ea & 0x0fffffff);
	vpn = va >> PAGE_SHIFT;

	slot = pSeries_lpar_hpte_find(vpn);
	BUG_ON(slot == -1);

	flags = newpp & 3;
	lpar_rc = plpar_pte_protect(flags, slot, 0);

	BUG_ON(lpar_rc != H_Success);
}

static void pSeries_lpar_hpte_invalidate(unsigned long slot, unsigned long va,
					 int large, int local)
{
	unsigned long avpn = va >> 23;
	unsigned long lpar_rc;
	unsigned long dummy1, dummy2;

	if (large)
		avpn &= ~0x1UL;

	lpar_rc = plpar_pte_remove(H_AVPN, slot, (avpn << 7), &dummy1,
				   &dummy2);

	if (lpar_rc == H_Not_Found)
		return;

	BUG_ON(lpar_rc != H_Success);
}

/*
 * Take a spinlock around flushes to avoid bouncing the hypervisor tlbie
 * lock.
 */
void pSeries_lpar_flush_hash_range(unsigned long context, unsigned long number,
				   int local)
{
	int i;
	unsigned long flags;
	struct ppc64_tlb_batch *batch = &__get_cpu_var(ppc64_tlb_batch);
	int lock_tlbie = !(cur_cpu_spec->cpu_features & CPU_FTR_LOCKLESS_TLBIE);

	if (lock_tlbie)
		spin_lock_irqsave(&pSeries_lpar_tlbie_lock, flags);

	for (i = 0; i < number; i++)
		flush_hash_page(context, batch->addr[i], batch->pte[i], local);

	if (lock_tlbie)
		spin_unlock_irqrestore(&pSeries_lpar_tlbie_lock, flags);
}

void hpte_init_lpar(void)
{
	ppc_md.hpte_invalidate	= pSeries_lpar_hpte_invalidate;
	ppc_md.hpte_updatepp	= pSeries_lpar_hpte_updatepp;
	ppc_md.hpte_updateboltedpp = pSeries_lpar_hpte_updateboltedpp;
	ppc_md.hpte_insert	= pSeries_lpar_hpte_insert;
	ppc_md.hpte_remove	= pSeries_lpar_hpte_remove;
	ppc_md.flush_hash_range	= pSeries_lpar_flush_hash_range;
	ppc_md.hpte_clear_all   = pSeries_lpar_hptab_clear;

	htab_finish_init();
}
