/*
 *      Copyright (C) 1997 Claus-Justus Heine

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *
 * $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/ftape-buffer.c,v $
 * $Revision: 1.3 $
 * $Date: 1997/10/16 23:33:11 $
 *
 *  This file contains the allocator/dealloctor for ftape's dynamic dma
 *  buffer.
 */

#include <asm/segment.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/wrapper.h>
#include <asm/dma.h>

#include <linux/ftape.h>
#include "../lowlevel/ftape-rw.h"
#include "../lowlevel/ftape-read.h"
#include "../lowlevel/ftape-tracing.h"

/*  DMA'able memory allocation stuff.
 */

/* Pure 2^n version of get_order */
static inline int __get_order(size_t size)
{
	unsigned long order;

	size = (size-1) >> (PAGE_SHIFT-1);
	order = -1;
	do {
		size >>= 1;
		order++;
	} while (size);
	return order;
}

static inline void *dmaalloc(size_t size)
{
	unsigned long addr;

	if (size == 0) {
		return NULL;
	}
	addr = __get_dma_pages(GFP_KERNEL, __get_order(size));
	if (addr) {
		int i;

		for (i = MAP_NR(addr); i < MAP_NR(addr+size); i++) {
			mem_map_reserve(i);
		}
	}
	return (void *)addr;
}

static inline void dmafree(void *addr, size_t size)
{
	if (size > 0) {
		int i;

		for (i = MAP_NR((unsigned long)addr);
		     i < MAP_NR((unsigned long)addr+size); i++) {
			mem_map_unreserve (i);
		}
		free_pages((unsigned long) addr, __get_order(size));
	}
}

static int add_one_buffer(void)
{
	TRACE_FUN(ft_t_flow);
	
	if (ft_nr_buffers >= FT_MAX_NR_BUFFERS) {
		TRACE_EXIT -ENOMEM;
	}
	ft_buffer[ft_nr_buffers] = kmalloc(sizeof(buffer_struct), GFP_KERNEL);
	if (ft_buffer[ft_nr_buffers] == NULL) {
		TRACE_EXIT -ENOMEM;
	}
	memset(ft_buffer[ft_nr_buffers], 0, sizeof(buffer_struct));
	ft_buffer[ft_nr_buffers]->address = dmaalloc(FT_BUFF_SIZE);
	if (ft_buffer[ft_nr_buffers]->address == NULL) {
		kfree(ft_buffer[ft_nr_buffers]);
		ft_buffer[ft_nr_buffers] = NULL;
		TRACE_EXIT -ENOMEM;
	}
	ft_nr_buffers ++;
	TRACE(ft_t_info, "buffer nr #%d @ %p, dma area @ %p",
	      ft_nr_buffers,
	      ft_buffer[ft_nr_buffers-1],
	      ft_buffer[ft_nr_buffers-1]->address);
	TRACE_EXIT 0;
}

static void del_one_buffer(void)
{
	TRACE_FUN(ft_t_flow);
	if (ft_nr_buffers > 0) {
		TRACE(ft_t_info, "releasing buffer nr #%d @ %p, dma area @ %p",
		      ft_nr_buffers,
		      ft_buffer[ft_nr_buffers-1],
		      ft_buffer[ft_nr_buffers-1]->address);
		ft_nr_buffers --;
		dmafree(ft_buffer[ft_nr_buffers]->address, FT_BUFF_SIZE);
		kfree(ft_buffer[ft_nr_buffers]);
		ft_buffer[ft_nr_buffers] = NULL;
	}
	TRACE_EXIT;
}

int ftape_set_nr_buffers(int cnt)
{
	int delta = cnt - ft_nr_buffers;
	TRACE_FUN(ft_t_flow);

	if (delta > 0) {
		while (delta--) {
			if (add_one_buffer() < 0) {
				TRACE_EXIT -ENOMEM;
			}
		}
	} else if (delta < 0) {
		while (delta++) {
			del_one_buffer();
		}
	}
	ftape_zap_read_buffers();
	TRACE_EXIT 0;
}
