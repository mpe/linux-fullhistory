/*======================================================================
  
    Cardbus device configuration
    
    cardbus.c 1.63 1999/11/08 20:47:02

    The contents of this file are subject to the Mozilla Public
    License Version 1.1 (the "License"); you may not use this file
    except in compliance with the License. You may obtain a copy of
    the License at http://www.mozilla.org/MPL/

    Software distributed under the License is distributed on an "AS
    IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
    implied. See the License for the specific language governing
    rights and limitations under the License.

    The initial developer of the original code is David A. Hinds
    <dhinds@pcmcia.sourceforge.org>.  Portions created by David A. Hinds
    are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.

    Alternatively, the contents of this file may be used under the
    terms of the GNU Public License version 2 (the "GPL"), in which
    case the provisions of the GPL are applicable instead of the
    above.  If you wish to allow the use of your version of this file
    only under the terms of the GPL and not to allow others to use
    your version of this file under the MPL, indicate your decision
    by deleting the provisions above and replace them with the notice
    and other provisions required by the GPL.  If you do not delete
    the provisions above, a recipient may use your version of this
    file under either the MPL or the GPL.
    
    These routines handle allocating resources for Cardbus cards, as
    well as setting up and shutting down Cardbus sockets.  They are
    called from cs.c in response to Request/ReleaseConfiguration and
    Request/ReleaseIO calls.
    
======================================================================*/

#define __NO_VERSION__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <asm/irq.h>
#include <asm/io.h>

#ifndef PCMCIA_DEBUG
#define PCMCIA_DEBUG 1
#endif
static int pc_debug = PCMCIA_DEBUG;

#define IN_CARD_SERVICES
#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/ss.h>
#include <pcmcia/cs.h>
#include <pcmcia/bulkmem.h>
#include <pcmcia/cistpl.h>
#include "cs_internal.h"
#include "rsrc_mgr.h"

/*====================================================================*/

#define FIND_FIRST_BIT(n)	((n) - ((n) & ((n)-1)))

#define pci_readb		pcibios_read_config_byte
#define pci_writeb		pcibios_write_config_byte
#define pci_readw		pcibios_read_config_word
#define pci_writew		pcibios_write_config_word
#define pci_readl		pcibios_read_config_dword
#define pci_writel		pcibios_write_config_dword

#define CB_BAR(n)		(PCI_BASE_ADDRESS_0+(4*(n)))
#define CB_ROM_BASE		0x0030

/* Offsets in the Expansion ROM Image Header */
#define ROM_SIGNATURE		0x0000	/* 2 bytes */
#define ROM_DATA_PTR		0x0018	/* 2 bytes */

/* Offsets in the CardBus PC Card Data Structure */
#define PCDATA_SIGNATURE	0x0000	/* 4 bytes */
#define PCDATA_VPD_PTR		0x0008	/* 2 bytes */
#define PCDATA_LENGTH		0x000a	/* 2 bytes */
#define PCDATA_REVISION		0x000c
#define PCDATA_IMAGE_SZ		0x0010	/* 2 bytes */
#define PCDATA_ROM_LEVEL	0x0012	/* 2 bytes */
#define PCDATA_CODE_TYPE	0x0014
#define PCDATA_INDICATOR	0x0015

typedef struct cb_config_t {
    u_int		size[7];
    struct pci_dev	dev;
} cb_config_t;

#define BASE(dev,n)	((dev).resource[n].start)
#define ROM(dev)	((dev).resource[6].start)

/* There are three classes of bridge maps: IO ports,
   non-prefetchable memory, and prefetchable memory */
typedef enum { B_IO, B_M1, B_M2 } bridge_type;

/*=====================================================================

    Expansion ROM's have a special layout, and pointers specify an
    image number and an offset within that image.  check_rom()
    verifies that the expansion ROM exists and has the standard
    layout.  xlate_rom_addr() converts an image/offset address to an
    absolute offset from the ROM's base address.
    
=====================================================================*/

static int check_rom(u_char *b, u_long len)
{
    u_int img = 0, ofs = 0, sz;
    u_short data;
    DEBUG(0, "ROM image dump:\n");
    while ((readb(b) == 0x55) && (readb(b+1) == 0xaa)) {
	data = readb(b+ROM_DATA_PTR) +
	    (readb(b+ROM_DATA_PTR+1) << 8);
	sz = 512 * (readb(b+data+PCDATA_IMAGE_SZ) +
		    (readb(b+data+PCDATA_IMAGE_SZ+1) << 8));
	DEBUG(0, "  image %d: 0x%06x-0x%06x, signature %c%c%c%c\n",
	      img, ofs, ofs+sz-1,
	      readb(b+data+PCDATA_SIGNATURE),
	      readb(b+data+PCDATA_SIGNATURE+1),
	      readb(b+data+PCDATA_SIGNATURE+2),
	      readb(b+data+PCDATA_SIGNATURE+3));
	ofs += sz; img++;
	if ((readb(b+data+PCDATA_INDICATOR) & 0x80) ||
	    (sz == 0) || (ofs >= len)) break;
	b += sz;
    }
    return img;
}

static u_int xlate_rom_addr(u_char *b, u_int addr)
{
    u_int img = 0, ofs = 0, sz;
    u_short data;
    while ((readb(b) == 0x55) && (readb(b+1) == 0xaa)) {
	if (img == (addr >> 28))
	    return (addr & 0x0fffffff) + ofs;
	data = readb(b+ROM_DATA_PTR) + (readb(b+ROM_DATA_PTR+1) << 8);
	sz = 512 * (readb(b+data+PCDATA_IMAGE_SZ) +
		    (readb(b+data+PCDATA_IMAGE_SZ+1) << 8));
	if ((sz == 0) || (readb(b+data+PCDATA_INDICATOR) & 0x80))
	    break;
	b += sz; ofs += sz; img++;
    }
    return 0;
}

/*=====================================================================

    These are similar to setup_cis_mem and release_cis_mem for 16-bit
    cards.  The "result" that is used externally is the cb_cis_virt
    pointer in the socket_info_t structure.
    
=====================================================================*/

int cb_setup_cis_mem(socket_info_t *s, int space)
{
    cb_bridge_map *m = &s->cb_cis_map;
    u_long base = 0;
    u_int sz, br;

    if (space == s->cb_cis_space)
	return CS_SUCCESS;
    else if (s->cb_cis_space != 0)
	cb_release_cis_mem(s);
    DEBUG(1, "cs: cb_setup_cis_mem(space %d)\n", space);
    /* If socket is configured, then use existing memory mapping */
    if (s->lock_count) {
	s->cb_cis_virt =
	    ioremap(BASE(s->cb_config[0].dev, space-1),
		    s->cb_config[0].size[space-1] & ~3);
	s->cb_cis_space = space;
	return CS_SUCCESS;
    }
    
    /* Not configured?  Then set up temporary map */
    br = (space == 7) ? CB_ROM_BASE : CB_BAR(space-1);
    pci_writel(s->cap.cardbus, 0, br, 0xffffffff);
    pci_readl(s->cap.cardbus, 0, br, &sz);
    sz &= PCI_BASE_ADDRESS_MEM_MASK;
    sz = FIND_FIRST_BIT(sz);
    if (sz < PAGE_SIZE) sz = PAGE_SIZE;
    if (find_mem_region(&base, sz, sz, 0, "cb_enabler") != 0) {
	printk(KERN_NOTICE "cs: could not allocate %dK memory for"
	       " CardBus socket %d\n", sz/1024, s->sock);
	return CS_OUT_OF_RESOURCE;
    }
    s->cb_cis_space = space;
    s->cb_cis_virt = ioremap(base, sz);
    DEBUG(1, "  phys 0x%08lx-0x%08lx, virt 0x%08lx\n",
	  base, base+sz-1, (u_long)s->cb_cis_virt);
    pci_writel(s->cap.cardbus, 0, br, base | 1);
    pci_writeb(s->cap.cardbus, 0, PCI_COMMAND, PCI_COMMAND_MEMORY);
    m->map = 0; m->flags = MAP_ACTIVE;
    m->start = base; m->stop = base+sz-1;
    s->ss_entry->set_bridge(s->sock, m);
    if ((space == 7) && (check_rom(s->cb_cis_virt, sz) == 0)) {
	printk(KERN_NOTICE "cs: no valid ROM images found!\n");
	return CS_READ_FAILURE;
    }
    return CS_SUCCESS;
}

void cb_release_cis_mem(socket_info_t *s)
{
    cb_bridge_map *m = &s->cb_cis_map;
    u_int br;
    if (s->cb_cis_virt) {
	DEBUG(1, "cs: cb_release_cis_mem()\n");
	iounmap(s->cb_cis_virt);
	s->cb_cis_virt = NULL;
	s->cb_cis_space = 0;
    }
    if (m->start) {
	/* This is overkill: we probably only need to release the
	   memory region, but the rest should be safe */
	br = (s->cb_cis_space == 7) ?
	    CB_ROM_BASE : CB_BAR(s->cb_cis_space-1);
	m->map = 0; m->flags = 0;
	s->ss_entry->set_bridge(s->sock, m);
	pci_writeb(s->cap.cardbus, 0, PCI_COMMAND, 0);
	pci_writel(s->cap.cardbus, 0, br, 0);
	release_mem_region(m->start, m->stop - m->start + 1);
	m->start = 0;
    }
}

/*=====================================================================

    This is used by the CIS processing code to read CIS information
    from a CardBus device.
    
=====================================================================*/

void read_cb_mem(socket_info_t *s, u_char fn, int space,
		 u_int addr, u_int len, void *ptr)
{
    DEBUG(3, "cs: read_cb_mem(%d, %#x, %u)\n", space, addr, len);
    if (space == 0) {
	if (addr+len > 0x100) goto fail;
	for (; len; addr++, ptr++, len--)
	    pci_readb(s->cap.cardbus, fn, addr, (u_char *)ptr);
    } else {
	if (cb_setup_cis_mem(s, space) != 0) goto fail;
	if (space == 7) {
	    addr = xlate_rom_addr(s->cb_cis_virt, addr);
	    if (addr == 0) goto fail;
	}
	if (addr+len > s->cb_cis_map.stop - s->cb_cis_map.start)
	    goto fail;
	if (s->cb_cis_virt != NULL)
	    for (; len; addr++, ptr++, len--)
		*(u_char *)ptr = readb(s->cb_cis_virt+addr);
    }
    return;
 fail:
    memset(ptr, 0xff, len);
    return;
}

/*=====================================================================

    cb_alloc() and cb_free() allocate and free the kernel data
    structures for a Cardbus device, and handle the lowest level PCI
    device setup issues.
    
=====================================================================*/

int cb_alloc(socket_info_t *s)
{
    struct pci_dev tmp;
    u_short vend, v, dev;
    u_char i, hdr, fn, bus = s->cap.cardbus;
    cb_config_t *c;

    memset(&tmp, 0, sizeof(tmp));
    tmp.bus = s->cap.cb_bus; tmp.devfn = 0;


    pci_read_config_word(&tmp, PCI_VENDOR_ID, &vend);
    pci_read_config_word(&tmp, PCI_DEVICE_ID, &dev);
    printk(KERN_INFO "cs: cb_alloc(bus %d): vendor 0x%04x, "
	   "device 0x%04x\n", bus, vend, dev);

    pci_read_config_byte(&tmp, PCI_HEADER_TYPE, &hdr);
    if (hdr & 0x80) {
	/* Count functions */
	for (fn = 0; fn < 8; fn++) {
	    tmp.devfn = fn;
	    pci_read_config_word(&tmp, PCI_VENDOR_ID, &v);
	    if (v != vend) break;
	}
    } else fn = 1;
    s->functions = fn;
    
    c = kmalloc(fn * sizeof(struct cb_config_t), GFP_ATOMIC);
    if (!c) return CS_OUT_OF_RESOURCE;
    memset(c, 0, fn * sizeof(struct cb_config_t));
    s->cb_config = c;

    for (i = 0; i < fn; i++) {
	c[i].dev.bus = s->cap.cb_bus;
	c[i].dev.devfn = i;
	if (i < fn-1) {
	    c[i].dev.sibling = c[i].dev.next = &c[i+1].dev;
	}
    }
    s->cap.cb_bus->devices = &c[0].dev;
    /* Link into PCI device chain */
    c[fn-1].dev.next = pci_devices;
    pci_devices = &c[0].dev;
    for (i = 0; i < fn; i++) {
	c[i].dev.vendor = vend;
	pci_readw(bus, i, PCI_DEVICE_ID, &c[i].dev.device);
	pci_readl(bus, i, PCI_CLASS_REVISION, &c[i].dev.class);
	c[i].dev.class >>= 8;
	c[i].dev.hdr_type = hdr;
#ifdef CONFIG_PROC_FS
	pci_proc_attach_device(&c[i].dev);
#endif
    }
    
    return CS_SUCCESS;
}

void cb_free(socket_info_t *s)
{
    cb_config_t *c = s->cb_config;

    if (c) {
	struct pci_dev **p;
	/* Unlink from PCI device chain */
	p = &pci_devices;
	while (*p) {
	    struct pci_dev * dev = *p;
	    if (dev->bus != s->cap.cb_bus) {
	    	p = &dev->next;
		continue;
	    }
	    *p = dev->next;
#ifdef CONFIG_PROC_FS
	    pci_proc_detach_device(dev);
#endif
	}
	s->cap.cb_bus->devices = NULL;
	kfree(s->cb_config);
	s->cb_config = NULL;
	printk(KERN_INFO "cs: cb_free(bus %d)\n", s->cap.cardbus);
    }
}

/*=====================================================================

    cb_config() has the job of allocating all system resources that
    a Cardbus card requires.  Rather than using the CIS (which seems
    to not always be present), it treats the card as an ordinary PCI
    device, and probes the base address registers to determine each
    function's IO and memory space needs.

    It is called from the RequestIO card service.
    
======================================================================*/

int cb_config(socket_info_t *s)
{
    cb_config_t *c = s->cb_config;
    u_char fn = s->functions;
    u_char i, j, bus = s->cap.cardbus, *name;
    u_int sz, align, m, mask[3], num[3], base[3];
    int irq, try, ret;

    printk(KERN_INFO "cs: cb_config(bus %d)\n", s->cap.cardbus);

    /* Determine IO and memory space needs */
    num[B_IO] = num[B_M1] = num[B_M2] = 0;
    mask[B_IO] = mask[B_M1] = mask[B_M2] = 0;
    for (i = 0; i < fn; i++) {
	for (j = 0; j < 6; j++) {
	    pci_writel(bus, i, CB_BAR(j), 0xffffffff);
	    pci_readl(bus, i, CB_BAR(j), &sz);
	    if (sz == 0) continue;
	    if (sz & PCI_BASE_ADDRESS_SPACE) {
		m = B_IO;
		sz &= PCI_BASE_ADDRESS_IO_MASK;
	    } else {
		m = (sz & PCI_BASE_ADDRESS_MEM_PREFETCH) ? B_M2 : B_M1;
		sz &= PCI_BASE_ADDRESS_MEM_MASK;
	    }
	    sz = FIND_FIRST_BIT(sz);
	    c[i].size[j] = sz | m;
	    if (m && (sz < PAGE_SIZE)) sz = PAGE_SIZE;
	    num[m] += sz; mask[m] |= sz;
	}
	pci_writel(bus, i, CB_ROM_BASE, 0xffffffff);
	pci_readl(bus, i, CB_ROM_BASE, &sz);
	if (sz != 0) {
	    sz = FIND_FIRST_BIT(sz & ~0x00000001);
	    c[i].size[6] = sz | B_M1;
	    if (sz < PAGE_SIZE) sz = PAGE_SIZE;
	    num[B_M1] += sz; mask[B_M1] |= sz;
	}
    }

    /* Allocate system resources */
    name = "cb_enabler";
    s->io[0].NumPorts = num[B_IO];
    s->io[0].BasePort = 0;
    if (num[B_IO]) {
	if (find_io_region(&s->io[0].BasePort, num[B_IO],
			   num[B_IO], name) != 0) {
	    printk(KERN_NOTICE "cs: could not allocate %d IO ports for"
		   " CardBus socket %d\n", num[B_IO], s->sock);
	    goto failed;
	}
	base[B_IO] = s->io[0].BasePort + num[B_IO];
    }
    s->win[0].size = num[B_M1];
    s->win[0].base = 0;
    if (num[B_M1]) {
	if (find_mem_region(&s->win[0].base, num[B_M1], num[B_M1],
			    0, name) != 0) {
	    printk(KERN_NOTICE "cs: could not allocate %dK memory for"
		   " CardBus socket %d\n", num[B_M1]/1024, s->sock);
	    goto failed;
	}
	base[B_M1] = s->win[0].base + num[B_M1];
    }
    s->win[1].size = num[B_M2];
    s->win[1].base = 0;
    if (num[B_M2]) {
	if (find_mem_region(&s->win[1].base, num[B_M2], num[B_M2],
			    0, name) != 0) {
	    printk(KERN_NOTICE "cs: could not allocate %dK memory for"
		   " CardBus socket %d\n", num[B_M2]/1024, s->sock);
	    goto failed;
	}
	base[B_M2] = s->win[1].base + num[B_M2];
    }
    
    /* Set up base address registers */
    while (mask[B_IO] | mask[B_M1] | mask[B_M2]) {
	num[B_IO] = FIND_FIRST_BIT(mask[B_IO]); mask[B_IO] -= num[B_IO];
	num[B_M1] = FIND_FIRST_BIT(mask[B_M1]); mask[B_M1] -= num[B_M1];
	num[B_M2] = FIND_FIRST_BIT(mask[B_M2]); mask[B_M2] -= num[B_M2];
	for (i = 0; i < fn; i++) {
	    for (j = 0; j < 7; j++) {
		sz = c[i].size[j];
		m = sz & 3; sz &= ~3;
		align = (m && (sz < PAGE_SIZE)) ? PAGE_SIZE : sz;
		if (sz && (align == num[m])) {
		    base[m] -= align;
		    if (j < 6)
			printk(KERN_INFO "  fn %d bar %d: ", i, j+1);
		    else
			printk(KERN_INFO "  fn %d rom: ", i);
		    printk("%s 0x%x-0x%x\n", (m) ? "mem" : "io",
			   base[m], base[m]+sz-1);
		    BASE(c[i].dev, j) = base[m];
		}
	    }
	}
    }
    
    /* Allocate interrupt if needed */
    s->irq.AssignedIRQ = irq = 0; ret = -1;
    for (i = 0; i < fn; i++) {
	pci_readb(bus, i, PCI_INTERRUPT_PIN, &j);
	if (j == 0) continue;
	if (irq == 0) {
	    if (s->cap.irq_mask & (1 << s->cap.pci_irq)) {
		irq = s->cap.pci_irq;
		ret = 0;
	    }
#ifdef CONFIG_ISA
	    else
		for (try = 0; try < 2; try++) {
		    for (irq = 0; irq < 32; irq++)
			if ((s->cap.irq_mask >> irq) & 1) {
			    ret = try_irq(IRQ_TYPE_EXCLUSIVE, irq, try);
			    if (ret == 0) break;
			}
		    if (ret == 0) break;
		}
	    if (ret != 0) {
		printk(KERN_NOTICE "cs: could not allocate interrupt"
		       " for CardBus socket %d\n", s->sock);
		goto failed;
	    }
#endif
	    s->irq.AssignedIRQ = irq;
	}
    }
    for (i = 0; i < fn; i++)
	c[i].dev.irq = irq;
    
    return CS_SUCCESS;

failed:
    cb_release(s);
    return CS_OUT_OF_RESOURCE;
}

/*======================================================================

    cb_release() releases all the system resources (IO and memory
    space, and interrupt) committed for a Cardbus card by a prior call
    to cb_config().

    It is called from the ReleaseIO() service.
    
======================================================================*/

void cb_release(socket_info_t *s)
{
    cb_config_t *c = s->cb_config;
    
    DEBUG(0, "cs: cb_release(bus %d)\n", s->cap.cardbus);
    
    if (s->win[0].size > 0)
	release_mem_region(s->win[0].base, s->win[0].size);
    if (s->win[1].size > 0)
	release_mem_region(s->win[1].base, s->win[1].size);
    if (s->io[0].NumPorts > 0)
	release_region(s->io[0].BasePort, s->io[0].NumPorts);
    s->io[0].NumPorts = 0;
#ifdef CONFIG_ISA
    if ((c[0].dev.irq != 0) && (c[0].dev.irq != s->cap.pci_irq))
	undo_irq(IRQ_TYPE_EXCLUSIVE, c[0].dev.irq);
#endif
}

/*=====================================================================

    cb_enable() has the job of configuring a socket for a Cardbus
    card, and initializing the card's PCI configuration registers.

    It first sets up the Cardbus bridge windows, for IO and memory
    accesses.  Then, it initializes each card function's base address
    registers, interrupt line register, and command register.

    It is called as part of the RequestConfiguration card service.
    It should be called after a previous call to cb_config() (via the
    RequestIO service).
    
======================================================================*/

void cb_enable(socket_info_t *s)
{
    u_char i, j, bus = s->cap.cardbus;
    cb_config_t *c = s->cb_config;
    
    DEBUG(0, "cs: cb_enable(bus %d)\n", bus);
    
    /* Configure bridge */
    if (s->cb_cis_map.start)
	cb_release_cis_mem(s);
    for (i = 0; i < 3; i++) {
	cb_bridge_map m;
	switch (i) {
	case B_IO:
	    m.map = 0; m.flags = MAP_IOSPACE | MAP_ACTIVE;
	    m.start = s->io[0].BasePort;
	    m.stop = m.start + s->io[0].NumPorts - 1;
	    break;
	case B_M1:
	    m.map = 0; m.flags = MAP_ACTIVE;
	    m.start = s->win[0].base;
	    m.stop = m.start + s->win[0].size - 1;
	    break;
	case B_M2:
	    m.map = 1; m.flags = MAP_PREFETCH | MAP_ACTIVE;
	    m.start = s->win[1].base;
	    m.stop = m.start + s->win[1].size - 1;
	    break;
	}
	if (m.start == 0) continue;
	DEBUG(0, "  bridge %s map %d (flags 0x%x): 0x%x-0x%x\n",
	      (m.flags & MAP_IOSPACE) ? "io" : "mem",
	      m.map, m.flags, m.start, m.stop);
	s->ss_entry->set_bridge(s->sock, &m);
    }

    /* Set up base address registers */
    for (i = 0; i < s->functions; i++) {
	for (j = 0; j < 6; j++) {
	    if (BASE(c[i].dev, j) != 0)
		pci_writel(bus, i, CB_BAR(j), BASE(c[i].dev, j));
	}
	if (ROM(c[i].dev) != 0)
	    pci_writel(bus, i, CB_ROM_BASE, ROM(c[i].dev) | 1);
    }

    /* Set up PCI interrupt and command registers */
    for (i = 0; i < s->functions; i++) {
	pci_writeb(bus, i, PCI_COMMAND, PCI_COMMAND_MASTER |
		   PCI_COMMAND_IO | PCI_COMMAND_MEMORY);
	pci_writeb(bus, i, PCI_CACHE_LINE_SIZE, 8);
    }
    
    if (s->irq.AssignedIRQ) {
	for (i = 0; i < s->functions; i++)
	    pci_writeb(bus, i, PCI_INTERRUPT_LINE,
		       s->irq.AssignedIRQ);
	s->socket.io_irq = s->irq.AssignedIRQ;
	s->ss_entry->set_socket(s->sock, &s->socket);
    }
}

/*======================================================================

    cb_disable() unconfigures a Cardbus card previously set up by
    cb_enable().

    It is called from the ReleaseConfiguration service.
    
======================================================================*/

void cb_disable(socket_info_t *s)
{
    u_char i;
    cb_bridge_map m = { 0, 0, 0, 0xffff };
    
    DEBUG(0, "cs: cb_disable(bus %d)\n", s->cap.cardbus);
    
    /* Turn off bridge windows */
    if (s->cb_cis_map.start)
	cb_release_cis_mem(s);
    for (i = 0; i < 3; i++) {
	switch (i) {
	case B_IO: m.map = 0; m.flags = MAP_IOSPACE; break;
	case B_M1: m.map = m.flags = 0; break;
	case B_M2: m.map = 1; m.flags = 0; break;
	}
	s->ss_entry->set_bridge(s->sock, &m);
    }
}
