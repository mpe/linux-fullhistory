/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id$
 */

#include "mthca_memfree.h"
#include "mthca_dev.h"
#include "mthca_cmd.h"

/*
 * We allocate in as big chunks as we can, up to a maximum of 256 KB
 * per chunk.
 */
enum {
	MTHCA_ICM_ALLOC_SIZE   = 1 << 18,
	MTHCA_TABLE_CHUNK_SIZE = 1 << 18
};

void mthca_free_icm(struct mthca_dev *dev, struct mthca_icm *icm)
{
	struct mthca_icm_chunk *chunk, *tmp;
	int i;

	if (!icm)
		return;

	list_for_each_entry_safe(chunk, tmp, &icm->chunk_list, list) {
		if (chunk->nsg > 0)
			pci_unmap_sg(dev->pdev, chunk->mem, chunk->npages,
				     PCI_DMA_BIDIRECTIONAL);

		for (i = 0; i < chunk->npages; ++i)
			__free_pages(chunk->mem[i].page,
				     get_order(chunk->mem[i].length));

		kfree(chunk);
	}

	kfree(icm);
}

struct mthca_icm *mthca_alloc_icm(struct mthca_dev *dev, int npages,
				  unsigned int gfp_mask)
{
	struct mthca_icm *icm;
	struct mthca_icm_chunk *chunk = NULL;
	int cur_order;

	icm = kmalloc(sizeof *icm, gfp_mask & ~(__GFP_HIGHMEM | __GFP_NOWARN));
	if (!icm)
		return icm;

	icm->refcount = 0;
	INIT_LIST_HEAD(&icm->chunk_list);

	cur_order = get_order(MTHCA_ICM_ALLOC_SIZE);

	while (npages > 0) {
		if (!chunk) {
			chunk = kmalloc(sizeof *chunk,
					gfp_mask & ~(__GFP_HIGHMEM | __GFP_NOWARN));
			if (!chunk)
				goto fail;

			chunk->npages = 0;
			chunk->nsg    = 0;
			list_add_tail(&chunk->list, &icm->chunk_list);
		}

		while (1 << cur_order > npages)
			--cur_order;

		chunk->mem[chunk->npages].page = alloc_pages(gfp_mask, cur_order);
		if (chunk->mem[chunk->npages].page) {
			chunk->mem[chunk->npages].length = PAGE_SIZE << cur_order;
			chunk->mem[chunk->npages].offset = 0;

			if (++chunk->npages == MTHCA_ICM_CHUNK_LEN) {
				chunk->nsg = pci_map_sg(dev->pdev, chunk->mem,
							chunk->npages,
							PCI_DMA_BIDIRECTIONAL);

				if (chunk->nsg <= 0)
					goto fail;

				chunk = NULL;
			}

			npages -= 1 << cur_order;
		} else {
			--cur_order;
			if (cur_order < 0)
				goto fail;
		}
	}

	if (chunk) {
		chunk->nsg = pci_map_sg(dev->pdev, chunk->mem,
					chunk->npages,
					PCI_DMA_BIDIRECTIONAL);

		if (chunk->nsg <= 0)
			goto fail;
	}

	return icm;

fail:
	mthca_free_icm(dev, icm);
	return NULL;
}

int mthca_table_get(struct mthca_dev *dev, struct mthca_icm_table *table, int obj)
{
	int i = (obj & (table->num_obj - 1)) * table->obj_size / MTHCA_TABLE_CHUNK_SIZE;
	int ret = 0;
	u8 status;

	down(&table->mutex);

	if (table->icm[i]) {
		++table->icm[i]->refcount;
		goto out;
	}

	table->icm[i] = mthca_alloc_icm(dev, MTHCA_TABLE_CHUNK_SIZE >> PAGE_SHIFT,
					(table->lowmem ? GFP_KERNEL : GFP_HIGHUSER) |
					__GFP_NOWARN);
	if (!table->icm[i]) {
		ret = -ENOMEM;
		goto out;
	}

	if (mthca_MAP_ICM(dev, table->icm[i], table->virt + i * MTHCA_TABLE_CHUNK_SIZE,
			  &status) || status) {
		mthca_free_icm(dev, table->icm[i]);
		table->icm[i] = NULL;
		ret = -ENOMEM;
		goto out;
	}

	++table->icm[i]->refcount;

out:
	up(&table->mutex);
	return ret;
}

void mthca_table_put(struct mthca_dev *dev, struct mthca_icm_table *table, int obj)
{
	int i = (obj & (table->num_obj - 1)) * table->obj_size / MTHCA_TABLE_CHUNK_SIZE;
	u8 status;

	down(&table->mutex);

	if (--table->icm[i]->refcount == 0) {
		mthca_UNMAP_ICM(dev, table->virt + i * MTHCA_TABLE_CHUNK_SIZE,
				MTHCA_TABLE_CHUNK_SIZE >> 12, &status);
		mthca_free_icm(dev, table->icm[i]);
		table->icm[i] = NULL;
	}

	up(&table->mutex);
}

struct mthca_icm_table *mthca_alloc_icm_table(struct mthca_dev *dev,
					      u64 virt, int obj_size,
					      int nobj, int reserved,
					      int use_lowmem)
{
	struct mthca_icm_table *table;
	int num_icm;
	int i;
	u8 status;

	num_icm = obj_size * nobj / MTHCA_TABLE_CHUNK_SIZE;

	table = kmalloc(sizeof *table + num_icm * sizeof *table->icm, GFP_KERNEL);
	if (!table)
		return NULL;

	table->virt     = virt;
	table->num_icm  = num_icm;
	table->num_obj  = nobj;
	table->obj_size = obj_size;
	table->lowmem   = use_lowmem;
	init_MUTEX(&table->mutex);

	for (i = 0; i < num_icm; ++i)
		table->icm[i] = NULL;

	for (i = 0; i * MTHCA_TABLE_CHUNK_SIZE < reserved * obj_size; ++i) {
		table->icm[i] = mthca_alloc_icm(dev, MTHCA_TABLE_CHUNK_SIZE >> PAGE_SHIFT,
						(use_lowmem ? GFP_KERNEL : GFP_HIGHUSER) |
						__GFP_NOWARN);
		if (!table->icm[i])
			goto err;
		if (mthca_MAP_ICM(dev, table->icm[i], virt + i * MTHCA_TABLE_CHUNK_SIZE,
				  &status) || status) {
			mthca_free_icm(dev, table->icm[i]);
			table->icm[i] = NULL;
			goto err;
		}

		/*
		 * Add a reference to this ICM chunk so that it never
		 * gets freed (since it contains reserved firmware objects).
		 */
		++table->icm[i]->refcount;
	}

	return table;

err:
	for (i = 0; i < num_icm; ++i)
		if (table->icm[i]) {
			mthca_UNMAP_ICM(dev, virt + i * MTHCA_TABLE_CHUNK_SIZE,
					MTHCA_TABLE_CHUNK_SIZE >> 12, &status);
			mthca_free_icm(dev, table->icm[i]);
		}

	kfree(table);

	return NULL;
}

void mthca_free_icm_table(struct mthca_dev *dev, struct mthca_icm_table *table)
{
	int i;
	u8 status;

	for (i = 0; i < table->num_icm; ++i)
		if (table->icm[i]) {
			mthca_UNMAP_ICM(dev, table->virt + i * MTHCA_TABLE_CHUNK_SIZE,
					MTHCA_TABLE_CHUNK_SIZE >> 12, &status);
			mthca_free_icm(dev, table->icm[i]);
		}

	kfree(table);
}

static u64 mthca_uarc_virt(struct mthca_dev *dev, int page)
{
	return dev->uar_table.uarc_base +
		dev->driver_uar.index * dev->uar_table.uarc_size +
		page * 4096;
}

int mthca_alloc_db(struct mthca_dev *dev, int type, u32 qn, u32 **db)
{
	int group;
	int start, end, dir;
	int i, j;
	struct mthca_db_page *page;
	int ret = 0;
	u8 status;

	down(&dev->db_tab->mutex);

	switch (type) {
	case MTHCA_DB_TYPE_CQ_ARM:
	case MTHCA_DB_TYPE_SQ:
		group = 0;
		start = 0;
		end   = dev->db_tab->max_group1;
		dir   = 1;
		break;

	case MTHCA_DB_TYPE_CQ_SET_CI:
	case MTHCA_DB_TYPE_RQ:
	case MTHCA_DB_TYPE_SRQ:
		group = 1;
		start = dev->db_tab->npages - 1;
		end   = dev->db_tab->min_group2;
		dir   = -1;
		break;

	default:
		return -1;
	}

	for (i = start; i != end; i += dir)
		if (dev->db_tab->page[i].db_rec &&
		    !bitmap_full(dev->db_tab->page[i].used,
				 MTHCA_DB_REC_PER_PAGE)) {
			page = dev->db_tab->page + i;
			goto found;
		}

	if (dev->db_tab->max_group1 >= dev->db_tab->min_group2 - 1) {
		ret = -ENOMEM;
		goto out;
	}

	page = dev->db_tab->page + end;
	page->db_rec = dma_alloc_coherent(&dev->pdev->dev, 4096,
					  &page->mapping, GFP_KERNEL);
	if (!page->db_rec) {
		ret = -ENOMEM;
		goto out;
	}
	memset(page->db_rec, 0, 4096);

	ret = mthca_MAP_ICM_page(dev, page->mapping, mthca_uarc_virt(dev, i), &status);
	if (!ret && status)
		ret = -EINVAL;
	if (ret) {
		dma_free_coherent(&dev->pdev->dev, 4096,
				  page->db_rec, page->mapping);
		goto out;
	}

	bitmap_zero(page->used, MTHCA_DB_REC_PER_PAGE);
	if (group == 0)
		++dev->db_tab->max_group1;
	else
		--dev->db_tab->min_group2;

found:
	j = find_first_zero_bit(page->used, MTHCA_DB_REC_PER_PAGE);
	set_bit(j, page->used);

	if (group == 1)
		j = MTHCA_DB_REC_PER_PAGE - 1 - j;

	ret = i * MTHCA_DB_REC_PER_PAGE + j;

	page->db_rec[j] = cpu_to_be64((qn << 8) | (type << 5));

	*db = (u32 *) &page->db_rec[j];

out:
	up(&dev->db_tab->mutex);

	return ret;
}

void mthca_free_db(struct mthca_dev *dev, int type, int db_index)
{
	int i, j;
	struct mthca_db_page *page;
	u8 status;

	i = db_index / MTHCA_DB_REC_PER_PAGE;
	j = db_index % MTHCA_DB_REC_PER_PAGE;

	page = dev->db_tab->page + i;

	down(&dev->db_tab->mutex);

	page->db_rec[j] = 0;
	if (i >= dev->db_tab->min_group2)
		j = MTHCA_DB_REC_PER_PAGE - 1 - j;
	clear_bit(j, page->used);

	if (bitmap_empty(page->used, MTHCA_DB_REC_PER_PAGE) &&
	    i >= dev->db_tab->max_group1 - 1) {
		mthca_UNMAP_ICM(dev, mthca_uarc_virt(dev, i), 1, &status);

		dma_free_coherent(&dev->pdev->dev, 4096,
				  page->db_rec, page->mapping);
		page->db_rec = NULL;

		if (i == dev->db_tab->max_group1) {
			--dev->db_tab->max_group1;
			/* XXX may be able to unmap more pages now */
		}
		if (i == dev->db_tab->min_group2)
			++dev->db_tab->min_group2;
	}

	up(&dev->db_tab->mutex);
}

int mthca_init_db_tab(struct mthca_dev *dev)
{
	int i;

	if (dev->hca_type != ARBEL_NATIVE)
		return 0;

	dev->db_tab = kmalloc(sizeof *dev->db_tab, GFP_KERNEL);
	if (!dev->db_tab)
		return -ENOMEM;

	init_MUTEX(&dev->db_tab->mutex);

	dev->db_tab->npages     = dev->uar_table.uarc_size / PAGE_SIZE;
	dev->db_tab->max_group1 = 0;
	dev->db_tab->min_group2 = dev->db_tab->npages - 1;

	dev->db_tab->page = kmalloc(dev->db_tab->npages *
				    sizeof *dev->db_tab->page,
				    GFP_KERNEL);
	if (!dev->db_tab->page) {
		kfree(dev->db_tab);
		return -ENOMEM;
	}

	for (i = 0; i < dev->db_tab->npages; ++i)
		dev->db_tab->page[i].db_rec = NULL;

	return 0;
}

void mthca_cleanup_db_tab(struct mthca_dev *dev)
{
	int i;
	u8 status;

	if (dev->hca_type != ARBEL_NATIVE)
		return;

	/*
	 * Because we don't always free our UARC pages when they
	 * become empty to make mthca_free_db() simpler we need to
	 * make a sweep through the doorbell pages and free any
	 * leftover pages now.
	 */
	for (i = 0; i < dev->db_tab->npages; ++i) {
		if (!dev->db_tab->page[i].db_rec)
			continue;

		if (!bitmap_empty(dev->db_tab->page[i].used, MTHCA_DB_REC_PER_PAGE))
			mthca_warn(dev, "Kernel UARC page %d not empty\n", i);

		mthca_UNMAP_ICM(dev, mthca_uarc_virt(dev, i), 1, &status);

		dma_free_coherent(&dev->pdev->dev, 4096,
				  dev->db_tab->page[i].db_rec,
				  dev->db_tab->page[i].mapping);
	}

	kfree(dev->db_tab->page);
	kfree(dev->db_tab);
}
