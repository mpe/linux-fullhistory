/*
 * Architecture specific parts of the Floppy driver
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995
 */
#ifndef __ASM_I386_FLOPPY_H
#define __ASM_I386_FLOPPY_H


#define SW fd_routine[use_virtual_dma&1]


#define fd_inb(port)			inb_p(port)
#define fd_outb(port,value)		outb_p(port,value)

#define fd_enable_dma()         SW._enable_dma(FLOPPY_DMA)
#define fd_disable_dma()        SW._disable_dma(FLOPPY_DMA)
#define fd_request_dma()        SW._request_dma(FLOPPY_DMA,"floppy")
#define fd_free_dma()           SW._free_dma(FLOPPY_DMA)
#define fd_clear_dma_ff()       SW._clear_dma_ff(FLOPPY_DMA)
#define fd_set_dma_mode(mode)   SW._set_dma_mode(FLOPPY_DMA,mode)
#define fd_set_dma_addr(addr)   SW._set_dma_addr(FLOPPY_DMA,addr)
#define fd_set_dma_count(count) SW._set_dma_count(FLOPPY_DMA,count)
#define fd_enable_irq()         enable_irq(FLOPPY_IRQ)
#define fd_disable_irq()        disable_irq(FLOPPY_IRQ)
#define fd_cacheflush(addr,size) /* nothing */
#define fd_request_irq()        SW._request_irq(FLOPPY_IRQ, floppy_interrupt, \
					       SA_INTERRUPT|SA_SAMPLE_RANDOM, \
					       "floppy", NULL)
#define fd_free_irq()		free_irq(FLOPPY_IRQ, NULL)
#define fd_get_dma_residue()    SW._get_dma_residue(FLOPPY_DMA)
#define fd_dma_mem_alloc(size)	SW._dma_mem_alloc(size)
#define fd_dma_mem_free(addr,size)	SW._dma_mem_free(addr,size)

static int virtual_dma_count=0;
static int virtual_dma_residue=0;
static unsigned long virtual_dma_addr=0;
static int virtual_dma_mode=0;
static int doing_pdma=0;

static void floppy_hardint(int irq, void *dev_id, struct pt_regs * regs)
{
	register unsigned char st;

#undef TRACE_FLPY_INT
#undef NO_FLOPPY_ASSEMBLER

#ifdef TRACE_FLPY_INT
	static int calls=0;
	static int bytes=0;
	static int dma_wait=0;
#endif
	if(!doing_pdma) {
		floppy_interrupt(irq, dev_id, regs);
		return;
	}

#ifdef TRACE_FLPY_INT
	if(!calls)
		bytes = virtual_dma_count;
#endif

#ifndef NO_FLOPPY_ASSEMBLER
	__asm__ (
       "testl %1,%1
	je 3f
1:	inb %w4,%b0
	andb $160,%b0
	cmpb $160,%b0
	jne 2f
	incw %w4
	testl %3,%3
	jne 4f
	inb %w4,%b0
	movb %0,(%2)
	jmp 5f
4:     	movb (%2),%0
	outb %b0,%w4
5:	decw %w4
	outb %0,$0x80
	decl %1
	incl %2
	testl %1,%1
	jne 1b
3:	inb %w4,%b0
2:	"
       : "=a" ((char) st), 
       "=c" ((long) virtual_dma_count), 
       "=S" ((long) virtual_dma_addr)
       : "b" ((long) virtual_dma_mode),
       "d" ((short) virtual_dma_port+4), 
       "1" ((long) virtual_dma_count),
       "2" ((long) virtual_dma_addr));
#else	
	{
		register int lcount;
		register char *lptr;

		st = 1;
		for(lcount=virtual_dma_count, lptr=(char *)virtual_dma_addr; 
		    lcount; lcount--, lptr++) {
			st=inb(virtual_dma_port+4) & 0xa0 ;
			if(st != 0xa0) 
				break;
			if(virtual_dma_mode)
				outb_p(*lptr, virtual_dma_port+5);
			else
				*lptr = inb_p(virtual_dma_port+5);
			st = inb(virtual_dma_port+4);
		}
		virtual_dma_count = lcount;
		virtual_dma_addr = (int) lptr;
	}
#endif

#ifdef TRACE_FLPY_INT
	calls++;
#endif
	if(st == 0x20)
		return;
	if(!(st & 0x20)) {
		virtual_dma_residue += virtual_dma_count;
		virtual_dma_count=0;
#ifdef TRACE_FLPY_INT
		printk("count=%x, residue=%x calls=%d bytes=%d dma_wait=%d\n", 
		       virtual_dma_count, virtual_dma_residue, calls, bytes,
		       dma_wait);
		calls = 0;
		dma_wait=0;
#endif
		doing_pdma = 0;
		floppy_interrupt(irq, dev_id, regs);
		return;
	}
#ifdef TRACE_FLPY_INT
	if(!virtual_dma_count)
		dma_wait++;
#endif
}

static void vdma_enable_dma(unsigned int dummy)
{
	doing_pdma = 1;
}

static void vdma_disable_dma(unsigned int dummy)
{
	doing_pdma = 0;
	virtual_dma_residue += virtual_dma_count;
	virtual_dma_count=0;		
}

static int vdma_request_dma(unsigned int dmanr, const char * device_id)
{
	return 0;
}

static void vdma_nop(unsigned int dummy)
{
}

static void vdma_set_dma_mode(unsigned int dummy,char mode)
{
	virtual_dma_mode = (mode  == DMA_MODE_WRITE);
}

static void vdma_set_dma_addr(unsigned int dummy,unsigned int addr)
{
	virtual_dma_addr = addr;
}

static void vdma_set_dma_count(unsigned int dummy,unsigned int count)
{
	virtual_dma_count = count;
	virtual_dma_residue = 0;
}

static int vdma_get_dma_residue(unsigned int dummy)
{
	return virtual_dma_count + virtual_dma_residue;
}


static int vdma_request_irq(unsigned int irq,
			    void (*handler)(int, void *, struct pt_regs *),
			    unsigned long flags, 
			    const char *device,
			    void *dev_id)
{
	return request_irq(irq, floppy_hardint,SA_INTERRUPT,device, dev_id);

}

static unsigned long dma_mem_alloc(unsigned long size)
{
	return __get_dma_pages(GFP_KERNEL,__get_order(size));
}

static void dma_mem_free(unsigned long addr, unsigned long size)
{
	free_pages(addr, __get_order(size));
}

static unsigned long vdma_mem_alloc(unsigned long size)
{
	return (unsigned long) vmalloc(size);
}

static void vdma_mem_free(unsigned long addr, unsigned long size)
{
	return vfree((void *)addr);
}

struct fd_routine_l {
	void (*_enable_dma)(unsigned int dummy);
	void (*_disable_dma)(unsigned int dummy);
	int (*_request_dma)(unsigned int dmanr, const char * device_id);
	void (*_free_dma)(unsigned int dmanr);
	void (*_clear_dma_ff)(unsigned int dummy);
	void (*_set_dma_mode)(unsigned int dummy, char mode);
	void (*_set_dma_addr)(unsigned int dummy, unsigned int addr);
	void (*_set_dma_count)(unsigned int dummy, unsigned int count);
	int (*_get_dma_residue)(unsigned int dummy);
	int (*_request_irq)(unsigned int irq,
			   void (*handler)(int, void *, struct pt_regs *),
			   unsigned long flags, 
			   const char *device,
			   void *dev_id);
	unsigned long (*_dma_mem_alloc) (unsigned long size);
	void (*_dma_mem_free)(unsigned long addr, unsigned long size);
} fd_routine[] = {
	{
		enable_dma,
		disable_dma,
		request_dma,
		free_dma,
		clear_dma_ff,
		set_dma_mode,
		set_dma_addr,
		set_dma_count,
		get_dma_residue,
		request_irq,
		dma_mem_alloc,
		dma_mem_free
	},
	{
		vdma_enable_dma,
		vdma_disable_dma,
		vdma_request_dma,
		vdma_nop,
		vdma_nop,
		vdma_set_dma_mode,
		vdma_set_dma_addr,
		vdma_set_dma_count,
		vdma_get_dma_residue,
		vdma_request_irq,
		vdma_mem_alloc,
		vdma_mem_free
	}
};

__inline__ void virtual_dma_init(void)
{
	/* Nothing to do on an i386 */
}

static int FDC1 = 0x3f0;
static int FDC2 = -1;

#define FLOPPY0_TYPE	((CMOS_READ(0x10) >> 4) & 15)
#define FLOPPY1_TYPE	(CMOS_READ(0x10) & 15)

#define N_FDC 2
#define N_DRIVE 8

/*
 * The DMA channel used by the floppy controller cannot access data at
 * addresses >= 16MB
 *
 * Went back to the 1MB limit, as some people had problems with the floppy
 * driver otherwise. It doesn't matter much for performance anyway, as most
 * floppy accesses go through the track buffer.
 */
#define CROSS_64KB(a,s) (((unsigned long)(a)/K_64 != ((unsigned long)(a) + (s) - 1) / K_64) && ! (use_virtual_dma & 1))

#endif /* __ASM_I386_FLOPPY_H */
