/*
 * include/asm-mips/vector.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995 by Ralf Baechle
 */
#ifndef __ASM_MIPS_VECTOR_H
#define __ASM_MIPS_VECTOR_H

/*
 * This structure defines how to access various features of
 * different machine types and how to access them.
 *
 * FIXME: More things need to be accessed via this vector.
 */
struct feature {
	void (*handle_int)(void);
	/*
	 * How to access the floppy controller's ports.
	 */
	unsigned char (*fd_inb)(unsigned int port);
	void (*fd_outb)(unsigned char value, unsigned int port);
	/*
	 * How to access the floppy DMA functions.
	 */
	void (*fd_enable_dma)(void);
	void (*fd_disable_dma)(void);
	int (*fd_request_dma)(void);
	void (*fd_free_dma)(void);
	void (*fd_clear_dma_ff)(void);
	void (*fd_set_dma_mode)(char mode);
	void (*fd_set_dma_addr)(unsigned int a);
	void (*fd_set_dma_count)(unsigned int count);
	int (*fd_get_dma_residue)(void);
	void (*fd_enable_irq)(void);
	void (*fd_disable_irq)(void);
	void (*fd_cacheflush)(unsigned char *addr, unsigned int size);
	/*
	 * How to access the RTC register of DS1287
	 */
	unsigned char (*rtc_read_data)(void);
	void (*rtc_write_data)(unsigned char);
};

/*
 * Similar to the above this is a structure that describes various
 * CPU dependent features.
 *
 * FIXME: This vector isn't being used yet
 */
struct cpu {
	int dummy;	/* keep GCC from complaining */
};

extern struct feature *feature;
extern struct cpu *cpu;

#endif /* __ASM_MIPS_VECTOR_H */
