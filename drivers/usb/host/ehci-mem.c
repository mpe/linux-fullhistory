/*
 * Copyright (c) 2001 by David Brownell
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* this file is part of ehci-hcd.c */

/*-------------------------------------------------------------------------*/

/*
 * There's basically three types of memory:
 *	- data used only by the HCD ... kmalloc is fine
 *	- async and periodic schedules, shared by HC and HCD ... these
 *	  need to use pci_pool or pci_alloc_consistent
 *	- driver buffers, read/written by HC ... single shot DMA mapped 
 *
 * There's also PCI "register" data, which is memory mapped.
 * No memory seen by this driver is pagable.
 */

/*-------------------------------------------------------------------------*/
/* 
 * Allocator / cleanup for the per device structure
 * Called by hcd init / removal code
 */
static struct usb_hcd *ehci_hcd_alloc (void)
{
	struct ehci_hcd *ehci;

	ehci = (struct ehci_hcd *)
		kmalloc (sizeof (struct ehci_hcd), GFP_KERNEL);
	if (ehci != 0) {
		memset (ehci, 0, sizeof (struct ehci_hcd));
		ehci->hcd.product_desc = "EHCI Host Controller";
		return &ehci->hcd;
	}
	return 0;
}

static void ehci_hcd_free (struct usb_hcd *hcd)
{
	kfree (hcd_to_ehci (hcd));
}

/*-------------------------------------------------------------------------*/

/* Allocate the key transfer structures from the previously allocated pool */

static inline void ehci_qtd_init (struct ehci_qtd *qtd, dma_addr_t dma)
{
	memset (qtd, 0, sizeof *qtd);
	qtd->qtd_dma = dma;
	qtd->hw_token = cpu_to_le32 (QTD_STS_HALT);
	qtd->hw_next = EHCI_LIST_END;
	qtd->hw_alt_next = EHCI_LIST_END;
	INIT_LIST_HEAD (&qtd->qtd_list);
}

static struct ehci_qtd *ehci_qtd_alloc (struct ehci_hcd *ehci, int flags)
{
	struct ehci_qtd		*qtd;
	dma_addr_t		dma;

	qtd = pci_pool_alloc (ehci->qtd_pool, flags, &dma);
	if (qtd != 0) {
		ehci_qtd_init (qtd, dma);
	}
	return qtd;
}

static inline void ehci_qtd_free (struct ehci_hcd *ehci, struct ehci_qtd *qtd)
{
	pci_pool_free (ehci->qtd_pool, qtd, qtd->qtd_dma);
}


static struct ehci_qh *ehci_qh_alloc (struct ehci_hcd *ehci, int flags)
{
	struct ehci_qh		*qh;
	dma_addr_t		dma;

	qh = (struct ehci_qh *)
		pci_pool_alloc (ehci->qh_pool, flags, &dma);
	if (!qh)
		return qh;

	memset (qh, 0, sizeof *qh);
	atomic_set (&qh->refcount, 1);
	qh->qh_dma = dma;
	// INIT_LIST_HEAD (&qh->qh_list);
	INIT_LIST_HEAD (&qh->qtd_list);

	/* dummy td enables safe urb queuing */
	qh->dummy = ehci_qtd_alloc (ehci, flags);
	if (qh->dummy == 0) {
		ehci_dbg (ehci, "no dummy td\n");
		pci_pool_free (ehci->qh_pool, qh, qh->qh_dma);
		qh = 0;
	}
	return qh;
}

/* to share a qh (cpu threads, or hc) */
static inline struct ehci_qh *qh_get (/* ehci, */ struct ehci_qh *qh)
{
	atomic_inc (&qh->refcount);
	return qh;
}

static void qh_put (struct ehci_hcd *ehci, struct ehci_qh *qh)
{
	if (!atomic_dec_and_test (&qh->refcount))
		return;
	/* clean qtds first, and know this is not linked */
	if (!list_empty (&qh->qtd_list) || qh->qh_next.ptr) {
		ehci_dbg (ehci, "unused qh not empty!\n");
		BUG ();
	}
	if (qh->dummy)
		ehci_qtd_free (ehci, qh->dummy);
	pci_pool_free (ehci->qh_pool, qh, qh->qh_dma);
}

/*-------------------------------------------------------------------------*/

/* The queue heads and transfer descriptors are managed from pools tied 
 * to each of the "per device" structures.
 * This is the initialisation and cleanup code.
 */

static void ehci_mem_cleanup (struct ehci_hcd *ehci)
{
	if (ehci->async)
		qh_put (ehci, ehci->async);
	ehci->async = 0;

	/* PCI consistent memory and pools */
	if (ehci->qtd_pool)
		pci_pool_destroy (ehci->qtd_pool);
	ehci->qtd_pool = 0;

	if (ehci->qh_pool) {
		pci_pool_destroy (ehci->qh_pool);
		ehci->qh_pool = 0;
	}

	if (ehci->itd_pool)
		pci_pool_destroy (ehci->itd_pool);
	ehci->itd_pool = 0;

	if (ehci->sitd_pool)
		pci_pool_destroy (ehci->sitd_pool);
	ehci->sitd_pool = 0;

	if (ehci->periodic)
		pci_free_consistent (ehci->hcd.pdev,
			ehci->periodic_size * sizeof (u32),
			ehci->periodic, ehci->periodic_dma);
	ehci->periodic = 0;

	/* shadow periodic table */
	if (ehci->pshadow)
		kfree (ehci->pshadow);
	ehci->pshadow = 0;
}

/* remember to add cleanup code (above) if you add anything here */
static int ehci_mem_init (struct ehci_hcd *ehci, int flags)
{
	int i;

	/* QTDs for control/bulk/intr transfers */
	ehci->qtd_pool = pci_pool_create ("ehci_qtd", ehci->hcd.pdev,
			sizeof (struct ehci_qtd),
			32 /* byte alignment (for hw parts) */,
			4096 /* can't cross 4K */);
	if (!ehci->qtd_pool) {
		goto fail;
	}

	/* QHs for control/bulk/intr transfers */
	ehci->qh_pool = pci_pool_create ("ehci_qh", ehci->hcd.pdev,
			sizeof (struct ehci_qh),
			32 /* byte alignment (for hw parts) */,
			4096 /* can't cross 4K */);
	if (!ehci->qh_pool) {
		goto fail;
	}
	ehci->async = ehci_qh_alloc (ehci, flags);
	if (!ehci->async) {
		goto fail;
	}

	/* ITD for high speed ISO transfers */
	ehci->itd_pool = pci_pool_create ("ehci_itd", ehci->hcd.pdev,
			sizeof (struct ehci_itd),
			32 /* byte alignment (for hw parts) */,
			4096 /* can't cross 4K */);
	if (!ehci->itd_pool) {
		goto fail;
	}

	/* SITD for full/low speed split ISO transfers */
	ehci->sitd_pool = pci_pool_create ("ehci_sitd", ehci->hcd.pdev,
			sizeof (struct ehci_sitd),
			32 /* byte alignment (for hw parts) */,
			4096 /* can't cross 4K */);
	if (!ehci->sitd_pool) {
		goto fail;
	}

	/* Hardware periodic table */
	ehci->periodic = (u32 *)
		pci_alloc_consistent (ehci->hcd.pdev,
			ehci->periodic_size * sizeof (u32),
			&ehci->periodic_dma);
	if (ehci->periodic == 0) {
		goto fail;
	}
	for (i = 0; i < ehci->periodic_size; i++)
		ehci->periodic [i] = EHCI_LIST_END;

	/* software shadow of hardware table */
	ehci->pshadow = kmalloc (ehci->periodic_size * sizeof (void *), flags);
	if (ehci->pshadow == 0) {
		goto fail;
	}
	memset (ehci->pshadow, 0, ehci->periodic_size * sizeof (void *));

	return 0;

fail:
	ehci_dbg (ehci, "couldn't init memory\n");
	ehci_mem_cleanup (ehci);
	return -ENOMEM;
}
