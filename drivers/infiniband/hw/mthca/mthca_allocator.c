/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
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
 * $Id: mthca_allocator.c 1349 2004-12-16 21:09:43Z roland $
 */

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/bitmap.h>

#include "mthca_dev.h"

/* Trivial bitmap-based allocator */
u32 mthca_alloc(struct mthca_alloc *alloc)
{
	u32 obj;

	spin_lock(&alloc->lock);
	obj = find_next_zero_bit(alloc->table, alloc->max, alloc->last);
	if (obj >= alloc->max) {
		alloc->top = (alloc->top + alloc->max) & alloc->mask;
		obj = find_first_zero_bit(alloc->table, alloc->max);
	}

	if (obj < alloc->max) {
		set_bit(obj, alloc->table);
		obj |= alloc->top;
	} else
		obj = -1;

	spin_unlock(&alloc->lock);

	return obj;
}

void mthca_free(struct mthca_alloc *alloc, u32 obj)
{
	obj &= alloc->max - 1;
	spin_lock(&alloc->lock);
	clear_bit(obj, alloc->table);
	alloc->last = min(alloc->last, obj);
	alloc->top = (alloc->top + alloc->max) & alloc->mask;
	spin_unlock(&alloc->lock);
}

int mthca_alloc_init(struct mthca_alloc *alloc, u32 num, u32 mask,
		     u32 reserved)
{
	int i;

	/* num must be a power of 2 */
	if (num != 1 << (ffs(num) - 1))
		return -EINVAL;

	alloc->last = 0;
	alloc->top  = 0;
	alloc->max  = num;
	alloc->mask = mask;
	spin_lock_init(&alloc->lock);
	alloc->table = kmalloc(BITS_TO_LONGS(num) * sizeof (long),
			       GFP_KERNEL);
	if (!alloc->table)
		return -ENOMEM;

	bitmap_zero(alloc->table, num);
	for (i = 0; i < reserved; ++i)
		set_bit(i, alloc->table);

	return 0;
}

void mthca_alloc_cleanup(struct mthca_alloc *alloc)
{
	kfree(alloc->table);
}

/*
 * Array of pointers with lazy allocation of leaf pages.  Callers of
 * _get, _set and _clear methods must use a lock or otherwise
 * serialize access to the array.
 */

void *mthca_array_get(struct mthca_array *array, int index)
{
	int p = (index * sizeof (void *)) >> PAGE_SHIFT;

	if (array->page_list[p].page) {
		int i = index & (PAGE_SIZE / sizeof (void *) - 1);
		return array->page_list[p].page[i];
	} else
		return NULL;
}

int mthca_array_set(struct mthca_array *array, int index, void *value)
{
	int p = (index * sizeof (void *)) >> PAGE_SHIFT;

	/* Allocate with GFP_ATOMIC because we'll be called with locks held. */
	if (!array->page_list[p].page)
		array->page_list[p].page = (void **) get_zeroed_page(GFP_ATOMIC);

	if (!array->page_list[p].page)
		return -ENOMEM;

	array->page_list[p].page[index & (PAGE_SIZE / sizeof (void *) - 1)] =
		value;
	++array->page_list[p].used;

	return 0;
}

void mthca_array_clear(struct mthca_array *array, int index)
{
	int p = (index * sizeof (void *)) >> PAGE_SHIFT;

	if (--array->page_list[p].used == 0) {
		free_page((unsigned long) array->page_list[p].page);
		array->page_list[p].page = NULL;
	}

	if (array->page_list[p].used < 0)
		pr_debug("Array %p index %d page %d with ref count %d < 0\n",
			 array, index, p, array->page_list[p].used);
}

int mthca_array_init(struct mthca_array *array, int nent)
{
	int npage = (nent * sizeof (void *) + PAGE_SIZE - 1) / PAGE_SIZE;
	int i;

	array->page_list = kmalloc(npage * sizeof *array->page_list, GFP_KERNEL);
	if (!array->page_list)
		return -ENOMEM;

	for (i = 0; i < npage; ++i) {
		array->page_list[i].page = NULL;
		array->page_list[i].used = 0;
	}

	return 0;
}

void mthca_array_cleanup(struct mthca_array *array, int nent)
{
	int i;

	for (i = 0; i < (nent * sizeof (void *) + PAGE_SIZE - 1) / PAGE_SIZE; ++i)
		free_page((unsigned long) array->page_list[i].page);

	kfree(array->page_list);
}
