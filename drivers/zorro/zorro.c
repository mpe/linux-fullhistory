/*
 *    $Id: zorro.c,v 1.1.2.1 1998/06/07 23:21:02 geert Exp $
 *
 *    Zorro Bus Services
 *
 *    Copyright (C) 1995-1998 Geert Uytterhoeven
 *
 *    This file is subject to the terms and conditions of the GNU General Public
 *    License.  See the file COPYING in the main directory of this archive
 *    for more details.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/zorro.h>
#include <asm/setup.h>
#include <asm/bitops.h>
#include <asm/amigahw.h>


    /*
     *  Expansion Devices
     */

u_int zorro_num_autocon = 0;
struct ConfigDev zorro_autocon[ZORRO_NUM_AUTO];
static u32 zorro_autocon_parts[ZORRO_NUM_AUTO] = { 0, };

   
    /*
     *  Find the key for the next unconfigured expansion device of a specific
     *  type.
     *
     *  Part is a device specific number (0 <= part <= 31) to allow for the
     *  independent configuration of independent parts of an expansion board.
     *  Thanks to Jes Soerensen for this idea!
     *
     *  Index is used to specify the first board in the autocon list
     *  to be tested. It was inserted in order to solve the problem
     *  with the GVP boards that use the same product code, but
     *  it should help if there are other companies which use the same
     *  method as GVP. Drivers for boards which are not using this
     *  method do not need to think of this - just set index = 0.
     *
     *  Example:
     *
     *      while ((key = zorro_find(ZORRO_PROD_MY_BOARD, MY_PART, 0))) {
     *      	cd = zorro_get_board(key);
     *      	initialise_this_board;
     *      	zorro_config_board(key, MY_PART);
     *      }
     */

u_int zorro_find(zorro_id id, u_int part, u_int index)
{
    u16 manuf = ZORRO_MANUF(id);
    u8 prod = ZORRO_PROD(id);
    u8 epc = ZORRO_EPC(id);
    u_int key;
    const struct ConfigDev *cd;
    u32 addr;

    if (!MACH_IS_AMIGA || !AMIGAHW_PRESENT(ZORRO))
	return 0;

    if (part > 31) {
	printk("zorro_find: bad part %d\n", part);
	return 0;
    }

    for (key = index+1; key <= zorro_num_autocon; key++) {
	cd = &zorro_autocon[key-1];
	addr = (u32)cd->cd_BoardAddr;
	if ((cd->cd_Rom.er_Manufacturer == manuf) &&
	    (cd->cd_Rom.er_Product == prod) &&
	    !(zorro_autocon_parts[key-1] & (1<<part)) &&
	    (manuf != ZORRO_MANUF(ZORRO_PROD_GVP_EPC_BASE) ||
	     prod != ZORRO_PROD(ZORRO_PROD_GVP_EPC_BASE) || /* GVP quirk */
	     (*(u16 *)ZTWO_VADDR(addr+0x8000) & GVP_PRODMASK) == epc))
	    return key;
    }
    return 0;
}


    /*
     *  Get the board corresponding to a specific key
     */

const struct ConfigDev *zorro_get_board(u_int key)
{
    if ((key < 1) || (key > zorro_num_autocon)) {
	printk("zorro_get_board: bad key %d\n", key);
	return NULL;
    }
    return &zorro_autocon[key-1];
}


    /*
     *  Mark a part of a board as configured
     */

void zorro_config_board(u_int key, u_int part)
{
    if ((key < 1) || (key > zorro_num_autocon))
	printk("zorro_config_board: bad key %d\n", key);
    else if (part > 31)
	printk("zorro_config_board: bad part %d\n", part);
    else if (zorro_autocon_parts[key-1] & (1<<part))
	printk("zorro_config_board: key %d part %d is already configured\n",
	       key, part);
    else
	zorro_autocon_parts[key-1] |= 1<<part;
}


    /*
     *  Mark a part of a board as unconfigured
     *
     *  This function is mainly intended for the unloading of LKMs
     */

void zorro_unconfig_board(u_int key, u_int part)
{
    if ((key < 1) || (key > zorro_num_autocon))
	printk("zorro_unconfig_board: bad key %d\n", key);
    else if (part > 31)
	printk("zorro_unconfig_board: bad part %d\n", part);
    else if (!(zorro_autocon_parts[key-1] & (1<<part)))
	printk("zorro_config_board: key %d part %d is not yet configured\n",
	       key, part);
    else
	zorro_autocon_parts[key-1] &= ~(1<<part);
}


    /*
     *  Bitmask indicating portions of available Zorro II RAM that are unused
     *  by the system. Every bit represents a 64K chunk, for a maximum of 8MB
     *  (128 chunks, physical 0x00200000-0x009fffff).
     *
     *  If you want to use (= allocate) portions of this RAM, you should clear
     *  the corresponding bits.
     *
     *  Possible uses:
     *      - z2ram device
     *      - SCSI DMA bounce buffers
     */

u32 zorro_unused_z2ram[4] = { 0, 0, 0, 0 };


__initfunc(static void mark_region(u32 addr, u_int size, int flag))
{
    u32 start, end;

    if (flag) {
	start = (addr+Z2RAM_CHUNKMASK) & ~Z2RAM_CHUNKMASK;
	end = (addr+size) & ~Z2RAM_CHUNKMASK;
    } else {
	start = addr & ~Z2RAM_CHUNKMASK;
	end = (addr+size+Z2RAM_CHUNKMASK) & ~Z2RAM_CHUNKMASK;
    }
    if (end <= Z2RAM_START || start >= Z2RAM_END)
	return;
    start = start < Z2RAM_START ? 0x00000000 : start-Z2RAM_START;
    end = end > Z2RAM_END ? Z2RAM_SIZE : end-Z2RAM_START;
    while (start < end) {
	u32 chunk = start>>Z2RAM_CHUNKSHIFT;
	if (flag)
	    set_bit(chunk, zorro_unused_z2ram);
	else
	    clear_bit(chunk, zorro_unused_z2ram);
	start += Z2RAM_CHUNKSIZE;
    }
}


    /*
     *  Initialization
     */

__initfunc(void zorro_init(void))
{
    u_int i;

    if (!MACH_IS_AMIGA || !AMIGAHW_PRESENT(ZORRO)) {
	printk("Zorro: No Zorro bus detected\n");
	return;
    }

    printk("Zorro: Probing AutoConfig expansion devices: %d device%s\n",
	   zorro_num_autocon, zorro_num_autocon == 1 ? "" : "s");

    /* Mark all available Zorro II memory */
    for (i = 0; i < zorro_num_autocon; i++) {
	const struct ConfigDev *cd = &zorro_autocon[i];
	if (cd->cd_Rom.er_Type & ERTF_MEMLIST)
	    mark_region((u32)cd->cd_BoardAddr, cd->cd_BoardSize, 1);
    }

    /* Unmark all used Zorro II memory */
    for (i = 0; i < m68k_num_memory; i++)
	if (m68k_memory[i].addr < 16*1024*1024)
	    mark_region(m68k_memory[i].addr, m68k_memory[i].size, 0);

#ifdef CONFIG_PROC_FS
    zorro_proc_init();
#endif
}
