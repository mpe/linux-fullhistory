
/*
 **********************************************************************
 *     osutils.c - OS Services layer for emu10k1 driver
 *     Copyright 1999, 2000 Creative Labs, Inc.
 *
 **********************************************************************
 *
 *     Date                 Author          Summary of changes
 *     ----                 ------          ------------------
 *     October 20, 1999     Bertrand Lee    base code release
 *     November 2, 1999     Alan Cox        cleaned up
 *
 **********************************************************************
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *     USA.
 *
 **********************************************************************
 */

#include "hwaccess.h"

struct memhandle *emu10k1_alloc_memphysical(u32 size)
{
	struct memhandle *handle;
	u32 reqpage, order;

	if ((handle = (struct memhandle *) kmalloc(sizeof(struct memhandle), GFP_KERNEL)) == NULL)
		return handle;

	DPD(3, "kmalloc: [%p]\n", handle);

	order = 0;
	reqpage = size / PAGE_SIZE;

	if (size % PAGE_SIZE)
		reqpage++;

	if (reqpage != 0) {
		reqpage--;
		while (reqpage > 0) {
			reqpage >>= 1;
			order++;
		}
	}

	if ((handle->virtaddx = (void *) __get_free_pages(GFP_KERNEL, order)) == NULL) {
		kfree(handle);

		DPD(3, "kfree: [%p]\n", handle);
		return (void *) NULL;
	}

	/* in linux, we can directly access physical address, don't need to do
	 * phys_to_virt.
	 * In linux kernel 2.0.36, virt_to_bus does nothing, get_free_pages
	 * returns physical address. But in kernel 2.2.1 upwards,
	 * get_free_pages returns virtual address, we need to convert it
	 * to physical address. Then this physical address can be used to
	 * program hardware registers. */
	handle->busaddx = virt_to_bus(handle->virtaddx);
	handle->order = order;

	DPD(3, "__get_free_pages: [%p] %lx\n", handle->virtaddx, handle->busaddx);

	return handle;
}

void emu10k1_free_memphysical(struct memhandle *handle)
{
	free_pages((unsigned long) handle->virtaddx, handle->order);
	kfree(handle);

	DPD(3, "free_pages: [%p]\n", handle->virtaddx);
	DPD(3, "kfree: [%p]\n", handle);

	return;
}
