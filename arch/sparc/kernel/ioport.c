/* $Id: ioport.c,v 1.29 2000/01/22 07:35:25 zaitcev Exp $
 * ioport.c:  Simple io mapping allocator.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1995 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *
 * 1996: sparc_free_io, 1999: ioremap()/iounmap() by Pete Zaitcev.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/malloc.h>

#include <asm/io.h>
#include <asm/vaddrs.h>
#include <asm/oplib.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>

struct resource *sparc_find_resource_bystart(struct resource *, unsigned long);
struct resource *sparc_find_resource_by_hit(struct resource *, unsigned long);

static void *_sparc_ioremap(struct resource *res, u32 bus, u32 pa, int sz);
static void *_sparc_alloc_io(unsigned int busno, unsigned long phys,
    unsigned long size, char *name);
static void _sparc_free_io(struct resource *res);

/* This points to the next to use virtual memory for DVMA mappings */
static struct resource sparc_dvma = {
	"sparc_dvma", DVMA_VADDR, DVMA_VADDR + DVMA_LEN - 1
};
/* This points to the start of I/O mappings, cluable from outside. */
       struct resource sparc_iomap = {
	"sparc_iomap", IOBASE_VADDR, IOBASE_END-1
};

/*
 * Our mini-allocator...
 * Boy this is gross! We need it because we must map I/O for
 * timers and interrupt controller before the kmalloc is available.
 */

#define XNMLN  15
#define XNRES  10	/* SS-10 uses 8 */

struct xresource {
	struct resource xres;	/* Must be first */
	int xflag;		/* 1 == used */
	char xname[XNMLN+1];
};

static struct xresource xresv[XNRES];

static struct xresource *xres_alloc(void) {
	struct xresource *xrp;
	int n;

	xrp = xresv;
	for (n = 0; n < XNRES; n++) {
		if (xrp->xflag == 0) {
			xrp->xflag = 1;
			return xrp;
		}
		xrp++;
	}
	return NULL;
}

static void xres_free(struct xresource *xrp) {
	xrp->xflag = 0;
}

/*
 */
extern void sun4c_mapioaddr(unsigned long, unsigned long, int bus_type, int rdonly);
extern void srmmu_mapioaddr(unsigned long, unsigned long, int bus_type, int rdonly);

static void mapioaddr(unsigned long physaddr, unsigned long virt_addr,
				 int bus, int rdonly)
{
	switch(sparc_cpu_model) {
	case sun4c:
	case sun4:
		sun4c_mapioaddr(physaddr, virt_addr, bus, rdonly);
		break;
	case sun4m:
	case sun4d:
	case sun4e:
		srmmu_mapioaddr(physaddr, virt_addr, bus, rdonly);
		break;
	default:
		printk("mapioaddr: Trying to map IO space for unsupported machine.\n");
		printk("mapioaddr: sparc_cpu_model = %d\n", sparc_cpu_model);
		printk("mapioaddr: Halting...\n");
		halt();
	};
	return;
}

extern void srmmu_unmapioaddr(unsigned long virt);
extern void sun4c_unmapioaddr(unsigned long virt);

static void unmapioaddr(unsigned long virt_addr)
{
	switch(sparc_cpu_model) {
	case sun4c:
	case sun4:
		sun4c_unmapioaddr(virt_addr);
		break;
	case sun4m:
	case sun4d:
	case sun4e:
		srmmu_unmapioaddr(virt_addr);
		break;
	default:
		printk("unmapioaddr: sparc_cpu_model = %d, halt...\n", sparc_cpu_model);
		halt();
	};
	return;
}

/*
 * These are typically used in PCI drivers
 * which are trying to be cross-platform.
 *
 * Bus type is always zero on IIep.
 */
void *ioremap(unsigned long offset, unsigned long size)
{
	char name[14];

	sprintf(name, "phys_%08x", (u32)offset);
	return _sparc_alloc_io(0, offset, size, name);
}

/*
 * Comlimentary to ioremap().
 */
void iounmap(void *virtual)
{
	unsigned long vaddr = (unsigned long) virtual & PAGE_MASK;
	struct resource *res;

	if ((res = sparc_find_resource_bystart(&sparc_iomap, vaddr)) == NULL) {
		printk("free_io/iounmap: cannot free %lx\n", vaddr);
		return;
	}
	_sparc_free_io(res);

	if ((char *)res >= (char*)xresv && (char *)res < (char *)&xresv[XNRES]) {
		xres_free((struct xresource *)res);
	} else {
		kfree(res);
	}
}

/*
 * Davem's version of sbus_ioremap.
 */
unsigned long sbus_ioremap(struct resource *phyres, unsigned long offset,
    unsigned long size, char *name)
{
	return (unsigned long) _sparc_alloc_io(phyres->flags & 0xF,
	    phyres->start + offset, size, name);
}

/*
 */
void sbus_iounmap(unsigned long addr, unsigned long size)
{
	iounmap((void *)addr);
}

/*
 * Meat of mapping
 */
static void *_sparc_alloc_io(unsigned int busno, unsigned long phys,
    unsigned long size, char *name)
{
	static int printed_full = 0;
	struct xresource *xres;
	struct resource *res;
	char *tack;
	int tlen;
	void *va;	/* P3 diag */

	if (name == NULL) name = "???";

	if ((xres = xres_alloc()) != 0) {
		tack = xres->xname;
		res = &xres->xres;
	} else {
		if (!printed_full) {
			printk("ioremap: done with statics, switching to malloc\n");
			printed_full = 1;
		}
		tlen = strlen(name);
		tack = kmalloc(sizeof (struct resource) + tlen + 1, GFP_KERNEL);
		if (tack == NULL) return NULL;
		memset(tack, 0, sizeof(struct resource));
		res = (struct resource *) tack;
		tack += sizeof (struct resource);
	}

	strncpy(tack, name, XNMLN);
	tack[XNMLN] = 0;
	res->name = tack;

	va = _sparc_ioremap(res, busno, phys, size);
	/* printk("ioremap(0x%x:%08lx[0x%lx])=%p\n", busno, phys, size, va); */ /* P3 diag */
	return va;
}

/*
 * This is called from _sparc_alloc_io only, we left it separate
 * in case Davem changes his mind about interface to sbus_ioremap().
 */
static void *
_sparc_ioremap(struct resource *res, u32 bus, u32 pa, int sz)
{
	unsigned long offset = ((unsigned long) pa) & (~PAGE_MASK);
	unsigned long va;
	unsigned int psz;

	if (allocate_resource(&sparc_iomap, res,
	    (offset + sz + PAGE_SIZE-1) & PAGE_MASK,
	    sparc_iomap.start, sparc_iomap.end, PAGE_SIZE, NULL, NULL) != 0) {
		/* Usually we cannot see printks in this case. */
		prom_printf("alloc_io_res(%s): cannot occupy\n",
		    (res->name != NULL)? res->name: "???");
		prom_halt();
	}

	va = res->start;
	pa &= PAGE_MASK;
	for (psz = res->end - res->start + 1; psz != 0; psz -= PAGE_SIZE) {
		mapioaddr(pa, va, bus, 0);
		va += PAGE_SIZE;
		pa += PAGE_SIZE;
	}

	/*
	 * XXX Playing with implementation details here.
	 * On sparc64 Ebus has resources with precise boundaries.
	 * We share drivers with sparc64. Too clever drivers use
	 * start of a resource instead of a base adress.
	 *
	 * XXX-2 This may be not valid anymore, clean when
	 * interface to sbus_ioremap() is resolved.
	 */
	res->start += offset;
	res->end = res->start + sz - 1;		/* not strictly necessary.. */

	return (void *) res->start;
}

/*
 * Comlimentary to _sparc_ioremap().
 */
static void _sparc_free_io(struct resource *res)
{
	unsigned long plen;

	plen = res->end - res->start + 1;
	while (plen != 0) {
		plen -= PAGE_SIZE;
		unmapioaddr(res->start + plen);
	}

	release_resource(res);
}

#ifdef CONFIG_SBUS

void sbus_set_sbus64(struct sbus_dev *sdev, int x) {
	printk("sbus_set_sbus64: unsupported\n");
}

/*
 * Allocate a chunk of memory suitable for DMA.
 * Typically devices use them for control blocks.
 * CPU may access them without any explicit flushing.
 *
 * XXX Some clever people know that sdev is not used and supply NULL. Watch.
 */
void *sbus_alloc_consistant(struct sbus_dev *sdev, long len, u32 *dma_addrp)
{
	unsigned long len_total = (len + PAGE_SIZE-1) & PAGE_MASK;
	unsigned long va;
	struct resource *res;
	int order;

	/* XXX why are some lenghts signed, others unsigned? */
	if (len <= 0) {
		return NULL;
	}
	/* XXX So what is maxphys for us and how do drivers know it? */
	if (len > 256*1024) {			/* __get_free_pages() limit */
		return NULL;
	}

	for (order = 0; order < 6; order++)	/* 2^6 pages == 256K */
		if ((1 << (order + PAGE_SHIFT)) >= len_total)
			break;
	va = __get_free_pages(GFP_KERNEL, order);
	if (va == 0) {
		/*
		 * printk here may be flooding... Consider removal XXX.
		 */
		printk("sbus_alloc_consistant: no %ld pages\n", len_total>>PAGE_SHIFT);
		return NULL;
	}

	if ((res = kmalloc(sizeof(struct resource), GFP_KERNEL)) == NULL) {
		free_pages(va, order);
		printk("sbus_alloc_consistant: no core\n");
		return NULL;
	}
	memset((char*)res, 0, sizeof(struct resource));

	if (allocate_resource(&sparc_dvma, res, len_total,
	    sparc_dvma.start, sparc_dvma.end, PAGE_SIZE, NULL, NULL) != 0) {
		printk("sbus_alloc_consistant: cannot occupy 0x%lx", len_total);
		free_pages(va, order);
		kfree(res);
		return NULL;
	}

	*dma_addrp = res->start;
	mmu_map_dma_area(va, res->start, len_total);

	/*
	 * "Official" or "natural" address of pages we got is va.
	 * We want to return uncached range. We could make va[len]
	 * uncached but it's difficult to make cached back [P3: hmm]
	 * We use the artefact of sun4c, replicated everywhere else,
	 * that CPU can use bus addresses to access the same memory.
	 */
	res->name = (void *)va;	/* XXX Ouch.. we got to hide it somewhere */
	return (void *)res->start;
}

void sbus_free_consistant(struct sbus_dev *sdev, long n, void *p, u32 ba)
{
	struct resource *res;
	unsigned long pgp;
	int order;

	if ((res = sparc_find_resource_bystart(&sparc_dvma,
	    (unsigned long)p)) == NULL) {
		printk("sbus_free_consistant: cannot free %p\n", p);
		return;
	}

	if (((unsigned long)p & (PAGE_MASK-1)) != 0) {
		printk("sbus_free_consistant: unaligned va %p\n", p);
		return;
	}

	n = (n + PAGE_SIZE-1) & PAGE_MASK;
	if ((res->end-res->start)+1 != n) {
		printk("sbus_free_consistant: region 0x%lx asked 0x%lx\n",
		    (long)((res->end-res->start)+1), n);
		return;
	}

	mmu_inval_dma_area((unsigned long)res->name, n);	/* XXX Ouch */
	mmu_unmap_dma_area(ba, n);
	release_resource(res);

	pgp = (unsigned long) res->name;	/* XXX Ouch */
	for (order = 0; order < 6; order++)
		if ((1 << (order + PAGE_SHIFT)) >= n)
			break;
	free_pages(pgp, order);

	kfree(res);
}

/*
 * Map a chunk of memory so that devices can see it.
 * CPU view of this memory may be inconsistent with
 * a device view and explicit flushing is necessary.
 */
u32 sbus_map_single(struct sbus_dev *sdev, void *va, long len)
{
#if 0 /* This is the version that abuses consistant space */
	unsigned long len_total = (len + PAGE_SIZE-1) & PAGE_MASK;
	struct resource *res;

	/* XXX why are some lenghts signed, others unsigned? */
	if (len <= 0) {
		return 0;
	}
	/* XXX So what is maxphys for us and how do drivers know it? */
	if (len > 256*1024) {			/* __get_free_pages() limit */
		return 0;
	}

	if ((res = kmalloc(sizeof(struct resource), GFP_KERNEL)) == NULL) {
		printk("sbus_map_single: no core\n");
		return 0;
	}
	memset((char*)res, 0, sizeof(struct resource));
	res->name = va;

	if (allocate_resource(&sparc_dvma, res, len_total,
	    sparc_dvma.start, sparc_dvma.end, PAGE_SIZE) != 0) {
		printk("sbus_map_single: cannot occupy 0x%lx", len);
		kfree(res);
		return 0;
	}

	mmu_map_dma_area(va, res->start, len_total);
	mmu_flush_dma_area((unsigned long)va, len_total); /* in all contexts? */

	return res->start;
#endif
#if 1 /* "trampoline" version */
	/* XXX why are some lenghts signed, others unsigned? */
	if (len <= 0) {
		return 0;
	}
	/* XXX So what is maxphys for us and how do drivers know it? */
	if (len > 256*1024) {			/* __get_free_pages() limit */
		return 0;
	}
/* BTFIXUPDEF_CALL(__u32, mmu_get_scsi_one, char *, unsigned long, struct sbus_bus *sbus) */
	return mmu_get_scsi_one(va, len, sdev->bus);
#endif
}

void sbus_unmap_single(struct sbus_dev *sdev, u32 ba, long n)
{
#if 0 /* This is the version that abuses consistant space */
	struct resource *res;
	unsigned long va;

	if ((res = sparc_find_resource_bystart(&sparc_dvma, ba)) == NULL) {
		printk("sbus_unmap_single: cannot find %08x\n", (unsigned)ba);
		return;
	}

	n = (n + PAGE_SIZE-1) & PAGE_MASK;
	if ((res->end-res->start)+1 != n) {
		printk("sbus_unmap_single: region 0x%lx asked 0x%lx\n",
		    (long)((res->end-res->start)+1), n);
		return;
	}

	va = (unsigned long) res->name;	/* XXX Ouch */
	mmu_inval_dma_area(va, n);	/* in all contexts, mm's?... */
	mmu_unmap_dma_area(ba, n);	/* iounit cache flush is here */
	release_resource(res);
	kfree(res);
#endif
#if 1 /* "trampoline" version */
/* BTFIXUPDEF_CALL(void,  mmu_release_scsi_one, __u32, unsigned long, struct sbus_bus *sbus) */
	mmu_release_scsi_one(ba, n, sdev->bus);
#endif
}

int sbus_map_sg(struct sbus_dev *sdev, struct scatterlist *sg, int n)
{
/* BTFIXUPDEF_CALL(void,  mmu_get_scsi_sgl, struct scatterlist *, int, struct sbus_bus *sbus) */
	mmu_get_scsi_sgl(sg, n, sdev->bus);

	/*
	 * XXX sparc64 can return a partial length here. sun4c should do this
	 * but it currently panics if it can't fulfill the request - Anton
	 */
	return n;
}

void sbus_unmap_sg(struct sbus_dev *sdev, struct scatterlist *sg, int n)
{
/* BTFIXUPDEF_CALL(void,  mmu_release_scsi_sgl, struct scatterlist *, int, struct sbus_bus *sbus) */
	mmu_release_scsi_sgl(sg, n, sdev->bus);
}
#endif

/*
 * P3: I think a partial flush is permitted...
 * We are not too efficient at doing it though.
 *
 * If only DaveM understood a concept of an allocation cookie,
 * we could avoid find_resource_by_hit() here and a major
 * performance hit.
 */
void sbus_dma_sync_single(struct sbus_dev *sdev, u32 ba, long size)
{
	unsigned long va;
	struct resource *res;

	res = sparc_find_resource_by_hit(&sparc_dvma, ba);
	if (res == NULL)
		panic("sbus_dma_sync_single: 0x%x\n", ba);

	va = (unsigned long) res->name;
	/* if (va == 0) */

	mmu_inval_dma_area(va, (res->end - res->start) + 1);
}

void sbus_dma_sync_sg(struct sbus_dev *sdev, struct scatterlist *sg, int n)
{
	printk("dma_sync_sg: not implemented yet\n");
}

/*
 * This is a version of find_resource and it belongs to kernel/resource.c.
 * Until we have agreement with Linus and Martin, it lingers here.
 *
 * "same start" is more strict than "hit into"
 */
struct resource *
sparc_find_resource_bystart(struct resource *root, unsigned long start)
{
        struct resource *tmp;

        for (tmp = root->child; tmp != 0; tmp = tmp->sibling) {
                if (tmp->start == start)
			return tmp;
        }
        return NULL;
}

struct resource *
sparc_find_resource_by_hit(struct resource *root, unsigned long hit)
{
        struct resource *tmp;

        for (tmp = root->child; tmp != 0; tmp = tmp->sibling) {
                if (tmp->start <= hit && tmp->end >= hit)
			return tmp;
        }
        return NULL;
}
