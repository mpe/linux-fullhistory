/*
 * FILE NAME
 *	arch/mips/vr41xx/common/pciu.c
 *
 * BRIEF MODULE DESCRIPTION
 *	PCI Control Unit routines for the NEC VR4100 series.
 *
 * Author: Yoichi Yuasa
 *         yyuasa@mvista.com or source@mvista.com
 *
 * Copyright 2001,2002 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
/*
 * Changes:
 *  Paul Mundt <lethal@chaoticdreams.org>
 *  - Fix deadlock-causing PCIU access race for VR4131.
 *
 *  MontaVista Software Inc. <yyuasa@mvista.com> or <source@mvista.com>
 *  - New creation, NEC VR4122 and VR4131 are supported.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/delay.h>

#include <asm/cpu.h>
#include <asm/io.h>
#include <asm/pci_channel.h>
#include <asm/vr41xx/vr41xx.h>

#include "pciu.h"

extern unsigned long vr41xx_vtclock;

static inline int vr41xx_pci_config_access(unsigned char bus,
					   unsigned int devfn, int where)
{
	if (bus == 0) {
		/*
		 * Type 0 configuration
		 */
		if (PCI_SLOT(devfn) < 11 || where > 255)
			return -1;

		writel((1UL << PCI_SLOT(devfn)) |
		       (PCI_FUNC(devfn) << 8) |
		       (where & 0xfc), PCICONFAREG);
	} else {
		/*
		 * Type 1 configuration
		 */
		if (where > 255)
			return -1;

		writel((bus << 16) |
		       (devfn << 8) | (where & 0xfc) | 1UL, PCICONFAREG);
	}

	return 0;
}

static int vr41xx_pci_config_read(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 * val)
{
	u32 data;

	*val = 0xffffffffUL;
	if (vr41xx_pci_config_access(bus->number, devfn, where) < 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	data = readl(PCICONFDREG);

	switch (size) {
	case 1:
		*val = (data >> ((where & 3) << 3)) & 0xffUL;
		break;
	case 2:
		*val = (data >> ((where & 2) << 3)) & 0xffffUL;
		break;
	case 4:
		*val = data;
		break;
	default:
		return PCIBIOS_FUNC_NOT_SUPPORTED;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int vr41xx_pci_config_write(struct pci_bus *bus, unsigned int devfn,
				   int where, int size, u32 val)
{
	u32 data;
	int shift;

	if (vr41xx_pci_config_access(bus->number, devfn, where) < 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	data = readl(PCICONFDREG);

	switch (size) {
	case 1:
		shift = (where & 3) << 3;
		data &= ~(0xff << shift);
		data |= ((val & 0xff) << shift);
		break;
	case 2:
		shift = (where & 2) << 3;
		data &= ~(0xffff << shift);
		data |= ((val & 0xffff) << shift);
		break;
	case 4:
		data = val;
		break;
	default:
		return PCIBIOS_FUNC_NOT_SUPPORTED;
	}

	writel(data, PCICONFDREG);

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops vr41xx_pci_ops = {
	.read = vr41xx_pci_config_read,
	.write = vr41xx_pci_config_write,
};

void __init vr41xx_pciu_init(struct vr41xx_pci_address_map *map)
{
	struct vr41xx_pci_address_space *s;
	u32 config;
	int n;

	if (!map)
		return;

	/* Disable PCI interrupt */
	writew(0, MPCIINTREG);

	/* Supply VTClock to PCIU */
	vr41xx_clock_supply(PCIU_CLOCK);

	/*
	 * Sleep for 1us after setting MSKPPCIU bit in CMUCLKMSK
	 * before doing any PCIU access to avoid deadlock on VR4131.
	 */
	udelay(1);

	/* Select PCI clock */
	if (vr41xx_vtclock < MAX_PCI_CLOCK)
		writel(EQUAL_VTCLOCK, PCICLKSELREG);
	else if ((vr41xx_vtclock / 2) < MAX_PCI_CLOCK)
		writel(HALF_VTCLOCK, PCICLKSELREG);
	else if ((vr41xx_vtclock / 4) < MAX_PCI_CLOCK)
		writel(QUARTER_VTCLOCK, PCICLKSELREG);
	else
		printk(KERN_INFO "Warning: PCI Clock is over 33MHz.\n");

	/* Supply PCI clock by PCI bus */
	vr41xx_clock_supply(PCI_CLOCK);

	/*
	 * Set PCI memory & I/O space address conversion registers
	 * for master transaction.
	 */
	if (map->mem1 != NULL) {
		s = map->mem1;
		config = (s->internal_base & 0xff000000) |
		    ((s->address_mask & 0x7f000000) >> 11) | (1UL << 12) |
		    ((s->pci_base & 0xff000000) >> 24);
		writel(config, PCIMMAW1REG);
	}
	if (map->mem2 != NULL) {
		s = map->mem2;
		config = (s->internal_base & 0xff000000) |
		    ((s->address_mask & 0x7f000000) >> 11) | (1UL << 12) |
		    ((s->pci_base & 0xff000000) >> 24);
		writel(config, PCIMMAW2REG);
	}
	if (map->io != NULL) {
		s = map->io;
		config = (s->internal_base & 0xff000000) |
		    ((s->address_mask & 0x7f000000) >> 11) | (1UL << 12) |
		    ((s->pci_base & 0xff000000) >> 24);
		writel(config, PCIMIOAWREG);
	}

	/* Set target memory windows */
	writel(0x00081000, PCITAW1REG);
	writel(0UL, PCITAW2REG);
	pciu_write_config_dword(PCI_BASE_ADDRESS_0, 0UL);
	pciu_write_config_dword(PCI_BASE_ADDRESS_1, 0UL);

	/* Clear bus error */
	n = readl(BUSERRADREG);

	if (current_cpu_data.cputype == CPU_VR4122) {
		writel(0UL, PCITRDYVREG);
		pciu_write_config_dword(PCI_CACHE_LINE_SIZE, 0x0000f804);
	} else {
		writel(100UL, PCITRDYVREG);
		pciu_write_config_dword(PCI_CACHE_LINE_SIZE, 0x00008004);
	}

	writel(CONFIG_DONE, PCIENREG);
	pciu_write_config_dword(PCI_COMMAND,
				PCI_COMMAND_IO |
				PCI_COMMAND_MEMORY |
				PCI_COMMAND_MASTER |
				PCI_COMMAND_PARITY | PCI_COMMAND_SERR);
}
