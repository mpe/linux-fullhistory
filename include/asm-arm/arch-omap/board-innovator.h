/*
 * linux/include/asm-arm/arch-omap/board-innovator.h
 *
 * Copyright (C) 2001 RidgeRun, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef __ASM_ARCH_OMAP_INNOVATOR_H
#define __ASM_ARCH_OMAP_INNOVATOR_H

#if defined (CONFIG_ARCH_OMAP1510)

#ifndef OMAP_SDRAM_DEVICE
#define OMAP_SDRAM_DEVICE			D256M_1X16_4B
#endif

#define OMAP1510P1_IMIF_PRI_VALUE		0x00
#define OMAP1510P1_EMIFS_PRI_VALUE		0x00
#define OMAP1510P1_EMIFF_PRI_VALUE		0x00

/*
 * These definitions define an area of FLASH set aside
 * for the use of MTD/JFFS2. This is the area of flash
 * that a JFFS2 filesystem will reside which is mounted
 * at boot with the "root=/dev/mtdblock/0 rw"
 * command line option. The flash address used here must
 * fall within the legal range defined by rrload for storing
 * the filesystem component. This address will be sufficiently
 * deep into the overall flash range to avoid the other
 * components also stored in flash such as the bootloader,
 * the bootloader params, and the kernel.
 * The SW2 settings for the map below are:
 * 1 off, 2 off, 3 on, 4 off.
 */

/* Intel flash_0, partitioned as expected by rrload */
#define OMAP_FLASH_0_BASE	0xD8000000
#define OMAP_FLASH_0_START	0x00000000
#define OMAP_FLASH_0_SIZE	SZ_16M

/* Intel flash_1, used for cramfs or other flash file systems */
#define OMAP_FLASH_1_BASE	0xD9000000
#define OMAP_FLASH_1_START	0x01000000
#define OMAP_FLASH_1_SIZE	SZ_16M

#define NR_FPGA_IRQS		24
#define NR_IRQS                 IH_BOARD_BASE + NR_FPGA_IRQS

#ifndef __ASSEMBLY__
void fpga_write(unsigned char val, int reg);
unsigned char fpga_read(int reg);
#endif

#endif /* CONFIG_ARCH_OMAP1510 */

#if defined (CONFIG_ARCH_OMAP1610)

/* At OMAP1610 Innovator the Ethernet is directly connected to CS1 */
#define OMAP1610_ETHR_BASE		0xE8000000
#define OMAP1610_ETHR_SIZE		SZ_4K
#define OMAP1610_ETHR_START		0x04000000

/* Intel STRATA NOR flash at CS3 */
#define OMAP1610_NOR_FLASH_BASE		0xD8000000
#define OMAP1610_NOR_FLASH_SIZE		SZ_32M
#define OMAP1610_NOR_FLASH_START	0x0C000000

#endif /* CONFIG_ARCH_OMAP1610 */
#endif /* __ASM_ARCH_OMAP_INNOVATOR_H */
