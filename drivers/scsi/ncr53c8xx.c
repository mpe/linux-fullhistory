/******************************************************************************
**  Device driver for the PCI-SCSI NCR538XX controller family.
**
**  Copyright (C) 1994  Wolfgang Stanglmeier
**
**  This program is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 2 of the License, or
**  (at your option) any later version.
**
**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with this program; if not, write to the Free Software
**  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
**-----------------------------------------------------------------------------
**
**  This driver has been ported to Linux from the FreeBSD NCR53C8XX driver
**  and is currently maintained by
**
**          Gerard Roudier              <groudier@club-internet.fr>
**
**  Being given that this driver originates from the FreeBSD version, and
**  in order to keep synergy on both, any suggested enhancements and corrections
**  received on Linux are automatically a potential candidate for the FreeBSD 
**  version.
**
**  The original driver has been written for 386bsd and FreeBSD by
**          Wolfgang Stanglmeier        <wolf@cologne.de>
**          Stefan Esser                <se@mi.Uni-Koeln.de>
**
**  And has been ported to NetBSD by
**          Charles M. Hannum           <mycroft@gnu.ai.mit.edu>
**
**-----------------------------------------------------------------------------
**
**                     Brief history
**
**  December 10 1995 by Gerard Roudier:
**     Initial port to Linux.
**
**  June 23 1996 by Gerard Roudier:
**     Support for 64 bits architectures (Alpha).
**
**  November 30 1996 by Gerard Roudier:
**     Support for Fast-20 scsi.
**     Support for large DMA fifo and 128 dwords bursting.
**
**  February 27 1997 by Gerard Roudier:
**     Support for Fast-40 scsi.
**     Support for on-Board RAM.
**
**  May 3 1997 by Gerard Roudier:
**     Full support for scsi scripts instructions pre-fetching.
**
**  May 19 1997 by Richard Waltham <dormouse@farsrobt.demon.co.uk>:
**     Support for NvRAM detection and reading.
**
**  August 18 1997 by Cort <cort@cs.nmt.edu>:
**     Support for Power/PC (Big Endian).
**
*******************************************************************************
*/

/*
**	2 January 1998, version 2.5f
**
**	Supported SCSI-II features:
**	    Synchronous negotiation
**	    Wide negotiation        (depends on the NCR Chip)
**	    Enable disconnection
**	    Tagged command queuing
**	    Parity checking
**	    Etc...
**
**	Supported NCR chips:
**		53C810		(8 bits, Fast SCSI-2, no rom BIOS) 
**		53C815		(8 bits, Fast SCSI-2, on board rom BIOS)
**		53C820		(Wide,   Fast SCSI-2, no rom BIOS)
**		53C825		(Wide,   Fast SCSI-2, on board rom BIOS)
**		53C860		(8 bits, Fast 20,     no rom BIOS)
**		53C875		(Wide,   Fast 20,     on board rom BIOS)
**		53C895		(Wide,   Fast 40,     on board rom BIOS)
**
**	Other features:
**		Memory mapped IO (linux-1.3.X and above only)
**		Module
**		Shared IRQ (since linux-1.3.72)
*/

#define SCSI_NCR_DEBUG_FLAGS	(0)		

#define NCR_GETCC_WITHMSG

/*==========================================================
**
**      Include files
**
**==========================================================
*/

#define LinuxVersionCode(v, p, s) (((v)<<16)+((p)<<8)+(s))

#ifdef MODULE
#include <linux/module.h>
#endif

#include <asm/dma.h>
#include <asm/io.h>
#include <asm/system.h>
#include <linux/delay.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/stat.h>

#include <linux/version.h>
#if LINUX_VERSION_CODE >= LinuxVersionCode(1,3,0)
#include <linux/blk.h>
#else
#include "../block/blk.h"
#endif

#if LINUX_VERSION_CODE >= LinuxVersionCode(2,1,35)
#include <linux/init.h>
#else
#include <linux/bios32.h>
#ifndef	__initdata
#define	__initdata
#endif
#ifndef	__initfunc
#define	__initfunc(__arginit) __arginit
#endif
#endif

#include "scsi.h"
#include "hosts.h"
#include "constants.h"
#include "sd.h"

#include <linux/types.h>

/*
**	Define the BSD style u_int32 type
*/
typedef u32 u_int32;

#include "ncr53c8xx.h"

/*==========================================================
**
**	Configuration and Debugging
**
**==========================================================
*/

/*
**    SCSI address of this device.
**    The boot routines should have set it.
**    If not, use this.
*/

#ifndef SCSI_NCR_MYADDR
#define SCSI_NCR_MYADDR      (7)
#endif

/*
**    The maximum number of tags per logic unit.
**    Used only for disk devices that support tags.
*/

#ifndef SCSI_NCR_MAX_TAGS
#define SCSI_NCR_MAX_TAGS    (4)
#endif

/*
**    Number of targets supported by the driver.
**    n permits target numbers 0..n-1.
**    Default is 7, meaning targets #0..#6.
**    #7 .. is myself.
*/

#ifdef SCSI_NCR_MAX_TARGET
#define MAX_TARGET  (SCSI_NCR_MAX_TARGET)
#else
#define MAX_TARGET  (16)
#endif

/*
**    Number of logic units supported by the driver.
**    n enables logic unit numbers 0..n-1.
**    The common SCSI devices require only
**    one lun, so take 1 as the default.
*/

#ifdef SCSI_NCR_MAX_LUN
#define MAX_LUN    SCSI_NCR_MAX_LUN
#else
#define MAX_LUN    (1)
#endif

/*
**    Asynchronous pre-scaler (ns). Shall be 40
*/
 
#ifndef SCSI_NCR_MIN_ASYNC
#define SCSI_NCR_MIN_ASYNC (40)
#endif

/*
**    The maximum number of jobs scheduled for starting.
**    There should be one slot per target, and one slot
**    for each tag of each target in use.
**    The calculation below is actually quite silly ...
*/

#ifdef SCSI_NCR_CAN_QUEUE
#define MAX_START   (SCSI_NCR_CAN_QUEUE + 4)
#else
#define MAX_START   (MAX_TARGET + 7 * SCSI_NCR_MAX_TAGS)
#endif

/*
**    The maximum number of segments a transfer is split into.
*/

#define MAX_SCATTER (SCSI_NCR_MAX_SCATTER)

/*
**    Io mapped or memory mapped.
*/

#if defined(SCSI_NCR_IOMAPPED)
#define NCR_IOMAPPED
#endif

/*
**	other
*/

#define NCR_SNOOP_TIMEOUT (1000000)

/*==========================================================
**
**	Defines for Linux.
**
**	Linux and Bsd kernel functions are quite different.
**	These defines allow a minimum change of the original
**	code.
**
**==========================================================
*/

 /*
 **	Obvious definitions
 */

#define printf		printk
#define u_char		unsigned char
#define u_short		unsigned short
#define u_int		unsigned int
#define u_long		unsigned long

typedef	u_long		vm_offset_t;
typedef	int		vm_size_t;

#ifndef bcopy
#define bcopy(s, d, n)	memcpy((d), (s), (n))
#endif
#ifndef bzero
#define bzero(d, n)	memset((d), 0, (n))
#endif

#ifndef offsetof
#define offsetof(t, m)	((size_t) (&((t *)0)->m))
#endif

/*
**	Address translation
**
**	On Linux 1.3.X, virt_to_bus() must be used to translate
**	virtual memory addresses of the kernel data segment into
**	IO bus adresses.
**	On i386 architecture, IO bus addresses match the physical
**	addresses. But on other architectures they can be different.
**	In the original Bsd driver, vtophys() is called to translate
**	data addresses to IO bus addresses. In order to minimize
**	change, I decide to define vtophys() as virt_to_bus().
*/

#if LINUX_VERSION_CODE >= LinuxVersionCode(1,3,0)
#define vtophys(p)	virt_to_bus(p)

/*
**	Memory mapped IO
**
**	Since linux-2.1, we must use ioremap() to map the io memory space.
**	iounmap() to unmap it. That allows portability.
**	Linux 1.3.X and 2.0.X allow to remap physical pages addresses greater 
**	than the highest physical memory address to kernel virtual pages with 
**	vremap() / vfree(). That was not portable but worked with i386 
**	architecture.
*/

#ifdef __sparc__
#define remap_pci_mem(base, size)	((vm_offset_t) __va(base))
#define unmap_pci_mem(vaddr, size)
#define pcivtophys(p)			((p) & pci_dvma_mask)
#else	/* __sparc__ */
#define pcivtophys(p)			(p)
#ifndef NCR_IOMAPPED
__initfunc(
static vm_offset_t remap_pci_mem(u_long base, u_long size)
)
{
	u_long page_base	= ((u_long) base) & PAGE_MASK;
	u_long page_offs	= ((u_long) base) - page_base;
#if LINUX_VERSION_CODE >= LinuxVersionCode(2,1,0)
	u_long page_remapped	= (u_long) ioremap(page_base, page_offs+size);
#else
	u_long page_remapped	= (u_long) vremap(page_base, page_offs+size);
#endif

	return (vm_offset_t) (page_remapped ? (page_remapped + page_offs) : 0UL);
}

__initfunc(
static void unmap_pci_mem(vm_offset_t vaddr, u_long size)
)
{
	if (vaddr)
#if LINUX_VERSION_CODE >= LinuxVersionCode(2,1,0)
		iounmap((void *) (vaddr & PAGE_MASK));
#else
		vfree((void *) (vaddr & PAGE_MASK));
#endif
}
#endif	/* !NCR_IOMAPPED */
#endif	/* __sparc__ */

#else /* linux-1.2.13 */

/*
**	Linux 1.2.X assumes that addresses (virtual, physical, bus)
**	are the same.
**
**	I have not found how to do MMIO. It seems that only processes can
**	map high physical pages to virtual (Xservers can do MMIO).
*/

#define vtophys(p)	((u_long) (p))
#endif

/*
**	Insert a delay in micro-seconds.
*/

static void DELAY(long us)
{
	for (;us>1000;us-=1000) udelay(1000);
	if (us) udelay(us);
}

/*
**	Internal data structure allocation.
**
**	Linux scsi memory poor pool is adjusted for the need of
**	middle-level scsi driver.
**	We allocate our control blocks in the kernel memory pool
**	to avoid scsi pool shortage.
**	I notice that kmalloc() returns NULL during host attach under
**	Linux 1.2.13. But this ncr driver is reliable enough to
**	accomodate with this joke.
**
**	kmalloc() only ensure 8 bytes boundary alignment.
**	The NCR need better alignment for cache line bursting.
**	The global header is moved betewen the NCB and CCBs and need 
**	origin and destination addresses to have same lower four bits.
**
**	We use 32 boundary alignment for NCB and CCBs and offset multiple 
**	of 32 for global header fields. That's too much but at least enough.
*/

#define ALIGN_SIZE(shift)	(1UL << shift)
#define ALIGN_MASK(shift)	(~(ALIGN_SIZE(shift)-1))

#define NCB_ALIGN_SHIFT		5
#define CCB_ALIGN_SHIFT		5
#define LCB_ALIGN_SHIFT		5
#define SCR_ALIGN_SHIFT		5

#define NCB_ALIGN_SIZE		ALIGN_SIZE(NCB_ALIGN_SHIFT)
#define NCB_ALIGN_MASK		ALIGN_MASK(NCB_ALIGN_SHIFT)
#define CCB_ALIGN_SIZE		ALIGN_SIZE(CCB_ALIGN_SHIFT)
#define CCB_ALIGN_MASK		ALIGN_MASK(CCB_ALIGN_SHIFT)
#define SCR_ALIGN_SIZE		ALIGN_SIZE(SCR_ALIGN_SHIFT)
#define SCR_ALIGN_MASK		ALIGN_MASK(SCR_ALIGN_SHIFT)

static void *m_alloc(int size, int a_shift)
{
	u_long addr;
	void *ptr;
	u_long a_size, a_mask;

	if (a_shift < 3)
		a_shift = 3;

	a_size	= ALIGN_SIZE(a_shift);
	a_mask	= ALIGN_MASK(a_shift);

	ptr = (void *) kmalloc(size + a_size, GFP_ATOMIC);
	if (ptr) {
		addr	= (((u_long) ptr) + a_size) & a_mask;
		*((void **) (addr - sizeof(void *))) = ptr;
		ptr	= (void *) addr;
	}

	return ptr;
}

#ifdef MODULE
static void m_free(void *ptr, int size)
{
	u_long addr;

	if (ptr) {
		addr	= (u_long) ptr;
		ptr	= *((void **) (addr - sizeof(void *)));

		kfree(ptr);
	}
}
#endif

/*
**	Transfer direction
**
**	Low-level scsi drivers under Linux do not receive the expected 
**	data transfer direction from upper scsi drivers.
**	The driver will only check actual data direction for common 
**	scsi opcodes. Other ones may cause problem, since they may 
**	depend on device type or be vendor specific.
**	I would prefer to never trust the device for data direction, 
**	but that is not possible.
**
**	The original driver requires the expected direction to be known.
**	The Linux version of the driver has been enhanced in order to 
**	be able to transfer data in the direction choosen by the target. 
*/

#define XferNone	0
#define XferIn		1
#define XferOut		2
#define XferBoth	3
static int guess_xfer_direction(int opcode);

/*
**	Head of list of NCR boards
**
**	For kernel version < 1.3.70, host is retrieved by its irq level.
**	For later kernels, the internal host control block address 
**	(struct ncb) is used as device id parameter of the irq stuff.
*/

static struct Scsi_Host		*first_host	= NULL;
static Scsi_Host_Template	*the_template	= NULL;	


/*
**	/proc directory entry and proc_info function
*/

#if LINUX_VERSION_CODE >= LinuxVersionCode(1,3,0)
struct proc_dir_entry proc_scsi_ncr53c8xx = {
    PROC_SCSI_NCR53C8XX, 9, "ncr53c8xx",
    S_IFDIR | S_IRUGO | S_IXUGO, 2
};
# ifdef SCSI_NCR_PROC_INFO_SUPPORT
int ncr53c8xx_proc_info(char *buffer, char **start, off_t offset,
			int length, int hostno, int func);
# endif
#endif

/*
**	Table of target capabilities.
**
**	This bitmap is anded with the byte 7 of inquiry data on completion of
**	INQUIRY command.
** 	The driver never see zeroed bits and will ignore the corresponding
**	capabilities of the target.
*/

static struct {
	unsigned char and_map[MAX_TARGET];
} target_capabilities[SCSI_NCR_MAX_HOST] = { NCR53C8XX_TARGET_CAPABILITIES };

/*
**	Driver setup.
**
**	This structure is initialized from linux config options.
**	It can be overridden at boot-up by the boot command line.
*/
struct ncr_driver_setup {
	unsigned master_parity	: 1;
	unsigned scsi_parity	: 1;
	unsigned disconnection	: 1;
	unsigned special_features : 2;
	unsigned ultra_scsi	: 2;
	unsigned force_sync_nego: 1;
	unsigned reverse_probe: 1;
	unsigned pci_fix_up: 4;
	u_char	use_nvram;
	u_char	verbose;
	u_char	default_tags;
	u_short	default_sync;
	u_short	debug;
	u_char	burst_max;
	u_char	led_pin;
	u_char	max_wide;
	u_char	settle_delay;
	u_char	diff_support;
	u_char	irqm;
	u_char	bus_check;
};

static struct ncr_driver_setup
	driver_setup			= SCSI_NCR_DRIVER_SETUP;

#ifdef	SCSI_NCR_BOOT_COMMAND_LINE_SUPPORT
static struct ncr_driver_setup
	driver_safe_setup __initdata	= SCSI_NCR_DRIVER_SAFE_SETUP;
#ifdef	MODULE
char *ncr53c8xx = 0;	/* command line passed by insmod */
#endif
#endif

/*
**	Other Linux definitions
*/

#define ScsiResult(host_code, scsi_code) (((host_code) << 16) + ((scsi_code) & 0x7f))

#if LINUX_VERSION_CODE >= LinuxVersionCode(2,0,0)
static void ncr53c8xx_select_queue_depths(struct Scsi_Host *host, struct scsi_device *devlist);
#endif

#if LINUX_VERSION_CODE >= LinuxVersionCode(1,3,70)
static void ncr53c8xx_intr(int irq, void *dev_id, struct pt_regs * regs);
#else
static void ncr53c8xx_intr(int irq, struct pt_regs * regs);
#endif

static void ncr53c8xx_timeout(unsigned long np);

#define initverbose (driver_setup.verbose)
#define bootverbose (np->verbose)

#ifdef SCSI_NCR_NVRAM_SUPPORT
/*
**	Symbios NvRAM data format
*/
#define SYMBIOS_NVRAM_SIZE 368
#define SYMBIOS_NVRAM_ADDRESS 0x100

struct Symbios_nvram {
/* Header 6 bytes */
	u_short start_marker;	/* 0x0000 */
	u_short byte_count;	/* excluding header/trailer */
	u_short checksum;

/* Controller set up 20 bytes */
	u_short	word0;		/* 0x3000 */
	u_short	word2;		/* 0x0000 */
	u_short	word4;		/* 0x0000 */
	u_short	flags;
#define SYMBIOS_SCAM_ENABLE	(1)
#define SYMBIOS_PARITY_ENABLE	(1<<1)
#define SYMBIOS_VERBOSE_MSGS	(1<<2)
	u_short	flags1;
#define SYMBIOS_SCAN_HI_LO	(1)
	u_short	word10;		/* 0x00 */
	u_short	flags3;		/* 0x00 */
#define SYMBIOS_REMOVABLE_FLAGS	(3)		/* 0=none, 1=bootable, 2=all */
	u_char	host_id;
	u_char	byte15;		/* 0x04 */
	u_short	word16;		/* 0x0410 */
	u_short	word18;		/* 0x0000 */

/* Boot order 14 bytes * 4 */
	struct Symbios_host{
		u_char	word0;		/* 0x0004:ok / 0x0000:nok */
		u_short	device_id;	/* PCI device id */
		u_short	vendor_id;	/* PCI vendor id */
		u_char	byte6;		/* 0x00 */
		u_char	device_fn;	/* PCI device/function number << 3*/
		u_short	word8;
		u_short	flags;
#define	SYMBIOS_INIT_SCAN_AT_BOOT	(1)
		u_short	io_port;	/* PCI io_port address */
	} host[4];

/* Targets 8 bytes * 16 */
	struct Symbios_target {
		u_short	flags;
#define SYMBIOS_DISCONNECT_ENABLE	(1)
#define SYMBIOS_SCAN_AT_BOOT_TIME	(1<<1)
#define SYMBIOS_SCAN_LUNS		(1<<2)
#define SYMBIOS_QUEUE_TAGS_ENABLED	(1<<3)
		u_char	bus_width;	/* 0x08/0x10 */
		u_char	sync_offset;
		u_char	sync_period;	/* 4*period factor */
		u_char	byte6;		/* 0x00 */
		u_short	timeout;
	} target[16];
	u_char	spare_devices[19*8];
	u_char	trailer[6];		/* 0xfe 0xfe 0x00 0x00 0x00 0x00 */
};
typedef struct Symbios_nvram	Symbios_nvram;
typedef struct Symbios_host	Symbios_host;
typedef struct Symbios_target	Symbios_target;

/*
**	Tekram NvRAM data format.
*/
#define TEKRAM_NVRAM_SIZE 64
#define TEKRAM_NVRAM_ADDRESS 0

struct Tekram_nvram {
	struct Tekram_target {
		u_char	flags;
#define	TEKRAM_PARITY_CHECK		(1)
#define TEKRAM_SYNC_NEGO		(1<<1)
#define TEKRAM_DISCONNECT_ENABLE	(1<<2)
#define	TEKRAM_START_CMD		(1<<3)
#define TEKRAM_TAGGED_COMMANDS		(1<<4)
#define TEKRAM_WIDE_NEGO		(1<<5)
		u_char	sync_index;
		u_short	word2;
	} target[16];
	u_char	host_id;
	u_char	flags;
#define TEKRAM_MORE_THAN_2_DRIVES	(1)
#define TEKRAM_DRIVES_SUP_1GB		(1<<1)
#define	TEKRAM_RESET_ON_POWER_ON	(1<<2)
#define TEKRAM_ACTIVE_NEGATION		(1<<3)
#define TEKRAM_IMMEDIATE_SEEK		(1<<4)
#define	TEKRAM_SCAN_LUNS		(1<<5)
#define	TEKRAM_REMOVABLE_FLAGS		(3<<6)	/* 0: disable; 1: boot device; 2:all */
	u_char	boot_delay_index;
	u_char	max_tags_index;
	u_short	flags1;
#define TEKRAM_F2_F6_ENABLED		(1)
	u_short	spare[29];
};
typedef struct Tekram_nvram	Tekram_nvram;
typedef struct Tekram_target	Tekram_target;

static u_char Tekram_sync[12] __initdata = {25,31,37,43,50,62,75,125,12,15,18,21};

#endif /* SCSI_NCR_NVRAM_SUPPORT */

/*
**	Structures used by ncr53c8xx_detect/ncr53c8xx_pci_init to 
**	transmit device configuration to the ncr_attach() function.
*/
typedef struct {
	int	bus;
	u_char	device_fn;
	u_long	base;
	u_long	base_2;
	u_long	io_port;
	int	irq;
/* port and reg fields to use INB, OUTB macros */
	u_long	port;
	volatile struct	ncr_reg	*reg;
} ncr_slot;

typedef struct {
	int type;
#define	SCSI_NCR_SYMBIOS_NVRAM	(1)
#define	SCSI_NCR_TEKRAM_NVRAM	(2)
#ifdef	SCSI_NCR_NVRAM_SUPPORT
	union {
		Symbios_nvram Symbios;
		Tekram_nvram Tekram;
	} data;
#endif
} ncr_nvram;

/*
**	Structure used by ncr53c8xx_detect/ncr53c8xx_pci_init
**	to save data on each detected board for ncr_attach().
*/
typedef struct {
	ncr_slot  slot;
	ncr_chip  chip;
	ncr_nvram *nvram;
	int attach_done;
} ncr_device;

/*==========================================================
**
**	Debugging tags
**
**==========================================================
*/

#define DEBUG_ALLOC    (0x0001)
#define DEBUG_PHASE    (0x0002)
#define DEBUG_POLL     (0x0004)
#define DEBUG_QUEUE    (0x0008)
#define DEBUG_RESULT   (0x0010)
#define DEBUG_SCATTER  (0x0020)
#define DEBUG_SCRIPT   (0x0040)
#define DEBUG_TINY     (0x0080)
#define DEBUG_TIMING   (0x0100)
#define DEBUG_NEGO     (0x0200)
#define DEBUG_TAGS     (0x0400)
#define DEBUG_FREEZE   (0x0800)
#define DEBUG_RESTART  (0x1000)

/*
**    Enable/Disable debug messages.
**    Can be changed at runtime too.
*/

#ifdef SCSI_NCR_DEBUG_INFO_SUPPORT
	#define DEBUG_FLAGS ncr_debug
#else
	#define DEBUG_FLAGS	SCSI_NCR_DEBUG_FLAGS
#endif



/*==========================================================
**
**	assert ()
**
**==========================================================
**
**	modified copy from 386bsd:/usr/include/sys/assert.h
**
**----------------------------------------------------------
*/

#define	assert(expression) { \
	if (!(expression)) { \
		(void)printf(\
			"assertion \"%s\" failed: file \"%s\", line %d\n", \
			#expression, \
			__FILE__, __LINE__); \
	} \
}

/*==========================================================
**
**	Big/Little endian support.
**
**==========================================================
*/

/*
**	If the NCR uses big endian addressing mode over the 
**	PCI, actual io register addresses for byte and word 
**	accesses must be changed according to lane routing.
**	Btw, ncr_offb() and ncr_offw() macros only apply to 
**	constants and so donnot generate bloated code.
*/

#if	defined(SCSI_NCR_BIG_ENDIAN)

#define ncr_offb(o)	(((o)&~3)+((~((o)&3))&3))
#define ncr_offw(o)	(((o)&~3)+((~((o)&3))&2))

#else

#define ncr_offb(o)	(o)
#define ncr_offw(o)	(o)

#endif

/*
**	If the CPU and the NCR use same endian-ness adressing,
**	no byte reordering is needed for script patching.
**	Macro cpu_to_scr() is to be used for script patching.
**	Macro scr_to_cpu() is to be used for getting a DWORD 
**	from the script.
*/

#if	defined(__BIG_ENDIAN) && !defined(SCSI_NCR_BIG_ENDIAN)

#define cpu_to_scr(dw)	cpu_to_le32(dw)
#define scr_to_cpu(dw)	le32_to_cpu(dw)

#elif	defined(__LITTLE_ENDIAN) && defined(SCSI_NCR_BIG_ENDIAN)

#define cpu_to_scr(dw)	cpu_to_be32(dw)
#define scr_to_cpu(dw)	be32_to_cpu(dw)

#else

#define cpu_to_scr(dw)	(dw)
#define scr_to_cpu(dw)	(dw)

#endif

/*==========================================================
**
**	Access to the controller chip.
**
**	If NCR_IOMAPPED is defined, only IO are used by the driver.
**
**==========================================================
*/

/*
**	If the CPU and the NCR use same endian-ness adressing,
**	no byte reordering is needed for accessing chip io 
**	registers. Functions suffixed by '_raw' are assumed 
**	to access the chip over the PCI without doing byte 
**	reordering. Functions suffixed by '_l2b' are 
**	assumed to perform little-endian to big-endian byte 
**	reordering, those suffixed by '_b2l' blah, blah,
**	blah, ...
*/

#if defined(NCR_IOMAPPED)

/*
**	IO mapped only input / ouput
*/

#define	INB_OFF(o)		inb (np->port + ncr_offb(o))
#define	OUTB_OFF(o, val)	outb ((val), np->port + ncr_offb(o))

#if	defined(__BIG_ENDIAN) && !defined(SCSI_NCR_BIG_ENDIAN)

#define	INW_OFF(o)		inw_l2b (np->port + ncr_offw(o))
#define	INL_OFF(o)		inl_l2b (np->port + (o))

#define	OUTW_OFF(o, val)	outw_b2l ((val), np->port + ncr_offw(o))
#define	OUTL_OFF(o, val)	outl_b2l ((val), np->port + (o))

#elif	defined(__LITTLE_ENDIAN) && defined(SCSI_NCR_BIG_ENDIAN)

#define	INW_OFF(o)		inw_b2l (np->port + ncr_offw(o))
#define	INL_OFF(o)		inl_b2l (np->port + (o))

#define	OUTW_OFF(o, val)	outw_l2b ((val), np->port + ncr_offw(o))
#define	OUTL_OFF(o, val)	outl_l2b ((val), np->port + (o))

#else

#define	INW_OFF(o)		inw_raw (np->port + ncr_offw(o))
#define	INL_OFF(o)		inl_raw (np->port + (o))

#define	OUTW_OFF(o, val)	outw_raw ((val), np->port + ncr_offw(o))
#define	OUTL_OFF(o, val)	outl_raw ((val), np->port + (o))

#endif	/* ENDIANs */

#else	/* defined NCR_IOMAPPED */

/*
**	MEMORY mapped IO input / output
*/

#define INB_OFF(o)		readb((char *)np->reg + ncr_offb(o))
#define OUTB_OFF(o, val)	writeb((val), (char *)np->reg + ncr_offb(o))

#if	defined(__BIG_ENDIAN) && !defined(SCSI_NCR_BIG_ENDIAN)

#define INW_OFF(o)		readw_l2b((char *)np->reg + ncr_offw(o))
#define INL_OFF(o)		readl_l2b((char *)np->reg + (o))

#define OUTW_OFF(o, val)	writew_b2l((val), (char *)np->reg + ncr_offw(o))
#define OUTL_OFF(o, val)	writel_b2l((val), (char *)np->reg + (o))

#elif	defined(__LITTLE_ENDIAN) && defined(SCSI_NCR_BIG_ENDIAN)

#define INW_OFF(o)		readw_b2l((char *)np->reg + ncr_offw(o))
#define INL_OFF(o)		readl_b2l((char *)np->reg + (o))

#define OUTW_OFF(o, val)	writew_l2b((val), (char *)np->reg + ncr_offw(o))
#define OUTL_OFF(o, val)	writel_l2b((val), (char *)np->reg + (o))

#else

#define INW_OFF(o)		readw_raw((char *)np->reg + ncr_offw(o))
#define INL_OFF(o)		readl_raw((char *)np->reg + (o))

#define OUTW_OFF(o, val)	writew_raw((val), (char *)np->reg + ncr_offw(o))
#define OUTL_OFF(o, val)	writel_raw((val), (char *)np->reg + (o))

#endif

#endif	/* defined NCR_IOMAPPED */

#define INB(r)		INB_OFF (offsetof(struct ncr_reg,r))
#define INW(r)		INW_OFF (offsetof(struct ncr_reg,r))
#define INL(r)		INL_OFF (offsetof(struct ncr_reg,r))

#define OUTB(r, val)	OUTB_OFF (offsetof(struct ncr_reg,r), (val))
#define OUTW(r, val)	OUTW_OFF (offsetof(struct ncr_reg,r), (val))
#define OUTL(r, val)	OUTL_OFF (offsetof(struct ncr_reg,r), (val))

/*
**	Set bit field ON, OFF 
*/

#define OUTONB(r, m)	OUTB(r, INB(r) | (m))
#define OUTOFFB(r, m)	OUTB(r, INB(r) & ~(m))
#define OUTONW(r, m)	OUTW(r, INW(r) | (m))
#define OUTOFFW(r, m)	OUTW(r, INW(r) & ~(m))
#define OUTONL(r, m)	OUTL(r, INL(r) | (m))
#define OUTOFFL(r, m)	OUTL(r, INL(r) & ~(m))


/*==========================================================
**
**	Command control block states.
**
**==========================================================
*/

#define HS_IDLE		(0)
#define HS_BUSY		(1)
#define HS_NEGOTIATE	(2)	/* sync/wide data transfer*/
#define HS_DISCONNECT	(3)	/* Disconnected by target */

#define HS_COMPLETE	(4)
#define HS_SEL_TIMEOUT	(5)	/* Selection timeout      */
#define HS_RESET	(6)	/* SCSI reset	     */
#define HS_ABORTED	(7)	/* Transfer aborted       */
#define HS_TIMEOUT	(8)	/* Software timeout       */
#define HS_FAIL		(9)	/* SCSI or PCI bus errors */
#define HS_UNEXPECTED	(10)	/* Unexpected disconnect  */

#define HS_DONEMASK	(0xfc)

/*==========================================================
**
**	Software Interrupt Codes
**
**==========================================================
*/

#define	SIR_SENSE_RESTART	(1)
#define	SIR_SENSE_FAILED	(2)
#define	SIR_STALL_RESTART	(3)
#define	SIR_STALL_QUEUE		(4)
#define	SIR_NEGO_SYNC		(5)
#define	SIR_NEGO_WIDE		(6)
#define	SIR_NEGO_FAILED		(7)
#define	SIR_NEGO_PROTO		(8)
#define	SIR_REJECT_RECEIVED	(9)
#define	SIR_REJECT_SENT		(10)
#define	SIR_IGN_RESIDUE		(11)
#define	SIR_MISSING_SAVE	(12)
#define	SIR_DATA_IO_IS_OUT	(13)
#define	SIR_DATA_IO_IS_IN	(14)
#define	SIR_MAX			(14)

/*==========================================================
**
**	Extended error codes.
**	xerr_status field of struct ccb.
**
**==========================================================
*/

#define	XE_OK		(0)
#define	XE_EXTRA_DATA	(1)	/* unexpected data phase */
#define	XE_BAD_PHASE	(2)	/* illegal phase (4/5)   */

/*==========================================================
**
**	Negotiation status.
**	nego_status field	of struct ccb.
**
**==========================================================
*/

#define NS_SYNC		(1)
#define NS_WIDE		(2)

/*==========================================================
**
**	"Special features" of targets.
**	quirks field		of struct tcb.
**	actualquirks field	of struct ccb.
**
**==========================================================
*/

#define	QUIRK_AUTOSAVE	(0x01)
#define	QUIRK_NOMSG	(0x02)
#define QUIRK_NOSYNC	(0x10)
#define QUIRK_NOWIDE16	(0x20)
#define	QUIRK_UPDATE	(0x80)

/*==========================================================
**
**	Capability bits in Inquire response byte 7.
**
**==========================================================
*/

#define	INQ7_QUEUE	(0x02)
#define	INQ7_SYNC	(0x10)
#define	INQ7_WIDE16	(0x20)

/*==========================================================
**
**	Misc.
**
**==========================================================
*/

#define CCB_MAGIC	(0xf2691ad2)

/*==========================================================
**
**	Declaration of structs.
**
**==========================================================
*/

struct tcb;
struct lcb;
struct ccb;
struct ncb;
struct script;

typedef struct ncb * ncb_p;
typedef struct tcb * tcb_p;
typedef struct lcb * lcb_p;
typedef struct ccb * ccb_p;

struct link {
	ncrcmd	l_cmd;
	ncrcmd	l_paddr;
};

struct	usrcmd {
	u_long	target;
	u_long	lun;
	u_long	data;
	u_long	cmd;
};

#define UC_SETSYNC      10
#define UC_SETTAGS	11
#define UC_SETDEBUG	12
#define UC_SETORDER	13
#define UC_SETWIDE	14
#define UC_SETFLAG	15
#define UC_CLEARPROF	16

#ifdef	SCSI_NCR_DEBUG_ERROR_RECOVERY_SUPPORT
#define UC_DEBUG_ERROR_RECOVERY 17
#endif

#define	UF_TRACE	(0x01)
#define	UF_NODISC	(0x02)
#define	UF_NOSCAN	(0x04)

/*---------------------------------------
**
**	Timestamps for profiling
**
**---------------------------------------
*/

struct tstamp {
	u_long start;
	u_long end;
	u_long select;
	u_long command;
	u_long status;
	u_long disconnect;
	u_long reselect;
};

/*
**	profiling data (per device)
*/

struct profile {
	u_long	num_trans;
	u_long	num_kbytes;
	u_long	rest_bytes;
	u_long	num_disc;
	u_long	num_break;
	u_long	num_int;
	u_long	num_fly;
	u_long	ms_setup;
	u_long	ms_data;
	u_long	ms_disc;
	u_long	ms_post;
};

/*==========================================================
**
**	Declaration of structs:		target control block
**
**==========================================================
*/

struct tcb {
	/*
	**	during reselection the ncr jumps to this point
	**	with SFBR set to the encoded target number
	**	with bit 7 set.
	**	if it's not this target, jump to the next.
	**
	**	JUMP  IF (SFBR != #target#)
	**	@(next tcb)
	*/

	struct link   jump_tcb;

	/*
	**	load the actual values for the sxfer and the scntl3
	**	register (sync/wide mode).
	**
	**	SCR_COPY (1);
	**	@(sval field of this tcb)
	**	@(sxfer register)
	**	SCR_COPY (1);
	**	@(wval field of this tcb)
	**	@(scntl3 register)
	*/

	ncrcmd	getscr[6];

	/*
	**	if next message is "identify"
	**	then load the message to SFBR,
	**	else load 0 to SFBR.
	**
	**	CALL
	**	<RESEL_LUN>
	*/

	struct link   call_lun;

	/*
	**	now look for the right lun.
	**
	**	JUMP
	**	@(first ccb of this lun)
	*/

	struct link   jump_lcb;

	/*
	**	pointer to interrupted getcc ccb
	*/

	ccb_p   hold_cp;

	/*
	**	pointer to ccb used for negotiating.
	**	Avoid to start a nego for all queued commands 
	**	when tagged command queuing is enabled.
	*/

	ccb_p   nego_cp;

	/*
	**	statistical data
	*/

	u_long	transfers;
	u_long	bytes;

	/*
	**	user settable limits for sync transfer
	**	and tagged commands.
	**	These limits are read from the NVRAM if present.
	*/

	u_char	usrsync;
	u_char	usrwide;
	u_char	usrtags;
	u_char	usrflag;

	u_char	numtags;
	u_char	maxtags;
	u_short	num_good;

	/*
	**	negotiation of wide and synch transfer.
	**	device quirks.
	*/

/*0*/	u_char	minsync;
/*1*/	u_char	sval;
/*2*/	u_short	period;
/*0*/	u_char	maxoffs;

/*1*/	u_char	quirks;

/*2*/	u_char	widedone;
/*3*/	u_char	wval;
	/*
	**	inquire data
	*/
#define MAX_INQUIRE 36
	u_char	inqdata[MAX_INQUIRE];

	/*
	**	the lcb's of this tcb
	*/

	lcb_p   lp[MAX_LUN];
};

/*==========================================================
**
**	Declaration of structs:		lun control block
**
**==========================================================
*/

struct lcb {
	/*
	**	during reselection the ncr jumps to this point
	**	with SFBR set to the "Identify" message.
	**	if it's not this lun, jump to the next.
	**
	**	JUMP  IF (SFBR != #lun#)
	**	@(next lcb of this target)
	*/

	struct link	jump_lcb;

	/*
	**	if next message is "simple tag",
	**	then load the tag to SFBR,
	**	else load 0 to SFBR.
	**
	**	CALL
	**	<RESEL_TAG>
	*/

	struct link	call_tag;

	/*
	**	now look for the right ccb.
	**
	**	JUMP
	**	@(first ccb of this lun)
	*/

	struct link	jump_ccb;

	/*
	**	start of the ccb chain
	*/

	ccb_p	next_ccb;

	/*
	**	Control of tagged queueing
	*/

	u_char		reqccbs;
	u_char		actccbs;
	u_char		reqlink;
	u_char		actlink;
	u_char		usetags;
	u_char		lasttag;

	/*
	**	Linux specific fields:
	**	Number of active commands and current credit.
	**	Should be managed by the generic scsi driver
	*/

	u_char		active;
	u_char		opennings;

	/*-----------------------------------------------
	**	Flag to force M_ORDERED_TAG on next command
	**	in order to avoid spurious timeout when
	**	M_SIMPLE_TAG is used for all operations.
	**-----------------------------------------------
	*/
	u_char	force_ordered_tag;
#define NCR_TIMEOUT_INCREASE	(5*HZ)
};

/*==========================================================
**
**      Declaration of structs:     COMMAND control block
**
**==========================================================
**
**	This substructure is copied from the ccb to a
**	global address after selection (or reselection)
**	and copied back before disconnect.
**
**	These fields are accessible to the script processor.
**
**----------------------------------------------------------
*/

struct head {
	/*
	**	Execution of a ccb starts at this point.
	**	It's a jump to the "SELECT" label
	**	of the script.
	**
	**	After successful selection the script
	**	processor overwrites it with a jump to
	**	the IDLE label of the script.
	*/

	struct link	launch;

	/*
	**	Saved data pointer.
	**	Points to the position in the script
	**	responsible for the actual transfer
	**	of data.
	**	It's written after reception of a
	**	"SAVE_DATA_POINTER" message.
	**	The goalpointer points after
	**	the last transfer command.
	*/

	u_int32		savep;
	u_int32		lastp;
	u_int32		goalp;

	/*
	**	The virtual address of the ccb
	**	containing this header.
	*/

	ccb_p	cp;

	/*
	**	space for some timestamps to gather
	**	profiling data about devices and this driver.
	*/

	struct tstamp	stamp;

	/*
	**	status fields.
	*/

	u_char		scr_st[4];	/* script status */
	u_char		status[4];	/* host status. Must be the last */
					/* DWORD of the CCB header */
};

/*
**	The status bytes are used by the host and the script processor.
**
**	The byte corresponding to the host_status must be stored in the 
**	last DWORD of the CCB header since it is used for command 
**	completion (ncr_wakeup()). Doing so, we are sure that the header 
**	has been entirely copied back to the CCB when the host_status is 
**	seen complete by the CPU.
**
**	The last four bytes (status[4]) are copied to the scratchb register
**	(declared as scr0..scr3 in ncr_reg.h) just after the select/reselect,
**	and copied back just after disconnecting.
**	Inside the script the XX_REG are used.
**
**	The first four bytes (scr_st[4]) are used inside the script by 
**	"COPY" commands.
**	Because source and destination must have the same alignment
**	in a DWORD, the fields HAVE to be at the choosen offsets.
**		xerr_st		0	(0x34)	scratcha
**		sync_st		1	(0x05)	sxfer
**		wide_st		3	(0x03)	scntl3
*/

/*
**	Last four bytes (script)
*/
#define  QU_REG	scr0
#define  HS_REG	scr1
#define  HS_PRT	nc_scr1
#define  SS_REG	scr2
#define  PS_REG	scr3

/*
**	Last four bytes (host)
*/
#define  actualquirks  phys.header.status[0]
#define  host_status   phys.header.status[1]
#define  scsi_status   phys.header.status[2]
#define  parity_status phys.header.status[3]

/*
**	First four bytes (script)
*/
#define  xerr_st       header.scr_st[0]
#define  sync_st       header.scr_st[1]
#define  nego_st       header.scr_st[2]
#define  wide_st       header.scr_st[3]

/*
**	First four bytes (host)
*/
#define  xerr_status   phys.xerr_st
#define  sync_status   phys.sync_st
#define  nego_status   phys.nego_st
#define  wide_status   phys.wide_st

/*==========================================================
**
**      Declaration of structs:     Data structure block
**
**==========================================================
**
**	During execution of a ccb by the script processor,
**	the DSA (data structure address) register points
**	to this substructure of the ccb.
**	This substructure contains the header with
**	the script-processor-changable data and
**	data blocks for the indirect move commands.
**
**----------------------------------------------------------
*/

struct dsb {

	/*
	**	Header.
	**	Has to be the first entry,
	**	because it's jumped to by the
	**	script processor
	*/

	struct head	header;

	/*
	**	Table data for Script
	*/

	struct scr_tblsel  select;
	struct scr_tblmove smsg  ;
	struct scr_tblmove smsg2 ;
	struct scr_tblmove cmd   ;
	struct scr_tblmove scmd  ;
	struct scr_tblmove sense ;
	struct scr_tblmove data [MAX_SCATTER];
};

/*==========================================================
**
**      Declaration of structs:     Command control block.
**
**==========================================================
**
**	During execution of a ccb by the script processor,
**	the DSA (data structure address) register points
**	to this substructure of the ccb.
**	This substructure contains the header with
**	the script-processor-changable data and then
**	data blocks for the indirect move commands.
**
**----------------------------------------------------------
*/


struct ccb {
	/*
	**	This field forces 32 bytes alignement for phys.header,
	**	in order to use cache line bursting when copying it 
	**	to the ncb.
	*/

	struct link		filler[2];

	/*
	**	during reselection the ncr jumps to this point.
	**	If a "SIMPLE_TAG" message was received,
	**	then SFBR is set to the tag.
	**	else SFBR is set to 0
	**	If looking for another tag, jump to the next ccb.
	**
	**	JUMP  IF (SFBR != #TAG#)
	**	@(next ccb of this lun)
	*/

	struct link		jump_ccb;

	/*
	**	After execution of this call, the return address
	**	(in  the TEMP register) points to the following
	**	data structure block.
	**	So copy it to the DSA register, and start
	**	processing of this data structure.
	**
	**	CALL
	**	<RESEL_TMP>
	*/

	struct link		call_tmp;

	/*
	**	This is the data structure which is
	**	to be executed by the script processor.
	*/

	struct dsb		phys;

	/*
	**	If a data transfer phase is terminated too early
	**	(after reception of a message (i.e. DISCONNECT)),
	**	we have to prepare a mini script to transfer
	**	the rest of the data.
	*/

	ncrcmd			patch[8];

	/*
	**	The general SCSI driver provides a
	**	pointer to a control block.
	*/

	Scsi_Cmnd	*cmd;
	int data_len;

	/*
	**	We prepare a message to be sent after selection,
	**	and a second one to be sent after getcc selection.
	**      Contents are IDENTIFY and SIMPLE_TAG.
	**	While negotiating sync or wide transfer,
	**	a SDTM or WDTM message is appended.
	*/

	u_char			scsi_smsg [8];
	u_char			scsi_smsg2[8];

	/*
	**	Lock this ccb.
	**	Flag is used while looking for a free ccb.
	*/

	u_long		magic;

	/*
	**	Physical address of this instance of ccb
	*/

	u_long		p_ccb;

	/*
	**	Completion time out for this job.
	**	It's set to time of start + allowed number of seconds.
	*/

	u_long		tlimit;

	/*
	**	All ccbs of one hostadapter are chained.
	*/

	ccb_p		link_ccb;

	/*
	**	All ccbs of one target/lun are chained.
	*/

	ccb_p		next_ccb;

	/*
	**	Sense command
	*/

	u_char		sensecmd[6];

	/*
	**	Tag for this transfer.
	**	It's patched into jump_ccb.
	**	If it's not zero, a SIMPLE_TAG
	**	message is included in smsg.
	*/

	u_char			tag;

	/*
	**	Number of segments of the scatter list.
	**	Used for recalculation of savep/goalp/lastp on 
	**	SIR_DATA_IO_IS_OUT interrupt.
	*/
	
	u_char			segments;
};

#define CCB_PHYS(cp,lbl)	(cp->p_ccb + offsetof(struct ccb, lbl))

/*==========================================================
**
**      Declaration of structs:     NCR device descriptor
**
**==========================================================
*/

struct ncb {
	/*
	**	The global header.
	**	Accessible to both the host and the
	**	script-processor.
	**	Is 32 bytes aligned since ncb is, in order to 
	**	allow cache line bursting when copying it from or 
	**	to ccbs.
	*/
	struct head     header;

	/*-----------------------------------------------
	**	Specific Linux fields
	**-----------------------------------------------
	*/
	int    unit;			/* Unit number                       */
	char   chip_name[8];		/* Chip name                         */
	char   inst_name[16];		/* Instance name                     */
	struct timer_list timer;	/* Timer link header                 */
	int	ncr_cache;		/* Cache test variable               */
	Scsi_Cmnd *waiting_list;	/* Waiting list header for commands  */
					/* that we can't put into the squeue */
	u_long	settle_time;		/* Reset in progess		     */
	u_char	release_stage;		/* Synchronisation stage on release  */
	u_char	verbose;		/* Boot verbosity for this controller*/
#ifdef	SCSI_NCR_DEBUG_ERROR_RECOVERY_SUPPORT
	u_char	debug_error_recovery;
	u_char	stalling;
	u_char	assert_atn;
#endif

	/*-----------------------------------------------
	**	Added field to support differences
	**	between ncr chips.
	**	sv_xxx are some io register bit value at start-up and
	**	so assumed to have been set by the sdms bios.
	**	rv_xxx are the bit fields of io register that will keep 
	**	the features used by the driver.
	**-----------------------------------------------
	*/
	u_short	device_id;
	u_char	revision_id;

	u_char	sv_scntl0;
	u_char	sv_scntl3;
	u_char	sv_dmode;
	u_char	sv_dcntl;
	u_char	sv_ctest3;
	u_char	sv_ctest4;
	u_char	sv_ctest5;
	u_char	sv_gpcntl;
	u_char	sv_stest2;
	u_char	sv_stest4;

	u_char	rv_scntl0;
	u_char	rv_scntl3;
	u_char	rv_dmode;
	u_char	rv_dcntl;
	u_char	rv_ctest3;
	u_char	rv_ctest4;
	u_char	rv_ctest5;
	u_char	rv_stest2;

	u_char	scsi_mode;

	/*-----------------------------------------------
	**	Scripts ..
	**-----------------------------------------------
	**
	**	During reselection the ncr jumps to this point.
	**	The SFBR register is loaded with the encoded target id.
	**
	**	Jump to the first target.
	**
	**	JUMP
	**	@(next tcb)
	*/
	struct link     jump_tcb;

	/*-----------------------------------------------
	**	Configuration ..
	**-----------------------------------------------
	**
	**	virtual and physical addresses
	**	of the 53c810 chip.
	*/
	vm_offset_t     vaddr;
	vm_offset_t     paddr;

	vm_offset_t     vaddr2;
	vm_offset_t     paddr2;

	/*
	**	pointer to the chip's registers.
	*/
	volatile
	struct ncr_reg* reg;

	/*
	**	A copy of the scripts, relocated for this ncb.
	*/
	struct script	*script0;
	struct scripth	*scripth0;

	/*
	**	Scripts instance virtual address.
	*/
	struct script	*script;
	struct scripth	*scripth;

	/*
	**	Scripts instance physical address.
	*/
	u_long		p_script;
	u_long		p_scripth;

	/*
	**	The SCSI address of the host adapter.
	*/
	u_char	  myaddr;

	/*
	**	Max dwords burst supported by the adapter.
	*/
	u_char		maxburst;	/* log base 2 of dwords burst	*/

	/*
	**	timing parameters
	*/
	u_char		minsync;	/* Minimum sync period factor	*/
	u_char		maxsync;	/* Maximum sync period factor	*/
	u_char		maxoffs;	/* Max scsi offset		*/
	u_char		multiplier;	/* Clock multiplier (1,2,4)	*/
	u_char		clock_divn;	/* Number of clock divisors	*/
	u_long		clock_khz;	/* SCSI clock frequency in KHz	*/
	u_int		features;	/* Chip features map		*/


	/*-----------------------------------------------
	**	Link to the generic SCSI driver
	**-----------------------------------------------
	*/

	/* struct scsi_link	sc_link; */

	/*-----------------------------------------------
	**	Job control
	**-----------------------------------------------
	**
	**	Commands from user
	*/
	struct usrcmd	user;
	u_char		order;

	/*
	**	Target data
	*/
	struct tcb	target[MAX_TARGET];

	/*
	**	Start queue.
	*/
	u_int32		squeue [MAX_START];
	u_short		squeueput;
	u_short		actccbs;

	/*
	**	Timeout handler
	*/
#if 0
	u_long		heartbeat;
	u_short		ticks;
	u_short		latetime;
#endif
	u_long		lasttime;

	/*-----------------------------------------------
	**	Debug and profiling
	**-----------------------------------------------
	**
	**	register dump
	*/
	struct ncr_reg	regdump;
	u_long		regtime;

	/*
	**	Profiling data
	*/
	struct profile	profile;
	u_int		disc_phys;
	u_int		disc_ref;

	/*
	**	The global control block.
	**	It's used only during the configuration phase.
	**	A target control block will be created
	**	after the first successful transfer.
	*/
	struct ccb      *ccb;

	/*
	**	message buffers.
	**	Should be longword aligned,
	**	because they're written with a
	**	COPY script command.
	*/
	u_char		msgout[8];
	u_char		msgin [8];
	u_int32		lastmsg;

	/*
	**	Buffer for STATUS_IN phase.
	*/
	u_char		scratch;

	/*
	**	controller chip dependent maximal transfer width.
	*/
	u_char		maxwide;

	/*
	**	option for M_IDENTIFY message: enables disconnecting
	*/
	u_char		disc;

	/*
	**	address of the ncr control registers in io space
	*/
	u_long		port;

	/*
	**	irq level
	*/
	u_int		irq;
};

#define NCB_SCRIPT_PHYS(np,lbl)	 (np->p_script  + offsetof (struct script, lbl))
#define NCB_SCRIPTH_PHYS(np,lbl) (np->p_scripth + offsetof (struct scripth, lbl))

/*==========================================================
**
**
**      Script for NCR-Processor.
**
**	Use ncr_script_fill() to create the variable parts.
**	Use ncr_script_copy_and_bind() to make a copy and
**	bind to physical addresses.
**
**
**==========================================================
**
**	We have to know the offsets of all labels before
**	we reach them (for forward jumps).
**	Therefore we declare a struct here.
**	If you make changes inside the script,
**	DONT FORGET TO CHANGE THE LENGTHS HERE!
**
**----------------------------------------------------------
*/

/*
**	Script fragments which are loaded into the on-board RAM 
**	of 825A, 875 and 895 chips.
*/
struct script {
	ncrcmd	start		[  4];
	ncrcmd	start0		[  2];
	ncrcmd	start1		[  3];
	ncrcmd  startpos	[  1];
	ncrcmd  trysel		[  8];
	ncrcmd	skip		[  8];
	ncrcmd	skip2		[  3];
	ncrcmd  idle		[  2];
	ncrcmd	select		[ 22];
	ncrcmd	prepare		[  4];
	ncrcmd	loadpos		[ 14];
	ncrcmd	prepare2	[ 24];
	ncrcmd	setmsg		[  5];
	ncrcmd  clrack		[  2];
	ncrcmd  dispatch	[ 38];
	ncrcmd	no_data		[ 17];
	ncrcmd  checkatn	[ 10];
	ncrcmd  command		[ 15];
	ncrcmd  status		[ 27];
	ncrcmd  msg_in		[ 26];
	ncrcmd  msg_bad		[  6];
	ncrcmd  complete	[ 13];
	ncrcmd	cleanup		[ 12];
	ncrcmd	cleanup0	[ 11];
	ncrcmd	signal		[ 10];
	ncrcmd  save_dp		[  5];
	ncrcmd  restore_dp	[  5];
	ncrcmd  disconnect	[ 12];
	ncrcmd  disconnect0	[  5];
	ncrcmd  disconnect1	[ 23];
	ncrcmd	msg_out		[  9];
	ncrcmd	msg_out_done	[  7];
	ncrcmd  badgetcc	[  6];
	ncrcmd	reselect	[  8];
	ncrcmd	reselect1	[  8];
	ncrcmd	reselect2	[  8];
	ncrcmd	resel_tmp	[  5];
	ncrcmd  resel_lun	[ 18];
	ncrcmd	resel_tag	[ 24];
	ncrcmd  data_io		[  6];
	ncrcmd  data_in		[MAX_SCATTER * 4 + 4];
};

/*
**	Script fragments which stay in main memory for all chips.
*/
struct scripth {
	ncrcmd  tryloop		[MAX_START*5+2];
	ncrcmd  msg_parity	[  6];
	ncrcmd	msg_reject	[  8];
	ncrcmd	msg_ign_residue	[ 32];
	ncrcmd  msg_extended	[ 18];
	ncrcmd  msg_ext_2	[ 18];
	ncrcmd	msg_wdtr	[ 27];
	ncrcmd  msg_ext_3	[ 18];
	ncrcmd	msg_sdtr	[ 27];
	ncrcmd	msg_out_abort	[ 10];
	ncrcmd  getcc		[  4];
	ncrcmd  getcc1		[  5];
#ifdef NCR_GETCC_WITHMSG
	ncrcmd	getcc2		[ 33];
#else
	ncrcmd	getcc2		[ 14];
#endif
	ncrcmd	getcc3		[ 10];
	ncrcmd  data_out	[MAX_SCATTER * 4 + 4];
	ncrcmd	aborttag	[  4];
	ncrcmd	abort		[ 22];
	ncrcmd	snooptest	[  9];
	ncrcmd	snoopend	[  2];
};

/*==========================================================
**
**
**      Function headers.
**
**
**==========================================================
*/

static	void	ncr_alloc_ccb	(ncb_p np, u_long t, u_long l);
static	void	ncr_complete	(ncb_p np, ccb_p cp);
static	void	ncr_exception	(ncb_p np);
static	void	ncr_free_ccb	(ncb_p np, ccb_p cp, u_long t, u_long l);
static	void	ncr_getclock	(ncb_p np, int mult);
static	void	ncr_selectclock	(ncb_p np, u_char scntl3);
static	ccb_p	ncr_get_ccb	(ncb_p np, u_long t,u_long l);
static	void	ncr_init	(ncb_p np, char * msg, u_long code);
static	int	ncr_int_sbmc	(ncb_p np);
static	int	ncr_int_par	(ncb_p np);
static	void	ncr_int_ma	(ncb_p np);
static	void	ncr_int_sir	(ncb_p np);
static  void    ncr_int_sto     (ncb_p np);
static	u_long	ncr_lookup	(char* id);
static	void	ncr_negotiate	(struct ncb* np, struct tcb* tp);
static	void	ncr_opennings	(ncb_p np, lcb_p lp, Scsi_Cmnd * xp);

#ifdef SCSI_NCR_PROFILE_SUPPORT
static	void	ncb_profile	(ncb_p np, ccb_p cp);
#endif

static	void	ncr_script_copy_and_bind
				(ncb_p np, ncrcmd *src, ncrcmd *dst, int len);
static  void    ncr_script_fill (struct script * scr, struct scripth * scripth);
static	int	ncr_scatter	(ccb_p cp, Scsi_Cmnd *cmd);
static	void	ncr_setmaxtags	(ncb_p np, tcb_p tp, u_long numtags);
static	void	ncr_getsync	(ncb_p np, u_char sfac, u_char *fakp, u_char *scntl3p);
static	void	ncr_setsync	(ncb_p np, ccb_p cp, u_char scntl3, u_char sxfer);
static	void	ncr_settags     (tcb_p tp, lcb_p lp);
static	void	ncr_setwide	(ncb_p np, ccb_p cp, u_char wide, u_char ack);
static	int	ncr_show_msg	(u_char * msg);
static	int	ncr_snooptest	(ncb_p np);
static	void	ncr_timeout	(ncb_p np);
static  void    ncr_wakeup      (ncb_p np, u_long code);
static	void	ncr_start_reset	(ncb_p np, int settle_delay);
static	int	ncr_reset_scsi_bus (ncb_p np, int enab_int, int settle_delay);

#ifdef SCSI_NCR_USER_COMMAND_SUPPORT
static	void	ncr_usercmd	(ncb_p np);
#endif

static int ncr_attach (Scsi_Host_Template *tpnt, int unit, ncr_device *device);

static void insert_into_waiting_list(ncb_p np, Scsi_Cmnd *cmd);
static Scsi_Cmnd *retrieve_from_waiting_list(int to_remove, ncb_p np, Scsi_Cmnd *cmd);
static void process_waiting_list(ncb_p np, int sts);

#define remove_from_waiting_list(np, cmd) \
		retrieve_from_waiting_list(1, (np), (cmd))
#define requeue_waiting_list(np) process_waiting_list((np), DID_OK)
#define reset_waiting_list(np) process_waiting_list((np), DID_RESET)

#ifdef SCSI_NCR_NVRAM_SUPPORT
static	int	ncr_get_Symbios_nvram	(ncr_slot *np, Symbios_nvram *nvram);
static	int	ncr_get_Tekram_nvram	(ncr_slot *np, Tekram_nvram *nvram);
#endif

/*==========================================================
**
**
**      Global static data.
**
**
**==========================================================
*/

#ifdef SCSI_NCR_DEBUG_INFO_SUPPORT
static int ncr_debug = SCSI_NCR_DEBUG_FLAGS;
#endif

static inline char *ncr_name (ncb_p np)
{
	return np->inst_name;
}


/*==========================================================
**
**
**      Scripts for NCR-Processor.
**
**      Use ncr_script_bind for binding to physical addresses.
**
**
**==========================================================
**
**	NADDR generates a reference to a field of the controller data.
**	PADDR generates a reference to another part of the script.
**	RADDR generates a reference to a script processor register.
**	FADDR generates a reference to a script processor register
**		with offset.
**
**----------------------------------------------------------
*/

#define	RELOC_SOFTC	0x40000000
#define	RELOC_LABEL	0x50000000
#define	RELOC_REGISTER	0x60000000
#define	RELOC_KVAR	0x70000000
#define	RELOC_LABELH	0x80000000
#define	RELOC_MASK	0xf0000000

#define	NADDR(label)	(RELOC_SOFTC | offsetof(struct ncb, label))
#define PADDR(label)    (RELOC_LABEL | offsetof(struct script, label))
#define PADDRH(label)   (RELOC_LABELH | offsetof(struct scripth, label))
#define	RADDR(label)	(RELOC_REGISTER | REG(label))
#define	FADDR(label,ofs)(RELOC_REGISTER | ((REG(label))+(ofs)))
#define	KVAR(which)	(RELOC_KVAR | (which))

#define	SCRIPT_KVAR_JIFFIES	(0)

#define	SCRIPT_KVAR_FIRST		SCRIPT_KVAR_JIFFIES
#define	SCRIPT_KVAR_LAST		SCRIPT_KVAR_JIFFIES

/*
 * Kernel variables referenced in the scripts.
 * THESE MUST ALL BE ALIGNED TO A 4-BYTE BOUNDARY.
 */
static void *script_kvars[] __initdata =
	{ (void *)&jiffies };

static	struct script script0 __initdata = {
/*--------------------------< START >-----------------------*/ {
#if 0
	/*
	**	Claim to be still alive ...
	*/
	SCR_COPY (sizeof (((struct ncb *)0)->heartbeat)),
		KVAR(SCRIPT_KVAR_JIFFIES),
		NADDR (heartbeat),
#endif
	/*
	**      Make data structure address invalid.
	**      clear SIGP.
	*/
	SCR_LOAD_REG (dsa, 0xff),
		0,
	SCR_FROM_REG (ctest2),
		0,
}/*-------------------------< START0 >----------------------*/,{
	/*
	**	Hook for interrupted GetConditionCode.
	**	Will be patched to ... IFTRUE by
	**	the interrupt handler.
	*/
	SCR_INT ^ IFFALSE (0),
		SIR_SENSE_RESTART,

}/*-------------------------< START1 >----------------------*/,{
	/*
	**	Hook for stalled start queue.
	**	Will be patched to IFTRUE by the interrupt handler.
	*/
	SCR_INT ^ IFFALSE (0),
		SIR_STALL_RESTART,
	/*
	**	Then jump to a certain point in tryloop.
	**	Due to the lack of indirect addressing the code
	**	is self modifying here.
	*/
	SCR_JUMP,
}/*-------------------------< STARTPOS >--------------------*/,{
		PADDRH(tryloop),
}/*-------------------------< TRYSEL >----------------------*/,{
	/*
	**	Now:
	**	DSA: Address of a Data Structure
	**	or   Address of the IDLE-Label.
	**
	**	TEMP:	Address of a script, which tries to
	**		start the NEXT entry.
	**
	**	Save the TEMP register into the SCRATCHA register.
	**	Then copy the DSA to TEMP and RETURN.
	**	This is kind of an indirect jump.
	**	(The script processor has NO stack, so the
	**	CALL is actually a jump and link, and the
	**	RETURN is an indirect jump.)
	**
	**	If the slot was empty, DSA contains the address
	**	of the IDLE part of this script. The processor
	**	jumps to IDLE and waits for a reselect.
	**	It will wake up and try the same slot again
	**	after the SIGP bit becomes set by the host.
	**
	**	If the slot was not empty, DSA contains
	**	the address of the phys-part of a ccb.
	**	The processor jumps to this address.
	**	phys starts with head,
	**	head starts with launch,
	**	so actually the processor jumps to
	**	the lauch part.
	**	If the entry is scheduled for execution,
	**	then launch contains a jump to SELECT.
	**	If it's not scheduled, it contains a jump to IDLE.
	*/
	SCR_COPY (4),
		RADDR (temp),
		RADDR (scratcha),
	SCR_COPY (4),
		RADDR (dsa),
		RADDR (temp),
	SCR_RETURN,
		0

}/*-------------------------< SKIP >------------------------*/,{
	/*
	**	This entry has been canceled.
	**	Next time use the next slot.
	*/
	SCR_COPY (4),
		RADDR (scratcha),
		PADDR (startpos),
	/*
	**	patch the launch field.
	**	should look like an idle process.
	*/
	SCR_COPY_F (4),
		RADDR (dsa),
		PADDR (skip2),
	SCR_COPY (8),
		PADDR (idle),
}/*-------------------------< SKIP2 >-----------------------*/,{
		0,
	SCR_JUMP,
		PADDR(start),
}/*-------------------------< IDLE >------------------------*/,{
	/*
	**	Nothing to do?
	**	Wait for reselect.
	*/
	SCR_JUMP,
		PADDR(reselect),

}/*-------------------------< SELECT >----------------------*/,{
	/*
	**	DSA	contains the address of a scheduled
	**		data structure.
	**
	**	SCRATCHA contains the address of the script,
	**		which starts the next entry.
	**
	**	Set Initiator mode.
	**
	**	(Target mode is left as an exercise for the reader)
	*/

	SCR_CLR (SCR_TRG),
		0,
	SCR_LOAD_REG (HS_REG, 0xff),
		0,

	/*
	**      And try to select this target.
	*/
	SCR_SEL_TBL_ATN ^ offsetof (struct dsb, select),
		PADDR (reselect),

	/*
	**	Now there are 4 possibilities:
	**
	**	(1) The ncr looses arbitration.
	**	This is ok, because it will try again,
	**	when the bus becomes idle.
	**	(But beware of the timeout function!)
	**
	**	(2) The ncr is reselected.
	**	Then the script processor takes the jump
	**	to the RESELECT label.
	**
	**	(3) The ncr completes the selection.
	**	Then it will execute the next statement.
	**
	**	(4) There is a selection timeout.
	**	Then the ncr should interrupt the host and stop.
	**	Unfortunately, it seems to continue execution
	**	of the script. But it will fail with an
	**	IID-interrupt on the next WHEN.
	*/

	SCR_JUMPR ^ IFTRUE (WHEN (SCR_MSG_IN)),
		0,

	/*
	**	Save target id to ctest0 register
	*/

	SCR_FROM_REG (sdid),
		0,
	SCR_TO_REG (ctest0),
		0,
	/*
	**	Send the IDENTIFY and SIMPLE_TAG messages
	**	(and the M_X_SYNC_REQ message)
	*/
	SCR_MOVE_TBL ^ SCR_MSG_OUT,
		offsetof (struct dsb, smsg),
#ifdef undef /* XXX better fail than try to deal with this ... */
	SCR_JUMPR ^ IFTRUE (WHEN (SCR_MSG_OUT)),
		-16,
#endif
	SCR_CLR (SCR_ATN),
		0,
	SCR_COPY (1),
		RADDR (sfbr),
		NADDR (lastmsg),
	/*
	**	Selection complete.
	**	Next time use the next slot.
	*/
	SCR_COPY (4),
		RADDR (scratcha),
		PADDR (startpos),
}/*-------------------------< PREPARE >----------------------*/,{
	/*
	**      The ncr doesn't have an indirect load
	**	or store command. So we have to
	**	copy part of the control block to a
	**	fixed place, where we can access it.
	**
	**	We patch the address part of a
	**	COPY command with the DSA-register.
	*/
	SCR_COPY_F (4),
		RADDR (dsa),
		PADDR (loadpos),
	/*
	**	then we do the actual copy.
	*/
	SCR_COPY (sizeof (struct head)),
	/*
	**	continued after the next label ...
	*/

}/*-------------------------< LOADPOS >---------------------*/,{
		0,
		NADDR (header),
	/*
	**      Mark this ccb as not scheduled.
	*/
	SCR_COPY (8),
		PADDR (idle),
		NADDR (header.launch),
	/*
	**      Set a time stamp for this selection
	*/
	SCR_COPY (sizeof (u_long)),
		KVAR(SCRIPT_KVAR_JIFFIES),
		NADDR (header.stamp.select),
	/*
	**      load the savep (saved pointer) into
	**      the TEMP register (actual pointer)
	*/
	SCR_COPY (4),
		NADDR (header.savep),
		RADDR (temp),
	/*
	**      Initialize the status registers
	*/
	SCR_COPY (4),
		NADDR (header.status),
		RADDR (scr0),

}/*-------------------------< PREPARE2 >---------------------*/,{
	/*
	**      Load the synchronous mode register
	*/
	SCR_COPY (1),
		NADDR (sync_st),
		RADDR (sxfer),
	/*
	**      Load the wide mode and timing register
	*/
	SCR_COPY (1),
		NADDR (wide_st),
		RADDR (scntl3),
	/*
	**	Initialize the msgout buffer with a NOOP message.
	*/
	SCR_LOAD_REG (scratcha, M_NOOP),
		0,
	SCR_COPY (1),
		RADDR (scratcha),
		NADDR (msgout),
	SCR_COPY (1),
		RADDR (scratcha),
		NADDR (msgin),
	/*
	**	Message in phase ?
	*/
	SCR_JUMP ^ IFFALSE (WHEN (SCR_MSG_IN)),
		PADDR (dispatch),
	/*
	**	Extended or reject message ?
	*/
	SCR_FROM_REG (sbdl),
		0,
	SCR_JUMP ^ IFTRUE (DATA (M_EXTENDED)),
		PADDR (msg_in),
	SCR_JUMP ^ IFTRUE (DATA (M_REJECT)),
		PADDRH (msg_reject),
	/*
	**	normal processing
	*/
	SCR_JUMP,
		PADDR (dispatch),
}/*-------------------------< SETMSG >----------------------*/,{
	SCR_COPY (1),
		RADDR (scratcha),
		NADDR (msgout),
	SCR_SET (SCR_ATN),
		0,
}/*-------------------------< CLRACK >----------------------*/,{
	/*
	**	Terminate possible pending message phase.
	*/
	SCR_CLR (SCR_ACK),
		0,

}/*-----------------------< DISPATCH >----------------------*/,{
	SCR_FROM_REG (HS_REG),
		0,
	SCR_INT ^ IFTRUE (DATA (HS_NEGOTIATE)),
		SIR_NEGO_FAILED,
	/*
	**	remove bogus output signals
	*/
	SCR_REG_REG (socl, SCR_AND, CACK|CATN),
		0,
	SCR_RETURN ^ IFTRUE (WHEN (SCR_DATA_OUT)),
		0,
	/*
	**	DEL 397 - 53C875 Rev 3 - Part Number 609-0392410 - ITEM 4.
	**	Possible data corruption during Memory Write and Invalidate.
	**	This work-around resets the addressing logic prior to the 
	**	start of the first MOVE of a DATA IN phase.
	**	(See README.ncr53c8xx for more information)
	*/
	SCR_JUMPR ^ IFFALSE (IF (SCR_DATA_IN)),
		20,
	SCR_COPY (4),
		RADDR (scratcha),
		RADDR (scratcha),
	SCR_RETURN,
		0,

	SCR_JUMP ^ IFTRUE (IF (SCR_MSG_OUT)),
		PADDR (msg_out),
	SCR_JUMP ^ IFTRUE (IF (SCR_MSG_IN)),
		PADDR (msg_in),
	SCR_JUMP ^ IFTRUE (IF (SCR_COMMAND)),
		PADDR (command),
	SCR_JUMP ^ IFTRUE (IF (SCR_STATUS)),
		PADDR (status),
	/*
	**      Discard one illegal phase byte, if required.
	*/
	SCR_LOAD_REG (scratcha, XE_BAD_PHASE),
		0,
	SCR_COPY (1),
		RADDR (scratcha),
		NADDR (xerr_st),
	SCR_JUMPR ^ IFFALSE (IF (SCR_ILG_OUT)),
		8,
	SCR_MOVE_ABS (1) ^ SCR_ILG_OUT,
		NADDR (scratch),
	SCR_JUMPR ^ IFFALSE (IF (SCR_ILG_IN)),
		8,
	SCR_MOVE_ABS (1) ^ SCR_ILG_IN,
		NADDR (scratch),
	SCR_JUMP,
		PADDR (dispatch),

}/*-------------------------< NO_DATA >--------------------*/,{
	/*
	**	The target wants to tranfer too much data
	**	or in the wrong direction.
	**      Remember that in extended error.
	*/
	SCR_LOAD_REG (scratcha, XE_EXTRA_DATA),
		0,
	SCR_COPY (1),
		RADDR (scratcha),
		NADDR (xerr_st),
	/*
	**      Discard one data byte, if required.
	*/
	SCR_JUMPR ^ IFFALSE (WHEN (SCR_DATA_OUT)),
		8,
	SCR_MOVE_ABS (1) ^ SCR_DATA_OUT,
		NADDR (scratch),
	SCR_JUMPR ^ IFFALSE (IF (SCR_DATA_IN)),
		8,
	SCR_MOVE_ABS (1) ^ SCR_DATA_IN,
		NADDR (scratch),
	/*
	**      .. and repeat as required.
	*/
	SCR_CALL,
		PADDR (dispatch),
	SCR_JUMP,
		PADDR (no_data),
}/*-------------------------< CHECKATN >--------------------*/,{
	/*
	**	If AAP (bit 1 of scntl0 register) is set
	**	and a parity error is detected,
	**	the script processor asserts ATN.
	**
	**	The target should switch to a MSG_OUT phase
	**	to get the message.
	*/
	SCR_FROM_REG (socl),
		0,
	SCR_JUMP ^ IFFALSE (MASK (CATN, CATN)),
		PADDR (dispatch),
	/*
	**	count it
	*/
	SCR_REG_REG (PS_REG, SCR_ADD, 1),
		0,
	/*
	**	Prepare a M_ID_ERROR message
	**	(initiator detected error).
	**	The target should retry the transfer.
	*/
	SCR_LOAD_REG (scratcha, M_ID_ERROR),
		0,
	SCR_JUMP,
		PADDR (setmsg),

}/*-------------------------< COMMAND >--------------------*/,{
	/*
	**	If this is not a GETCC transfer ...
	*/
	SCR_FROM_REG (SS_REG),
		0,
/*<<<*/	SCR_JUMPR ^ IFTRUE (DATA (S_CHECK_COND)),
		28,
	/*
	**	... set a timestamp ...
	*/
	SCR_COPY (sizeof (u_long)),
		KVAR(SCRIPT_KVAR_JIFFIES),
		NADDR (header.stamp.command),
	/*
	**	... and send the command
	*/
	SCR_MOVE_TBL ^ SCR_COMMAND,
		offsetof (struct dsb, cmd),
	SCR_JUMP,
		PADDR (dispatch),
	/*
	**	Send the GETCC command
	*/
/*>>>*/	SCR_MOVE_TBL ^ SCR_COMMAND,
		offsetof (struct dsb, scmd),
	SCR_JUMP,
		PADDR (dispatch),

}/*-------------------------< STATUS >--------------------*/,{
	/*
	**	set the timestamp.
	*/
	SCR_COPY (sizeof (u_long)),
		KVAR(SCRIPT_KVAR_JIFFIES),
		NADDR (header.stamp.status),
	/*
	**	If this is a GETCC transfer,
	*/
	SCR_FROM_REG (SS_REG),
		0,
/*<<<*/	SCR_JUMPR ^ IFFALSE (DATA (S_CHECK_COND)),
		40,
	/*
	**	get the status
	*/
	SCR_MOVE_ABS (1) ^ SCR_STATUS,
		NADDR (scratch),
	/*
	**	Save status to scsi_status.
	**	Mark as complete.
	**	And wait for disconnect.
	*/
	SCR_TO_REG (SS_REG),
		0,
	SCR_REG_REG (SS_REG, SCR_OR, S_SENSE),
		0,
	SCR_LOAD_REG (HS_REG, HS_COMPLETE),
		0,
	SCR_JUMP,
		PADDR (checkatn),
	/*
	**	If it was no GETCC transfer,
	**	save the status to scsi_status.
	*/
/*>>>*/	SCR_MOVE_ABS (1) ^ SCR_STATUS,
		NADDR (scratch),
	SCR_TO_REG (SS_REG),
		0,
	/*
	**	if it was no check condition ...
	*/
	SCR_JUMP ^ IFTRUE (DATA (S_CHECK_COND)),
		PADDR (checkatn),
	/*
	**	... mark as complete.
	*/
	SCR_LOAD_REG (HS_REG, HS_COMPLETE),
		0,
	SCR_JUMP,
		PADDR (checkatn),

}/*-------------------------< MSG_IN >--------------------*/,{
	/*
	**	Get the first byte of the message
	**	and save it to SCRATCHA.
	**
	**	The script processor doesn't negate the
	**	ACK signal after this transfer.
	*/
	SCR_MOVE_ABS (1) ^ SCR_MSG_IN,
		NADDR (msgin[0]),
	/*
	**	Check for message parity error.
	*/
	SCR_TO_REG (scratcha),
		0,
	SCR_FROM_REG (socl),
		0,
	SCR_JUMP ^ IFTRUE (MASK (CATN, CATN)),
		PADDRH (msg_parity),
	SCR_FROM_REG (scratcha),
		0,
	/*
	**	Parity was ok, handle this message.
	*/
	SCR_JUMP ^ IFTRUE (DATA (M_COMPLETE)),
		PADDR (complete),
	SCR_JUMP ^ IFTRUE (DATA (M_SAVE_DP)),
		PADDR (save_dp),
	SCR_JUMP ^ IFTRUE (DATA (M_RESTORE_DP)),
		PADDR (restore_dp),
	SCR_JUMP ^ IFTRUE (DATA (M_DISCONNECT)),
		PADDR (disconnect),
	SCR_JUMP ^ IFTRUE (DATA (M_EXTENDED)),
		PADDRH (msg_extended),
	SCR_JUMP ^ IFTRUE (DATA (M_NOOP)),
		PADDR (clrack),
	SCR_JUMP ^ IFTRUE (DATA (M_REJECT)),
		PADDRH (msg_reject),
	SCR_JUMP ^ IFTRUE (DATA (M_IGN_RESIDUE)),
		PADDRH (msg_ign_residue),
	/*
	**	Rest of the messages left as
	**	an exercise ...
	**
	**	Unimplemented messages:
	**	fall through to MSG_BAD.
	*/
}/*-------------------------< MSG_BAD >------------------*/,{
	/*
	**	unimplemented message - reject it.
	*/
	SCR_INT,
		SIR_REJECT_SENT,
	SCR_LOAD_REG (scratcha, M_REJECT),
		0,
	SCR_JUMP,
		PADDR (setmsg),

}/*-------------------------< COMPLETE >-----------------*/,{
	/*
	**	Complete message.
	**
	**	If it's not the get condition code,
	**	copy TEMP register to LASTP in header.
	*/
	SCR_FROM_REG (SS_REG),
		0,
/*<<<*/	SCR_JUMPR ^ IFTRUE (MASK (S_SENSE, S_SENSE)),
		12,
	SCR_COPY (4),
		RADDR (temp),
		NADDR (header.lastp),
/*>>>*/	/*
	**	When we terminate the cycle by clearing ACK,
	**	the target may disconnect immediately.
	**
	**	We don't want to be told of an
	**	"unexpected disconnect",
	**	so we disable this feature.
	*/
	SCR_REG_REG (scntl2, SCR_AND, 0x7f),
		0,
	/*
	**	Terminate cycle ...
	*/
	SCR_CLR (SCR_ACK|SCR_ATN),
		0,
	/*
	**	... and wait for the disconnect.
	*/
	SCR_WAIT_DISC,
		0,
}/*-------------------------< CLEANUP >-------------------*/,{
	/*
	**      dsa:    Pointer to ccb
	**	      or xxxxxxFF (no ccb)
	**
	**      HS_REG:   Host-Status (<>0!)
	*/
	SCR_FROM_REG (dsa),
		0,
	SCR_JUMP ^ IFTRUE (DATA (0xff)),
		PADDR (signal),
	/*
	**      dsa is valid.
	**	save the status registers
	*/
	SCR_COPY (4),
		RADDR (scr0),
		NADDR (header.status),
	/*
	**	and copy back the header to the ccb.
	*/
	SCR_COPY_F (4),
		RADDR (dsa),
		PADDR (cleanup0),
	SCR_COPY (sizeof (struct head)),
		NADDR (header),
}/*-------------------------< CLEANUP0 >--------------------*/,{
		0,

	/*
	**	If command resulted in "check condition"
	**	status and is not yet completed,
	**	try to get the condition code.
	*/
	SCR_FROM_REG (HS_REG),
		0,
/*<<<*/	SCR_JUMPR ^ IFFALSE (MASK (0, HS_DONEMASK)),
		16,
	SCR_FROM_REG (SS_REG),
		0,
	SCR_JUMP ^ IFTRUE (DATA (S_CHECK_COND)),
		PADDRH(getcc2),
	/*
	**	And make the DSA register invalid.
	*/
/*>>>*/	SCR_LOAD_REG (dsa, 0xff), /* invalid */
		0,
}/*-------------------------< SIGNAL >----------------------*/,{
	/*
	**	if status = queue full,
	**	reinsert in startqueue and stall queue.
	*/
	SCR_FROM_REG (SS_REG),
		0,
	SCR_INT ^ IFTRUE (DATA (S_QUEUE_FULL)),
		SIR_STALL_QUEUE,
	/*
	**	if job completed ...
	*/
	SCR_FROM_REG (HS_REG),
		0,
	/*
	**	... signal completion to the host
	*/
	SCR_INT_FLY ^ IFFALSE (MASK (0, HS_DONEMASK)),
		0,
	/*
	**	Auf zu neuen Schandtaten!
	*/
	SCR_JUMP,
		PADDR(start),

}/*-------------------------< SAVE_DP >------------------*/,{
	/*
	**	SAVE_DP message:
	**	Copy TEMP register to SAVEP in header.
	*/
	SCR_COPY (4),
		RADDR (temp),
		NADDR (header.savep),
	SCR_JUMP,
		PADDR (clrack),
}/*-------------------------< RESTORE_DP >---------------*/,{
	/*
	**	RESTORE_DP message:
	**	Copy SAVEP in header to TEMP register.
	*/
	SCR_COPY (4),
		NADDR (header.savep),
		RADDR (temp),
	SCR_JUMP,
		PADDR (clrack),

}/*-------------------------< DISCONNECT >---------------*/,{
	/*
	**	If QUIRK_AUTOSAVE is set,
	**	do an "save pointer" operation.
	*/
	SCR_FROM_REG (QU_REG),
		0,
/*<<<*/	SCR_JUMPR ^ IFFALSE (MASK (QUIRK_AUTOSAVE, QUIRK_AUTOSAVE)),
		12,
	/*
	**	like SAVE_DP message:
	**	Copy TEMP register to SAVEP in header.
	*/
	SCR_COPY (4),
		RADDR (temp),
		NADDR (header.savep),
/*>>>*/	/*
	**	Check if temp==savep or temp==goalp:
	**	if not, log a missing save pointer message.
	**	In fact, it's a comparison mod 256.
	**
	**	Hmmm, I hadn't thought that I would be urged to
	**	write this kind of ugly self modifying code.
	**
	**	It's unbelievable, but the ncr53c8xx isn't able
	**	to subtract one register from another.
	*/
	SCR_FROM_REG (temp),
		0,
	/*
	**	You are not expected to understand this ..
	**
	**	CAUTION: only little endian architectures supported! XXX
	*/
	SCR_COPY_F (1),
		NADDR (header.savep),
		PADDR (disconnect0),
}/*-------------------------< DISCONNECT0 >--------------*/,{
/*<<<*/	SCR_JUMPR ^ IFTRUE (DATA (1)),
		20,
	/*
	**	neither this
	*/
	SCR_COPY_F (1),
		NADDR (header.goalp),
		PADDR (disconnect1),
}/*-------------------------< DISCONNECT1 >--------------*/,{
	SCR_INT ^ IFFALSE (DATA (1)),
		SIR_MISSING_SAVE,
/*>>>*/

	/*
	**	DISCONNECTing  ...
	**
	**	disable the "unexpected disconnect" feature,
	**	and remove the ACK signal.
	*/
	SCR_REG_REG (scntl2, SCR_AND, 0x7f),
		0,
	SCR_CLR (SCR_ACK|SCR_ATN),
		0,
	/*
	**	Wait for the disconnect.
	*/
	SCR_WAIT_DISC,
		0,
	/*
	**	Profiling:
	**	Set a time stamp,
	**	and count the disconnects.
	*/
	SCR_COPY (sizeof (u_long)),
		KVAR(SCRIPT_KVAR_JIFFIES),
		NADDR (header.stamp.disconnect),
	SCR_COPY (4),
		NADDR (disc_phys),
		RADDR (temp),
	SCR_REG_REG (temp, SCR_ADD, 0x01),
		0,
	SCR_COPY (4),
		RADDR (temp),
		NADDR (disc_phys),
	/*
	**	Status is: DISCONNECTED.
	*/
	SCR_LOAD_REG (HS_REG, HS_DISCONNECT),
		0,
	SCR_JUMP,
		PADDR (cleanup),

}/*-------------------------< MSG_OUT >-------------------*/,{
	/*
	**	The target requests a message.
	*/
	SCR_MOVE_ABS (1) ^ SCR_MSG_OUT,
		NADDR (msgout),
	SCR_COPY (1),
		RADDR (sfbr),
		NADDR (lastmsg),
	/*
	**	If it was no ABORT message ...
	*/
	SCR_JUMP ^ IFTRUE (DATA (M_ABORT)),
		PADDRH (msg_out_abort),
	/*
	**	... wait for the next phase
	**	if it's a message out, send it again, ...
	*/
	SCR_JUMP ^ IFTRUE (WHEN (SCR_MSG_OUT)),
		PADDR (msg_out),
}/*-------------------------< MSG_OUT_DONE >--------------*/,{
	/*
	**	... else clear the message ...
	*/
	SCR_LOAD_REG (scratcha, M_NOOP),
		0,
	SCR_COPY (4),
		RADDR (scratcha),
		NADDR (msgout),
	/*
	**	... and process the next phase
	*/
	SCR_JUMP,
		PADDR (dispatch),
}/*------------------------< BADGETCC >---------------------*/,{
	/*
	**	If SIGP was set, clear it and try again.
	*/
	SCR_FROM_REG (ctest2),
		0,
	SCR_JUMP ^ IFTRUE (MASK (CSIGP,CSIGP)),
		PADDRH (getcc2),
	SCR_INT,
		SIR_SENSE_FAILED,
}/*-------------------------< RESELECT >--------------------*/,{
	/*
	**	This NOP will be patched with LED OFF
	**	SCR_REG_REG (gpreg, SCR_OR, 0x01)
	*/
	SCR_NO_OP,
		0,
	/*
	**	make the DSA invalid.
	*/
	SCR_LOAD_REG (dsa, 0xff),
		0,
	SCR_CLR (SCR_TRG),
		0,
	/*
	**	Sleep waiting for a reselection.
	**	If SIGP is set, special treatment.
	**
	**	Zu allem bereit ..
	*/
	SCR_WAIT_RESEL,
		PADDR(reselect2),
}/*-------------------------< RESELECT1 >--------------------*/,{
	/*
	**	This NOP will be patched with LED ON
	**	SCR_REG_REG (gpreg, SCR_AND, 0xfe)
	*/
	SCR_NO_OP,
		0,
	/*
	**	... zu nichts zu gebrauchen ?
	**
	**      load the target id into the SFBR
	**	and jump to the control block.
	**
	**	Look at the declarations of
	**	- struct ncb
	**	- struct tcb
	**	- struct lcb
	**	- struct ccb
	**	to understand what's going on.
	*/
	SCR_REG_SFBR (ssid, SCR_AND, 0x8F),
		0,
	SCR_TO_REG (ctest0),
		0,
	SCR_JUMP,
		NADDR (jump_tcb),
}/*-------------------------< RESELECT2 >-------------------*/,{
	/*
	**	This NOP will be patched with LED ON
	**	SCR_REG_REG (gpreg, SCR_AND, 0xfe)
	*/
	SCR_NO_OP,
		0,
	/*
	**	If it's not connected :(
	**	-> interrupted by SIGP bit.
	**	Jump to start.
	*/
	SCR_FROM_REG (ctest2),
		0,
	SCR_JUMP ^ IFTRUE (MASK (CSIGP,CSIGP)),
		PADDR (start),
	SCR_JUMP,
		PADDR (reselect),

}/*-------------------------< RESEL_TMP >-------------------*/,{
	/*
	**	The return address in TEMP
	**	is in fact the data structure address,
	**	so copy it to the DSA register.
	*/
	SCR_COPY (4),
		RADDR (temp),
		RADDR (dsa),
	SCR_JUMP,
		PADDR (prepare),

}/*-------------------------< RESEL_LUN >-------------------*/,{
	/*
	**	come back to this point
	**	to get an IDENTIFY message
	**	Wait for a msg_in phase.
	*/
/*<<<*/	SCR_JUMPR ^ IFFALSE (WHEN (SCR_MSG_IN)),
		48,
	/*
	**	message phase
	**	It's not a sony, it's a trick:
	**	read the data without acknowledging it.
	*/
	SCR_FROM_REG (sbdl),
		0,
/*<<<*/	SCR_JUMPR ^ IFFALSE (MASK (M_IDENTIFY, 0x98)),
		32,
	/*
	**	It WAS an Identify message.
	**	get it and ack it!
	*/
	SCR_MOVE_ABS (1) ^ SCR_MSG_IN,
		NADDR (msgin),
	SCR_CLR (SCR_ACK),
		0,
	/*
	**	Mask out the lun.
	*/
	SCR_REG_REG (sfbr, SCR_AND, 0x07),
		0,
	SCR_RETURN,
		0,
	/*
	**	No message phase or no IDENTIFY message:
	**	return 0.
	*/
/*>>>*/	SCR_LOAD_SFBR (0),
		0,
	SCR_RETURN,
		0,

}/*-------------------------< RESEL_TAG >-------------------*/,{
	/*
	**	come back to this point
	**	to get a SIMPLE_TAG message
	**	Wait for a MSG_IN phase.
	*/
/*<<<*/	SCR_JUMPR ^ IFFALSE (WHEN (SCR_MSG_IN)),
		64,
	/*
	**	message phase
	**	It's a trick - read the data
	**	without acknowledging it.
	*/
	SCR_FROM_REG (sbdl),
		0,
/*<<<*/	SCR_JUMPR ^ IFFALSE (DATA (M_SIMPLE_TAG)),
		48,
	/*
	**	It WAS a SIMPLE_TAG message.
	**	get it and ack it!
	*/
	SCR_MOVE_ABS (1) ^ SCR_MSG_IN,
		NADDR (msgin),
	SCR_CLR (SCR_ACK),
		0,
	/*
	**	Wait for the second byte (the tag)
	*/
/*<<<*/	SCR_JUMPR ^ IFFALSE (WHEN (SCR_MSG_IN)),
		24,
	/*
	**	Get it and ack it!
	*/
	SCR_MOVE_ABS (1) ^ SCR_MSG_IN,
		NADDR (msgin),
	SCR_CLR (SCR_ACK|SCR_CARRY),
		0,
	SCR_RETURN,
		0,
	/*
	**	No message phase or no SIMPLE_TAG message
	**	or no second byte: return 0.
	*/
/*>>>*/	SCR_LOAD_SFBR (0),
		0,
	SCR_SET (SCR_CARRY),
		0,
	SCR_RETURN,
		0,

}/*-------------------------< DATA_IO >--------------------*/,{
/*
**	Because Linux does not provide xfer data direction 
**	to low-level scsi drivers, we must trust the target 
**	for actual data direction when we cannot guess it.
**	The programmed interrupt patches savep, lastp, goalp,
**	etc.., and restarts the scsi script at data_out/in.
*/
	SCR_INT ^ IFTRUE (WHEN (SCR_DATA_OUT)),
		SIR_DATA_IO_IS_OUT,
	SCR_INT ^ IFTRUE (WHEN (SCR_DATA_IN)),
		SIR_DATA_IO_IS_IN,
	SCR_JUMP,
		PADDR (no_data),

}/*-------------------------< DATA_IN >--------------------*/,{
/*
**	Because the size depends on the
**	#define MAX_SCATTER parameter,
**	it is filled in at runtime.
**
**  ##===========< i=0; i<MAX_SCATTER >=========
**  ||	SCR_CALL ^ IFFALSE (WHEN (SCR_DATA_IN)),
**  ||		PADDR (checkatn),
**  ||	SCR_MOVE_TBL ^ SCR_DATA_IN,
**  ||		offsetof (struct dsb, data[ i]),
**  ##==========================================
**
**	SCR_CALL,
**		PADDR (checkatn),
**	SCR_JUMP,
**		PADDR (no_data),
*/
0
}/*--------------------------------------------------------*/
};

static	struct scripth scripth0 __initdata = {
/*-------------------------< TRYLOOP >---------------------*/{
/*
**	Load an entry of the start queue into dsa
**	and try to start it by jumping to TRYSEL.
**
**	Because the size depends on the
**	#define MAX_START parameter, it is filled
**	in at runtime.
**
**-----------------------------------------------------------
**
**  ##===========< I=0; i<MAX_START >===========
**  ||	SCR_COPY (4),
**  ||		NADDR (squeue[i]),
**  ||		RADDR (dsa),
**  ||	SCR_CALL,
**  ||		PADDR (trysel),
**  ##==========================================
**
**	SCR_JUMP,
**		PADDRH(tryloop),
**
**-----------------------------------------------------------
*/
0
},/*-------------------------< MSG_PARITY >---------------*/{
	/*
	**	count it
	*/
	SCR_REG_REG (PS_REG, SCR_ADD, 0x01),
		0,
	/*
	**	send a "message parity error" message.
	*/
	SCR_LOAD_REG (scratcha, M_PARITY),
		0,
	SCR_JUMP,
		PADDR (setmsg),
}/*-------------------------< MSG_REJECT >---------------*/,{
	/*
	**	If a negotiation was in progress,
	**	negotiation failed.
	*/
	SCR_FROM_REG (HS_REG),
		0,
	SCR_INT ^ IFTRUE (DATA (HS_NEGOTIATE)),
		SIR_NEGO_FAILED,
	/*
	**	else make host log this message
	*/
	SCR_INT ^ IFFALSE (DATA (HS_NEGOTIATE)),
		SIR_REJECT_RECEIVED,
	SCR_JUMP,
		PADDR (clrack),

}/*-------------------------< MSG_IGN_RESIDUE >----------*/,{
	/*
	**	Terminate cycle
	*/
	SCR_CLR (SCR_ACK),
		0,
	SCR_JUMP ^ IFFALSE (WHEN (SCR_MSG_IN)),
		PADDR (dispatch),
	/*
	**	get residue size.
	*/
	SCR_MOVE_ABS (1) ^ SCR_MSG_IN,
		NADDR (msgin[1]),
	/*
	**	Check for message parity error.
	*/
	SCR_TO_REG (scratcha),
		0,
	SCR_FROM_REG (socl),
		0,
	SCR_JUMP ^ IFTRUE (MASK (CATN, CATN)),
		PADDRH (msg_parity),
	SCR_FROM_REG (scratcha),
		0,
	/*
	**	Size is 0 .. ignore message.
	*/
	SCR_JUMP ^ IFTRUE (DATA (0)),
		PADDR (clrack),
	/*
	**	Size is not 1 .. have to interrupt.
	*/
/*<<<*/	SCR_JUMPR ^ IFFALSE (DATA (1)),
		40,
	/*
	**	Check for residue byte in swide register
	*/
	SCR_FROM_REG (scntl2),
		0,
/*<<<*/	SCR_JUMPR ^ IFFALSE (MASK (WSR, WSR)),
		16,
	/*
	**	There IS data in the swide register.
	**	Discard it.
	*/
	SCR_REG_REG (scntl2, SCR_OR, WSR),
		0,
	SCR_JUMP,
		PADDR (clrack),
	/*
	**	Load again the size to the sfbr register.
	*/
/*>>>*/	SCR_FROM_REG (scratcha),
		0,
/*>>>*/	SCR_INT,
		SIR_IGN_RESIDUE,
	SCR_JUMP,
		PADDR (clrack),

}/*-------------------------< MSG_EXTENDED >-------------*/,{
	/*
	**	Terminate cycle
	*/
	SCR_CLR (SCR_ACK),
		0,
	SCR_JUMP ^ IFFALSE (WHEN (SCR_MSG_IN)),
		PADDR (dispatch),
	/*
	**	get length.
	*/
	SCR_MOVE_ABS (1) ^ SCR_MSG_IN,
		NADDR (msgin[1]),
	/*
	**	Check for message parity error.
	*/
	SCR_TO_REG (scratcha),
		0,
	SCR_FROM_REG (socl),
		0,
	SCR_JUMP ^ IFTRUE (MASK (CATN, CATN)),
		PADDRH (msg_parity),
	SCR_FROM_REG (scratcha),
		0,
	/*
	*/
	SCR_JUMP ^ IFTRUE (DATA (3)),
		PADDRH (msg_ext_3),
	SCR_JUMP ^ IFFALSE (DATA (2)),
		PADDR (msg_bad),
}/*-------------------------< MSG_EXT_2 >----------------*/,{
	SCR_CLR (SCR_ACK),
		0,
	SCR_JUMP ^ IFFALSE (WHEN (SCR_MSG_IN)),
		PADDR (dispatch),
	/*
	**	get extended message code.
	*/
	SCR_MOVE_ABS (1) ^ SCR_MSG_IN,
		NADDR (msgin[2]),
	/*
	**	Check for message parity error.
	*/
	SCR_TO_REG (scratcha),
		0,
	SCR_FROM_REG (socl),
		0,
	SCR_JUMP ^ IFTRUE (MASK (CATN, CATN)),
		PADDRH (msg_parity),
	SCR_FROM_REG (scratcha),
		0,
	SCR_JUMP ^ IFTRUE (DATA (M_X_WIDE_REQ)),
		PADDRH (msg_wdtr),
	/*
	**	unknown extended message
	*/
	SCR_JUMP,
		PADDR (msg_bad)
}/*-------------------------< MSG_WDTR >-----------------*/,{
	SCR_CLR (SCR_ACK),
		0,
	SCR_JUMP ^ IFFALSE (WHEN (SCR_MSG_IN)),
		PADDR (dispatch),
	/*
	**	get data bus width
	*/
	SCR_MOVE_ABS (1) ^ SCR_MSG_IN,
		NADDR (msgin[3]),
	SCR_FROM_REG (socl),
		0,
	SCR_JUMP ^ IFTRUE (MASK (CATN, CATN)),
		PADDRH (msg_parity),
	/*
	**	let the host do the real work.
	*/
	SCR_INT,
		SIR_NEGO_WIDE,
	/*
	**	let the target fetch our answer.
	*/
	SCR_SET (SCR_ATN),
		0,
	SCR_CLR (SCR_ACK),
		0,

	SCR_INT ^ IFFALSE (WHEN (SCR_MSG_OUT)),
		SIR_NEGO_PROTO,
	/*
	**	Send the M_X_WIDE_REQ
	*/
	SCR_MOVE_ABS (4) ^ SCR_MSG_OUT,
		NADDR (msgout),
	SCR_CLR (SCR_ATN),
		0,
	SCR_COPY (1),
		RADDR (sfbr),
		NADDR (lastmsg),
	SCR_JUMP,
		PADDR (msg_out_done),

}/*-------------------------< MSG_EXT_3 >----------------*/,{
	SCR_CLR (SCR_ACK),
		0,
	SCR_JUMP ^ IFFALSE (WHEN (SCR_MSG_IN)),
		PADDR (dispatch),
	/*
	**	get extended message code.
	*/
	SCR_MOVE_ABS (1) ^ SCR_MSG_IN,
		NADDR (msgin[2]),
	/*
	**	Check for message parity error.
	*/
	SCR_TO_REG (scratcha),
		0,
	SCR_FROM_REG (socl),
		0,
	SCR_JUMP ^ IFTRUE (MASK (CATN, CATN)),
		PADDRH (msg_parity),
	SCR_FROM_REG (scratcha),
		0,
	SCR_JUMP ^ IFTRUE (DATA (M_X_SYNC_REQ)),
		PADDRH (msg_sdtr),
	/*
	**	unknown extended message
	*/
	SCR_JUMP,
		PADDR (msg_bad)

}/*-------------------------< MSG_SDTR >-----------------*/,{
	SCR_CLR (SCR_ACK),
		0,
	SCR_JUMP ^ IFFALSE (WHEN (SCR_MSG_IN)),
		PADDR (dispatch),
	/*
	**	get period and offset
	*/
	SCR_MOVE_ABS (2) ^ SCR_MSG_IN,
		NADDR (msgin[3]),
	SCR_FROM_REG (socl),
		0,
	SCR_JUMP ^ IFTRUE (MASK (CATN, CATN)),
		PADDRH (msg_parity),
	/*
	**	let the host do the real work.
	*/
	SCR_INT,
		SIR_NEGO_SYNC,
	/*
	**	let the target fetch our answer.
	*/
	SCR_SET (SCR_ATN),
		0,
	SCR_CLR (SCR_ACK),
		0,

	SCR_INT ^ IFFALSE (WHEN (SCR_MSG_OUT)),
		SIR_NEGO_PROTO,
	/*
	**	Send the M_X_SYNC_REQ
	*/
	SCR_MOVE_ABS (5) ^ SCR_MSG_OUT,
		NADDR (msgout),
	SCR_CLR (SCR_ATN),
		0,
	SCR_COPY (1),
		RADDR (sfbr),
		NADDR (lastmsg),
	SCR_JUMP,
		PADDR (msg_out_done),

}/*-------------------------< MSG_OUT_ABORT >-------------*/,{
	/*
	**	After ABORT message,
	**
	**	expect an immediate disconnect, ...
	*/
	SCR_REG_REG (scntl2, SCR_AND, 0x7f),
		0,
	SCR_CLR (SCR_ACK|SCR_ATN),
		0,
	SCR_WAIT_DISC,
		0,
	/*
	**	... and set the status to "ABORTED"
	*/
	SCR_LOAD_REG (HS_REG, HS_ABORTED),
		0,
	SCR_JUMP,
		PADDR (cleanup),

}/*-------------------------< GETCC >-----------------------*/,{
	/*
	**	The ncr doesn't have an indirect load
	**	or store command. So we have to
	**	copy part of the control block to a
	**	fixed place, where we can modify it.
	**
	**	We patch the address part of a COPY command
	**	with the address of the dsa register ...
	*/
	SCR_COPY_F (4),
		RADDR (dsa),
		PADDRH (getcc1),
	/*
	**	... then we do the actual copy.
	*/
	SCR_COPY (sizeof (struct head)),
}/*-------------------------< GETCC1 >----------------------*/,{
		0,
		NADDR (header),
	/*
	**	Initialize the status registers
	*/
	SCR_COPY (4),
		NADDR (header.status),
		RADDR (scr0),
}/*-------------------------< GETCC2 >----------------------*/,{
	/*
	**	Get the condition code from a target.
	**
	**	DSA points to a data structure.
	**	Set TEMP to the script location
	**	that receives the condition code.
	**
	**	Because there is no script command
	**	to load a longword into a register,
	**	we use a CALL command.
	*/
/*<<<*/	SCR_CALLR,
		24,
	/*
	**	Get the condition code.
	*/
	SCR_MOVE_TBL ^ SCR_DATA_IN,
		offsetof (struct dsb, sense),
	/*
	**	No data phase may follow!
	*/
	SCR_CALL,
		PADDR (checkatn),
	SCR_JUMP,
		PADDR (no_data),
/*>>>*/

	/*
	**	The CALL jumps to this point.
	**	Prepare for a RESTORE_POINTER message.
	**	Save the TEMP register into the saved pointer.
	*/
	SCR_COPY (4),
		RADDR (temp),
		NADDR (header.savep),
	/*
	**	Load scratcha, because in case of a selection timeout,
	**	the host will expect a new value for startpos in
	**	the scratcha register.
	*/
	SCR_COPY (4),
		PADDR (startpos),
		RADDR (scratcha),
#ifdef NCR_GETCC_WITHMSG
	/*
	**	If QUIRK_NOMSG is set, select without ATN.
	**	and don't send a message.
	*/
	SCR_FROM_REG (QU_REG),
		0,
	SCR_JUMP ^ IFTRUE (MASK (QUIRK_NOMSG, QUIRK_NOMSG)),
		PADDRH(getcc3),
	/*
	**	Then try to connect to the target.
	**	If we are reselected, special treatment
	**	of the current job is required before
	**	accepting the reselection.
	*/
	SCR_SEL_TBL_ATN ^ offsetof (struct dsb, select),
		PADDR(badgetcc),
	/*
	**	save target id.
	*/
	SCR_FROM_REG (sdid),
		0,
	SCR_TO_REG (ctest0),
		0,
	/*
	**	Send the IDENTIFY message.
	**	In case of short transfer, remove ATN.
	*/
	SCR_MOVE_TBL ^ SCR_MSG_OUT,
		offsetof (struct dsb, smsg2),
	SCR_CLR (SCR_ATN),
		0,
	/*
	**	save the first byte of the message.
	*/
	SCR_COPY (1),
		RADDR (sfbr),
		NADDR (lastmsg),
	SCR_JUMP,
		PADDR (prepare2),

#endif
}/*-------------------------< GETCC3 >----------------------*/,{
	/*
	**	Try to connect to the target.
	**	If we are reselected, special treatment
	**	of the current job is required before
	**	accepting the reselection.
	**
	**	Silly target won't accept a message.
	**	Select without ATN.
	*/
	SCR_SEL_TBL ^ offsetof (struct dsb, select),
		PADDR(badgetcc),
	/*
	**	save target id.
	*/
	SCR_FROM_REG (sdid),
		0,
	SCR_TO_REG (ctest0),
		0,
	/*
	**	Force error if selection timeout
	*/
	SCR_JUMPR ^ IFTRUE (WHEN (SCR_MSG_IN)),
		0,
	/*
	**	don't negotiate.
	*/
	SCR_JUMP,
		PADDR (prepare2),

}/*-------------------------< DATA_OUT >-------------------*/,{
/*
**	Because the size depends on the
**	#define MAX_SCATTER parameter,
**	it is filled in at runtime.
**
**  ##===========< i=0; i<MAX_SCATTER >=========
**  ||	SCR_CALL ^ IFFALSE (WHEN (SCR_DATA_OUT)),
**  ||		PADDR (dispatch),
**  ||	SCR_MOVE_TBL ^ SCR_DATA_OUT,
**  ||		offsetof (struct dsb, data[ i]),
**  ##==========================================
**
**	SCR_CALL,
**		PADDR (dispatch),
**	SCR_JUMP,
**		PADDR (no_data),
**
**---------------------------------------------------------
*/
0
}/*-------------------------< ABORTTAG >-------------------*/,{
	/*
	**      Abort a bad reselection.
	**	Set the message to ABORT vs. ABORT_TAG
	*/
	SCR_LOAD_REG (scratcha, M_ABORT_TAG),
		0,
	SCR_JUMPR ^ IFFALSE (CARRYSET),
		8,
}/*-------------------------< ABORT >----------------------*/,{
	SCR_LOAD_REG (scratcha, M_ABORT),
		0,
	SCR_COPY (1),
		RADDR (scratcha),
		NADDR (msgout),
	SCR_SET (SCR_ATN),
		0,
	SCR_CLR (SCR_ACK),
		0,
	/*
	**	and send it.
	**	we expect an immediate disconnect
	*/
	SCR_REG_REG (scntl2, SCR_AND, 0x7f),
		0,
	SCR_MOVE_ABS (1) ^ SCR_MSG_OUT,
		NADDR (msgout),
	SCR_COPY (1),
		RADDR (sfbr),
		NADDR (lastmsg),
	SCR_CLR (SCR_ACK|SCR_ATN),
		0,
	SCR_WAIT_DISC,
		0,
	SCR_JUMP,
		PADDR (start),
}/*-------------------------< SNOOPTEST >-------------------*/,{
	/*
	**	Read the variable.
	*/
	SCR_COPY (4),
		NADDR(ncr_cache),
		RADDR (scratcha),
	/*
	**	Write the variable.
	*/
	SCR_COPY (4),
		RADDR (temp),
		NADDR(ncr_cache),
	/*
	**	Read back the variable.
	*/
	SCR_COPY (4),
		NADDR(ncr_cache),
		RADDR (temp),
}/*-------------------------< SNOOPEND >-------------------*/,{
	/*
	**	And stop.
	*/
	SCR_INT,
		99,
}/*--------------------------------------------------------*/
};

/*==========================================================
**
**
**	Fill in #define dependent parts of the script
**
**
**==========================================================
*/

__initfunc(
void ncr_script_fill (struct script * scr, struct scripth * scrh)
)
{
	int	i;
	ncrcmd	*p;

	p = scrh->tryloop;
	for (i=0; i<MAX_START; i++) {
		*p++ =SCR_COPY (4);
		*p++ =NADDR (squeue[i]);
		*p++ =RADDR (dsa);
		*p++ =SCR_CALL;
		*p++ =PADDR (trysel);
	};
	*p++ =SCR_JUMP;
	*p++ =PADDRH(tryloop);

	assert ((u_long)p == (u_long)&scrh->tryloop + sizeof (scrh->tryloop));

	p = scr->data_in;

	for (i=0; i<MAX_SCATTER; i++) {
		*p++ =SCR_CALL ^ IFFALSE (WHEN (SCR_DATA_IN));
		*p++ =PADDR (checkatn);
		*p++ =SCR_MOVE_TBL ^ SCR_DATA_IN;
		*p++ =offsetof (struct dsb, data[i]);
	};

	*p++ =SCR_CALL;
	*p++ =PADDR (checkatn);
	*p++ =SCR_JUMP;
	*p++ =PADDR (no_data);

	assert ((u_long)p == (u_long)&scr->data_in + sizeof (scr->data_in));

	p = scrh->data_out;

	for (i=0; i<MAX_SCATTER; i++) {
		*p++ =SCR_CALL ^ IFFALSE (WHEN (SCR_DATA_OUT));
		*p++ =PADDR (dispatch);
		*p++ =SCR_MOVE_TBL ^ SCR_DATA_OUT;
		*p++ =offsetof (struct dsb, data[i]);
	};

	*p++ =SCR_CALL;
	*p++ =PADDR (dispatch);
	*p++ =SCR_JUMP;
	*p++ =PADDR (no_data);

	assert ((u_long)p == (u_long)&scrh->data_out + sizeof (scrh->data_out));
}

/*==========================================================
**
**
**	Copy and rebind a script.
**
**
**==========================================================
*/

__initfunc(
static void ncr_script_copy_and_bind (ncb_p np, ncrcmd *src, ncrcmd *dst, int len)
)
{
	ncrcmd  opcode, new, old, tmp1, tmp2;
	ncrcmd	*start, *end;
	int relocs;
	int opchanged = 0;

	start = src;
	end = src + len/4;

	while (src < end) {

		opcode = *src++;
		*dst++ = cpu_to_scr(opcode);

		/*
		**	If we forget to change the length
		**	in struct script, a field will be
		**	padded with 0. This is an illegal
		**	command.
		*/

		if (opcode == 0) {
			printf ("%s: ERROR0 IN SCRIPT at %d.\n",
				ncr_name(np), (int) (src-start-1));
			DELAY (1000000);
		};

		if (DEBUG_FLAGS & DEBUG_SCRIPT)
			printf ("%p:  <%x>\n",
				(src-1), (unsigned)opcode);

		/*
		**	We don't have to decode ALL commands
		*/
		switch (opcode >> 28) {

		case 0xc:
			/*
			**	COPY has TWO arguments.
			*/
			relocs = 2;
			tmp1 = src[0];
			if ((tmp1 & RELOC_MASK) == RELOC_KVAR)
				tmp1 = 0;
			tmp2 = src[1];
			if ((tmp2 & RELOC_MASK) == RELOC_KVAR)
				tmp2 = 0;
			if ((tmp1 ^ tmp2) & 3) {
				printf ("%s: ERROR1 IN SCRIPT at %d.\n",
					ncr_name(np), (int) (src-start-1));
				DELAY (1000000);
			}
			/*
			**	If PREFETCH feature not enabled, remove 
			**	the NO FLUSH bit if present.
			*/
			if ((opcode & SCR_NO_FLUSH) && !(np->features & FE_PFEN)) {
				dst[-1] = cpu_to_scr(opcode & ~SCR_NO_FLUSH);
				++opchanged;
			}
			break;

		case 0x0:
			/*
			**	MOVE (absolute address)
			*/
			relocs = 1;
			break;

		case 0x8:
			/*
			**	JUMP / CALL
			**	dont't relocate if relative :-)
			*/
			if (opcode & 0x00800000)
				relocs = 0;
			else
				relocs = 1;
			break;

		case 0x4:
		case 0x5:
		case 0x6:
		case 0x7:
			relocs = 1;
			break;

		default:
			relocs = 0;
			break;
		};

		if (relocs) {
			while (relocs--) {
				old = *src++;

				switch (old & RELOC_MASK) {
				case RELOC_REGISTER:
					new = (old & ~RELOC_MASK)
							+ pcivtophys(np->paddr);
					break;
				case RELOC_LABEL:
					new = (old & ~RELOC_MASK) + np->p_script;
					break;
				case RELOC_LABELH:
					new = (old & ~RELOC_MASK) + np->p_scripth;
					break;
				case RELOC_SOFTC:
					new = (old & ~RELOC_MASK) + vtophys(np);
					break;
				case RELOC_KVAR:
					if (((old & ~RELOC_MASK) <
					     SCRIPT_KVAR_FIRST) ||
					    ((old & ~RELOC_MASK) >
					     SCRIPT_KVAR_LAST))
						panic("ncr KVAR out of range");
					new = vtophys(script_kvars[old &
					    ~RELOC_MASK]);
					break;
				case 0:
					/* Don't relocate a 0 address. */
					if (old == 0) {
						new = old;
						break;
					}
					/* fall through */
				default:
					panic("ncr_script_copy_and_bind: weird relocation %x\n", old);
					break;
				}

				*dst++ = cpu_to_scr(new);
			}
		} else
			*dst++ = cpu_to_scr(*src++);

	};
	if (bootverbose > 1 && opchanged)
		printf("%s: NO FLUSH bit removed from %d script instructions\n",
			ncr_name(np), opchanged); 
}

/*==========================================================
**
**
**      Auto configuration:  attach and init a host adapter.
**
**
**==========================================================
*/

/*
**	Linux host data structure
**
**	The script area is allocated in the host data structure
**	because kmalloc() returns NULL during scsi initialisations
**	with Linux 1.2.X
*/

struct host_data {
     struct ncb *ncb;

     char ncb_align[NCB_ALIGN_SIZE-1];	/* Filler for alignment */
     struct ncb _ncb_data;

     char ccb_align[CCB_ALIGN_SIZE-1];	/* Filler for alignment */
     struct ccb _ccb_data;

     char scr_align[SCR_ALIGN_SIZE-1];	/* Filler for alignment */
     struct script script_data;

     struct scripth scripth_data;
};

/*
**	Print something which allow to retrieve the controler type, unit,
**	target, lun concerned by a kernel message.
*/

#define PRINT_LUN(np, target, lun) \
printf(KERN_INFO "%s-<%d,%d>: ", ncr_name(np), (int) (target), (int) (lun))

static void PRINT_ADDR(Scsi_Cmnd *cmd)
{
	struct host_data *host_data = (struct host_data *) cmd->host->hostdata;
	ncb_p np                    = host_data->ncb;
	if (np) PRINT_LUN(np, cmd->target, cmd->lun);
}

/*==========================================================
**
**	NCR chip clock divisor table.
**	Divisors are multiplied by 10,000,000 in order to make 
**	calculations more simple.
**
**==========================================================
*/

#define _5M 5000000
static u_long div_10M[] =
	{2*_5M, 3*_5M, 4*_5M, 6*_5M, 8*_5M, 12*_5M, 16*_5M};


/*===============================================================
**
**	Prepare io register values used by ncr_init() according 
**	to selected and supported features.
**
**	NCR chips allow burst lengths of 2, 4, 8, 16, 32, 64, 128 
**	transfers. 32,64,128 are only supported by 875 and 895 chips.
**	We use log base 2 (burst length) as internal code, with 
**	value 0 meaning "burst disabled".
**
**===============================================================
*/

/*
 *	Burst length from burst code.
 */
#define burst_length(bc) (!(bc))? 0 : 1 << (bc)

/*
 *	Burst code from io register bits.
 */
#define burst_code(dmode, ctest4, ctest5) \
	(ctest4) & 0x80? 0 : (((dmode) & 0xc0) >> 6) + ((ctest5) & 0x04) + 1

/*
 *	Set initial io register bits from burst code.
 */
static inline void ncr_init_burst(ncb_p np, u_char bc)
{
	np->rv_ctest4	&= ~0x80;
	np->rv_dmode	&= ~(0x3 << 6);
	np->rv_ctest5	&= ~0x4;

	if (!bc) {
		np->rv_ctest4	|= 0x80;
	}
	else {
		--bc;
		np->rv_dmode	|= ((bc & 0x3) << 6);
		np->rv_ctest5	|= (bc & 0x4);
	}
}

#ifdef SCSI_NCR_NVRAM_SUPPORT

/*
**	Get target set-up from Symbios format NVRAM.
*/

__initfunc(
static void
	ncr_Symbios_setup_target(ncb_p np, int target, Symbios_nvram *nvram)
)
{
	tcb_p tp = &np->target[target];
	Symbios_target *tn = &nvram->target[target];

	tp->usrsync = tn->sync_period ? (tn->sync_period + 3) / 4 : 255;
	tp->usrwide = tn->bus_width == 0x10 ? 1 : 0;
	tp->usrtags =
		(tn->flags & SYMBIOS_QUEUE_TAGS_ENABLED)? SCSI_NCR_MAX_TAGS : 0;

	if (!(tn->flags & SYMBIOS_DISCONNECT_ENABLE))
		tp->usrflag |= UF_NODISC;
	if (!(tn->flags & SYMBIOS_SCAN_AT_BOOT_TIME))
		tp->usrflag |= UF_NOSCAN;
}

/*
**	Get target set-up from Tekram format NVRAM.
*/

__initfunc(
static void
	ncr_Tekram_setup_target(ncb_p np, int target, Tekram_nvram *nvram)
)
{
	tcb_p tp = &np->target[target];
	struct Tekram_target *tn = &nvram->target[target];
	int i;

	if (tn->flags & TEKRAM_SYNC_NEGO) {
		i = tn->sync_index & 0xf;
		tp->usrsync = i < 12 ? Tekram_sync[i] : 255;
	}

	tp->usrwide = (tn->flags & TEKRAM_WIDE_NEGO) ? 1 : 0;

	if (tn->flags & TEKRAM_TAGGED_COMMANDS) {
		tp->usrtags = 2 << nvram->max_tags_index;
		if (tp->usrtags > SCSI_NCR_MAX_TAGS)
			tp->usrtags = SCSI_NCR_MAX_TAGS;
	}

	if (!(tn->flags & TEKRAM_DISCONNECT_ENABLE))
		tp->usrflag = UF_NODISC;
 
	/* If any device does not support parity, we will not use this option */
	if (!(tn->flags & TEKRAM_PARITY_CHECK))
		np->rv_scntl0  &= ~0x0a; /* SCSI parity checking disabled */
}
#endif /* SCSI_NCR_NVRAM_SUPPORT */

__initfunc(
static int ncr_prepare_setting(ncb_p np, ncr_nvram *nvram)
)
{
	u_char	burst_max;
	u_long	period;
	int i;

	/*
	**	Save assumed BIOS setting
	*/

	np->sv_scntl0	= INB(nc_scntl0) & 0x0a;
	np->sv_scntl3	= INB(nc_scntl3) & 0x07;
	np->sv_dmode	= INB(nc_dmode)  & 0xce;
	np->sv_dcntl	= INB(nc_dcntl)  & 0xa8;
	np->sv_ctest3	= INB(nc_ctest3) & 0x01;
	np->sv_ctest4	= INB(nc_ctest4) & 0x80;
	np->sv_ctest5	= INB(nc_ctest5) & 0x24;
	np->sv_gpcntl	= INB(nc_gpcntl);
	np->sv_stest2	= INB(nc_stest2) & 0x20;
	np->sv_stest4	= INB(nc_stest4);

	/*
	**	Wide ?
	*/

	np->maxwide	= (np->features & FE_WIDE)? 1 : 0;

	/*
	**	Get the frequency of the chip's clock.
	**	Find the right value for scntl3.
	*/

	if	(np->features & FE_QUAD)
		np->multiplier	= 4;
	else if	(np->features & FE_DBLR)
		np->multiplier	= 2;
	else
		np->multiplier	= 1;

	np->clock_khz	= (np->features & FE_CLK80)? 80000 : 40000;
	np->clock_khz	*= np->multiplier;

	if (np->clock_khz != 40000)
		ncr_getclock(np, np->multiplier);

	/*
	 * Divisor to be used for async (timer pre-scaler).
	 */
	i = np->clock_divn - 1;
	while (i >= 0) {
		--i;
		if (10ul * SCSI_NCR_MIN_ASYNC * np->clock_khz > div_10M[i]) {
			++i;
			break;
		}
	}
	np->rv_scntl3 = i+1;

	/*
	 * Minimum synchronous period factor supported by the chip.
	 * Btw, 'period' is in tenths of nanoseconds.
	 */

	period = (4 * div_10M[0] + np->clock_khz - 1) / np->clock_khz;
	if	(period <= 250)		np->minsync = 10;
	else if	(period <= 303)		np->minsync = 11;
	else if	(period <= 500)		np->minsync = 12;
	else				np->minsync = (period + 40 - 1) / 40;

	/*
	 * Check against chip SCSI standard support (SCSI-2,ULTRA,ULTRA2).
	 */

	if	(np->minsync < 25 && !(np->features & (FE_ULTRA|FE_ULTRA2)))
		np->minsync = 25;
	else if	(np->minsync < 12 && !(np->features & FE_ULTRA2))
		np->minsync = 12;

	/*
	 * Maximum synchronous period factor supported by the chip.
	 */

	period = (11 * div_10M[np->clock_divn - 1]) / (4 * np->clock_khz);
	np->maxsync = period > 2540 ? 254 : period / 10;

	/*
	**	Prepare initial value of other IO registers
	*/
#if defined SCSI_NCR_TRUST_BIOS_SETTING
	np->rv_scntl0	= np->sv_scntl0;
	np->rv_dmode	= np->sv_dmode;
	np->rv_dcntl	= np->sv_dcntl;
	np->rv_ctest3	= np->sv_ctest3;
	np->rv_ctest4	= np->sv_ctest4;
	np->rv_ctest5	= np->sv_ctest5;
	burst_max	= burst_code(np->sv_dmode, np->sv_ctest4, np->sv_ctest5);
#else

	/*
	**	Select burst length (dwords)
	*/
	burst_max	= driver_setup.burst_max;
	if (burst_max == 255)
		burst_max = burst_code(np->sv_dmode, np->sv_ctest4, np->sv_ctest5);
	if (burst_max > 7)
		burst_max = 7;
	if (burst_max > np->maxburst)
		burst_max = np->maxburst;

	/*
	**	Select all supported special features
	*/
	if (np->features & FE_ERL)
		np->rv_dmode	|= ERL;		/* Enable Read Line */
	if (np->features & FE_BOF)
		np->rv_dmode	|= BOF;		/* Burst Opcode Fetch */
	if (np->features & FE_ERMP)
		np->rv_dmode	|= ERMP;	/* Enable Read Multiple */
	if (np->features & FE_PFEN)
		np->rv_dcntl	|= PFEN;	/* Prefetch Enable */
	if (np->features & FE_CLSE)
		np->rv_dcntl	|= CLSE;	/* Cache Line Size Enable */
	if (np->features & FE_WRIE)
		np->rv_ctest3	|= WRIE;	/* Write and Invalidate */
	if (np->features & FE_DFS)
		np->rv_ctest5	|= DFS;		/* Dma Fifo Size */

	/*
	**	Select some other
	*/
	if (driver_setup.master_parity)
		np->rv_ctest4	|= MPEE;	/* Master parity checking */
	if (driver_setup.scsi_parity)
		np->rv_scntl0	|= 0x0a;	/*  full arb., ena parity, par->ATN  */

#ifdef SCSI_NCR_NVRAM_SUPPORT
	/*
	**	Get parity checking, host ID and verbose mode from NVRAM
	**/
	if (nvram) {
		switch(nvram->type) {
		case SCSI_NCR_TEKRAM_NVRAM:
			np->myaddr = nvram->data.Tekram.host_id & 0x0f;
			break;
		case SCSI_NCR_SYMBIOS_NVRAM:
			if (!(nvram->data.Symbios.flags & SYMBIOS_PARITY_ENABLE))
				np->rv_scntl0  &= ~0x0a;
			np->myaddr = nvram->data.Symbios.host_id & 0x0f;
			if (nvram->data.Symbios.flags & SYMBIOS_VERBOSE_MSGS)
				np->verbose += 1;
			break;
		}
	}
#endif
	/*
	**  Get SCSI addr of host adapter (set by bios?).
	*/
	if (!np->myaddr) np->myaddr = INB(nc_scid) & 0x07;
	if (!np->myaddr) np->myaddr = SCSI_NCR_MYADDR;


#endif /* SCSI_NCR_TRUST_BIOS_SETTING */

	/*
	 *	Prepare initial io register bits for burst length
	 */
	ncr_init_burst(np, burst_max);

	/*
	**	Set differential mode and LED support.
	**	Ignore these features for boards known to use a 
	**	specific GPIO wiring (Tekram only for now).
	**	Probe initial setting of GPREG and GPCNTL for 
	**	other ones.
	*/
	if (!nvram || nvram->type != SCSI_NCR_TEKRAM_NVRAM) {
		switch(driver_setup.diff_support) {
		case 3:
			if (INB(nc_gpreg) & 0x08)
			break;
		case 2:
			np->rv_stest2	|= 0x20;
			break;
		case 1:
			np->rv_stest2	|= (np->sv_stest2 & 0x20);
			break;
		default:
			break;
		}
	}
	if ((driver_setup.led_pin ||
	     (nvram && nvram->type == SCSI_NCR_SYMBIOS_NVRAM)) &&
	    !(np->sv_gpcntl & 0x01))
		np->features |= FE_LED0;

	/*
	**	Set irq mode.
	*/
	switch(driver_setup.irqm) {
	case 2:
		np->rv_dcntl	|= IRQM;
		break;
	case 1:
		np->rv_dcntl	|= (np->sv_dcntl & IRQM);
		break;
	default:
		break;
	}

	/*
	**	Configure targets according to driver setup.
	**	If NVRAM present get targets setup from NVRAM.
	**	Allow to override sync, wide and NOSCAN from 
	**	boot command line.
	*/
	for (i = 0 ; i < MAX_TARGET ; i++) {
		tcb_p tp = &np->target[i];

		tp->usrsync = 255;
#ifdef SCSI_NCR_NVRAM_SUPPORT
		if (nvram) {
			switch(nvram->type) {
			case SCSI_NCR_TEKRAM_NVRAM:
				ncr_Tekram_setup_target(np, i, &nvram->data.Tekram);
				break;
			case SCSI_NCR_SYMBIOS_NVRAM:
				ncr_Symbios_setup_target(np, i, &nvram->data.Symbios);
				break;
			}
			if (driver_setup.use_nvram & 0x2)
				tp->usrsync = driver_setup.default_sync;
			if (driver_setup.use_nvram & 0x4)
				tp->usrwide = driver_setup.max_wide;
			if (driver_setup.use_nvram & 0x8)
				tp->usrflag &= ~UF_NOSCAN;
		}
		else {
#else
		if (1) {
#endif
			tp->usrsync = driver_setup.default_sync;
			tp->usrwide = driver_setup.max_wide;
			tp->usrtags = driver_setup.default_tags;
			if (!driver_setup.disconnection)
				np->target[i].usrflag = UF_NODISC;
		}
	}

	/*
	**	Announce all that stuff to user.
	*/

	i = nvram ? nvram->type : 0;
	printf(KERN_INFO "%s: %sID %d, Fast-%d%s%s\n", ncr_name(np),
		i  == SCSI_NCR_SYMBIOS_NVRAM ? "Symbios format NVRAM, " :
		(i == SCSI_NCR_TEKRAM_NVRAM  ? "Tekram format NVRAM, " : ""),
		np->myaddr,
		np->minsync < 12 ? 40 : (np->minsync < 25 ? 20 : 10),
		(np->rv_scntl0 & 0xa)	? ", Parity Checking"	: ", NO Parity",
		(np->rv_stest2 & 0x20)	? ", Differential"	: "");

	if (bootverbose > 1) {
		printf ("%s: initial SCNTL3/DMODE/DCNTL/CTEST3/4/5 = "
			"(hex) %02x/%02x/%02x/%02x/%02x/%02x\n",
			ncr_name(np), np->sv_scntl3, np->sv_dmode, np->sv_dcntl,
			np->sv_ctest3, np->sv_ctest4, np->sv_ctest5);

		printf ("%s: final   SCNTL3/DMODE/DCNTL/CTEST3/4/5 = "
			"(hex) %02x/%02x/%02x/%02x/%02x/%02x\n",
			ncr_name(np), np->rv_scntl3, np->rv_dmode, np->rv_dcntl,
			np->rv_ctest3, np->rv_ctest4, np->rv_ctest5);
	}

	if (bootverbose && np->paddr2)
		printf (KERN_INFO "%s: on-board RAM at 0x%lx\n",
			ncr_name(np), np->paddr2);

	return 0;
}


#ifdef SCSI_NCR_DEBUG_NVRAM

__initfunc(
void ncr_display_Symbios_nvram(ncb_p np, Symbios_nvram *nvram)
)
{
	int i;

	/* display Symbios nvram host data */
	printf("%s: HOST ID=%d%s%s%s%s\n",
		ncr_name(np), nvram->host_id & 0x0f,
		(nvram->flags  & SYMBIOS_SCAM_ENABLE)	? " SCAM"	:"",
		(nvram->flags  & SYMBIOS_PARITY_ENABLE)	? " PARITY"	:"",
		(nvram->flags  & SYMBIOS_VERBOSE_MSGS)	? " VERSBOSE"	:"", 
		(nvram->flags1 & SYMBIOS_SCAN_HI_LO)	? " HI_LO"	:"");

	/* display Symbios nvram drive data */
	for (i = 0 ; i < 15 ; i++) {
		struct Symbios_target *tn = &nvram->target[i];
		printf("%s-%d:%s%s%s%s WIDTH=%d SYNC=%d TMO=%d\n",
		ncr_name(np), i,
		(tn->flags & SYMBIOS_DISCONNECT_ENABLE)	? " DISC"	: "",
		(tn->flags & SYMBIOS_SCAN_AT_BOOT_TIME)	? " SCAN_BOOT"	: "",
		(tn->flags & SYMBIOS_SCAN_LUNS)		? " SCAN_LUNS"	: "",
		(tn->flags & SYMBIOS_QUEUE_TAGS_ENABLED)? " TCQ"	: "",
		tn->bus_width,
		tn->sync_period / 4,
		tn->timeout);
	}
}

static u_char Tekram_boot_delay[7] __initdata = {3, 5, 10, 20, 30, 60, 120};

__initfunc(
void ncr_display_Tekram_nvram(ncb_p np, Tekram_nvram *nvram)
)
{
	int i, tags, boot_delay;
	char *rem;

	/* display Tekram nvram host data */
	tags = 2 << nvram->max_tags_index;
	boot_delay = 0;
	if (nvram->boot_delay_index < 6)
		boot_delay = Tekram_boot_delay[nvram->boot_delay_index];
	switch((nvram->flags & TEKRAM_REMOVABLE_FLAGS) >> 6) {
	default:
	case 0:	rem = "";			break;
	case 1: rem = " REMOVABLE=boot device";	break;
	case 2: rem = " REMOVABLE=all";		break;
	}

	printf("%s: HOST ID=%d%s%s%s%s%s%s%s%s%s BOOT DELAY=%d tags=%d\n",
		ncr_name(np), nvram->host_id & 0x0f,
		(nvram->flags1 & SYMBIOS_SCAM_ENABLE)	? " SCAM"	:"",
		(nvram->flags & TEKRAM_MORE_THAN_2_DRIVES) ? " >2DRIVES"	:"",
		(nvram->flags & TEKRAM_DRIVES_SUP_1GB)	? " >1GB"	:"",
		(nvram->flags & TEKRAM_RESET_ON_POWER_ON) ? " RESET"	:"",
		(nvram->flags & TEKRAM_ACTIVE_NEGATION)	? " ACT_NEG"	:"",
		(nvram->flags & TEKRAM_IMMEDIATE_SEEK)	? " IMM_SEEK"	:"",
		(nvram->flags & TEKRAM_SCAN_LUNS)	? " SCAN_LUNS"	:"",
		(nvram->flags1 & TEKRAM_F2_F6_ENABLED)	? " F2_F6"	:"",
		rem, boot_delay, tags);

	/* display Tekram nvram drive data */
	for (i = 0; i <= 15; i++) {
		int sync, j;
		struct Tekram_target *tn = &nvram->target[i];
		j = tn->sync_index & 0xf;
		sync = j < 12 ? Tekram_sync[j] : 255;
		printf("%s-%d:%s%s%s%s%s%s PERIOD=%d\n",
		ncr_name(np), i,
		(tn->flags & TEKRAM_PARITY_CHECK)	? " PARITY"	: "",
		(tn->flags & TEKRAM_SYNC_NEGO)		? " SYNC"	: "",
		(tn->flags & TEKRAM_DISCONNECT_ENABLE)	? " DISC"	: "",
		(tn->flags & TEKRAM_START_CMD)		? " START"	: "",
		(tn->flags & TEKRAM_TAGGED_COMMANDS)	? " TCQ"	: "",
		(tn->flags & TEKRAM_WIDE_NEGO)		? " WIDE"	: "",
		sync);
	}
}
#endif /* SCSI_NCR_DEBUG_NVRAM */

/*
**	Host attach and initialisations.
**
**	Allocate host data and ncb structure.
**	Request IO region and remap MMIO region.
**	Do chip initialization.
**	If all is OK, install interrupt handling and
**	start the timer daemon.
*/

__initfunc(
static int ncr_attach (Scsi_Host_Template *tpnt, int unit, ncr_device *device)
)
{
        struct host_data *host_data;
	ncb_p np;
        struct Scsi_Host *instance = 0;
	u_long flags = 0;
	ncr_nvram *nvram = device->nvram;

#ifdef __sparc__
printf(KERN_INFO "ncr53c%s-%d: rev=0x%02x, base=0x%lx, io_port=0x%lx, irq=0x%x\n",
	device->chip.name, unit, device->chip.revision_id, device->slot.base,
	device->slot.io_port, device->slot.irq);
#else
printf(KERN_INFO "ncr53c%s-%d: rev=0x%02x, base=0x%lx, io_port=0x%lx, irq=%d\n",
	device->chip.name, unit, device->chip.revision_id, device->slot.base,
	device->slot.io_port, device->slot.irq);
#endif

	/*
	**	Allocate host_data structure
	*/
        if (!(instance = scsi_register(tpnt, sizeof(*host_data))))
	        goto attach_error;

	/*
	**	Initialize structure.
	*/
	host_data = (struct host_data *) instance->hostdata;

	/*
	**	Align np and first ccb to 32 boundary for cache line 
	**	bursting when copying the global header.
	*/
	np        = (ncb_p) (((u_long) &host_data->_ncb_data) & NCB_ALIGN_MASK);
	host_data->ncb = np;
	bzero (np, sizeof (*np));

	np->ccb   = (ccb_p) (((u_long) &host_data->_ccb_data) & CCB_ALIGN_MASK);
	bzero (np->ccb, sizeof (*np->ccb));

	/*
	**	Store input informations in the host data structure.
	*/
	strncpy(np->chip_name, device->chip.name, sizeof(np->chip_name) - 1);
	np->unit	= unit;
	np->verbose	= driver_setup.verbose;
	sprintf(np->inst_name, "ncr53c%s-%d", np->chip_name, np->unit);
	np->device_id	= device->chip.device_id;
	np->revision_id	= device->chip.revision_id;
	np->features	= device->chip.features;
	np->clock_divn	= device->chip.nr_divisor;
	np->maxoffs	= device->chip.offset_max;
	np->maxburst	= device->chip.burst_max;

	np->script0  =
	(struct script *) (((u_long) &host_data->script_data) & SCR_ALIGN_MASK);
	np->scripth0 = &host_data->scripth_data;

	/*
	**    Initialize timer structure
        **
        */
	init_timer(&np->timer);
	np->timer.data     = (unsigned long) np;
	np->timer.function = ncr53c8xx_timeout;

	/*
	**	Try to map the controller chip to
	**	virtual and physical memory.
	*/

	np->paddr	= device->slot.base;
	np->paddr2	= (np->features & FE_RAM)? device->slot.base_2 : 0;

#ifndef NCR_IOMAPPED
	np->vaddr = remap_pci_mem((u_long) np->paddr, (u_long) 128);
	if (!np->vaddr) {
		printf("%s: can't map memory mapped IO region\n", ncr_name(np));
		goto attach_error;
	}
	else
		if (bootverbose > 1)
			printf("%s: using memory mapped IO at virtual address 0x%lx\n", ncr_name(np), (u_long) np->vaddr);

	/*
	**	Make the controller's registers available.
	**	Now the INB INW INL OUTB OUTW OUTL macros
	**	can be used safely.
	*/

	np->reg = (struct ncr_reg*) np->vaddr;

#endif /* !defined NCR_IOMAPPED */

	/*
	**	Try to map the controller chip into iospace.
	*/

	request_region(device->slot.io_port, 128, "ncr53c8xx");
	np->port = device->slot.io_port;

#ifdef SCSI_NCR_NVRAM_SUPPORT
	if (nvram) {
		switch(nvram->type) {
		case SCSI_NCR_SYMBIOS_NVRAM:
#ifdef SCSI_NCR_DEBUG_NVRAM
			ncr_display_Symbios_nvram(np, &nvram->data.Symbios);
#endif
			break;
		case SCSI_NCR_TEKRAM_NVRAM:
#ifdef SCSI_NCR_DEBUG_NVRAM
			ncr_display_Tekram_nvram(np, &nvram->data.Tekram);
#endif
			break;
		default:
			nvram = 0;
#ifdef SCSI_NCR_DEBUG_NVRAM
			printf("%s: NVRAM: None or invalid data.\n", ncr_name(np));
#endif
		}
	}
#endif

	/*
	**	Do chip dependent initialization.
	*/
	(void)ncr_prepare_setting(np, nvram);

#ifndef NCR_IOMAPPED
	if (np->paddr2 && sizeof(struct script) <= 4096) {
		np->vaddr2 = remap_pci_mem((u_long) np->paddr2, (u_long) 4096);
		if (!np->vaddr2) {
			printf("%s: can't map memory mapped IO region\n", ncr_name(np));
			goto attach_error;
		}
		else
			if (bootverbose > 1)
				printf("%s: on-board ram mapped at virtual address 0x%lx\n", ncr_name(np), (u_long) np->vaddr2);
	}
#endif /* !defined NCR_IOMAPPED */

	/*
	**	Fill Linux host instance structure
	*/
#if LINUX_VERSION_CODE >= LinuxVersionCode(1,3,0)
	instance->max_channel	= 0;
	instance->max_id	= np->maxwide ? 16 : 8;
	instance->max_lun	= SCSI_NCR_MAX_LUN;
#endif
#ifndef NCR_IOMAPPED
	instance->base		= (char *) np->reg;
#endif
	instance->irq		= device->slot.irq;
	instance->io_port	= device->slot.io_port;
	instance->n_io_port	= 128;
	instance->dma_channel	= 0;
#if LINUX_VERSION_CODE >= LinuxVersionCode(2,0,0)
	instance->select_queue_depths = ncr53c8xx_select_queue_depths;
#endif

	/*
	**	Patch script to physical addresses
	*/
	ncr_script_fill (&script0, &scripth0);

	np->scripth	= np->scripth0;
	np->p_scripth	= vtophys(np->scripth);

	np->script	= (np->vaddr2) ? (struct script *) np->vaddr2 : np->script0;
	np->p_script	= (np->vaddr2) ? pcivtophys(np->paddr2) : vtophys(np->script0);

	ncr_script_copy_and_bind (np, (ncrcmd *) &script0, (ncrcmd *) np->script0, sizeof(struct script));
	ncr_script_copy_and_bind (np, (ncrcmd *) &scripth0, (ncrcmd *) np->scripth0, sizeof(struct scripth));
	np->ccb->p_ccb		= vtophys (np->ccb);

	/*
	**    Patch the script for LED support.
	*/

	if (np->features & FE_LED0) {
		np->script0->reselect[0]  =
				cpu_to_scr(SCR_REG_REG(gpreg, SCR_OR,  0x01));
		np->script0->reselect1[0] =
				cpu_to_scr(SCR_REG_REG(gpreg, SCR_AND, 0xfe));
		np->script0->reselect2[0] =
				cpu_to_scr(SCR_REG_REG(gpreg, SCR_AND, 0xfe));
	}

	/*
	**	init data structure
	*/

	np->jump_tcb.l_cmd	= cpu_to_scr(SCR_JUMP);
	np->jump_tcb.l_paddr	= cpu_to_scr(NCB_SCRIPTH_PHYS (np, abort));

	/*
	**	Reset chip.
	*/

	OUTB (nc_istat,  SRST);
	DELAY (1000);
	OUTB (nc_istat,  0   );

	/*
	**	Now check the cache handling of the pci chipset.
	*/

	if (ncr_snooptest (np)) {
		printf ("CACHE INCORRECTLY CONFIGURED.\n");
		goto attach_error;
	};

	/*
	**	Install the interrupt handler.
	*/
#if LINUX_VERSION_CODE >= LinuxVersionCode(1,3,70)
#ifdef SCSI_NCR_SHARE_IRQ
	if (bootverbose > 1)
		printf("%s: requesting shared irq %d (dev_id=0x%lx)\n",
		        ncr_name(np), device->slot.irq, (u_long) np);
	if (request_irq(device->slot.irq, ncr53c8xx_intr,
			SA_INTERRUPT|SA_SHIRQ, "ncr53c8xx", np)) {
#else
	if (request_irq(device->slot.irq, ncr53c8xx_intr,
			SA_INTERRUPT, "ncr53c8xx", np)) {
#endif
#else
	if (request_irq(device->slot.irq, ncr53c8xx_intr,
			SA_INTERRUPT, "ncr53c8xx")) {
#endif
		printf("%s: request irq %d failure\n", ncr_name(np), device->slot.irq);
		goto attach_error;
	}
	np->irq = device->slot.irq;

	/*
	**	After SCSI devices have been opened, we cannot
	**	reset the bus safely, so we do it here.
	**	Interrupt handler does the real work.
	**	Process the reset exception,
	**	if interrupts are not enabled yet.
	**	Then enable disconnects.
	*/
	save_flags(flags); cli();
	if (ncr_reset_scsi_bus(np, 1, driver_setup.settle_delay) != 0) {
		printf("%s: FATAL ERROR: CHECK SCSI BUS - CABLES, TERMINATION, DEVICE POWER etc.!\n", ncr_name(np));
		restore_flags(flags);
		goto attach_error;
	}
	ncr_exception (np);
	restore_flags(flags);

	np->disc = 1;

	/*
	**	The middle-level SCSI driver does not
	**	wait devices to settle.
	**	Wait synchronously if more than 2 seconds.
	*/
	if (driver_setup.settle_delay > 2) {
		printf("%s: waiting %d seconds for scsi devices to settle...\n",
			ncr_name(np), driver_setup.settle_delay);
		DELAY(1000000UL * driver_setup.settle_delay);
	}

	/*
	**	Now let the generic SCSI driver
	**	look for the SCSI devices on the bus ..
	*/

	/*
	**	start the timeout daemon
	*/
	np->lasttime=0;
	ncr_timeout (np);

	/*
	**  use SIMPLE TAG messages by default
	*/
#ifdef SCSI_NCR_ALWAYS_SIMPLE_TAG
	np->order = M_SIMPLE_TAG;
#endif

	/*
	**  Done.
	*/
        if (!the_template) {
        	the_template = instance->hostt;
        	first_host = instance;
	}

	return 0;

attach_error:
	if (!instance) return -1;
	printf("%s: detaching...\n", ncr_name(np));
#ifndef NCR_IOMAPPED
	if (np->vaddr) {
#ifdef DEBUG_NCR53C8XX
		printf("%s: releasing memory mapped IO region %lx[%d]\n", ncr_name(np), (u_long) np->vaddr, 128);
#endif
		unmap_pci_mem((vm_offset_t) np->vaddr, (u_long) 128);
	}
	if (np->vaddr2) {
#ifdef DEBUG_NCR53C8XX
		printf("%s: releasing memory mapped IO region %lx[%d]\n", ncr_name(np), (u_long) np->vaddr2, 4096);
#endif
		unmap_pci_mem((vm_offset_t) np->vaddr2, (u_long) 4096);
	}
#endif
	if (np->port) {
#ifdef DEBUG_NCR53C8XX
		printf("%s: releasing IO region %x[%d]\n", ncr_name(np), np->port, 128);
#endif
		release_region(np->port, 128);
	}
	if (np->irq) {
#ifdef DEBUG_NCR53C8XX
		printf("%s: freeing irq %d\n", ncr_name(np), np->irq);
#endif
#if LINUX_VERSION_CODE >= LinuxVersionCode(1,3,70)
		free_irq(np->irq, np);
#else
		free_irq(np->irq);
#endif
	}
	scsi_unregister(instance);

        return -1;
 }

/*==========================================================
**
**
**	Start execution of a SCSI command.
**	This is called from the generic SCSI driver.
**
**
**==========================================================
*/
int ncr_queue_command (Scsi_Cmnd *cmd, void (* done)(Scsi_Cmnd *))
{
        struct Scsi_Host   *host      = cmd->host;
/*	Scsi_Device        *device    = cmd->device; */
	struct host_data   *host_data = (struct host_data *) host->hostdata;
	ncb_p np                      = host_data->ncb;
	tcb_p tp                      = &np->target[cmd->target];

	ccb_p cp;
	lcb_p lp;

	int	segments;
	u_char	qidx, nego, idmsg, *msgptr;
	u_int  msglen, msglen2;
	u_long	flags;
	int	xfer_direction;

	cmd->scsi_done     = done;
	cmd->host_scribble = NULL;
	cmd->SCp.ptr       = NULL;
	cmd->SCp.buffer    = NULL;

	/*---------------------------------------------
	**
	**      Some shortcuts ...
	**
	**---------------------------------------------
	*/
	if ((cmd->target == np->myaddr	  ) ||
		(cmd->target >= MAX_TARGET) ||
		(cmd->lun    >= MAX_LUN   )) {
		return(DID_BAD_TARGET);
        }

	/*---------------------------------------------
	**
	**	Complete the 1st TEST UNIT READY command
	**	with error condition if the device is 
	**	flagged NOSCAN, in order to speed up 
	**	the boot.
	**
	**---------------------------------------------
	*/
	if (cmd->cmnd[0] == 0 && (tp->usrflag & UF_NOSCAN)) {
		tp->usrflag &= ~UF_NOSCAN;
		return DID_BAD_TARGET;
	}

	if (DEBUG_FLAGS & DEBUG_TINY) {
		PRINT_ADDR(cmd);
		printf ("CMD=%x ", cmd->cmnd[0]);
	}

	/*---------------------------------------------------
	**
	**	Assign a ccb / bind cmd.
	**	If resetting, shorten settle_time if necessary
	**	in order to avoid spurious timeouts.
	**	If resetting or no free ccb,
	**	insert cmd into the waiting list.
	**
	**----------------------------------------------------
	*/
	save_flags(flags); cli();

	if (np->settle_time && cmd->timeout_per_command >= HZ &&
		np->settle_time > jiffies + cmd->timeout_per_command - HZ) {
		np->settle_time = jiffies + cmd->timeout_per_command - HZ;
	}

        if (np->settle_time || !(cp=ncr_get_ccb (np, cmd->target, cmd->lun))) {
		insert_into_waiting_list(np, cmd);
		restore_flags(flags);
		return(DID_OK);
	}
	cp->cmd = cmd;

	/*---------------------------------------------------
	**
	**	Enable tagged queue if asked by scsi ioctl
	**
	**----------------------------------------------------
	*/
	if (!tp->usrtags && cmd->device && cmd->device->tagged_queue) {
		tp->usrtags = SCSI_NCR_MAX_TAGS;
		ncr_setmaxtags (np, tp, SCSI_NCR_MAX_TAGS);
	}

	/*---------------------------------------------------
	**
	**	timestamp
	**
	**----------------------------------------------------
	*/
#ifdef SCSI_NCR_PROFILE_SUPPORT
	bzero (&cp->phys.header.stamp, sizeof (struct tstamp));
	cp->phys.header.stamp.start = jiffies;
#endif

	/*----------------------------------------------------
	**
	**	Get device quirks from a speciality table.
	**
	**	@GENSCSI@
	**	This should be a part of the device table
	**	in "scsi_conf.c".
	**
	**----------------------------------------------------
	*/
	if (tp->quirks & QUIRK_UPDATE) {
		tp->quirks = ncr_lookup ((char*) &tp->inqdata[0]);
#ifndef NCR_GETCC_WITHMSG
		if (tp->quirks) {
			PRINT_ADDR(cmd);
			printf ("quirks=%x.\n", tp->quirks);
		}
#endif
	}

	/*---------------------------------------------------
	**
	**	negotiation required?
	**
	**	Only SCSI-II devices.
	**	To negotiate with SCSI-I devices is dangerous, since
	**	Synchronous Negotiation protocol is optional, and
	**	INQUIRY data do not contains capabilities in byte 7.
	**----------------------------------------------------
	*/

	nego = 0;

	if (cmd->lun == 0 && !tp->nego_cp && 
	    (tp->inqdata[2] & 0x7) >= 2 && tp->inqdata[7]) {
		/*
		**	negotiate wide transfers ?
		*/

		if (!tp->widedone) {
			if (tp->inqdata[7] & INQ7_WIDE16) {
				nego = NS_WIDE;
			} else
				tp->widedone=1;
		};

		/*
		**	negotiate synchronous transfers?
		*/

		if (!nego && !tp->period) {
			if ( 1
#if defined (CDROM_ASYNC)
			    && ((tp->inqdata[0] & 0x1f) != 5)
#endif
			    && (tp->inqdata[7] & INQ7_SYNC)) {
				nego = NS_SYNC;
			} else {
				tp->period  =0xffff;
				tp->sval = 0xe0;
				PRINT_ADDR(cmd);
				printf ("asynchronous.\n");
			};
		};

		/*
		**	remember nego is pending for the target.
		**	Avoid to start a nego for all queued commands 
		**	when tagged command queuing is enabled.
		*/

		if (nego)
			tp->nego_cp = cp;
	};

	/*---------------------------------------------------
	**
	**	choose a new tag ...
	**
	**----------------------------------------------------
	*/

	if ((lp = tp->lp[cmd->lun]) && (lp->usetags)) {
		/*
		**	assign a tag to this ccb!
		*/
		while (!cp->tag) {
			ccb_p cp2 = lp->next_ccb;
			lp->lasttag = lp->lasttag % 255 + 1;
			while (cp2 && cp2->tag != lp->lasttag)
				cp2 = cp2->next_ccb;
			if (cp2) continue;
			cp->tag=lp->lasttag;
			if (DEBUG_FLAGS & DEBUG_TAGS) {
				PRINT_ADDR(cmd);
				printf ("using tag #%d.\n", cp->tag);
			}
		}
	} else {
		cp->tag=0;
	}

	/*----------------------------------------------------
	**
	**	Build the identify / tag / sdtr message
	**
	**----------------------------------------------------
	*/

	idmsg = M_IDENTIFY | cmd->lun;

	if (cp != np->ccb && ((np->disc && !(tp->usrflag & UF_NODISC)) || cp->tag))
		idmsg |= 0x40;

	msgptr = cp->scsi_smsg;
	msglen = 0;
	msgptr[msglen++] = idmsg;

	if (cp->tag) {
	    char tag;

	    tag = np->order;
	    if (tag == 0) {
		/*
		**	Ordered write ops, unordered read ops.
		*/
		switch (cmd->cmnd[0]) {
		case 0x08:  /* READ_SMALL (6) */
		case 0x28:  /* READ_BIG  (10) */
		case 0xa8:  /* READ_HUGE (12) */
		    tag = M_SIMPLE_TAG;
		    break;
		default:
		    tag = M_ORDERED_TAG;
		}
	    }
	    /*
	    **	Have to force ordered tag to avoid timeouts
	    */
	    if ((lp = tp->lp[cmd->lun]) && (lp->force_ordered_tag)) {
		tag = M_ORDERED_TAG;
		lp->force_ordered_tag = 0;
		if (DEBUG_FLAGS & DEBUG_TAGS) {
			PRINT_ADDR(cmd);
			printf ("Ordered Queue Tag forced\n");
		}
	    }
	    msgptr[msglen++] = tag;
	    msgptr[msglen++] = cp -> tag;
	}

	switch (nego) {
	case NS_SYNC:
		msgptr[msglen++] = M_EXTENDED;
		msgptr[msglen++] = 3;
		msgptr[msglen++] = M_X_SYNC_REQ;
		msgptr[msglen++] = tp->maxoffs ? tp->minsync : 0;
		msgptr[msglen++] = tp->maxoffs;
		if (DEBUG_FLAGS & DEBUG_NEGO) {
			PRINT_ADDR(cp->cmd);
			printf ("sync msgout: ");
			ncr_show_msg (&cp->scsi_smsg [msglen-5]);
			printf (".\n");
		};
		break;
	case NS_WIDE:
		msgptr[msglen++] = M_EXTENDED;
		msgptr[msglen++] = 2;
		msgptr[msglen++] = M_X_WIDE_REQ;
		msgptr[msglen++] = tp->usrwide;
		if (DEBUG_FLAGS & DEBUG_NEGO) {
			PRINT_ADDR(cp->cmd);
			printf ("wide msgout: ");
			ncr_show_msg (&cp->scsi_smsg [msglen-4]);
			printf (".\n");
		};
		break;
	};

	/*----------------------------------------------------
	**
	**	Build the identify message for getcc.
	**
	**----------------------------------------------------
	*/

	cp -> scsi_smsg2 [0] = idmsg;
	msglen2 = 1;

	/*----------------------------------------------------
	**
	**	Build the data descriptors
	**
	**----------------------------------------------------
	*/

	segments = ncr_scatter (cp, cp->cmd);

	if (segments < 0) {
		ncr_free_ccb(np, cp, cmd->target, cmd->lun);
		restore_flags(flags);
		return(DID_ERROR);
	}

	/*----------------------------------------------------
	**
	**	Guess xfer direction.
	**	Spare some CPU by testing here frequently opcode.
	**
	**----------------------------------------------------
	*/
	switch((int) cmd->cmnd[0]) {
	case 0x08:  /*	READ(6)				08 */
	case 0x28:  /*	READ(10)			28 */
	case 0xA8:  /*	READ(12)			A8 */
		xfer_direction = XferIn;
		break;
	case 0x0A:  /*	WRITE(6)			0A */
	case 0x2A:  /*	WRITE(10)			2A */
	case 0xAA:  /*	WRITE(12)			AA */
		xfer_direction = XferOut;
		break;
	default:
		xfer_direction = guess_xfer_direction((int) cmd->cmnd[0]);
		break;
	}

	/*----------------------------------------------------
	**
	**	Set the SAVED_POINTER.
	**
	**----------------------------------------------------
	*/

	cp->segments = segments;
	if (!cp->data_len)
		xfer_direction = XferNone;

	switch (xfer_direction) {
		u_long endp;
	default:
	case XferBoth:
		cp->phys.header.savep =
			cpu_to_scr(NCB_SCRIPT_PHYS (np, data_io));
		cp->phys.header.goalp = cp->phys.header.savep;
		break;
	case XferIn:
		endp = NCB_SCRIPT_PHYS (np, data_in) + MAX_SCATTER*16;
		cp->phys.header.goalp = cpu_to_scr(endp + 8);
		cp->phys.header.savep = cpu_to_scr(endp - segments*16);
		break;
	case XferOut:
		endp = NCB_SCRIPTH_PHYS (np, data_out) + MAX_SCATTER*16;
		cp->phys.header.goalp = cpu_to_scr(endp + 8);
		cp->phys.header.savep = cpu_to_scr(endp - segments*16);
		break;
	case XferNone:
		cp->phys.header.savep =
			cpu_to_scr(NCB_SCRIPT_PHYS (np, no_data));
		cp->phys.header.goalp = cp->phys.header.savep;
		break;
	}

	cp->phys.header.lastp = cp->phys.header.savep;

	/*----------------------------------------------------
	**
	**	fill in ccb
	**
	**----------------------------------------------------
	**
	**
	**	physical -> virtual backlink
	**	Generic SCSI command
	*/
	cp->phys.header.cp		= cp;
	/*
	**	Startqueue
	*/
	cp->phys.header.launch.l_paddr	=
			cpu_to_scr(NCB_SCRIPT_PHYS (np, select));
	cp->phys.header.launch.l_cmd	= cpu_to_scr(SCR_JUMP);
	/*
	**	select
	*/
	cp->phys.select.sel_id		= cmd->target;
	cp->phys.select.sel_scntl3	= tp->wval;
	cp->phys.select.sel_sxfer	= tp->sval;
	/*
	**	message
	*/
	cp->phys.smsg.addr		= cpu_to_scr(CCB_PHYS (cp, scsi_smsg));
	cp->phys.smsg.size		= cpu_to_scr(msglen);

	cp->phys.smsg2.addr		= cpu_to_scr(CCB_PHYS (cp, scsi_smsg2));
	cp->phys.smsg2.size		= cpu_to_scr(msglen2);
	/*
	**	command
	*/
	cp->phys.cmd.addr		= cpu_to_scr(vtophys (&cmd->cmnd[0]));
	cp->phys.cmd.size		= cpu_to_scr(cmd->cmd_len);
	/*
	**	sense command
	*/
	cp->phys.scmd.addr		= cpu_to_scr(CCB_PHYS (cp, sensecmd));
	cp->phys.scmd.size		= cpu_to_scr(6);
	/*
	**	patch requested size into sense command
	*/
	cp->sensecmd[0]			= 0x03;
	cp->sensecmd[1]			= cmd->lun << 5;
	cp->sensecmd[4]			= sizeof(cmd->sense_buffer);
	/*
	**	sense data
	*/
	cp->phys.sense.addr		=
			cpu_to_scr(vtophys (&cmd->sense_buffer[0]));
	cp->phys.sense.size		= cpu_to_scr(sizeof(cmd->sense_buffer));
	/*
	**	status
	*/
	cp->actualquirks		= tp->quirks;
	cp->host_status			= nego ? HS_NEGOTIATE : HS_BUSY;
	cp->scsi_status			= S_ILLEGAL;
	cp->parity_status		= 0;

	cp->xerr_status			= XE_OK;
	cp->sync_status			= tp->sval;
	cp->nego_status			= nego;
	cp->wide_status			= tp->wval;

	/*----------------------------------------------------
	**
	**	Critical region: start this job.
	**
	**----------------------------------------------------
	*/

	/*
	**	reselect pattern and activate this job.
	*/

	cp->jump_ccb.l_cmd	=
		cpu_to_scr((SCR_JUMP ^ IFFALSE (DATA (cp->tag))));

	/* Compute a time limit greater than the middle-level driver one */
	if (cmd->timeout_per_command > 0)
		cp->tlimit	= jiffies + cmd->timeout_per_command + NCR_TIMEOUT_INCREASE;
	else
		cp->tlimit	= jiffies + 3600 * HZ; /* No timeout=one hour */
	cp->magic		= CCB_MAGIC;

	/*
	**	insert into start queue.
	*/

	qidx = np->squeueput + 1;
	if (qidx >= MAX_START) qidx=0;
	np->squeue [qidx	 ] = cpu_to_scr(NCB_SCRIPT_PHYS (np, idle));
	np->squeue [np->squeueput] = cpu_to_scr(CCB_PHYS (cp, phys));
	np->squeueput = qidx;

	if(DEBUG_FLAGS & DEBUG_QUEUE)
		printf ("%s: queuepos=%d tryoffset=%d.\n", ncr_name (np),
		np->squeueput,
		(unsigned)(scr_to_cpu(np->script->startpos[0]) - 
			   (NCB_SCRIPTH_PHYS (np, tryloop))));

	/*
	**	Script processor may be waiting for reselect.
	**	Wake it up.
	*/
#ifdef	SCSI_NCR_DEBUG_ERROR_RECOVERY_SUPPORT
	if (!np->stalling)
#endif
	OUTB (nc_istat, SIGP);

	/*
	**	and reenable interrupts
	*/
	restore_flags(flags);

	/*
	**	Command is successfully queued.
	*/

	return(DID_OK);
}

/*==========================================================
**
**
**	Start reset process.
**	If reset in progress do nothing.
**	The interrupt handler will reinitialize the chip.
**	The timeout handler will wait for settle_time before 
**	clearing it and so resuming command processing.
**
**
**==========================================================
*/
static void ncr_start_reset(ncb_p np, int settle_delay)
{
	u_long flags;

	save_flags(flags); cli();

	if (!np->settle_time) {
		(void) ncr_reset_scsi_bus(np, 1, settle_delay);
	}
	restore_flags(flags);
}

static int ncr_reset_scsi_bus(ncb_p np, int enab_int, int settle_delay)
{
	u_int32 term;
	int retv = 0;

	np->settle_time	= jiffies + settle_delay * HZ;

	if (bootverbose > 1)
		printf("%s: resetting, "
			"command processing suspended for %d seconds\n",
			ncr_name(np), settle_delay);

	OUTB (nc_istat, SRST);
	DELAY (1000);
	OUTB (nc_istat, 0);
	if (enab_int)
		OUTW (nc_sien, RST);
	OUTB (nc_scntl1, CRST);
	DELAY (100);

	if (!driver_setup.bus_check)
		goto out;
	/*
	**	Check for no terminators or SCSI bus shorts to ground.
	**	Read SCSI data bus, data parity bits and control signals.
	**	We are expecting RESET to be TRUE and other signals to be 
	**	FALSE.
	*/
	term =	INB(nc_sstat0);				/* rst, sdp0 */
	term =	((term & 2) << 7) + ((term & 1) << 16);
	term |= ((INB(nc_sstat2) & 0x01) << 25) |	/* sdp1 */
		(INW(nc_sbdl) << 9) |			/* d15-0 */
		INB(nc_sbcl);	/* req, ack, bsy, sel, atn, msg, cd, io */

	if (!(np->features & FE_WIDE))
		term &= 0x3ffff;

	if (term != (2<<7)) {
		printf("%s: suspicious SCSI data while resetting the BUS.\n",
			ncr_name(np));
		printf("%s: %sdp0,d7-0,rst,req,ack,bsy,sel,atn,msg,c/d,i/o = "
			"0x%lx, expecting 0x%lx\n",
			ncr_name(np),
			(np->features & FE_WIDE) ? "dp1,d15-8," : "",
			(u_long)term, (u_long)(2<<7));
		if (driver_setup.bus_check == 1)
			retv = 1;
	}
out:
	OUTB (nc_scntl1, 0);
	return retv;
}

/*==========================================================
**
**
**	Reset the SCSI BUS.
**	This is called from the generic SCSI driver.
**
**
**==========================================================
*/
int ncr_reset_bus (Scsi_Cmnd *cmd, int sync_reset)
{
        struct Scsi_Host   *host      = cmd->host;
/*	Scsi_Device        *device    = cmd->device; */
	struct host_data   *host_data = (struct host_data *) host->hostdata;
	ncb_p np                      = host_data->ncb;
	ccb_p cp;
	u_long flags;
	int found;

#ifdef	SCSI_NCR_DEBUG_ERROR_RECOVERY_SUPPORT
	if (np->stalling)
		np->stalling = 0;
#endif

	save_flags(flags); cli();
/*
 * Return immediately if reset is in progress.
 */
	if (np->settle_time) {
		restore_flags(flags);
		return SCSI_RESET_PUNT;
	}
/*
 * Start the reset process.
 * The script processor is then assumed to be stopped.
 * Commands will now be queued in the waiting list until a settle 
 * delay of 2 seconds will be completed.
 */
	ncr_start_reset(np, driver_setup.settle_delay);
/*
 * First, look in the wakeup list
 */
	for (found=0, cp=np->ccb; cp; cp=cp->link_ccb) {
		/*
		**	look for the ccb of this command.
		*/
		if (cp->host_status == HS_IDLE) continue;
		if (cp->cmd == cmd) {
			found = 1;
			break;
		}
	}
/*
 * Then, look in the waiting list
 */
	if (!found && retrieve_from_waiting_list(0, np, cmd))
		found = 1;
/*
 * Wake-up all awaiting commands with DID_RESET.
 */
	reset_waiting_list(np);
/*
 * Wake-up all pending commands with HS_RESET -> DID_RESET.
 */
	ncr_wakeup(np, HS_RESET);
/*
 * If the involved command was not in a driver queue, and the 
 * scsi driver told us reset is synchronous, and the command is not 
 * currently in the waiting list, complete it with DID_RESET status,
 * in order to keep it alive.
 */
	if (!found && sync_reset && !retrieve_from_waiting_list(0, np, cmd)) {
		cmd->result = ScsiResult(DID_RESET, 0);
		cmd->scsi_done(cmd);
	}

	restore_flags(flags);

	return SCSI_RESET_SUCCESS;
}

/*==========================================================
**
**
**	Abort an SCSI command.
**	This is called from the generic SCSI driver.
**
**
**==========================================================
*/
static int ncr_abort_command (Scsi_Cmnd *cmd)
{
        struct Scsi_Host   *host      = cmd->host;
/*	Scsi_Device        *device    = cmd->device; */
	struct host_data   *host_data = (struct host_data *) host->hostdata;
	ncb_p np                      = host_data->ncb;
	ccb_p cp;
	u_long flags;
	int found;
	int retv;

#ifdef	SCSI_NCR_DEBUG_ERROR_RECOVERY_SUPPORT
	if (np->stalling == 2)
		np->stalling = 0;
#endif

	save_flags(flags); cli();
/*
 * First, look for the scsi command in the waiting list
 */
	if (remove_from_waiting_list(np, cmd)) {
		cmd->result = ScsiResult(DID_ABORT, 0);
		cmd->scsi_done(cmd);
		restore_flags(flags);
		return SCSI_ABORT_SUCCESS;
	}

/*
 * Then, look in the wakeup list
 */
	for (found=0, cp=np->ccb; cp; cp=cp->link_ccb) {
		/*
		**	look for the ccb of this command.
		*/
		if (cp->host_status == HS_IDLE) continue;
		if (cp->cmd == cmd) {
			found = 1;
			break;
		}
	}

	if (!found) {
		restore_flags(flags);
		return SCSI_ABORT_NOT_RUNNING;
	}

	if (np->settle_time) {
		restore_flags(flags);
		return SCSI_ABORT_SNOOZE;
	}

	/*
	**	Disable reselect.
	**      Remove it from startqueue.
	**	Set cp->tlimit to 0. The ncr_timeout() handler will use 
	**	this condition in order to complete the canceled command 
	**	after the script skipped the ccb, if necessary.
	*/
	cp->jump_ccb.l_cmd = cpu_to_scr(SCR_JUMP);
	if (cp->phys.header.launch.l_paddr ==
		cpu_to_scr(NCB_SCRIPT_PHYS (np, select))) {
		printf ("%s: abort ccb=%p (skip)\n", ncr_name (np), cp);
		cp->phys.header.launch.l_paddr =
			cpu_to_scr(NCB_SCRIPT_PHYS (np, skip));
	}

	cp->tlimit = 0;
	retv = SCSI_ABORT_PENDING;

	/*
	**      If there are no requests, the script
	**      processor will sleep on SEL_WAIT_RESEL.
	**      Let's wake it up, since it may have to work.
	*/
#ifdef	SCSI_NCR_DEBUG_ERROR_RECOVERY_SUPPORT
	if (!np->stalling)
#endif
	OUTB (nc_istat, SIGP);

	restore_flags(flags);

	return retv;
}

/*==========================================================
**
**	Linux release module stuff.
**
**	Called before unloading the module
**	Detach the host.
**	We have to free resources and halt the NCR chip
**
**==========================================================
*/

#ifdef MODULE
static int ncr_detach(ncb_p np)
{
	ccb_p cp;
	tcb_p tp;
	lcb_p lp;
	int target, lun;
	int i;

	printf("%s: releasing host resources\n", ncr_name(np));

/*
**	Stop the ncr_timeout process
**	Set release_stage to 1 and wait that ncr_timeout() set it to 2.
*/

#ifdef DEBUG_NCR53C8XX
	printf("%s: stopping the timer\n", ncr_name(np));
#endif
	np->release_stage = 1;
	for (i = 50 ; i && np->release_stage != 2 ; i--) DELAY(100000);
	if (np->release_stage != 2)
		printf("%s: the timer seems to be already stopped\n", ncr_name(np));
	else np->release_stage = 2;

/*
**	Disable chip interrupts
*/

#ifdef DEBUG_NCR53C8XX
	printf("%s: disabling chip interrupts\n", ncr_name(np));
#endif
	OUTW (nc_sien , 0);
	OUTB (nc_dien , 0);

/*
**	Free irq
*/

#ifdef DEBUG_NCR53C8XX
#ifdef __sparc__
	printf("%s: freeing irq 0x%x\n", ncr_name(np), np->irq);
#else
	printf("%s: freeing irq %d\n", ncr_name(np), np->irq);
#endif
#endif
#if LINUX_VERSION_CODE >= LinuxVersionCode(1,3,70)
	free_irq(np->irq, np);
#else
	free_irq(np->irq);
#endif

	/*
	**	Reset NCR chip
	**	Restore bios setting for automatic clock detection.
	*/

	printf("%s: resetting chip\n", ncr_name(np));
	OUTB (nc_istat,  SRST);
	DELAY (1000);
	OUTB (nc_istat,  0   );

	OUTB(nc_dmode,	np->sv_dmode);
	OUTB(nc_dcntl,	np->sv_dcntl);
	OUTB(nc_ctest3,	np->sv_ctest3);
	OUTB(nc_ctest4,	np->sv_ctest4);
	OUTB(nc_ctest5,	np->sv_ctest5);
	OUTB(nc_gpcntl,	np->sv_gpcntl);
	OUTB(nc_stest2,	np->sv_stest2);

	ncr_selectclock(np, np->sv_scntl3);

	/*
	**	Release Memory mapped IO region and IO mapped region
	*/

#ifndef NCR_IOMAPPED
#ifdef DEBUG_NCR53C8XX
	printf("%s: releasing memory mapped IO region %lx[%d]\n", ncr_name(np), (u_long) np->vaddr, 128);
#endif
	unmap_pci_mem((vm_offset_t) np->vaddr, (u_long) 128);
#ifdef DEBUG_NCR53C8XX
	printf("%s: releasing memory mapped IO region %lx[%d]\n", ncr_name(np), (u_long) np->vaddr2, 4096);
#endif
	unmap_pci_mem((vm_offset_t) np->vaddr2, (u_long) 4096);
#endif

#ifdef DEBUG_NCR53C8XX
	printf("%s: releasing IO region %x[%d]\n", ncr_name(np), np->port, 128);
#endif
	release_region(np->port, 128);

	/*
	**	Free allocated ccb(s)
	*/

	while ((cp=np->ccb->link_ccb) != NULL) {
		np->ccb->link_ccb = cp->link_ccb;
		if (cp->host_status) {
		printf("%s: shall free an active ccb (host_status=%d)\n",
			ncr_name(np), cp->host_status);
		}
#ifdef DEBUG_NCR53C8XX
	printf("%s: freeing ccb (%lx)\n", ncr_name(np), (u_long) cp);
#endif
		m_free(cp, sizeof(*cp));
	}

	/*
	**	Free allocated tp(s)
	*/

	for (target = 0; target < MAX_TARGET ; target++) {
		tp=&np->target[target];
		for (lun = 0 ; lun < MAX_LUN ; lun++) {
			lp = tp->lp[lun];
			if (lp) {
#ifdef DEBUG_NCR53C8XX
	printf("%s: freeing lp (%lx)\n", ncr_name(np), (u_long) lp);
#endif
				m_free(lp, sizeof(*lp));
			}
		}
	}

	printf("%s: host resources successfully released\n", ncr_name(np));

	return 1;
}
#endif

/*==========================================================
**
**
**	Complete execution of a SCSI command.
**	Signal completion to the generic SCSI driver.
**
**
**==========================================================
*/

void ncr_complete (ncb_p np, ccb_p cp)
{
	Scsi_Cmnd *cmd;
	tcb_p tp;
	lcb_p lp;

	/*
	**	Sanity check
	*/

	if (!cp || (cp->magic!=CCB_MAGIC) || !cp->cmd) return;
	cp->magic = 1;
	cp->tlimit= 0;
	cmd = cp->cmd;

	/*
	**	No Reselect anymore.
	*/
	cp->jump_ccb.l_cmd = cpu_to_scr(SCR_JUMP);

	/*
	**	No starting.
	*/
	cp->phys.header.launch.l_paddr = cpu_to_scr(NCB_SCRIPT_PHYS (np, idle));

	/*
	**	timestamp
	**	Optional, spare some CPU time
	*/
#ifdef SCSI_NCR_PROFILE_SUPPORT
	ncb_profile (np, cp);
#endif

	if (DEBUG_FLAGS & DEBUG_TINY)
		printf ("CCB=%lx STAT=%x/%x\n", (unsigned long)cp & 0xfff,
			cp->host_status,cp->scsi_status);

	cmd = cp->cmd;
	cp->cmd = NULL;
	tp = &np->target[cmd->target];
	lp = tp->lp[cmd->lun];

	/*
	**	We donnot queue more than 1 ccb per target 
	**	with negotiation at any time. If this ccb was 
	**	used for negotiation, clear this info in the tcb.
	*/

	if (cp == tp->nego_cp)
		tp->nego_cp = 0;

	/*
	**	Check for parity errors.
	*/

	if (cp->parity_status) {
		PRINT_ADDR(cmd);
		printf ("%d parity error(s), fallback.\n", cp->parity_status);
		/*
		**	fallback to asynch transfer.
		*/
		tp->usrsync=255;
		tp->period =  0;
	}

	/*
	**	Check for extended errors.
	*/

	if (cp->xerr_status != XE_OK) {
		PRINT_ADDR(cmd);
		switch (cp->xerr_status) {
		case XE_EXTRA_DATA:
			printf ("extraneous data discarded.\n");
			break;
		case XE_BAD_PHASE:
			printf ("illegal scsi phase (4/5).\n");
			break;
		default:
			printf ("extended error %d.\n", cp->xerr_status);
			break;
		}
		if (cp->host_status==HS_COMPLETE)
			cp->host_status = HS_FAIL;
	}

	/*
	**	Check the status.
	*/
	if (   (cp->host_status == HS_COMPLETE)
		&& (cp->scsi_status == S_GOOD ||
		    cp->scsi_status == S_COND_MET)) {
		/*
		**	All went well (GOOD status).
		**	CONDITION MET status is returned on 
		**	`Pre-Fetch' or `Search data' success.
		*/
		cmd->result = ScsiResult(DID_OK, cp->scsi_status);

		/*
		** if (cp->phys.header.lastp != cp->phys.header.goalp)...
		**
		**	@RESID@
		**	Could dig out the correct value for resid,
		**	but it would be quite complicated.
		**
		**	The ah1542.c driver sets it to 0 too ...
		*/

		/*
		**	Try to assign a ccb to this nexus
		*/
		ncr_alloc_ccb (np, cmd->target, cmd->lun);

		/*
		**	On inquire cmd (0x12) save some data.
		**	Clear questionnable capacities.
		*/
		if (cmd->lun == 0 && cmd->cmnd[0] == 0x12) {
			if (np->unit < SCSI_NCR_MAX_HOST) {
				if (driver_setup.force_sync_nego)
					((char *) cmd->request_buffer)[7] |= INQ7_SYNC;
				else
					((char *) cmd->request_buffer)[7] &=
					(target_capabilities[np->unit].and_map[cmd->target]);
			}
			bcopy (	cmd->request_buffer,
				&tp->inqdata,
				sizeof (tp->inqdata));

			/*
			**	set number of tags
			*/
				ncr_setmaxtags (np, tp, driver_setup.default_tags);
			/*
			**	prepare negotiation of synch and wide.
			*/
			ncr_negotiate (np, tp);

			/*
			**	force quirks update before next command start
			*/
			tp->quirks |= QUIRK_UPDATE;
		}

		/*
		**	Announce changes to the generic driver.
		*/
		if (lp) {
			ncr_settags (tp, lp);
			if (lp->reqlink != lp->actlink)
				ncr_opennings (np, lp, cmd);
		};

		tp->bytes     += cp->data_len;
		tp->transfers ++;

		/*
		**	If tags was reduced due to queue full,
		**	increase tags if 100 good status received.
		*/
		if (tp->numtags < tp->maxtags) {
			++tp->num_good;
			if (tp->num_good >= 100) {
				tp->num_good = 0;
				++tp->numtags;
				if (tp->numtags == 1) {
					PRINT_ADDR(cmd);
					printf("tagged command queueing resumed\n");
				}
			}
		}
	} else if ((cp->host_status == HS_COMPLETE)
		&& (cp->scsi_status == (S_SENSE|S_GOOD) ||
		    cp->scsi_status == (S_SENSE|S_CHECK_COND))) {

		/*
		**   Check condition code
		*/
		cmd->result = ScsiResult(DID_OK, S_CHECK_COND);

		if (DEBUG_FLAGS & (DEBUG_RESULT|DEBUG_TINY)) {
			u_char * p = (u_char*) & cmd->sense_buffer;
			int i;
			printf ("\n%s: sense data:", ncr_name (np));
			for (i=0; i<14; i++) printf (" %x", *p++);
			printf (".\n");
		}

	} else if ((cp->host_status == HS_COMPLETE)
		&& (cp->scsi_status == S_BUSY ||
		    cp->scsi_status == S_CONFLICT)) {

		/*
		**   Target is busy.
		*/
		cmd->result = ScsiResult(DID_OK, cp->scsi_status);

	} else if ((cp->host_status == HS_COMPLETE)
		&& (cp->scsi_status == S_QUEUE_FULL)) {

		/*
		**   Target is stuffed.
		*/
		cmd->result = ScsiResult(DID_OK, cp->scsi_status);

		/*
		**  Suspend tagged queuing and start good status counter.
		**  Announce changes to the generic driver.
		*/
		if (tp->numtags) {
			/*
			 * Decrease tp->maxtags (ecd, 980110)
			 */
			tp->maxtags = tp->numtags - 1;

			PRINT_ADDR(cmd);
			printf("QUEUE FULL! suspending tagged command queueing (setting maxtags to %d)\n", tp->maxtags);

			tp->numtags	= 0;
			tp->num_good	= 0;
			if (lp) {
				ncr_settags (tp, lp);
				if (lp->reqlink != lp->actlink)
					ncr_opennings (np, lp, cmd);
			};
		}
	} else if ((cp->host_status == HS_SEL_TIMEOUT)
		|| (cp->host_status == HS_TIMEOUT)) {

		/*
		**   No response
		*/
		cmd->result = ScsiResult(DID_TIME_OUT, cp->scsi_status);

	} else if (cp->host_status == HS_RESET) {

		/*
		**   SCSI bus reset
		*/
		cmd->result = ScsiResult(DID_RESET, cp->scsi_status);

	} else if (cp->host_status == HS_ABORTED) {

		/*
		**   Transfer aborted
		*/
		cmd->result = ScsiResult(DID_ABORT, cp->scsi_status);

	} else {

		/*
		**  Other protocol messes
		*/
		PRINT_ADDR(cmd);
		printf ("COMMAND FAILED (%x %x) @%p.\n",
			cp->host_status, cp->scsi_status, cp);

		cmd->result = ScsiResult(DID_ERROR, cp->scsi_status);
	}

	/*
	**	trace output
	*/

	if (tp->usrflag & UF_TRACE) {
		u_char * p;
		int i;
		PRINT_ADDR(cmd);
		printf (" CMD:");
		p = (u_char*) &cmd->cmnd[0];
		for (i=0; i<cmd->cmd_len; i++) printf (" %x", *p++);

		if (cp->host_status==HS_COMPLETE) {
			switch (cp->scsi_status) {
			case S_GOOD:
				printf ("  GOOD");
				break;
			case S_CHECK_COND:
				printf ("  SENSE:");
				p = (u_char*) &cmd->sense_buffer;
				for (i=0; i<14; i++)
					printf (" %x", *p++);
				break;
			default:
				printf ("  STAT: %x\n", cp->scsi_status);
				break;
			}
		} else printf ("  HOSTERROR: %x", cp->host_status);
		printf ("\n");
	}

	/*
	**	Free this ccb
	*/
	ncr_free_ccb (np, cp, cmd->target, cmd->lun);

	/*
	**	requeue awaiting scsi commands
	*/
	if (np->waiting_list) requeue_waiting_list(np);

	/*
	**	signal completion to generic driver.
	*/
	cmd->scsi_done (cmd);
}

/*==========================================================
**
**
**	Signal all (or one) control block done.
**
**
**==========================================================
*/

void ncr_wakeup (ncb_p np, u_long code)
{
	/*
	**	Starting at the default ccb and following
	**	the links, complete all jobs with a
	**	host_status greater than "disconnect".
	**
	**	If the "code" parameter is not zero,
	**	complete all jobs that are not IDLE.
	*/

	ccb_p cp = np->ccb;
	while (cp) {
		switch (cp->host_status) {

		case HS_IDLE:
			break;

		case HS_DISCONNECT:
			if(DEBUG_FLAGS & DEBUG_TINY) printf ("D");
			/* fall through */

		case HS_BUSY:
		case HS_NEGOTIATE:
			if (!code) break;
			cp->host_status = code;

			/* fall through */

		default:
			ncr_complete (np, cp);
			break;
		};
		cp = cp -> link_ccb;
	};
}

/*==========================================================
**
**
**	Start NCR chip.
**
**
**==========================================================
*/

void ncr_init (ncb_p np, char * msg, u_long code)
{
	int	i;

	/*
	**	Reset chip.
	*/

	OUTB (nc_istat,  SRST);
	DELAY (10000);

	/*
	**	Message.
	*/

	if (msg) printf (KERN_INFO "%s: restart (%s).\n", ncr_name (np), msg);

	/*
	**	Clear Start Queue
	*/
	for (i=0;i<MAX_START;i++)
		np -> squeue [i] = cpu_to_scr(NCB_SCRIPT_PHYS (np, idle));

	/*
	**	Start at first entry.
	*/
	np->squeueput = 0;
	np->script0->startpos[0] = cpu_to_scr(NCB_SCRIPTH_PHYS (np, tryloop));
	np->script0->start0  [0] = cpu_to_scr(SCR_INT ^ IFFALSE (0));

	/*
	**	Wakeup all pending jobs.
	*/
	ncr_wakeup (np, code);

	/*
	**	Init chip.
	*/

	OUTB (nc_istat,  0x00   );	/*  Remove Reset, abort */
	OUTB (nc_scntl0, np->rv_scntl0 | 0xc0);
					/*  full arb., ena parity, par->ATN  */
	OUTB (nc_scntl1, 0x00);		/*  odd parity, and remove CRST!! */

	ncr_selectclock(np, np->rv_scntl3);	/* Select SCSI clock */

	OUTB (nc_scid  , RRE|np->myaddr);	/* Adapter SCSI address */
	OUTW (nc_respid, 1ul<<np->myaddr);	/* Id to respond to */
	OUTB (nc_istat , SIGP	);		/*  Signal Process */
	OUTB (nc_dmode , np->rv_dmode);		/* Burst length, dma mode */
	OUTB (nc_ctest5, np->rv_ctest5);	/* Large fifo + large burst */

	OUTB (nc_dcntl , NOCOM|np->rv_dcntl);	/* Protect SFBR */
	OUTB (nc_ctest3, np->rv_ctest3);	/* Write and invalidate */
	OUTB (nc_ctest4, np->rv_ctest4);	/* Master parity checking */

	OUTB (nc_stest2, EXT|np->rv_stest2);	/* Extended Sreq/Sack filtering */
	OUTB (nc_stest3, TE);			/* TolerANT enable */
	OUTB (nc_stime0, 0x0d	);		/* HTH disabled  STO 0.4 sec. */

	/*
	**	Disable disconnects.
	*/

	np->disc = 0;

	/*
	**    Enable GPIO0 pin for writing if LED support.
	*/

	if (np->features & FE_LED0) {
		OUTOFFB (nc_gpcntl, 0x01);
	}

	/*
	**    Upload the script into on-board RAM
	*/
	if (np->vaddr2) {
		if (bootverbose)
			printf ("%s: copying script fragments into the on-board RAM ...\n", ncr_name(np));
#if LINUX_VERSION_CODE >= LinuxVersionCode(2,0,0)
		memcpy_toio(np->script, np->script0, sizeof(struct script));
#else
		memcpy(np->script, np->script0, sizeof(struct script));
#endif
	}

	/*
	**      enable ints
	*/

	OUTW (nc_sien , STO|HTH|MA|SGE|UDC|RST);
	OUTB (nc_dien , MDPE|BF|ABRT|SSI|SIR|IID);

	/*
	**	For 895/6 enable SBMC interrupt and save current SCSI bus mode.
	*/
	if (np->features & FE_ULTRA2) {
		OUTONW (nc_sien, SBMC);
		np->scsi_mode = INB (nc_stest4) & SMODE;
	}

	/*
	**	Fill in target structure.
	**	Reinitialize usrsync.
	**	Reinitialize usrwide.
	**	Prepare sync negotiation according to actual SCSI bus mode.
	*/

	for (i=0;i<MAX_TARGET;i++) {
		tcb_p tp = &np->target[i];

		tp->sval    = 0;
		tp->wval    = np->rv_scntl3;

		if (tp->usrsync != 255) {
			if (tp->usrsync <= np->maxsync) {
				if (tp->usrsync < np->minsync) {
					tp->usrsync = np->minsync;
				}
			}
			else
				tp->usrsync = 255;
		};

		if (tp->usrwide > np->maxwide)
			tp->usrwide = np->maxwide;

		ncr_negotiate (np, tp);
	}

	/*
	**    Start script processor.
	*/

	OUTL (nc_dsp, NCB_SCRIPT_PHYS (np, start));
}

/*==========================================================
**
**	Prepare the negotiation values for wide and
**	synchronous transfers.
**
**==========================================================
*/

static void ncr_negotiate (struct ncb* np, struct tcb* tp)
{
	/*
	**	minsync unit is 4ns !
	*/

	u_long minsync = tp->usrsync;

	/*
	**	SCSI bus mode limit
	*/

	if (np->scsi_mode && np->scsi_mode == SMODE_SE) {
		if (minsync < 12) minsync = 12;
	}

	/*
	**	if not scsi 2
	**	don't believe FAST!
	*/

	if ((minsync < 50) && (tp->inqdata[2] & 0x0f) < 2)
		minsync=50;

	/*
	**	our limit ..
	*/

	if (minsync < np->minsync)
		minsync = np->minsync;

	/*
	**	divider limit
	*/

	if (minsync > np->maxsync)
		minsync = 255;

	tp->minsync = minsync;
	tp->maxoffs = (minsync<255 ? np->maxoffs : 0);

	/*
	**	period=0: has to negotiate sync transfer
	*/

	tp->period=0;

	/*
	**	widedone=0: has to negotiate wide transfer
	*/
	tp->widedone=0;
}

/*==========================================================
**
**	Get clock factor and sync divisor for a given 
**	synchronous factor period.
**	Returns the clock factor (in sxfer) and scntl3 
**	synchronous divisor field.
**
**==========================================================
*/

static void ncr_getsync(ncb_p np, u_char sfac, u_char *fakp, u_char *scntl3p)
{
	u_long	clk = np->clock_khz;	/* SCSI clock frequency in kHz	*/
	int	div = np->clock_divn;	/* Number of divisors supported	*/
	u_long	fak;			/* Sync factor in sxfer		*/
	u_long	per;			/* Period in tenths of ns	*/
	u_long	kpc;			/* (per * clk)			*/

	/*
	**	Compute the synchronous period in tenths of nano-seconds
	*/
	if	(sfac <= 10)	per = 250;
	else if	(sfac == 11)	per = 303;
	else if	(sfac == 12)	per = 500;
	else			per = 40 * sfac;

	/*
	**	Look for the greatest clock divisor that allows an 
	**	input speed faster than the period.
	*/
	kpc = per * clk;
	while (--div >= 0)
		if (kpc >= (div_10M[div] << 2)) break;

	/*
	**	Calculate the lowest clock factor that allows an output 
	**	speed not faster than the period.
	*/
	fak = (kpc - 1) / div_10M[div] + 1;

#if 0	/* This optimization does not seem very usefull */

	per = (fak * div_10M[div]) / clk;

	/*
	**	Why not to try the immediate lower divisor and to choose 
	**	the one that allows the fastest output speed ?
	**	We dont want input speed too much greater than output speed.
	*/
	if (div >= 1 && fak < 8) {
		u_long fak2, per2;
		fak2 = (kpc - 1) / div_10M[div-1] + 1;
		per2 = (fak2 * div_10M[div-1]) / clk;
		if (per2 < per && fak2 <= 8) {
			fak = fak2;
			per = per2;
			--div;
		}
	}
#endif

	if (fak < 4) fak = 4;	/* Should never happen, too bad ... */

	/*
	**	Compute and return sync parameters for the ncr
	*/
	*fakp		= fak - 4;
	*scntl3p	= ((div+1) << 4) + (sfac < 25 ? 0x80 : 0);
}


/*==========================================================
**
**	Set actual values, sync status and patch all ccbs of 
**	a target according to new sync/wide agreement.
**
**==========================================================
*/

static void ncr_set_sync_wide_status (ncb_p np, u_char target)
{
	ccb_p cp;
	tcb_p tp = &np->target[target];

	/*
	**	set actual value and sync_status
	*/
	OUTB (nc_sxfer, tp->sval);
	np->sync_st = tp->sval;
	OUTB (nc_scntl3, tp->wval);
	np->wide_st = tp->wval;

	/*
	**	patch ALL ccbs of this target.
	*/
	for (cp = np->ccb; cp; cp = cp->link_ccb) {
		if (!cp->cmd) continue;
		if (cp->cmd->target != target) continue;
		cp->sync_status = tp->sval;
		cp->wide_status = tp->wval;
	};
}

/*==========================================================
**
**	Switch sync mode for current job and it's target
**
**==========================================================
*/

static void ncr_setsync (ncb_p np, ccb_p cp, u_char scntl3, u_char sxfer)
{
	Scsi_Cmnd *cmd;
	tcb_p tp;
	u_char target = INB (nc_ctest0) & 0x0f;
	u_char idiv;

	assert (cp);
	if (!cp) return;

	cmd = cp->cmd;
	assert (cmd);
	if (!cmd) return;
	assert (target == (cmd->target & 0xf));

	tp = &np->target[target];

	if (!scntl3 || !(sxfer & 0x1f))
		scntl3 = np->rv_scntl3;
	scntl3 = (scntl3 & 0xf0) | (tp->wval & EWS) | (np->rv_scntl3 & 0x07);

	/*
	**	Deduce the value of controller sync period from scntl3.
	**	period is in tenths of nano-seconds.
	*/

	idiv = ((scntl3 >> 4) & 0x7);
	if ((sxfer & 0x1f) && idiv)
		tp->period = (((sxfer>>5)+4)*div_10M[idiv-1])/np->clock_khz;
	else
		tp->period = 0xffff;

	/*
	**	 Stop there if sync parameters are unchanged
	*/
	if (tp->sval == sxfer && tp->wval == scntl3) return;
	tp->sval = sxfer;
	tp->wval = scntl3;

	/*
	**	Bells and whistles   ;-)
	*/
	PRINT_ADDR(cmd);
	if (sxfer & 0x01f) {
		unsigned f10 = 100000 << (tp->widedone ? tp->widedone -1 : 0);
		unsigned mb10 = (f10 + tp->period/2) / tp->period;
		char *scsi;

		/*
		**  Disable extended Sreq/Sack filtering
		*/
		if (tp->period <= 2000) OUTOFFB (nc_stest2, EXT);

		/*
		**	Bells and whistles   ;-)
		*/
		if	(tp->period < 500)	scsi = "FAST-40";
		else if	(tp->period < 1000)	scsi = "FAST-20";
		else if	(tp->period < 2000)	scsi = "FAST-10";
		else				scsi = "FAST-5";

		printf ("%s %sSCSI %d.%d MB/s (%d ns, offset %d)\n", scsi,
			tp->widedone > 1 ? "WIDE " : "",
			mb10 / 10, mb10 % 10, tp->period / 10, sxfer & 0x1f);
	} else
		printf ("%sasynchronous.\n", tp->widedone > 1 ? "wide " : "");

	/*
	**	set actual value and sync_status
	**	patch ALL ccbs of this target.
	*/
	ncr_set_sync_wide_status(np, target);
}

/*==========================================================
**
**	Switch wide mode for current job and it's target
**	SCSI specs say: a SCSI device that accepts a WDTR 
**	message shall reset the synchronous agreement to 
**	asynchronous mode.
**
**==========================================================
*/

static void ncr_setwide (ncb_p np, ccb_p cp, u_char wide, u_char ack)
{
	Scsi_Cmnd *cmd;
	u_short target = INB (nc_ctest0) & 0x0f;
	tcb_p tp;
	u_char	scntl3;
	u_char	sxfer;

	assert (cp);
	if (!cp) return;

	cmd = cp->cmd;
	assert (cmd);
	if (!cmd) return;
	assert (target == (cmd->target & 0xf));

	tp = &np->target[target];
	tp->widedone  =  wide+1;
	scntl3 = (tp->wval & (~EWS)) | (wide ? EWS : 0);

	sxfer = ack ? 0 : tp->sval;

	/*
	**	 Stop there if sync/wide parameters are unchanged
	*/
	if (tp->sval == sxfer && tp->wval == scntl3) return;
	tp->sval = sxfer;
	tp->wval = scntl3;

	/*
	**	Bells and whistles   ;-)
	*/
	if (bootverbose >= 2) {
		PRINT_ADDR(cmd);
		if (scntl3 & EWS)
			printf ("WIDE SCSI (16 bit) enabled.\n");
		else
			printf ("WIDE SCSI disabled.\n");
	}

	/*
	**	set actual value and sync_status
	**	patch ALL ccbs of this target.
	*/
	ncr_set_sync_wide_status(np, target);
}

/*==========================================================
**
**	Switch tagged mode for a target.
**
**==========================================================
*/

static void ncr_setmaxtags (ncb_p np, tcb_p tp, u_long numtags)
{
	int l;
	if (numtags > tp->usrtags)
		numtags = tp->usrtags;
	tp->numtags = numtags;
	tp->maxtags = numtags;

	for (l=0; l<MAX_LUN; l++) {
		lcb_p lp;
		u_char wastags;

		if (!tp) break;
		lp=tp->lp[l];
		if (!lp) continue;

		wastags = lp->usetags;
		ncr_settags (tp, lp);

		if (numtags > 1 && lp->reqccbs > 1) {
			PRINT_LUN(np, tp - np->target, l);
			printf("using tagged command queueing, up to %ld cmds/lun\n", numtags);
		}
		else if (numtags <= 1 && wastags) {
			PRINT_LUN(np, tp - np->target, l);
			printf("disabling tagged command queueing\n");
		}
	};
}

static void ncr_settags (tcb_p tp, lcb_p lp)
{
	u_char reqtags, tmp;

	if ((!tp) || (!lp)) return;

	/*
	**	only devices conformant to ANSI Version >= 2
	**	only devices capable of tagges commands
	**	only disk devices
	**	only if enabled by user ..
	*/
	if ((  tp->inqdata[2] & 0x7) >= 2 &&
	    (  tp->inqdata[7] & INQ7_QUEUE) && ((tp->inqdata[0] & 0x1f)==0x00)
		&& tp->numtags > 1) {
		reqtags = tp->numtags;
		if (lp->actlink <= 1)
			lp->usetags=reqtags;
	} else {
		reqtags = 1;
		if (lp->actlink <= 1)
			lp->usetags=0;
	};

	/*
	**	don't announce more than available.
	*/
	tmp = lp->actccbs;
	if (tmp > reqtags) tmp = reqtags;
	lp->reqlink = tmp;

	/*
	**	don't discard if announced.
	*/
	tmp = lp->actlink;
	if (tmp < reqtags) tmp = reqtags;
	lp->reqccbs = tmp;
}

/*----------------------------------------------------
**
**	handle user commands
**
**----------------------------------------------------
*/

#ifdef SCSI_NCR_USER_COMMAND_SUPPORT

static void ncr_usercmd (ncb_p np)
{
	u_char t;
	tcb_p tp;

	switch (np->user.cmd) {

	case 0: return;

	case UC_SETSYNC:
		for (t=0; t<MAX_TARGET; t++) {
			if (!((np->user.target>>t)&1)) continue;
			tp = &np->target[t];
			tp->usrsync = np->user.data;
			ncr_negotiate (np, tp);
		};
		break;

	case UC_SETTAGS:
		if (np->user.data > SCSI_NCR_MAX_TAGS)
			np->user.data = SCSI_NCR_MAX_TAGS;
		for (t=0; t<MAX_TARGET; t++) {
			if (!((np->user.target>>t)&1)) continue;
			np->target[t].usrtags = np->user.data;
			ncr_setmaxtags (np, &np->target[t], np->user.data);
		};
		break;

	case UC_SETDEBUG:
#ifdef SCSI_NCR_DEBUG_INFO_SUPPORT
		ncr_debug = np->user.data;
#endif
		break;

	case UC_SETORDER:
		np->order = np->user.data;
		break;

	case UC_SETWIDE:
		for (t=0; t<MAX_TARGET; t++) {
			u_long size;
			if (!((np->user.target>>t)&1)) continue;
			tp = &np->target[t];
			size = np->user.data;
			if (size > np->maxwide) size=np->maxwide;
			tp->usrwide = size;
			ncr_negotiate (np, tp);
		};
		break;

	case UC_SETFLAG:
		for (t=0; t<MAX_TARGET; t++) {
			if (!((np->user.target>>t)&1)) continue;
			tp = &np->target[t];
			tp->usrflag = np->user.data;
		};
		break;

	case UC_CLEARPROF:
		bzero(&np->profile, sizeof(np->profile));
		break;
#ifdef	UC_DEBUG_ERROR_RECOVERY
	case UC_DEBUG_ERROR_RECOVERY:
		np->debug_error_recovery = np->user.data;
		break;
#endif
	}
	np->user.cmd=0;
}
#endif


/*=====================================================================
**
**    Embedded error recovery debugging code.
**
**=====================================================================
**
**    This code is conditionned by SCSI_NCR_DEBUG_ERROR_RECOVERY_SUPPORT.
**    It only can be enabled after boot-up with a control command.
**
**    Every 30 seconds the timer handler of the driver decides to 
**    change the behaviour of the driver in order to trigger errors.
**
**    If last command was "debug_error_recovery sge", the driver 
**    sets sync offset of all targets that use sync transfers to 2, 
**    and so hopes a SCSI gross error at the next read operation.
**
**    If last command was "debug_error_recovery abort", the driver 
**    does not signal new scsi commands to the script processor, until 
**    it is asked to abort or reset a command by the mid-level driver.
**
**    If last command was "debug_error_recovery reset", the driver 
**    does not signal new scsi commands to the script processor, until 
**    it is asked to reset a command by the mid-level driver.
**
**    If last command was "debug_error_recovery parity", the driver 
**    will assert ATN on the next DATA IN phase mismatch, and so will 
**    behave as if a parity error had been detected.
**
**    The command "debug_error_recovery none" makes the driver behave 
**    normaly.
**
**=====================================================================
*/

#ifdef	SCSI_NCR_DEBUG_ERROR_RECOVERY_SUPPORT
static void ncr_trigger_errors (ncb_p np)
{
	/*
	** 	If np->debug_error_recovery is not zero, we want to 
	**	simulate common errors in order to test error recovery.
	*/
	do {
		static u_long last = 0l;

		if (!np->debug_error_recovery)
			break;
		if (!last)
			last = jiffies;
		else if (jiffies < last + 30*HZ)
			break;
		last = jiffies;
		/*
		 * This one triggers SCSI gross errors.
		 */
		if (np->debug_error_recovery == 1) {
			int i;
			printf("%s: testing error recovery from SCSI gross error...\n", ncr_name(np));
			for (i = 0 ; i < MAX_TARGET ; i++) {
				if (np->target[i].sval & 0x1f) {
					np->target[i].sval &= ~0x1f;
					np->target[i].sval += 2;
				}
			}
		}
		/*
		 * This one triggers abort from the mid-level driver.
		 */
		else if (np->debug_error_recovery == 2) {
			printf("%s: testing error recovery from mid-level driver abort()...\n", ncr_name(np));
			np->stalling = 2;
		}
		/*
		 * This one triggers reset from the mid-level driver.
		 */
		else if (np->debug_error_recovery == 3) {
			printf("%s: testing error recovery from mid-level driver reset()...\n", ncr_name(np));
			np->stalling = 3;
		}
		/*
		 * This one set ATN on phase mismatch in DATA IN phase and so 
		 * will behave as on scsi parity error detected.
		 */
		else if (np->debug_error_recovery == 4) {
			printf("%s: testing data in parity error...\n", ncr_name(np));
			np->assert_atn = 1;
		}
	} while (0);
}
#endif

/*==========================================================
**
**
**	ncr timeout handler.
**
**
**==========================================================
**
**	Misused to keep the driver running when
**	interrupts are not configured correctly.
**
**----------------------------------------------------------
*/

static void ncr_timeout (ncb_p np)
{
	u_long	thistime = jiffies;
	u_long	count = 0;
	ccb_p cp;
	u_long flags;

	/*
	**	If release process in progress, let's go
	**	Set the release stage from 1 to 2 to synchronize
	**	with the release process.
	*/

	if (np->release_stage) {
		if (np->release_stage == 1) np->release_stage = 2;
		return;
	}

	np->timer.expires =
#if LINUX_VERSION_CODE >= LinuxVersionCode(1,3,0)
			jiffies +
#endif
			SCSI_NCR_TIMER_INTERVAL;

	add_timer(&np->timer);

#ifdef	SCSI_NCR_DEBUG_ERROR_RECOVERY_SUPPORT
	ncr_trigger_errors (np);
#endif

	/*
	**	If we are resetting the ncr, wait for settle_time before 
	**	clearing it. Then command processing will be resumed.
	*/
	if (np->settle_time) {
		if (np->settle_time <= thistime) {
			if (bootverbose > 1)
				printf("%s: command processing resumed\n", ncr_name(np));
			save_flags(flags); cli();
			np->settle_time	= 0;
			np->disc	= 1;
			requeue_waiting_list(np);
			restore_flags(flags);
		}
		return;
	}

	/*
	**	Since the generic scsi driver only allows us 0.5 second 
	**	to perform abort of a command, we must look at ccbs about 
	**	every 0.25 second.
	*/
	if (np->lasttime + (HZ>>2) <= thistime) {
		/*
		**	block ncr interrupts
		*/
		save_flags(flags); cli();

		np->lasttime = thistime;

		/*
		**	Reset profile data to avoid ugly overflow
		**	(Limited to 1024 GB for 32 bit architecture)
		*/
		if (np->profile.num_kbytes > (~0UL >> 2))
			bzero(&np->profile, sizeof(np->profile));

		/*----------------------------------------------------
		**
		**	handle ncr chip timeouts
		**
		**	Assumption:
		**	We have a chance to arbitrate for the
		**	SCSI bus at least every 10 seconds.
		**
		**----------------------------------------------------
		*/
#if 0
		if (thistime < np->heartbeat + HZ + HZ)
			np->latetime = 0;
		else
			np->latetime++;
#endif

		/*----------------------------------------------------
		**
		**	handle ccb timeouts
		**
		**----------------------------------------------------
		*/

		for (cp=np->ccb; cp; cp=cp->link_ccb) {
			/*
			**	look for timed out ccbs.
			*/
			if (!cp->host_status) continue;
			count++;
			/*
			**	Have to force ordered tag to avoid timeouts
			*/
			if (cp->cmd && cp->tlimit && cp->tlimit <= 
				thistime + NCR_TIMEOUT_INCREASE + SCSI_NCR_TIMEOUT_ALERT) {
				lcb_p lp;
				lp = np->target[cp->cmd->target].lp[cp->cmd->lun];
				if (lp && !lp->force_ordered_tag) {
					lp->force_ordered_tag = 1;
				}
			}
			/*
			**	ncr_abort_command() cannot complete canceled 
			**	commands immediately. It sets tlimit to zero 
			**	and ask the script to skip the scsi process if 
			**	necessary. We have to complete this work here.
			*/

			if (cp->tlimit) continue;

			switch (cp->host_status) {

			case HS_BUSY:
			case HS_NEGOTIATE:
				/*
				** still in start queue ?
				*/
				if (cp->phys.header.launch.l_paddr ==
					cpu_to_scr(NCB_SCRIPT_PHYS (np, skip)))
					continue;

				/* fall through */
			case HS_DISCONNECT:
				cp->host_status=HS_ABORTED;
			};
			cp->tag = 0;

			/*
			**	wakeup this ccb.
			*/
			ncr_complete (np, cp);

#ifdef	SCSI_NCR_DEBUG_ERROR_RECOVERY_SUPPORT
			if (!np->stalling)
#endif
			OUTB (nc_istat, SIGP);
		}
		restore_flags(flags);
	}

#ifdef SCSI_NCR_BROKEN_INTR
	if (INB(nc_istat) & (INTF|SIP|DIP)) {

		/*
		**	Process pending interrupts.
		*/
		save_flags(flags); cli();
		if (DEBUG_FLAGS & DEBUG_TINY) printf ("{");
		ncr_exception (np);
		if (DEBUG_FLAGS & DEBUG_TINY) printf ("}");
		restore_flags(flags);
	}
#endif /* SCSI_NCR_BROKEN_INTR */
}

/*==========================================================
**
**	log message for real hard errors
**
**	"ncr0 targ 0?: ERROR (ds:si) (so-si-sd) (sxfer/scntl3) @ name (dsp:dbc)."
**	"	      reg: r0 r1 r2 r3 r4 r5 r6 ..... rf."
**
**	exception register:
**		ds:	dstat
**		si:	sist
**
**	SCSI bus lines:
**		so:	control lines as driver by NCR.
**		si:	control lines as seen by NCR.
**		sd:	scsi data lines as seen by NCR.
**
**	wide/fastmode:
**		sxfer:	(see the manual)
**		scntl3:	(see the manual)
**
**	current script command:
**		dsp:	script adress (relative to start of script).
**		dbc:	first word of script command.
**
**	First 16 register of the chip:
**		r0..rf
**
**==========================================================
*/

static void ncr_log_hard_error(ncb_p np, u_short sist, u_char dstat)
{
	u_int32	dsp;
	int	script_ofs;
	int	script_size;
	char	*script_name;
	u_char	*script_base;
	int	i;

	dsp	= INL (nc_dsp);

	if (dsp > np->p_script && dsp <= np->p_script + sizeof(struct script)) {
		script_ofs	= dsp - np->p_script;
		script_size	= sizeof(struct script);
		script_base	= (u_char *) np->script;
		script_name	= "script";
	}
	else if (np->p_scripth < dsp && 
		 dsp <= np->p_scripth + sizeof(struct scripth)) {
		script_ofs	= dsp - np->p_scripth;
		script_size	= sizeof(struct scripth);
		script_base	= (u_char *) np->scripth;
		script_name	= "scripth";
	} else {
		script_ofs	= dsp;
		script_size	= 0;
		script_base	= 0;
		script_name	= "mem";
	}

	printf ("%s:%d: ERROR (%x:%x) (%x-%x-%x) (%x/%x) @ (%s %x:%08x).\n",
		ncr_name (np), (unsigned)INB (nc_ctest0)&0x0f, dstat, sist,
		(unsigned)INB (nc_socl), (unsigned)INB (nc_sbcl), (unsigned)INB (nc_sbdl),
		(unsigned)INB (nc_sxfer),(unsigned)INB (nc_scntl3), script_name, script_ofs,
		(unsigned)INL (nc_dbc));

	if (((script_ofs & 3) == 0) &&
	    (unsigned)script_ofs < script_size) {
		printf ("%s: script cmd = %08x\n", ncr_name(np),
			(int) *(ncrcmd *)(script_base + script_ofs));
	}

        printf ("%s: regdump:", ncr_name(np));
        for (i=0; i<16;i++)
            printf (" %02x", (unsigned)INB_OFF(i));
        printf (".\n");
}

/*============================================================
**
**	ncr chip exception handler.
**
**============================================================
**
**	In normal cases, interrupt conditions occur one at a 
**	time. The ncr is able to stack in some extra registers 
**	other interrupts that will occurs after the first one.
**	But severall interrupts may occur at the same time.
**
**	We probably should only try to deal with the normal 
**	case, but it seems that multiple interrupts occur in 
**	some cases that are not abnormal at all.
**
**	The most frequent interrupt condition is Phase Mismatch.
**	We should want to service this interrupt quickly.
**	A SCSI parity error may be delivered at the same time.
**	The SIR interrupt is not very frequent in this driver, 
**	since the INTFLY is likely used for command completion 
**	signaling.
**	The Selection Timeout interrupt may be triggered with 
**	IID and/or UDC.
**	The SBMC interrupt (SCSI Bus Mode Change) may probably 
**	occur at any time.
**
**	This handler try to deal as cleverly as possible with all
**	the above.
**
**============================================================
*/

void ncr_exception (ncb_p np)
{
	u_char	istat, dstat;
	u_short	sist;
	int	i;

	/*
	**	interrupt on the fly ?
	**	Since the global header may be copied back to a CCB 
	**	using a posted PCI memory write, the last operation on 
	**	the istat register is a READ in order to flush posted 
	**	PCI commands (Btw, the 'do' loop is probably useless).
	*/
	istat = INB (nc_istat);
	if (istat & INTF) {
		do {
			OUTB (nc_istat, (istat & SIGP) | INTF);
			istat = INB (nc_istat);
		} while (istat & INTF);
		if (DEBUG_FLAGS & DEBUG_TINY) printf ("F ");
		np->profile.num_fly++;
		ncr_wakeup (np, 0);
	};

	if (!(istat & (SIP|DIP)))
		return;

	np->profile.num_int++;

	if (istat & CABRT)
		OUTB (nc_istat, CABRT);

	/*
	**	Steinbach's Guideline for Systems Programming:
	**	Never test for an error condition you don't know how to handle.
	*/

	sist  = (istat & SIP) ? INW (nc_sist)  : 0;
	dstat = (istat & DIP) ? INB (nc_dstat) : 0;

	if (DEBUG_FLAGS & DEBUG_TINY)
		printf ("<%d|%x:%x|%x:%x>",
			(int)INB(nc_scr0),
			dstat,sist,
			(unsigned)INL(nc_dsp),
			(unsigned)INL(nc_dbc));

	/*========================================================
	**	First, interrupts we want to service cleanly.
	**
	**	Phase mismatch is the most frequent interrupt, and 
	**	so we have to service it as quickly and as cleanly 
	**	as possible.
	**	Programmed interrupts are rarely used in this driver,
	**	but we must handle them cleanly anyway.
	**	We try to deal with PAR and SBMC combined with 
	**	some other interrupt(s).
	**=========================================================
	*/

	if (!(sist  & (STO|GEN|HTH|SGE|UDC|RST)) &&
	    !(dstat & (MDPE|BF|ABRT|IID))) {
		if ((sist & SBMC) && ncr_int_sbmc (np))
			return;
		if ((sist & PAR)  && ncr_int_par  (np))
			return;
		if (sist & MA) {
			ncr_int_ma (np);
			return;
		}
		if (dstat & SIR) {
			ncr_int_sir (np);
			return;
		}
		/*
		**  DEL 397 - 53C875 Rev 3 - Part Number 609-0392410 - ITEM 2.
		*/
		if (!(sist & (SBMC|PAR)) && !(dstat & SSI)) {
			printf(	"%s: unknown interrupt(s) ignored, "
				"ISTAT=%x DSTAT=%x SIST=%x\n",
				ncr_name(np), istat, dstat, sist);
			return;
		}

		OUTONB (nc_dcntl, (STD|NOCOM));
		return;
	};

	/*========================================================
	**	Now, interrupts that need some fixing up.
	**	Order and multiple interrupts is so less important.
	**
	**	If SRST has been asserted, we just reset the chip.
	**
	**	Selection is intirely handled by the chip. If the 
	**	chip says STO, we trust it. Seems some other 
	**	interrupts may occur at the same time (UDC, IID), so 
	**	we ignore them. In any case we do enough fix-up 
	**	in the service routine.
	**	We just exclude some fatal dma errors.
	**=========================================================
	*/

	if (sist & RST) {
		ncr_init (np, bootverbose ? "scsi reset" : NULL, HS_RESET);
		return;
	};

	if ((sist & STO) &&
		!(dstat & (MDPE|BF|ABRT))) {
	/*
	**	DEL 397 - 53C875 Rev 3 - Part Number 609-0392410 - ITEM 1.
	*/
		OUTONB (nc_ctest3, CLF);

		ncr_int_sto (np);
		return;
	};

	/*=========================================================
	**	Now, interrupts we are not able to recover cleanly.
	**	(At least for the moment).
	**
	**	Do the register dump.
	**	Log message for real hard errors.
	**	Clear all fifos.
	**	For MDPE, BF, ABORT, IID, SGE and HTH we reset the 
	**	BUS and the chip.
	**	We are more soft for UDC.
	**=========================================================
	*/
	if (jiffies - np->regtime > 10*HZ) {
		np->regtime = jiffies;
		for (i = 0; i<sizeof(np->regdump); i++)
			((char*)&np->regdump)[i] = INB_OFF(i);
		np->regdump.nc_dstat = dstat;
		np->regdump.nc_sist  = sist;
	};

	ncr_log_hard_error(np, sist, dstat);

	printf ("%s: have to clear fifos.\n", ncr_name (np));
	OUTB (nc_stest3, TE|CSF);
	OUTONB (nc_ctest3, CLF);

	if ((sist & (SGE)) ||
		(dstat & (MDPE|BF|ABORT|IID))) {
		ncr_start_reset(np, driver_setup.settle_delay);
		return;
	};

	if (sist & HTH) {
		printf ("%s: handshake timeout\n", ncr_name(np));
		ncr_start_reset(np, driver_setup.settle_delay);
		return;
	};

	if (sist & UDC) {
		printf ("%s: unexpected disconnect\n", ncr_name(np));
		if (INB (nc_scr1) != 0xff) {
			OUTB (nc_scr1, HS_UNEXPECTED);
			OUTL (nc_dsp, NCB_SCRIPT_PHYS (np, cleanup));
		};
		ncr_start_reset(np, driver_setup.settle_delay);
		return;
	};

	/*=========================================================
	**	We just miss the cause of the interrupt. :(
	**	Print a message. The timeout will do the real work.
	**=========================================================
	*/
	printf ("%s: unknown interrupt\n", ncr_name(np));
}

/*==========================================================
**
**	ncr chip exception handler for selection timeout
**
**==========================================================
**
**	There seems to be a bug in the 53c810.
**	Although a STO-Interrupt is pending,
**	it continues executing script commands.
**	But it will fail and interrupt (IID) on
**	the next instruction where it's looking
**	for a valid phase.
**
**----------------------------------------------------------
*/

void ncr_int_sto (ncb_p np)
{
	u_long dsa, scratcha, diff;
	ccb_p cp;
	if (DEBUG_FLAGS & DEBUG_TINY) printf ("T");

	/*
	**	look for ccb and set the status.
	*/

	dsa = INL (nc_dsa);
	cp = np->ccb;
	while (cp && (CCB_PHYS (cp, phys) != dsa))
		cp = cp->link_ccb;

	if (cp) {
		cp-> host_status = HS_SEL_TIMEOUT;
		ncr_complete (np, cp);
	};

	/*
	**	repair start queue
	*/

	scratcha = INL (nc_scratcha);
	diff = scratcha - NCB_SCRIPTH_PHYS (np, tryloop);

/*	assert ((diff <= MAX_START * 20) && !(diff % 20));*/

	if ((diff <= MAX_START * 20) && !(diff % 20)) {
		np->script->startpos[0] = cpu_to_scr(scratcha);
		OUTL (nc_dsp, NCB_SCRIPT_PHYS (np, start));
		return;
	};
	ncr_init (np, "selection timeout", HS_FAIL);
	np->disc = 1;
}

/*==========================================================
**
**	ncr chip exception handler for SCSI bus mode change
**
**==========================================================
**
**	spi2-r12 11.2.3 says a transceiver mode change must 
**	generate a reset event and a device that detects a reset 
**	event shall initiate a hard reset. It says also that a
**	device that detects a mode change shall set data transfer 
**	mode to eight bit asynchronous, etc...
**	So, just resetting should be enough.
**	 
**
**----------------------------------------------------------
*/

static int ncr_int_sbmc (ncb_p np)
{
	u_char scsi_mode = INB (nc_stest4) & SMODE;

	printf("%s: SCSI bus mode change from %x to %x, resetting ...\n",
		ncr_name(np), np->scsi_mode, scsi_mode);

	np->scsi_mode = scsi_mode;
	ncr_start_reset(np, driver_setup.settle_delay);

	return 1;
}

/*==========================================================
**
**	ncr chip exception handler for SCSI parity error.
**
**==========================================================
**
**	SCSI parity errors are handled by the SCSI script.
**	So, we just print some message.
**
**----------------------------------------------------------
*/

static int ncr_int_par (ncb_p np)
{
	printf("%s: SCSI parity error detected\n", ncr_name(np));
	return 0;
}

/*==========================================================
**
**
**	ncr chip exception handler for phase errors.
**
**
**==========================================================
**
**	We have to construct a new transfer descriptor,
**	to transfer the rest of the current block.
**
**----------------------------------------------------------
*/

static void ncr_int_ma (ncb_p np)
{
	u_int32	dbc;
	u_int32	rest;
	u_int32	dsp;
	u_int32	dsa;
	u_int32	nxtdsp;
	u_int32	*vdsp;
	u_int32	oadr, olen;
	u_int32	*tblp;
        ncrcmd *newcmd;
	u_char	cmd, sbcl;
	ccb_p	cp;

	dsp	= INL (nc_dsp);
	dbc	= INL (nc_dbc);
	sbcl	= INB (nc_sbcl);

	cmd	= dbc >> 24;
	rest	= dbc & 0xffffff;

	/*
	**	Take into account dma fifo and various buffers and latches,
	**	only if the interrupted phase is an OUTPUT phase.
	*/

	if ((cmd & 1) == 0) {
		u_char	ctest5, ss0, ss2;
		u_short	delta;

		ctest5 = (np->rv_ctest5 & DFS) ? INB (nc_ctest5) : 0;
		if (ctest5 & DFS)
			delta=(((ctest5 << 8) | (INB (nc_dfifo) & 0xff)) - rest) & 0x3ff;
		else
			delta=(INB (nc_dfifo) - rest) & 0x7f;

		/*
		**	The data in the dma fifo has not been transfered to
		**	the target -> add the amount to the rest
		**	and clear the data.
		**	Check the sstat2 register in case of wide transfer.
		*/

		rest += delta;
		ss0  = INB (nc_sstat0);
		if (ss0 & OLF) rest++;
		if (ss0 & ORF) rest++;
		if (INB(nc_scntl3) & EWS) {
			ss2 = INB (nc_sstat2);
			if (ss2 & OLF1) rest++;
			if (ss2 & ORF1) rest++;
		};

		OUTONB (nc_ctest3, CLF );	/* clear dma fifo  */
		OUTB (nc_stest3, TE|CSF);	/* clear scsi fifo */

		if (DEBUG_FLAGS & (DEBUG_TINY|DEBUG_PHASE))
			printf ("P%x%x RL=%d D=%d SS0=%x ", cmd&7, sbcl&7,
				(unsigned) rest, (unsigned) delta, ss0);

	} else	{
		if (DEBUG_FLAGS & (DEBUG_TINY|DEBUG_PHASE))
			printf ("P%x%x RL=%d ", cmd&7, sbcl&7, rest);
		if ((cmd & 7) != 1) {
			OUTONB (nc_ctest3, CLF );
			OUTB (nc_stest3, TE|CSF);
		}
	}

	/*
	**	locate matching cp
	*/
	dsa = INL (nc_dsa);
	cp  = np->ccb;
	while (cp && (CCB_PHYS (cp, phys) != dsa))
		cp = cp->link_ccb;

	if (!cp) {
	    printf ("%s: SCSI phase error fixup: CCB already dequeued (0x%08lx)\n", 
		    ncr_name (np), (u_long) np->header.cp);
	    return;
	}
	if (cp != np->header.cp) {
	    printf ("%s: SCSI phase error fixup: CCB address mismatch (0x%08lx != 0x%08lx)\n", 
		    ncr_name (np), (u_long) cp, (u_long) np->header.cp);
/*	    return;*/
	}

	/*
	**	find the interrupted script command,
	**	and the address at which to continue.
	*/

	if (dsp == vtophys (&cp->patch[2])) {
		vdsp = &cp->patch[0];
		nxtdsp = scr_to_cpu(vdsp[3]);
	} else if (dsp == vtophys (&cp->patch[6])) {
		vdsp = &cp->patch[4];
		nxtdsp = scr_to_cpu(vdsp[3]);
	} else if (dsp > np->p_script && dsp <= np->p_script + sizeof(struct script)) {
		vdsp = (u_int32 *) ((char*)np->script - np->p_script + dsp -8);
		nxtdsp = dsp;
	} else {
		vdsp = (u_int32 *) ((char*)np->scripth - np->p_scripth + dsp -8);
		nxtdsp = dsp;
	};

	/*
	**	log the information
	*/

	if (DEBUG_FLAGS & DEBUG_PHASE) {
		printf ("\nCP=%p CP2=%p DSP=%x NXT=%x VDSP=%p CMD=%x ",
			cp, np->header.cp,
			(unsigned)dsp,
			(unsigned)nxtdsp, vdsp, cmd);
	};

	/*
	**	get old startaddress and old length.
	*/

	oadr = scr_to_cpu(vdsp[1]);

	if (cmd & 0x10) {	/* Table indirect */
		tblp = (u_int32 *) ((char*) &cp->phys + oadr);
		olen = scr_to_cpu(tblp[0]);
		oadr = scr_to_cpu(tblp[1]);
	} else {
		tblp = (u_int32 *) 0;
		olen = scr_to_cpu(vdsp[0]) & 0xffffff;
	};

	if (DEBUG_FLAGS & DEBUG_PHASE) {
		printf ("OCMD=%x\nTBLP=%p OLEN=%x OADR=%x\n",
			(unsigned) (scr_to_cpu(vdsp[0]) >> 24),
			tblp,
			(unsigned) olen,
			(unsigned) oadr);
	};

	/*
	**	check cmd against assumed interrupted script command.
	*/

	if (cmd != (scr_to_cpu(vdsp[0]) >> 24)) {
		PRINT_ADDR(cp->cmd);
		printf ("internal error: cmd=%02x != %02x=(vdsp[0] >> 24)\n",
			(unsigned)cmd, (unsigned)scr_to_cpu(vdsp[0]) >> 24);
		
		return;
	}

#ifdef	SCSI_NCR_DEBUG_ERROR_RECOVERY_SUPPORT
	if ((cmd & 7) == 1 && np->assert_atn) {
		np->assert_atn = 0;
		OUTONB(nc_socl, CATN);
	}
#endif

	/*
	**	if old phase not dataphase, leave here.
	*/

	if (cmd & 0x06) {
		PRINT_ADDR(cp->cmd);
		printf ("phase change %x-%x %d@%08x resid=%d.\n",
			cmd&7, sbcl&7, (unsigned)olen,
			(unsigned)oadr, (unsigned)rest);

		OUTONB (nc_dcntl, (STD|NOCOM));
		return;
	};

	/*
	**	choose the correct patch area.
	**	if savep points to one, choose the other.
	*/

	newcmd = cp->patch;
	if (cp->phys.header.savep == cpu_to_scr(vtophys (newcmd))) newcmd+=4;

	/*
	**	fillin the commands
	*/

	newcmd[0] = cpu_to_scr(((cmd & 0x0f) << 24) | rest);
	newcmd[1] = cpu_to_scr(oadr + olen - rest);
	newcmd[2] = cpu_to_scr(SCR_JUMP);
	newcmd[3] = cpu_to_scr(nxtdsp);

	if (DEBUG_FLAGS & DEBUG_PHASE) {
		PRINT_ADDR(cp->cmd);
		printf ("newcmd[%d] %x %x %x %x.\n",
			(int) (newcmd - cp->patch),
			(unsigned)scr_to_cpu(newcmd[0]),
			(unsigned)scr_to_cpu(newcmd[1]),
			(unsigned)scr_to_cpu(newcmd[2]),
			(unsigned)scr_to_cpu(newcmd[3]));
	}
	/*
	**	fake the return address (to the patch).
	**	and restart script processor at dispatcher.
	*/
	np->profile.num_break++;
	OUTL (nc_temp, vtophys (newcmd));
	if ((cmd & 7) == 0)
		OUTL (nc_dsp, NCB_SCRIPT_PHYS (np, dispatch));
	else
		OUTL (nc_dsp, NCB_SCRIPT_PHYS (np, checkatn));
}

/*==========================================================
**
**
**      ncr chip exception handler for programmed interrupts.
**
**
**==========================================================
*/

static int ncr_show_msg (u_char * msg)
{
	u_char i;
	printf ("%x",*msg);
	if (*msg==M_EXTENDED) {
		for (i=1;i<8;i++) {
			if (i-1>msg[1]) break;
			printf ("-%x",msg[i]);
		};
		return (i+1);
	} else if ((*msg & 0xf0) == 0x20) {
		printf ("-%x",msg[1]);
		return (2);
	};
	return (1);
}

void ncr_int_sir (ncb_p np)
{
	u_char scntl3;
	u_char chg, ofs, per, fak, wide;
	u_char num = INB (nc_dsps);
	ccb_p	cp=0;
	u_long	dsa;
	u_char	target = INB (nc_ctest0) & 0x0f;
	tcb_p	tp     = &np->target[target];
	int     i;
	if (DEBUG_FLAGS & DEBUG_TINY) printf ("I#%d", num);

	switch (num) {
	case SIR_SENSE_RESTART:
	case SIR_STALL_RESTART:
		break;
	case SIR_STALL_QUEUE:	/* Ignore, just restart the script */
		goto out;

	default:
		/*
		**	lookup the ccb
		*/
		dsa = INL (nc_dsa);
		cp = np->ccb;
		while (cp && (CCB_PHYS (cp, phys) != dsa))
			cp = cp->link_ccb;

		assert (cp);
		if (!cp)
			goto out;
		assert (cp == np->header.cp);
		if (cp != np->header.cp)
			goto out;
	}

	switch (num) {
		u_long endp;
	case SIR_DATA_IO_IS_OUT:
	case SIR_DATA_IO_IS_IN:
/*
**	We did not guess the direction of transfer. We have to wait for 
**	actual data direction driven by the target before setting 
**	pointers. We must patch the global header too.
*/
		if (num == SIR_DATA_IO_IS_OUT) {
			endp = NCB_SCRIPTH_PHYS (np, data_out) + MAX_SCATTER*16;
			cp->phys.header.goalp = cpu_to_scr(endp + 8);
			cp->phys.header.savep =
				cpu_to_scr(endp - cp->segments*16);
		} else {
			endp = NCB_SCRIPT_PHYS (np, data_in)  + MAX_SCATTER*16;
			cp->phys.header.goalp = cpu_to_scr(endp + 8);
			cp->phys.header.savep =
				cpu_to_scr(endp - cp->segments*16);
		}

		cp->phys.header.lastp	= cp->phys.header.savep;
		np->header.savep	= cp->phys.header.savep;
		np->header.goalp	= cp->phys.header.goalp;
		np->header.lastp	= cp->phys.header.lastp;

		OUTL (nc_temp,	scr_to_cpu(np->header.savep));
		OUTL (nc_dsp,	scr_to_cpu(np->header.savep));
		return;
		/* break; */

/*--------------------------------------------------------------------
**
**	Processing of interrupted getcc selects
**
**--------------------------------------------------------------------
*/

	case SIR_SENSE_RESTART:
		/*------------------------------------------
		**	Script processor is idle.
		**	Look for interrupted "check cond"
		**------------------------------------------
		*/

		if (DEBUG_FLAGS & DEBUG_RESTART)
			printf ("%s: int#%d",ncr_name (np),num);
		cp = (ccb_p) 0;
		for (i=0; i<MAX_TARGET; i++) {
			if (DEBUG_FLAGS & DEBUG_RESTART) printf (" t%d", i);
			tp = &np->target[i];
			if (DEBUG_FLAGS & DEBUG_RESTART) printf ("+");
			cp = tp->hold_cp;
			if (!cp) continue;
			if (DEBUG_FLAGS & DEBUG_RESTART) printf ("+");
			if ((cp->host_status==HS_BUSY) &&
				(cp->scsi_status==S_CHECK_COND))
				break;
			if (DEBUG_FLAGS & DEBUG_RESTART) printf ("- (remove)");
			tp->hold_cp = cp = (ccb_p) 0;
		};

		if (cp) {
			if (DEBUG_FLAGS & DEBUG_RESTART)
				printf ("+ restart job ..\n");
			OUTL (nc_dsa, CCB_PHYS (cp, phys));
			OUTL (nc_dsp, NCB_SCRIPTH_PHYS (np, getcc));
			return;
		};

		/*
		**	no job, resume normal processing
		*/
		if (DEBUG_FLAGS & DEBUG_RESTART) printf (" -- remove trap\n");
		np->script->start0[0] =  cpu_to_scr(SCR_INT ^ IFFALSE (0));
		break;

	case SIR_SENSE_FAILED:
		/*-------------------------------------------
		**	While trying to select for
		**	getting the condition code,
		**	a target reselected us.
		**-------------------------------------------
		*/
		if (DEBUG_FLAGS & DEBUG_RESTART) {
			PRINT_ADDR(cp->cmd);
			printf ("in getcc reselect by t%d.\n",
				(int)INB(nc_ssid) & 0x0f);
		}

		/*
		**	Mark this job
		*/
		cp->host_status = HS_BUSY;
		cp->scsi_status = S_CHECK_COND;
		np->target[cp->cmd->target].hold_cp = cp;

		/*
		**	And patch code to restart it.
		*/
		np->script->start0[0] =  cpu_to_scr(SCR_INT);
		break;

/*-----------------------------------------------------------------------------
**
**	Was Sie schon immer ueber transfermode negotiation wissen wollten ...
**
**	We try to negotiate sync and wide transfer only after
**	a successfull inquire command. We look at byte 7 of the
**	inquire data to determine the capabilities of the target.
**
**	When we try to negotiate, we append the negotiation message
**	to the identify and (maybe) simple tag message.
**	The host status field is set to HS_NEGOTIATE to mark this
**	situation.
**
**	If the target doesn't answer this message immidiately
**	(as required by the standard), the SIR_NEGO_FAIL interrupt
**	will be raised eventually.
**	The handler removes the HS_NEGOTIATE status, and sets the
**	negotiated value to the default (async / nowide).
**
**	If we receive a matching answer immediately, we check it
**	for validity, and set the values.
**
**	If we receive a Reject message immediately, we assume the
**	negotiation has failed, and fall back to standard values.
**
**	If we receive a negotiation message while not in HS_NEGOTIATE
**	state, it's a target initiated negotiation. We prepare a
**	(hopefully) valid answer, set our parameters, and send back 
**	this answer to the target.
**
**	If the target doesn't fetch the answer (no message out phase),
**	we assume the negotiation has failed, and fall back to default
**	settings.
**
**	When we set the values, we adjust them in all ccbs belonging 
**	to this target, in the controller's register, and in the "phys"
**	field of the controller's struct ncb.
**
**	Possible cases:		   hs  sir   msg_in value  send   goto
**	We try try to negotiate:
**	-> target doesnt't msgin   NEG FAIL  noop   defa.  -      dispatch
**	-> target rejected our msg NEG FAIL  reject defa.  -      dispatch
**	-> target answered  (ok)   NEG SYNC  sdtr   set    -      clrack
**	-> target answered (!ok)   NEG SYNC  sdtr   defa.  REJ--->msg_bad
**	-> target answered  (ok)   NEG WIDE  wdtr   set    -      clrack
**	-> target answered (!ok)   NEG WIDE  wdtr   defa.  REJ--->msg_bad
**	-> any other msgin	   NEG FAIL  noop   defa.  -      dispatch
**
**	Target tries to negotiate:
**	-> incoming message	   --- SYNC  sdtr   set    SDTR   -
**	-> incoming message	   --- WIDE  wdtr   set    WDTR   -
**      We sent our answer:
**	-> target doesn't msgout   --- PROTO ?      defa.  -      dispatch
**
**-----------------------------------------------------------------------------
*/

	case SIR_NEGO_FAILED:
		/*-------------------------------------------------------
		**
		**	Negotiation failed.
		**	Target doesn't send an answer message,
		**	or target rejected our message.
		**
		**      Remove negotiation request.
		**
		**-------------------------------------------------------
		*/
		OUTB (HS_PRT, HS_BUSY);

		/* fall through */

	case SIR_NEGO_PROTO:
		/*-------------------------------------------------------
		**
		**	Negotiation failed.
		**	Target doesn't fetch the answer message.
		**
		**-------------------------------------------------------
		*/

		if (DEBUG_FLAGS & DEBUG_NEGO) {
			PRINT_ADDR(cp->cmd);
			printf ("negotiation failed sir=%x status=%x.\n",
				num, cp->nego_status);
		};

		/*
		**	any error in negotiation:
		**	fall back to default mode.
		*/
		switch (cp->nego_status) {

		case NS_SYNC:
			ncr_setsync (np, cp, 0, 0xe0);
			break;

		case NS_WIDE:
			ncr_setwide (np, cp, 0, 0);
			break;

		};
		np->msgin [0] = M_NOOP;
		np->msgout[0] = M_NOOP;
		cp->nego_status = 0;
		OUTL (nc_dsp, NCB_SCRIPT_PHYS (np, dispatch));
		break;

	case SIR_NEGO_SYNC:
		/*
		**	Synchronous request message received.
		*/

		if (DEBUG_FLAGS & DEBUG_NEGO) {
			PRINT_ADDR(cp->cmd);
			printf ("sync msgin: ");
			(void) ncr_show_msg (np->msgin);
			printf (".\n");
		};

		/*
		**	get requested values.
		*/

		chg = 0;
		per = np->msgin[3];
		ofs = np->msgin[4];
		if (ofs==0) per=255;

		/*
		**      if target sends SDTR message,
		**	      it CAN transfer synch.
		*/

		if (ofs)
			tp->inqdata[7] |= INQ7_SYNC;

		/*
		**	check values against driver limits.
		*/

		if (per < np->minsync)
			{chg = 1; per = np->minsync;}
		if (per < tp->minsync)
			{chg = 1; per = tp->minsync;}
		if (ofs > tp->maxoffs)
			{chg = 1; ofs = tp->maxoffs;}

		/*
		**	Check against controller limits.
		*/
		fak	= 7;
		scntl3	= 0;
		if (ofs != 0) {
			ncr_getsync(np, per, &fak, &scntl3);
			if (fak > 7) {
				chg = 1;
				ofs = 0;
			}
		}
		if (ofs == 0) {
			fak	= 7;
			per	= 0;
			scntl3	= 0;
			tp->minsync = 0;
		}

		if (DEBUG_FLAGS & DEBUG_NEGO) {
			PRINT_ADDR(cp->cmd);
			printf ("sync: per=%d scntl3=0x%x ofs=%d fak=%d chg=%d.\n",
				per, scntl3, ofs, fak, chg);
		}

		if (INB (HS_PRT) == HS_NEGOTIATE) {
			OUTB (HS_PRT, HS_BUSY);
			switch (cp->nego_status) {

			case NS_SYNC:
				/*
				**      This was an answer message
				*/
				if (chg) {
					/*
					**	Answer wasn't acceptable.
					*/
					ncr_setsync (np, cp, 0, 0xe0);
					OUTL (nc_dsp, NCB_SCRIPT_PHYS (np, msg_bad));
				} else {
					/*
					**	Answer is ok.
					*/
					ncr_setsync (np, cp, scntl3, (fak<<5)|ofs);
					OUTL (nc_dsp, NCB_SCRIPT_PHYS (np, clrack));
				};
				return;

			case NS_WIDE:
				ncr_setwide (np, cp, 0, 0);
				break;
			};
		};

		/*
		**	It was a request.
		**      Check against the table of target capabilities.
		**      If target not capable force M_REJECT and asynchronous.
		*/
		if (np->unit < SCSI_NCR_MAX_HOST) {
			tp->inqdata[7] &=
				(target_capabilities[np->unit].and_map[target]);
			if (!(tp->inqdata[7] & INQ7_SYNC)) {
				ofs = 0;
				fak = 7;
			}
		}

		/*
		**	It was a request. Set value and
		**      prepare an answer message
		*/

		ncr_setsync (np, cp, scntl3, (fak<<5)|ofs);

		np->msgout[0] = M_EXTENDED;
		np->msgout[1] = 3;
		np->msgout[2] = M_X_SYNC_REQ;
		np->msgout[3] = per;
		np->msgout[4] = ofs;

		cp->nego_status = NS_SYNC;

		if (DEBUG_FLAGS & DEBUG_NEGO) {
			PRINT_ADDR(cp->cmd);
			printf ("sync msgout: ");
			(void) ncr_show_msg (np->msgout);
			printf (".\n");
		}

		if (!ofs) {
			OUTL (nc_dsp, NCB_SCRIPT_PHYS (np, msg_bad));
			return;
		}
		np->msgin [0] = M_NOOP;

		break;

	case SIR_NEGO_WIDE:
		/*
		**	Wide request message received.
		*/
		if (DEBUG_FLAGS & DEBUG_NEGO) {
			PRINT_ADDR(cp->cmd);
			printf ("wide msgin: ");
			(void) ncr_show_msg (np->msgin);
			printf (".\n");
		};

		/*
		**	get requested values.
		*/

		chg  = 0;
		wide = np->msgin[3];

		/*
		**      if target sends WDTR message,
		**	      it CAN transfer wide.
		*/

		if (wide)
			tp->inqdata[7] |= INQ7_WIDE16;

		/*
		**	check values against driver limits.
		*/

		if (wide > tp->usrwide)
			{chg = 1; wide = tp->usrwide;}

		if (DEBUG_FLAGS & DEBUG_NEGO) {
			PRINT_ADDR(cp->cmd);
			printf ("wide: wide=%d chg=%d.\n", wide, chg);
		}

		if (INB (HS_PRT) == HS_NEGOTIATE) {
			OUTB (HS_PRT, HS_BUSY);
			switch (cp->nego_status) {

			case NS_WIDE:
				/*
				**      This was an answer message
				*/
				if (chg) {
					/*
					**	Answer wasn't acceptable.
					*/
					ncr_setwide (np, cp, 0, 1);
					OUTL (nc_dsp, NCB_SCRIPT_PHYS (np, msg_bad));
				} else {
					/*
					**	Answer is ok.
					*/
					ncr_setwide (np, cp, wide, 1);
					OUTL (nc_dsp, NCB_SCRIPT_PHYS (np, clrack));
				};
				return;

			case NS_SYNC:
				ncr_setsync (np, cp, 0, 0xe0);
				break;
			};
		};

		/*
		**	It was a request, set value and
		**      prepare an answer message
		*/

		ncr_setwide (np, cp, wide, 1);

		np->msgout[0] = M_EXTENDED;
		np->msgout[1] = 2;
		np->msgout[2] = M_X_WIDE_REQ;
		np->msgout[3] = wide;

		np->msgin [0] = M_NOOP;

		cp->nego_status = NS_WIDE;

		if (DEBUG_FLAGS & DEBUG_NEGO) {
			PRINT_ADDR(cp->cmd);
			printf ("wide msgout: ");
			(void) ncr_show_msg (np->msgin);
			printf (".\n");
		}
		break;

/*--------------------------------------------------------------------
**
**	Processing of special messages
**
**--------------------------------------------------------------------
*/

	case SIR_REJECT_RECEIVED:
		/*-----------------------------------------------
		**
		**	We received a M_REJECT message.
		**
		**-----------------------------------------------
		*/

		PRINT_ADDR(cp->cmd);
		printf ("M_REJECT received (%x:%x).\n",
			(unsigned)scr_to_cpu(np->lastmsg), np->msgout[0]);
		break;

	case SIR_REJECT_SENT:
		/*-----------------------------------------------
		**
		**	We received an unknown message
		**
		**-----------------------------------------------
		*/

		PRINT_ADDR(cp->cmd);
		printf ("M_REJECT sent for ");
		(void) ncr_show_msg (np->msgin);
		printf (".\n");
		break;

/*--------------------------------------------------------------------
**
**	Processing of special messages
**
**--------------------------------------------------------------------
*/

	case SIR_IGN_RESIDUE:
		/*-----------------------------------------------
		**
		**	We received an IGNORE RESIDUE message,
		**	which couldn't be handled by the script.
		**
		**-----------------------------------------------
		*/

		PRINT_ADDR(cp->cmd);
		printf ("M_IGN_RESIDUE received, but not yet implemented.\n");
		break;

	case SIR_MISSING_SAVE:
		/*-----------------------------------------------
		**
		**	We received an DISCONNECT message,
		**	but the datapointer wasn't saved before.
		**
		**-----------------------------------------------
		*/

		PRINT_ADDR(cp->cmd);
		printf ("M_DISCONNECT received, but datapointer not saved: "
			"data=%x save=%x goal=%x.\n",
			(unsigned) INL (nc_temp),
			(unsigned) scr_to_cpu(np->header.savep),
			(unsigned) scr_to_cpu(np->header.goalp));
		break;

#if 0   /* This stuff does not work */
/*--------------------------------------------------------------------
**
**	Processing of a "S_QUEUE_FULL" status.
**
**	The current command has been rejected,
**	because there are too many in the command queue.
**	We have started too many commands for that target.
**
**	If possible, reinsert at head of queue.
**	Stall queue until there are no disconnected jobs
**	(ncr is REALLY idle). Then restart processing.
**
**	We should restart the current job after the controller
**	has become idle. But this is not yet implemented.
**
**--------------------------------------------------------------------
*/
	case SIR_STALL_QUEUE:
		/*-----------------------------------------------
		**
		**	Stall the start queue.
		**
		**-----------------------------------------------
		*/
		PRINT_ADDR(cp->cmd);
		printf ("queue full.\n");

		np->script->start1[0] =  cpu_to_scr(SCR_INT);

		/*
		**	Try to disable tagged transfers.
		*/
		ncr_setmaxtags (np, &np->target[target], 0);

		/*
		** @QUEUE@
		**
		**	Should update the launch field of the
		**	current job to be able to restart it.
		**	Then prepend it to the start queue.
		*/

		/* fall through */

	case SIR_STALL_RESTART:
		/*-----------------------------------------------
		**
		**	Enable selecting again,
		**	if NO disconnected jobs.
		**
		**-----------------------------------------------
		*/
		/*
		**	Look for a disconnected job.
		*/
		cp = np->ccb;
		while (cp && cp->host_status != HS_DISCONNECT)
			cp = cp->link_ccb;

		/*
		**	if there is one, ...
		*/
		if (cp) {
			/*
			**	wait for reselection
			*/
			OUTL (nc_dsp, NCB_SCRIPT_PHYS (np, reselect));
			return;
		};

		/*
		**	else remove the interrupt.
		*/

		printf ("%s: queue empty.\n", ncr_name (np));
		np->script->start1[0] =  cpu_to_scr(SCR_INT ^ IFFALSE (0));
		break;
#endif   /* This stuff does not work */
	};

out:
	OUTONB (nc_dcntl, (STD|NOCOM));
}

/*==========================================================
**
**
**	Aquire a control block
**
**
**==========================================================
*/

static	ccb_p ncr_get_ccb
	(ncb_p np, u_long target, u_long lun)
{
	lcb_p lp;
	ccb_p cp = (ccb_p) 0;

	/*
	**	Lun structure available ?
	*/

	lp = np->target[target].lp[lun];

	if (lp && lp->opennings && (!lp->active || lp->active < lp->reqlink)) {

		cp = lp->next_ccb;

		/*
		**	Look for free CCB
		*/

		while (cp && cp->magic) cp = cp->next_ccb;

		/*
		**	Increment active commands and decrement credit.
		*/

		if (cp) {
			++lp->active;
			--lp->opennings;
		}
	}

	/*
	**	if nothing available, take the default.
	**	DANGEROUS, because this ccb is not suitable for
	**	reselection.
	**	If lp->actccbs > 0 wait for a suitable ccb to be free.
	*/
	if ((!cp) && lp && lp->actccbs > 0)
		return ((ccb_p) 0);

	if (!cp) cp = np->ccb;

	/*
	**	Wait until available.
	*/
#if 0
	while (cp->magic) {
		if (flags & SCSI_NOSLEEP) break;
		if (tsleep ((caddr_t)cp, PRIBIO|PCATCH, "ncr", 0))
			break;
	};
#endif

	if (cp->magic)
		return ((ccb_p) 0);

	cp->magic = 1;
	return (cp);
}

/*==========================================================
**
**
**	Release one control block
**
**
**==========================================================
*/

void ncr_free_ccb (ncb_p np, ccb_p cp, u_long target, u_long lun)
{
	lcb_p lp;

	/*
	**    sanity
	*/

	assert (cp != NULL);

	/*
	**	Decrement active commands and increment credit.
	*/

	lp = np->target[target].lp[lun];
	if (lp) {
			--lp->active;
			++lp->opennings;
	}

	cp -> host_status = HS_IDLE;
	cp -> magic = 0;
#if 0
	if (cp == np->ccb)
		wakeup ((caddr_t) cp);
#endif
}

/*==========================================================
**
**
**      Allocation of resources for Targets/Luns/Tags.
**
**
**==========================================================
*/

static	void ncr_alloc_ccb (ncb_p np, u_long target, u_long lun)
{
	tcb_p tp;
	lcb_p lp;
	ccb_p cp;

	assert (np != NULL);

	if (target>=MAX_TARGET) return;
	if (lun   >=MAX_LUN   ) return;

	tp=&np->target[target];

	if (!tp->jump_tcb.l_cmd) {
		/*
		**	initialize it.
		*/
		tp->jump_tcb.l_cmd   =
			cpu_to_scr((SCR_JUMP^IFFALSE (DATA (0x80 + target))));
		tp->jump_tcb.l_paddr = np->jump_tcb.l_paddr;

		tp->getscr[0] =	(np->features & FE_PFEN) ?
			cpu_to_scr(SCR_COPY(1)):cpu_to_scr(SCR_COPY_F(1));
		tp->getscr[1] = cpu_to_scr(vtophys (&tp->sval));
		tp->getscr[2] =
		cpu_to_scr(pcivtophys(np->paddr) + offsetof (struct ncr_reg, nc_sxfer));

		tp->getscr[3] =	(np->features & FE_PFEN) ?
			cpu_to_scr(SCR_COPY(1)):cpu_to_scr(SCR_COPY_F(1));
		tp->getscr[4] = cpu_to_scr(vtophys (&tp->wval));
		tp->getscr[5] =
		cpu_to_scr(pcivtophys(np->paddr) + offsetof (struct ncr_reg, nc_scntl3));

		assert (( (offsetof(struct ncr_reg, nc_sxfer) ^
			offsetof(struct tcb    , sval    )) &3) == 0);
		assert (( (offsetof(struct ncr_reg, nc_scntl3) ^
			offsetof(struct tcb    , wval    )) &3) == 0);

		tp->call_lun.l_cmd   = cpu_to_scr(SCR_CALL);
		tp->call_lun.l_paddr =
			cpu_to_scr(NCB_SCRIPT_PHYS (np, resel_lun));

		tp->jump_lcb.l_cmd   = cpu_to_scr(SCR_JUMP);
		tp->jump_lcb.l_paddr = cpu_to_scr(NCB_SCRIPTH_PHYS (np, abort));
		np->jump_tcb.l_paddr = cpu_to_scr(vtophys (&tp->jump_tcb));
	}

	/*
	**	Logic unit control block
	*/
	lp = tp->lp[lun];
	if (!lp) {
		/*
		**	Allocate a lcb
		*/
		lp = (lcb_p) m_alloc (sizeof (struct lcb), LCB_ALIGN_SHIFT);
		if (!lp) return;

		if (DEBUG_FLAGS & DEBUG_ALLOC) {
			PRINT_LUN(np, target, lun);
			printf ("new lcb @%p.\n", lp);
		}

		/*
		**	Initialize it
		*/
		bzero (lp, sizeof (*lp));
		lp->jump_lcb.l_cmd   =
			cpu_to_scr(SCR_JUMP ^ IFFALSE (DATA (lun)));
		lp->jump_lcb.l_paddr = tp->jump_lcb.l_paddr;

		lp->call_tag.l_cmd   = cpu_to_scr(SCR_CALL);
		lp->call_tag.l_paddr =
			cpu_to_scr(NCB_SCRIPT_PHYS (np, resel_tag));

		lp->jump_ccb.l_cmd   = cpu_to_scr(SCR_JUMP);
		lp->jump_ccb.l_paddr =
			cpu_to_scr(NCB_SCRIPTH_PHYS (np, aborttag));

		lp->actlink = 1;

		lp->active  = 1;

		/*
		**   Chain into LUN list
		*/
		tp->jump_lcb.l_paddr = cpu_to_scr(vtophys (&lp->jump_lcb));
		tp->lp[lun] = lp;

		ncr_setmaxtags (np, tp, driver_setup.default_tags);
	}

	/*
	**	Allocate ccbs up to lp->reqccbs.
	*/

	/*
	**	Limit possible number of ccbs.
	**
	**	If tagged command queueing is enabled,
	**	can use more than one ccb.
	*/
	if (np->actccbs >= MAX_START-2) return;
	if (lp->actccbs && (lp->actccbs >= lp->reqccbs))
		return;

	/*
	**	Allocate a ccb
	*/
	cp = (ccb_p) m_alloc (sizeof (struct ccb), CCB_ALIGN_SHIFT);
	if (!cp)
		return;

	if (DEBUG_FLAGS & DEBUG_ALLOC) {
		PRINT_LUN(np, target, lun);
		printf ("new ccb @%p.\n", cp);
	}

	/*
	**	Count it
	*/
	lp->actccbs++;
	np->actccbs++;

	/*
	**	Initialize it
	*/
	bzero (cp, sizeof (*cp));

	/*
	**	Fill in physical addresses
	*/

	cp->p_ccb	     = vtophys (cp);

	/*
	**	Chain into reselect list
	*/
	cp->jump_ccb.l_cmd   = cpu_to_scr(SCR_JUMP);
	cp->jump_ccb.l_paddr = lp->jump_ccb.l_paddr;
	lp->jump_ccb.l_paddr = cpu_to_scr(CCB_PHYS (cp, jump_ccb));
	cp->call_tmp.l_cmd   = cpu_to_scr(SCR_CALL);
	cp->call_tmp.l_paddr = cpu_to_scr(NCB_SCRIPT_PHYS (np, resel_tmp));

	/*
	**	Chain into wakeup list
	*/
	cp->link_ccb      = np->ccb->link_ccb;
	np->ccb->link_ccb = cp;

	/*
	**	Chain into CCB list
	*/
	cp->next_ccb	= lp->next_ccb;
	lp->next_ccb	= cp;
}

/*==========================================================
**
**
**	Announce the number of ccbs/tags to the scsi driver.
**
**
**==========================================================
*/

static void ncr_opennings (ncb_p np, lcb_p lp, Scsi_Cmnd * cmd)
{
	/*
	**	want to reduce the number ...
	*/
	if (lp->actlink > lp->reqlink) {

		/*
		**	Try to  reduce the count.
		**	We assume to run at splbio ..
		*/
		u_char diff = lp->actlink - lp->reqlink;

		if (!diff) return;

		if (diff > lp->opennings)
			diff = lp->opennings;

		lp->opennings	-= diff;

		lp->actlink	-= diff;
		if (DEBUG_FLAGS & DEBUG_TAGS)
			printf ("%s: actlink: diff=%d, new=%d, req=%d\n",
				ncr_name(np), diff, lp->actlink, lp->reqlink);
		return;
	};

	/*
	**	want to increase the number ?
	*/
	if (lp->reqlink > lp->actlink) {
		u_char diff = lp->reqlink - lp->actlink;

		lp->opennings	+= diff;

		lp->actlink	+= diff;
#if 0
		wakeup ((caddr_t) xp->sc_link);
#endif
		if (DEBUG_FLAGS & DEBUG_TAGS)
			printf ("%s: actlink: diff=%d, new=%d, req=%d\n",
				ncr_name(np), diff, lp->actlink, lp->reqlink);
	};
}

/*==========================================================
**
**
**	Build Scatter Gather Block
**
**
**==========================================================
**
**	The transfer area may be scattered among
**	several non adjacent physical pages.
**
**	We may use MAX_SCATTER blocks.
**
**----------------------------------------------------------
*/

/*
**	We try to reduce the number of interrupts caused
**	by unexpected phase changes due to disconnects.
**	A typical harddisk may disconnect before ANY block.
**	If we wanted to avoid unexpected phase changes at all
**	we had to use a break point every 512 bytes.
**	Of course the number of scatter/gather blocks is
**	limited.
**	Under Linux, the scatter/gatter blocks are provided by 
**	the generic driver. We just have to copy addresses and 
**	sizes to the data segment array.
*/

static	int	ncr_scatter(ccb_p cp, Scsi_Cmnd *cmd)
{
	struct scr_tblmove *data;
	int segment	= 0;
	int use_sg	= (int) cmd->use_sg;

#if 0
	bzero (cp->phys.data, sizeof (cp->phys.data));
#endif
	data		= cp->phys.data;
	cp->data_len	= 0;

	if (!use_sg) {
		if (cmd->request_bufflen) {
			data = &data[MAX_SCATTER - 1];
			data[0].addr = cpu_to_scr(vtophys(cmd->request_buffer));
			data[0].size = cpu_to_scr(cmd->request_bufflen);
			cp->data_len = cmd->request_bufflen;
			segment = 1;
		}
	}
	else if (use_sg <= MAX_SCATTER) {
		struct scatterlist *scatter = (struct scatterlist *)cmd->buffer;

		data = &data[MAX_SCATTER - use_sg];
		while (segment < use_sg) {
			data[segment].addr =
				cpu_to_scr(vtophys(scatter[segment].address));
			data[segment].size =
				cpu_to_scr(scatter[segment].length);
			cp->data_len	   += scatter[segment].length;
			++segment;
		}
	}
	else {
		return -1;
	}

	return segment;
}

/*==========================================================
**
**
**	Test the pci bus snoop logic :-(
**
**	Has to be called with interrupts disabled.
**
**
**==========================================================
*/

#ifndef NCR_IOMAPPED
__initfunc(
static int ncr_regtest (struct ncb* np)
)
{
	register volatile u_long data;
	/*
	**	ncr registers may NOT be cached.
	**	write 0xffffffff to a read only register area,
	**	and try to read it back.
	*/
	data = 0xffffffff;
	OUTL_OFF(offsetof(struct ncr_reg, nc_dstat), data);
	data = INL_OFF(offsetof(struct ncr_reg, nc_dstat));
#if 1
	if (data == 0xffffffff) {
#else
	if ((data & 0xe2f0fffd) != 0x02000080) {
#endif
		printf ("CACHE TEST FAILED: reg dstat-sstat2 readback %x.\n",
			(unsigned) data);
		return (0x10);
	};
	return (0);
}
#endif

__initfunc(
static int ncr_snooptest (struct ncb* np)
)
{
	u_long	ncr_rd, ncr_wr, ncr_bk, host_rd, host_wr, pc, err=0;
	int	i;
#ifndef NCR_IOMAPPED
	if (np->reg) {
            err |= ncr_regtest (np);
            if (err) return (err);
	}
#endif
	/*
	**	init
	*/
	pc  = NCB_SCRIPTH_PHYS (np, snooptest);
	host_wr = 1;
	ncr_wr  = 2;
	/*
	**	Set memory and register.
	*/
	np->ncr_cache = cpu_to_scr(host_wr);
	OUTL (nc_temp, ncr_wr);
	/*
	**	Start script (exchange values)
	*/
	OUTL (nc_dsp, pc);
	/*
	**	Wait 'til done (with timeout)
	*/
	for (i=0; i<NCR_SNOOP_TIMEOUT; i++)
		if (INB(nc_istat) & (INTF|SIP|DIP))
			break;
	/*
	**	Save termination position.
	*/
	pc = INL (nc_dsp);
	/*
	**	Read memory and register.
	*/
	host_rd = scr_to_cpu(np->ncr_cache);
	ncr_rd  = INL (nc_scratcha);
	ncr_bk  = INL (nc_temp);
	/*
	**	Reset ncr chip
	*/
	OUTB (nc_istat,  SRST);
	DELAY (1000);
	OUTB (nc_istat,  0   );
	/*
	**	check for timeout
	*/
	if (i>=NCR_SNOOP_TIMEOUT) {
		printf ("CACHE TEST FAILED: timeout.\n");
		return (0x20);
	};
	/*
	**	Check termination position.
	*/
	if (pc != NCB_SCRIPTH_PHYS (np, snoopend)+8) {
		printf ("CACHE TEST FAILED: script execution failed.\n");
		printf ("start=%08lx, pc=%08lx, end=%08lx\n", 
			(u_long) NCB_SCRIPTH_PHYS (np, snooptest), pc,
			(u_long) NCB_SCRIPTH_PHYS (np, snoopend) +8);
		return (0x40);
	};
	/*
	**	Show results.
	*/
	if (host_wr != ncr_rd) {
		printf ("CACHE TEST FAILED: host wrote %d, ncr read %d.\n",
			(int) host_wr, (int) ncr_rd);
		err |= 1;
	};
	if (host_rd != ncr_wr) {
		printf ("CACHE TEST FAILED: ncr wrote %d, host read %d.\n",
			(int) ncr_wr, (int) host_rd);
		err |= 2;
	};
	if (ncr_bk != ncr_wr) {
		printf ("CACHE TEST FAILED: ncr wrote %d, read back %d.\n",
			(int) ncr_wr, (int) ncr_bk);
		err |= 4;
	};
	return (err);
}

/*==========================================================
**
**
**	Profiling the drivers and targets performance.
**
**
**==========================================================
*/

#ifdef SCSI_NCR_PROFILE_SUPPORT

/*
**	Compute the difference in jiffies ticks.
*/

#define ncr_delta(from, to) \
	( ((to) && (from))? (to) - (from) : -1 )

#define PROFILE  cp->phys.header.stamp
static	void ncb_profile (ncb_p np, ccb_p cp)
{
	long co, st, en, di, se, post, work, disc;
	u_int diff;

	PROFILE.end = jiffies;

	st = ncr_delta (PROFILE.start,PROFILE.status);
	if (st<0) return;	/* status  not reached  */

	co = ncr_delta (PROFILE.start,PROFILE.command);
	if (co<0) return;	/* command not executed */

	en = ncr_delta (PROFILE.start,PROFILE.end),
	di = ncr_delta (PROFILE.start,PROFILE.disconnect),
	se = ncr_delta (PROFILE.start,PROFILE.select);
	post = en - st;

	/*
	**	@PROFILE@  Disconnect time invalid if multiple disconnects
	*/

	if (di>=0) disc = se-di; else  disc = 0;

	work = (st - co) - disc;

	diff = (scr_to_cpu(np->disc_phys) - np->disc_ref) & 0xff;
	np->disc_ref += diff;

	np->profile.num_trans	+= 1;
	if (cp->cmd) {
		np->profile.num_kbytes	+= (cp->cmd->request_bufflen >> 10);
		np->profile.rest_bytes	+= (cp->cmd->request_bufflen & (0x400-1));
		if (np->profile.rest_bytes >= 0x400) {
			++np->profile.num_kbytes;
			np->profile.rest_bytes	-= 0x400;
		}
	}
	np->profile.num_disc	+= diff;
	np->profile.ms_setup	+= co;
	np->profile.ms_data	+= work;
	np->profile.ms_disc	+= disc;
	np->profile.ms_post	+= post;
}
#undef PROFILE

#endif /* SCSI_NCR_PROFILE_SUPPORT */

/*==========================================================
**
**
**	Device lookup.
**
**	@GENSCSI@ should be integrated to scsiconf.c
**
**
**==========================================================
*/

struct table_entry {
	char *	manufacturer;
	char *	model;
	char *	version;
	u_long	info;
};

static struct table_entry device_tab[] =
{
#ifdef NCR_GETCC_WITHMSG
	{"", "", "", QUIRK_NOMSG},
	{"SONY", "SDT-5000", "3.17", QUIRK_NOMSG},
	{"WangDAT", "Model 2600", "01.7", QUIRK_NOMSG},
	{"WangDAT", "Model 3200", "02.2", QUIRK_NOMSG},
	{"WangDAT", "Model 1300", "02.4", QUIRK_NOMSG},
#endif
	{"", "", "", 0} /* catch all: must be last entry. */
};

static u_long ncr_lookup(char * id)
{
	struct table_entry * p = device_tab;
	char *d, *r, c;

	for (;;p++) {

		d = id+8;
		r = p->manufacturer;
		while ((c=*r++)) if (c!=*d++) break;
		if (c) continue;

		d = id+16;
		r = p->model;
		while ((c=*r++)) if (c!=*d++) break;
		if (c) continue;

		d = id+32;
		r = p->version;
		while ((c=*r++)) if (c!=*d++) break;
		if (c) continue;

		return (p->info);
	}
}

/*==========================================================
**
**	Determine the ncr's clock frequency.
**	This is essential for the negotiation
**	of the synchronous transfer rate.
**
**==========================================================
**
**	Note: we have to return the correct value.
**	THERE IS NO SAVE DEFAULT VALUE.
**
**	Most NCR/SYMBIOS boards are delivered with a 40 Mhz clock.
**	53C860 and 53C875 rev. 1 support fast20 transfers but 
**	do not have a clock doubler and so are provided with a 
**	80 MHz clock. All other fast20 boards incorporate a doubler 
**	and so should be delivered with a 40 MHz clock.
**	The future fast40 chips (895/895) use a 40 Mhz base clock 
**	and provide a clock quadrupler (160 Mhz). The code below 
**	tries to deal as cleverly as possible with all this stuff.
**
**----------------------------------------------------------
*/

/*
 *	Select NCR SCSI clock frequency
 */
static void ncr_selectclock(ncb_p np, u_char scntl3)
{
	if (np->multiplier < 2) {
		OUTB(nc_scntl3,	scntl3);
		return;
	}

	if (bootverbose >= 2)
		printf ("%s: enabling clock multiplier\n", ncr_name(np));

	OUTB(nc_stest1, DBLEN);	   /* Enable clock multiplier		  */
	if (np->multiplier > 2) {  /* Poll bit 5 of stest4 for quadrupler */
		int i = 20;
		while (!(INB(nc_stest4) & LCKFRQ) && --i > 0)
			DELAY(20);
		if (!i)
			printf("%s: the chip cannot lock the frequency\n", ncr_name(np));
	} else			/* Wait 20 micro-seconds for doubler	*/
		DELAY(20);
	OUTB(nc_stest3, HSC);		/* Halt the scsi clock		*/
	OUTB(nc_scntl3,	scntl3);
	OUTB(nc_stest1, (DBLEN|DBLSEL));/* Select clock multiplier	*/
	OUTB(nc_stest3, 0x00);		/* Restart scsi clock 		*/
}


/*
 *	calculate NCR SCSI clock frequency (in KHz)
 */
__initfunc(
static unsigned ncrgetfreq (ncb_p np, int gen)
)
{
	unsigned ms = 0;

	/*
	 * Measure GEN timer delay in order 
	 * to calculate SCSI clock frequency
	 *
	 * This code will never execute too
	 * many loop iterations (if DELAY is 
	 * reasonably correct). It could get
	 * too low a delay (too high a freq.)
	 * if the CPU is slow executing the 
	 * loop for some reason (an NMI, for
	 * example). For this reason we will
	 * if multiple measurements are to be 
	 * performed trust the higher delay 
	 * (lower frequency returned).
	 */
	OUTB (nc_stest1, 0);	/* make sure clock doubler is OFF */
	OUTW (nc_sien , 0);	/* mask all scsi interrupts */
	(void) INW (nc_sist);	/* clear pending scsi interrupt */
	OUTB (nc_dien , 0);	/* mask all dma interrupts */
	(void) INW (nc_sist);	/* another one, just to be sure :) */
	OUTB (nc_scntl3, 4);	/* set pre-scaler to divide by 3 */
	OUTB (nc_stime1, 0);	/* disable general purpose timer */
	OUTB (nc_stime1, gen);	/* set to nominal delay of 1<<gen * 125us */
	while (!(INW(nc_sist) & GEN) && ms++ < 100000)
		DELAY(1000);	/* count ms */
	OUTB (nc_stime1, 0);	/* disable general purpose timer */
 	/*
 	 * set prescaler to divide by whatever 0 means
 	 * 0 ought to choose divide by 2, but appears
 	 * to set divide by 3.5 mode in my 53c810 ...
 	 */
 	OUTB (nc_scntl3, 0);

	if (bootverbose >= 2)
		printf ("%s: Delay (GEN=%d): %u msec\n", ncr_name(np), gen, ms);
  	/*
 	 * adjust for prescaler, and convert into KHz 
  	 */
	return ms ? ((1 << gen) * 4340) / ms : 0;
}

/*
 *	Get/probe NCR SCSI clock frequency
 */
__initfunc(
static void ncr_getclock (ncb_p np, int mult)
)
{
	unsigned char scntl3 = INB(nc_scntl3);
	unsigned char stest1 = INB(nc_stest1);
	unsigned f1;

	np->multiplier = 1;
	f1 = 40000;

	/*
	**	True with 875 or 895 with clock multiplier selected
	*/
	if (mult > 1 && (stest1 & (DBLEN+DBLSEL)) == DBLEN+DBLSEL) {
		if (bootverbose >= 2)
			printf ("%s: clock multiplier found\n", ncr_name(np));
		np->multiplier = mult;
	}

	/*
	**	If multiplier not found or scntl3 not 7,5,3,
	**	reset chip and get frequency from general purpose timer.
	**	Otherwise trust scntl3 BIOS setting.
	*/
	if (np->multiplier != mult || (scntl3 & 7) < 3 || !(scntl3 & 1)) {
		unsigned f2;

		OUTB(nc_istat, SRST); DELAY(5); OUTB(nc_istat, 0);

		(void) ncrgetfreq (np, 11);	/* throw away first result */
		f1 = ncrgetfreq (np, 11);
		f2 = ncrgetfreq (np, 11);

		if (bootverbose)
			printf ("%s: NCR clock is %uKHz, %uKHz\n", ncr_name(np), f1, f2);

		if (f1 > f2) f1 = f2;		/* trust lower result	*/

		if	(f1 <	45000)		f1 =  40000;
		else if (f1 <	55000)		f1 =  50000;
		else				f1 =  80000;

		if (f1 < 80000 && mult > 1) {
			if (bootverbose >= 2)
				printf ("%s: clock multiplier assumed\n", ncr_name(np));
			np->multiplier	= mult;
		}
	} else {
		if	((scntl3 & 7) == 3)	f1 =  40000;
		else if	((scntl3 & 7) == 5)	f1 =  80000;
		else 				f1 = 160000;

		f1 /= np->multiplier;
	}

	/*
	**	Compute controller synchronous parameters.
	*/
	f1		*= np->multiplier;
	np->clock_khz	= f1;
}

/*===================== LINUX ENTRY POINTS SECTION ==========================*/

#ifndef uchar
#define uchar unsigned char
#endif

#ifndef ushort
#define ushort unsigned short
#endif

#ifndef ulong
#define ulong unsigned long
#endif

/* ---------------------------------------------------------------------
**
**	Driver setup from the boot command line
**
** ---------------------------------------------------------------------
*/

__initfunc(
void ncr53c8xx_setup(char *str, int *ints)
)
{
#ifdef SCSI_NCR_BOOT_COMMAND_LINE_SUPPORT
	char *cur = str;
	char *pc, *pv;
	int val;
	int base;
	int c;

	while (cur != NULL && (pc = strchr(cur, ':')) != NULL) {
		val = 0;
		pv = pc;
		c = *++pv;
		if	(c == 'n')
			val = 0;
		else if	(c == 'y')
			val = 1;
		else {
			base = 0;
#if 0
			if	(c == '0') {
				c = *pv++;
				base = 8;
			}
			if	(c == 'x') {
				++pv;
				base = 16;
			}
			else if (c >= '0' && c <= '9')
				base = 10;
			else
				break;
#endif
			val = (int) simple_strtoul(pv, NULL, base);
		}

		if	(!strncmp(cur, "mpar:", 5))
			driver_setup.master_parity	= val;
		else if	(!strncmp(cur, "spar:", 5))
			driver_setup.scsi_parity	= val;
		else if	(!strncmp(cur, "disc:", 5))
			driver_setup.disconnection	= val;
		else if	(!strncmp(cur, "specf:", 6))
			driver_setup.special_features = val;
		else if	(!strncmp(cur, "ultra:", 6))
			driver_setup.ultra_scsi	= val;
		else if	(!strncmp(cur, "fsn:", 4))
			driver_setup.force_sync_nego	= val;
		else if	(!strncmp(cur, "revprob:", 8))
			driver_setup.reverse_probe	= val;
		else if	(!strncmp(cur, "tags:", 5)) {
			if (val > SCSI_NCR_MAX_TAGS)
				val = SCSI_NCR_MAX_TAGS;
			driver_setup.default_tags	= val;
			}
		else if	(!strncmp(cur, "sync:", 5))
			driver_setup.default_sync	= val;
		else if	(!strncmp(cur, "verb:", 5))
			driver_setup.verbose	= val;
		else if	(!strncmp(cur, "debug:", 6))
			driver_setup.debug	= val;
		else if	(!strncmp(cur, "burst:", 6))
			driver_setup.burst_max	= val;
		else if	(!strncmp(cur, "led:", 4))
			driver_setup.led_pin	= val;
		else if	(!strncmp(cur, "wide:", 5))
			driver_setup.max_wide	= val? 1:0;
		else if	(!strncmp(cur, "settle:", 7))
			driver_setup.settle_delay= val;
		else if	(!strncmp(cur, "diff:", 5))
			driver_setup.diff_support= val;
		else if	(!strncmp(cur, "irqm:", 5))
			driver_setup.irqm	= val;
		else if	(!strncmp(cur, "pcifix:", 7))
			driver_setup.pci_fix_up	= val;
		else if	(!strncmp(cur, "buschk:", 7))
			driver_setup.bus_check	= val;
#ifdef SCSI_NCR_NVRAM_SUPPORT
		else if	(!strncmp(cur, "nvram:", 6))
			driver_setup.use_nvram	= val;
#endif

		else if	(!strncmp(cur, "safe:", 5) && val)
			memcpy(&driver_setup, &driver_safe_setup, sizeof(driver_setup));
		else
			printf("ncr53c8xx_setup: unexpected boot option '%.*s' ignored\n", (int)(pc-cur+1), cur);

#ifdef MODULE
		if ((cur = strchr(cur, ' ')) != NULL)
#else
		if ((cur = strchr(cur, ',')) != NULL)
#endif
			++cur;
	}
#endif /* SCSI_NCR_BOOT_COMMAND_LINE_SUPPORT */
}

static int ncr53c8xx_pci_init(Scsi_Host_Template *tpnt,
	     uchar bus, uchar device_fn, ncr_device *device);

/*
**   Linux entry point for NCR53C8XX devices detection routine.
**
**   Called by the middle-level scsi drivers at initialization time,
**   or at module installation.
**
**   Read the PCI configuration and try to attach each
**   detected NCR board.
**
**   If NVRAM is present, try to attach boards according to 
**   the used defined boot order.
**
**   Returns the number of boards successfully attached.
*/

__initfunc(
static void ncr_print_driver_setup(void)
)
{
#define YesNo(y)	y ? 'y' : 'n'
	printk("ncr53c8xx: setup=disc:%c,specf:%d,ultra:%c,tags:%d,sync:%d,burst:%d,wide:%c,diff:%d\n",
			YesNo(driver_setup.disconnection),
			driver_setup.special_features,
			YesNo(driver_setup.ultra_scsi),
			driver_setup.default_tags,
			driver_setup.default_sync,
			driver_setup.burst_max,
			YesNo(driver_setup.max_wide),
			driver_setup.diff_support);
	printk("ncr53c8xx: setup=mpar:%c,spar:%c,fsn=%c,verb:%d,debug:0x%x,led:%c,settle:%d,irqm:%d\n",
			YesNo(driver_setup.master_parity),
			YesNo(driver_setup.scsi_parity),
			YesNo(driver_setup.force_sync_nego),
			driver_setup.verbose,
			driver_setup.debug,
			YesNo(driver_setup.led_pin),
			driver_setup.settle_delay,
			driver_setup.irqm);
#undef YesNo
}

/*
**   NCR53C8XX devices description table and chip ids list.
*/

static ncr_chip	ncr_chip_table[] __initdata	= SCSI_NCR_CHIP_TABLE;
static ushort	ncr_chip_ids[]   __initdata	= SCSI_NCR_CHIP_IDS;

#ifdef SCSI_NCR_NVRAM_SUPPORT
__initfunc(
static int
ncr_attach_using_nvram(Scsi_Host_Template *tpnt, int nvram_index, int count, ncr_device device[])
)
{
	int i, j;
	int attach_count = 0;
	ncr_nvram  *nvram;
	ncr_device *devp = 0;	/* to shut up gcc */

	if (!nvram_index)
		return 0;

	/* find first Symbios NVRAM if there is one as we need to check it for host boot order */
	for (i = 0, nvram_index = -1; i < count; i++) {
		devp  = &device[i];
		nvram = devp->nvram;
		if (!nvram)
			continue;
		if (nvram->type == SCSI_NCR_SYMBIOS_NVRAM) {
			if (nvram_index == -1)
				nvram_index = i;
#ifdef SCSI_NCR_DEBUG_NVRAM
			printf("ncr53c8xx: NVRAM: Symbios format Boot Block, 53c%s, PCI bus %d, device %d, function %d\n",
				devp->chip.name, devp->slot.bus, 
				(int) (devp->slot.device_fn & 0xf8) >> 3, 
				(int) devp->slot.device_fn & 7);
			for (j = 0 ; j < 4 ; j++) {
				Symbios_host *h = &nvram->data.Symbios.host[j];
			printf("ncr53c8xx: BOOT[%d] device_id=%04x vendor_id=%04x device_fn=%02x io_port=%04x %s\n",
				j,		h->device_id,	h->vendor_id,
				h->device_fn,	h->io_port,
				(h->flags & SYMBIOS_INIT_SCAN_AT_BOOT) ? "SCAN AT BOOT" : "");
			}
		}
		else if (nvram->type == SCSI_NCR_TEKRAM_NVRAM) {
			/* display Tekram nvram data */
			printf("ncr53c8xx: NVRAM: Tekram format data, 53c%s, PCI bus %d, device %d, function %d\n",
				devp->chip.name, devp->slot.bus, 
				(int) (devp->slot.device_fn & 0xf8) >> 3, 
				(int) devp->slot.device_fn & 7);
#endif
		}
	}

	if (nvram_index >= 0 && nvram_index < count)
		nvram = device[nvram_index].nvram;
	else
		nvram = 0;

	if (!nvram)
		goto out;

	/* 
	** check devices in the boot record against devices detected. 
	** attach devices if we find a match. boot table records that 
	** do not match any detected devices will be ignored. 
	** devices that do not match any boot table will not be attached
	** here but will attempt to be attached during the device table 
	** rescan.
	*/
     	for (i = 0; i < 4; i++) {
		Symbios_host *h = &nvram->data.Symbios.host[i];
		for (j = 0 ; j < count ; j++) {
			devp = &device[j];
			if (h->device_fn == devp->slot.device_fn &&
#if 0	/* bus number location in nvram ? */
			    h->bus	 == devp->slot.bus	 &&
#endif
			    h->device_id == devp->chip.device_id)
				break;
		}
		if (j < count && !devp->attach_done) {
			if (!ncr_attach (tpnt, attach_count, devp))
				attach_count++;
			devp->attach_done = 1;
		}
	}

out:
	return attach_count;
}
#endif /* SCSI_NCR_NVRAM_SUPPORT */

__initfunc(
int ncr53c8xx_detect(Scsi_Host_Template *tpnt)
)
{
	int i, j;
	int chips;
	int count = 0;
	uchar bus, device_fn;
	short index;
	int attach_count = 0;
	ncr_device device[8];
#ifdef SCSI_NCR_NVRAM_SUPPORT
	ncr_nvram  nvram[4];
	int k, nvrams;
#endif
	int hosts;

#ifdef SCSI_NCR_NVRAM_SUPPORT
	int nvram_index = 0;
#endif
	if (initverbose >= 2)
		ncr_print_driver_setup();

#ifdef SCSI_NCR_DEBUG_INFO_SUPPORT
	ncr_debug = driver_setup.debug;
#endif

#if LINUX_VERSION_CODE >= LinuxVersionCode(1,3,0)
     tpnt->proc_dir = &proc_scsi_ncr53c8xx;
# ifdef SCSI_NCR_PROC_INFO_SUPPORT
     tpnt->proc_info = ncr53c8xx_proc_info;
# endif
#endif

#if	defined(SCSI_NCR_BOOT_COMMAND_LINE_SUPPORT) && defined(MODULE)
if (ncr53c8xx)
	ncr53c8xx_setup(ncr53c8xx, (int *) 0);
#endif

	/* 
	** Detect all 53c8xx hosts and then attach them.
	**
	** If we are using NVRAM, once all hosts are detected, we need to check
	** any NVRAM for boot order in case detect and boot order differ and
	** attach them using the order in the NVRAM.
	**
	** If no NVRAM is found or data appears invalid attach boards in the 
	** the order they are detected.
	*/

	if (!pci_present())
		return 0;

	chips	= sizeof(ncr_chip_ids)	/ sizeof(ncr_chip_ids[0]);
	hosts	= sizeof(device)	/ sizeof(device[0]);
#ifdef SCSI_NCR_NVRAM_SUPPORT
	k = 0;
	if (driver_setup.use_nvram & 0x1)
		nvrams	= sizeof(nvram)	/ sizeof(nvram[0]);
	else
		nvrams	= 0;
#endif

	for (j = 0; j < chips ; ++j) {
		i = driver_setup.reverse_probe ? chips-1 - j : j;
		for (index = 0; ; index++) {
			char *msg = "";
			if ((pcibios_find_device(PCI_VENDOR_ID_NCR, ncr_chip_ids[i],
						index, &bus, &device_fn)) ||
			    (count == hosts))
				break;
#ifdef SCSI_NCR_NVRAM_SUPPORT
			device[count].nvram = k < nvrams ? &nvram[k] : 0;
#else
			device[count].nvram = 0;
#endif
			if (ncr53c8xx_pci_init(tpnt, bus, device_fn, &device[count])) {
				device[count].nvram = 0;
				continue;
			}
#ifdef SCSI_NCR_NVRAM_SUPPORT
			if (device[count].nvram) {
				++k;
				nvram_index |= device[count].nvram->type;
				switch (device[count].nvram->type) {
				case SCSI_NCR_TEKRAM_NVRAM:
					msg = "with Tekram NVRAM";
					break;
				case SCSI_NCR_SYMBIOS_NVRAM:
					msg = "with Symbios NVRAM";
					break;
				default:
					msg = "";
					device[count].nvram = 0;
					--k;
				}
			}
#endif
			printf(KERN_INFO "ncr53c8xx: 53c%s detected %s\n",
				device[count].chip.name, msg);
			++count;
		}
	}
#ifdef SCSI_NCR_NVRAM_SUPPORT
	attach_count = ncr_attach_using_nvram(tpnt, nvram_index, count, device);
#endif
	/* 
	** rescan device list to make sure all boards attached.
	** devices without boot records will not be attached yet
	** so try to attach them here.
	*/
	for (i= 0; i < count; i++) {
		if (!device[i].attach_done && 
		    !ncr_attach (tpnt, attach_count, &device[i])) {
 			attach_count++;
		}
	}

	return attach_count;
}

/*
**   Read and check the PCI configuration for any detected NCR 
**   boards and save data for attaching after all boards have 
**   been detected.
*/

__initfunc(
static int ncr53c8xx_pci_init(Scsi_Host_Template *tpnt,
			      uchar bus, uchar device_fn, ncr_device *device)
)
{
	ushort vendor_id, device_id, command;
	uchar cache_line_size, latency_timer;
	uchar revision;
#if LINUX_VERSION_CODE >= LinuxVersionCode(2,1,85)
	struct pci_dev *pdev;
	ulong base, base_2, io_port; 
	uint irq;
#elif LINUX_VERSION_CODE >= LinuxVersionCode(1,3,0) 
	uchar irq;
	uint base, base_2, io_port; 
#else
	uchar irq;
	ulong base, base_2, io_port; 
#endif
	int i;

#ifdef SCSI_NCR_NVRAM_SUPPORT
	ncr_nvram *nvram = device->nvram;
#endif
	ncr_chip *chip;

	printk(KERN_INFO "ncr53c8xx: at PCI bus %d, device %d, function %d\n",
		bus, (int) (device_fn & 0xf8) >> 3, (int) device_fn & 7);
	/*
	 * Read info from the PCI config space.
	 * pcibios_read_config_xxx() functions are assumed to be used for 
	 * successfully detected PCI devices.
	 * Expecting error conditions from them is just paranoia,
	 * thus void cast.
	 */
	(void) pcibios_read_config_word(bus, device_fn,
					PCI_VENDOR_ID, &vendor_id);
	(void) pcibios_read_config_word(bus, device_fn,
					PCI_DEVICE_ID, &device_id);
	(void) pcibios_read_config_word(bus, device_fn,
					PCI_COMMAND, &command);
#if LINUX_VERSION_CODE >= LinuxVersionCode(2,1,85)
	pdev = pci_find_slot(bus, device_fn);
	io_port = pdev->base_address[0];
	base = pdev->base_address[1];
	base_2 = pdev->base_address[2];
	irq = pdev->irq;
#else
	(void) pcibios_read_config_dword(bus, device_fn,
					PCI_BASE_ADDRESS_0, &io_port);	
	(void) pcibios_read_config_dword(bus, device_fn,
					PCI_BASE_ADDRESS_1, &base);
	(void) pcibios_read_config_dword(bus, device_fn,
					PCI_BASE_ADDRESS_2, &base_2);
	(void) pcibios_read_config_byte(bus, device_fn,
					PCI_INTERRUPT_LINE, &irq);
#endif
	(void) pcibios_read_config_byte(bus, device_fn,
					PCI_CLASS_REVISION,&revision);	
	(void) pcibios_read_config_byte(bus, device_fn,
					PCI_CACHE_LINE_SIZE, &cache_line_size);
	(void) pcibios_read_config_byte(bus, device_fn,
					PCI_LATENCY_TIMER, &latency_timer);

	/*
	 *	Check if the chip is supported
	 */
	chip = 0;
	for (i = 0; i < sizeof(ncr_chip_table)/sizeof(ncr_chip_table[0]); i++) {
		if (device_id != ncr_chip_table[i].device_id)
			continue;
		if (revision > ncr_chip_table[i].revision_id)
			continue;
		chip = &device->chip;
		memcpy(chip, &ncr_chip_table[i], sizeof(*chip));
		chip->revision_id = revision;
		break;
	}
	if (!chip) {
		printk("ncr53c8xx: not initializing, device not supported\n");
		return -1;
	}

#ifdef __powerpc__
	if (!(command & PCI_COMMAND_MASTER)) {
		printk("ncr53c8xx: attempting to force PCI_COMMAND_MASTER...");
		command |= PCI_COMMAND_MASTER;
		pcibios_write_config_word(bus, device_fn, PCI_COMMAND, command);
		pcibios_read_config_word(bus, device_fn, PCI_COMMAND, &command);
		if (!(command & PCI_COMMAND_MASTER)) {
			printk("failed!\n");
		} else {
			printk("succeeded.\n");
		}
	}

	if (!(command & PCI_COMMAND_IO)) {
		printk("ncr53c8xx: attempting to force PCI_COMMAND_IO...");
		command |= PCI_COMMAND_IO;
		pcibios_write_config_word(bus, device_fn, PCI_COMMAND, command);
		pcibios_read_config_word(bus, device_fn, PCI_COMMAND, &command);
		if (!(command & PCI_COMMAND_IO)) {
			printk("failed!\n");
		} else {
			printk("succeeded.\n");
		}
	}

	if (!(command & PCI_COMMAND_MEMORY)) {
		printk("ncr53c8xx: attempting to force PCI_COMMAND_MEMORY...");
		command |= PCI_COMMAND_MEMORY;
		pcibios_write_config_word(bus, device_fn, PCI_COMMAND, command);
		pcibios_read_config_word(bus, device_fn, PCI_COMMAND, &command);
		if (!(command & PCI_COMMAND_MEMORY)) {
			printk("failed!\n");
		} else {
			printk("succeeded.\n");
		}
	}
	
	if ( is_prep ) {
		if (io_port >= 0x10000000) {
			printk("ncr53c8xx: reallocating io_port (Wacky IBM)");
			io_port = (io_port & 0x00FFFFFF) | 0x01000000;
			pcibios_write_config_dword(bus, device_fn, PCI_BASE_ADDRESS_0, io_port);
		}
		if (base >= 0x10000000) {
			printk("ncr53c8xx: reallocating base (Wacky IBM)");
			base = (base & 0x00FFFFFF) | 0x01000000;
			pcibios_write_config_dword(bus, device_fn, PCI_BASE_ADDRESS_1, base);
		}
		if (base_2 >= 0x10000000) {
			printk("ncr53c8xx: reallocating base2 (Wacky IBM)");
			base_2 = (base_2 & 0x00FFFFFF) | 0x01000000;
			pcibios_write_config_dword(bus, device_fn, PCI_BASE_ADDRESS_2, base_2);
		}
	}
#endif
#ifdef __sparc__
	/*
	 *	Severall fix-ups for sparc.
	 *
	 *	Should not be performed by the driver, but how can OBP know
	 *	each and every PCI card, if they don't use Fcode?
	 */

	base = __pa(base);
	base_2 = __pa(base_2);

	if (!(command & PCI_COMMAND_MASTER)) {
		if (initverbose >= 2)
			printk("ncr53c8xx: setting PCI_COMMAND_MASTER bit (fixup)\n");
		command |= PCI_COMMAND_MASTER;
		pcibios_write_config_word(bus, device_fn, PCI_COMMAND, command);
		pcibios_read_config_word(bus, device_fn, PCI_COMMAND, &command);
	}

	if ((chip->features & FE_WRIE) && !(command & PCI_COMMAND_INVALIDATE)) {
		if (initverbose >= 2)
			printk("ncr53c8xx: setting PCI_COMMAND_INVALIDATE bit (fixup)\n");
		command |= PCI_COMMAND_INVALIDATE;
		pcibios_write_config_word(bus, device_fn, PCI_COMMAND, command);
		pcibios_read_config_word(bus, device_fn, PCI_COMMAND, &command);
	}

	if ((chip->features & FE_CLSE) && !cache_line_size) {
		cache_line_size = 16;
		if (initverbose >= 2)
			printk("ncr53c8xx: setting PCI_CACHE_LINE_SIZE to %d (fixup)\n", cache_line_size);
		pcibios_write_config_byte(bus, device_fn,
					  PCI_CACHE_LINE_SIZE, cache_line_size);
		pcibios_read_config_byte(bus, device_fn,
					 PCI_CACHE_LINE_SIZE, &cache_line_size);
	}

	if (!latency_timer) {
		latency_timer = 248;
		if (initverbose >= 2)
			printk("ncr53c8xx: setting PCI_LATENCY_TIMER to %d bus clocks (fixup)\n", latency_timer);
		pcibios_write_config_byte(bus, device_fn,
					  PCI_LATENCY_TIMER, latency_timer);
		pcibios_read_config_byte(bus, device_fn,
					 PCI_LATENCY_TIMER, &latency_timer);
	}
#endif

	/*
	 * Check availability of IO space, memory space and master capability.
	 */
	if (command & PCI_COMMAND_IO) { 
		if ((io_port & 3) != 1) {
			printk("ncr53c8xx: disabling I/O mapping since base address 0 (0x%x)\n"
				"           bits 0..1 indicate a non-IO mapping\n", (int) io_port);
			io_port = 0;
		}
		else
			io_port &= PCI_BASE_ADDRESS_IO_MASK;
	}
	else
		io_port = 0;

	if (command & PCI_COMMAND_MEMORY) {
		if ((base & PCI_BASE_ADDRESS_SPACE) != PCI_BASE_ADDRESS_SPACE_MEMORY) {
			printk("ncr53c8xx: disabling memory mapping since base address 1\n"
				"            contains a non-memory mapping\n");
			base = 0;
		}
		else 
			base &= PCI_BASE_ADDRESS_MEM_MASK;
	}
	else
		base = 0;
	
	if (!io_port && !base) {
		printk("ncr53c8xx: not initializing, both I/O and memory mappings disabled\n");
		return -1;
	}

	base_2 &= PCI_BASE_ADDRESS_MEM_MASK;

	if (io_port && check_region (io_port, 128)) {
#ifdef __sparc__
		printk("ncr53c8xx: IO region 0x%lx to 0x%lx is in use\n",
			io_port, (io_port + 127));
#else
		printk("ncr53c8xx: IO region 0x%x to 0x%x is in use\n",
			(int) io_port, (int) (io_port + 127));
#endif
		return -1;
	}
	
	if (!(command & PCI_COMMAND_MASTER)) {
		printk("ncr53c8xx: not initializing, BUS MASTERING was disabled\n");
		return -1;
	}

	/*
	 * Fix some features according to driver setup.
	 */
	if (!(driver_setup.special_features & 1))
		chip->features &= ~FE_SPECIAL_SET;
	else {
		if (driver_setup.special_features & 2)
			chip->features &= ~FE_WRIE;
	}
	if (driver_setup.ultra_scsi < 2 && (chip->features & FE_ULTRA2)) {
		chip->features |=  FE_ULTRA;
		chip->features &= ~FE_ULTRA2;
	}
	if (driver_setup.ultra_scsi < 1)
		chip->features &= ~FE_ULTRA;
	if (!driver_setup.max_wide)
		chip->features &= ~FE_WIDE;


#ifdef	SCSI_NCR_PCI_FIX_UP_SUPPORT

	/*
	 * Try to fix up PCI config according to wished features.
	 */
#if defined(__i386__) && !defined(MODULE)
	if ((driver_setup.pci_fix_up & 1) &&
	    (chip->features & FE_CLSE) && cache_line_size == 0) {
#if LINUX_VERSION_CODE < LinuxVersionCode(2,1,75)
		extern char x86;
		switch(x86) {
#else
		switch(boot_cpu_data.x86) {
#endif
		case 4:	cache_line_size = 4; break;
		case 5:	cache_line_size = 8; break;
		}
		if (cache_line_size)
			(void) pcibios_write_config_byte(bus, device_fn,
					PCI_CACHE_LINE_SIZE, cache_line_size);
		if (initverbose)
			printk("ncr53c8xx: setting PCI_CACHE_LINE_SIZE to %d (fix-up).\n", cache_line_size);
	}

	if ((driver_setup.pci_fix_up & 2) && cache_line_size &&
	    (chip->features & FE_WRIE) && !(command & PCI_COMMAND_INVALIDATE)) {
		command |= PCI_COMMAND_INVALIDATE;
		(void) pcibios_write_config_word(bus, device_fn,
						PCI_COMMAND, command);
		if (initverbose)
			printk("ncr53c8xx: setting PCI_COMMAND_INVALIDATE bit (fix-up).\n");
	}
#endif
	/*
	 * Fix up for old chips that support READ LINE but not CACHE LINE SIZE.
	 * - If CACHE LINE SIZE is unknown, set burst max to 32 bytes = 8 dwords
	 *   and donnot enable READ LINE.
	 * - Otherwise set it to the CACHE LINE SIZE (power of 2 assumed). 
	 */

	if (!(chip->features & FE_CLSE)) {
		int burst_max = chip->burst_max;
		if (cache_line_size == 0) {
			chip->features	&= ~FE_ERL;
			if (burst_max > 3)
				burst_max = 3;
		}
		else {
			while (cache_line_size < (1 << burst_max))
				--burst_max;
		}
		chip->burst_max = burst_max;
	}

	/*
	 * Tune PCI LATENCY TIMER according to burst max length transfer.
	 * (latency timer >= burst length + 6, we add 10 to be quite sure)
	 * If current value is zero, the device has probably been configured 
	 * for no bursting due to some broken hardware.
	 */

	if (latency_timer == 0 && chip->burst_max)
		printk("ncr53c8xx: PCI_LATENCY_TIMER=0, bursting should'nt be allowed.\n");

	if ((driver_setup.pci_fix_up & 4) && chip->burst_max) {
		uchar lt = (1 << chip->burst_max) + 6 + 10;
		if (latency_timer < lt) {
			latency_timer = lt;
			if (initverbose)
				printk("ncr53c8xx: setting PCI_LATENCY_TIMER to %d bus clocks (fix-up).\n", latency_timer);
			 (void) pcibios_write_config_byte(bus, device_fn,
					PCI_LATENCY_TIMER, latency_timer);
		}
	}

	/*
	 * Fix up for recent chips that support CACHE LINE SIZE.
	 * If PCI config space is not OK, remove features that shall not be 
	 * used by the chip. No need to trigger possible chip bugs.
	 */

	if ((chip->features & FE_CLSE) && cache_line_size == 0) {
		chip->features &= ~FE_CACHE_SET;
		printk("ncr53c8xx: PCI_CACHE_LINE_SIZE not set, features based on CACHE LINE SIZE not used.\n");
	}

	if ((chip->features & FE_WRIE) && !(command & PCI_COMMAND_INVALIDATE)) {
		chip->features &= ~FE_WRIE;
		printk("ncr53c8xx: PCI_COMMAND_INVALIDATE not set, WRITE AND INVALIDATE not used\n");
	}

#endif	/* SCSI_NCR_PCI_FIX_UP_SUPPORT */

 	/* initialise ncr_device structure with items required by ncr_attach */
	device->slot.bus	= bus;
	device->slot.device_fn	= device_fn;
	device->slot.base	= base;
	device->slot.base_2	= base_2;
	device->slot.io_port	= io_port;
	device->slot.irq	= irq;
	device->attach_done	= 0;
#ifdef SCSI_NCR_NVRAM_SUPPORT
	if (!nvram)
		goto out;

	/*
	** Get access to chip IO registers
	*/
#ifdef NCR_IOMAPPED
	request_region(io_port, 128, "ncr53c8xx");
	device->slot.port = io_port;
#else
	device->slot.reg = (struct ncr_reg *) remap_pci_mem((ulong) base, 128);
	if (!device->slot.reg)
		goto out;
#endif

	/*
	** Try to read SYMBIOS nvram.
	** Data can be used to order booting of boards.
	**
	** Data is saved in ncr_device structure if NVRAM found. This
	** is then used to find drive boot order for ncr_attach().
	**
	** NVRAM data is passed to Scsi_Host_Template later during ncr_attach()
	** for any device set up.
	**
	** Try to read TEKRAM nvram if Symbios nvram not found.
	*/

	if	(!ncr_get_Symbios_nvram(&device->slot, &nvram->data.Symbios))
		nvram->type = SCSI_NCR_SYMBIOS_NVRAM;
	else if	(!ncr_get_Tekram_nvram(&device->slot, &nvram->data.Tekram))
		nvram->type = SCSI_NCR_TEKRAM_NVRAM;
	else
		nvram->type = 0;
out:
	/*
	** Release access to chip IO registers
	*/
#ifdef NCR_IOMAPPED
	release_region(device->slot.port, 128);
#else
	unmap_pci_mem((vm_offset_t) device->slot.reg, (u_long) 128);
#endif

#endif	/* SCSI_NCR_NVRAM_SUPPORT */
	return 0;     
}

#if LINUX_VERSION_CODE >= LinuxVersionCode(2,0,0)
/*
**   Linux select queue depths function
*/
static void ncr53c8xx_select_queue_depths(struct Scsi_Host *host, struct scsi_device *devlist)
{
	struct scsi_device *device;

	for (device = devlist; device; device = device->next) {
		if (device->host == host) {
#if SCSI_NCR_MAX_TAGS > 1
			if (device->tagged_supported) {
				device->queue_depth = SCSI_NCR_MAX_TAGS;
			}
			else {
				device->queue_depth = 2;
			}
#else
			device->queue_depth = 1;
#endif

#ifdef DEBUG_NCR53C8XX
printk("ncr53c8xx_select_queue_depth: id=%d, lun=%d, queue_depth=%d\n",
	device->id, device->lun, device->queue_depth);
#endif
		}
	}
}
#endif

/*
**   Linux entry point of queuecommand() function
*/

int ncr53c8xx_queue_command (Scsi_Cmnd *cmd, void (* done)(Scsi_Cmnd *))
{
     int sts;
#ifdef DEBUG_NCR53C8XX
printk("ncr53c8xx_queue_command\n");
#endif

     if ((sts = ncr_queue_command(cmd, done)) != DID_OK) {
	  cmd->result = ScsiResult(sts, 0);
	  done(cmd);
#ifdef DEBUG_NCR53C8XX
printk("ncr53c8xx : command not queued - result=%d\n", sts);
#endif
          return sts;
     }
#ifdef DEBUG_NCR53C8XX
printk("ncr53c8xx : command successfully queued\n");
#endif
     return sts;
}

/*
**   Linux entry point of the interrupt handler.
**   Fort linux versions > 1.3.70, we trust the kernel for 
**   passing the internal host descriptor as 'dev_id'.
**   Otherwise, we scan the host list and call the interrupt 
**   routine for each host that uses this IRQ.
*/

#if LINUX_VERSION_CODE >= LinuxVersionCode(1,3,70)
static void ncr53c8xx_intr(int irq, void *dev_id, struct pt_regs * regs)
{
     unsigned long flags;

#ifdef DEBUG_NCR53C8XX
     printk("ncr53c8xx : interrupt received\n");
#endif

     if (DEBUG_FLAGS & DEBUG_TINY) printf ("[");
     spin_lock_irqsave(&io_request_lock, flags);
     ncr_exception((ncb_p) dev_id);
     spin_unlock_irqrestore(&io_request_lock, flags);
     if (DEBUG_FLAGS & DEBUG_TINY) printf ("]\n");
}

#else
static void ncr53c8xx_intr(int irq, struct pt_regs * regs)
{
     struct Scsi_Host *host;
     struct host_data *host_data;

     for (host = first_host; host; host = host->next) {
	  if (host->hostt == the_template && host->irq == irq) {
	       host_data = (struct host_data *) host->hostdata;
               if (DEBUG_FLAGS & DEBUG_TINY) printf ("[");
               ncr_exception(host_data->ncb);
               if (DEBUG_FLAGS & DEBUG_TINY) printf ("]\n");
	  }
     }
}
#endif

/*
**   Linux entry point of the timer handler
*/

static void ncr53c8xx_timeout(unsigned long np)
{
     ncr_timeout((ncb_p) np);
}

/*
**   Linux entry point of reset() function
*/

#if defined SCSI_RESET_SYNCHRONOUS && defined SCSI_RESET_ASYNCHRONOUS

int ncr53c8xx_reset(Scsi_Cmnd *cmd, unsigned int reset_flags)
{
	int sts;
	unsigned long flags;

	printk("ncr53c8xx_reset: pid=%lu reset_flags=%x serial_number=%ld serial_number_at_timeout=%ld\n",
		cmd->pid, reset_flags, cmd->serial_number, cmd->serial_number_at_timeout);

	save_flags(flags); cli();

	/*
	 * We have to just ignore reset requests in some situations.
	 */
#if defined SCSI_RESET_NOT_RUNNING
	if (cmd->serial_number != cmd->serial_number_at_timeout) {
		sts = SCSI_RESET_NOT_RUNNING;
		goto out;
	}
#endif
	/*
	 * If the mid-level driver told us reset is synchronous, it seems 
	 * that we must call the done() callback for the involved command, 
	 * even if this command was not queued to the low-level driver, 
	 * before returning SCSI_RESET_SUCCESS.
	 */

	sts = ncr_reset_bus(cmd,
	(reset_flags & (SCSI_RESET_SYNCHRONOUS | SCSI_RESET_ASYNCHRONOUS)) == SCSI_RESET_SYNCHRONOUS);
	/*
	 * Since we always reset the controller, when we return success, 
	 * we add this information to the return code.
	 */
#if defined SCSI_RESET_HOST_RESET
	if (sts == SCSI_RESET_SUCCESS)
		sts |= SCSI_RESET_HOST_RESET;
#endif

out:
	restore_flags(flags);
	return sts;
}
#else
int ncr53c8xx_reset(Scsi_Cmnd *cmd)
{
	printk("ncr53c8xx_reset: command pid %lu\n", cmd->pid);
	return ncr_reset_bus(cmd, 1);
}
#endif

/*
**   Linux entry point of abort() function
*/

#if defined SCSI_RESET_SYNCHRONOUS && defined SCSI_RESET_ASYNCHRONOUS

int ncr53c8xx_abort(Scsi_Cmnd *cmd)
{
	int sts;
	unsigned long flags;

	printk("ncr53c8xx_abort: pid=%lu serial_number=%ld serial_number_at_timeout=%ld\n",
		cmd->pid, cmd->serial_number, cmd->serial_number_at_timeout);

	save_flags(flags); cli();

	/*
	 * We have to just ignore abort requests in some situations.
	 */
	if (cmd->serial_number != cmd->serial_number_at_timeout) {
		sts = SCSI_ABORT_NOT_RUNNING;
		goto out;
	}

	sts = ncr_abort_command(cmd);
out:
	restore_flags(flags);
	return sts;
}
#else
int ncr53c8xx_abort(Scsi_Cmnd *cmd)
{
	printk("ncr53c8xx_abort: command pid %lu\n", cmd->pid);
	return ncr_abort_command(cmd);
}
#endif

#ifdef MODULE
int ncr53c8xx_release(struct Scsi_Host *host)
{
#ifdef DEBUG_NCR53C8XX
printk("ncr53c8xx : release\n");
#endif
     ncr_detach(((struct host_data *) host->hostdata)->ncb);

     return 1;
}
#endif


/*
**	Scsi command waiting list management.
**
**	It may happen that we cannot insert a scsi command into the start queue,
**	in the following circumstances.
** 		Too few preallocated ccb(s), 
**		maxtags < cmd_per_lun of the Linux host control block,
**		etc...
**	Such scsi commands are inserted into a waiting list.
**	When a scsi command complete, we try to requeue the commands of the
**	waiting list.
*/

#define next_wcmd host_scribble

static void insert_into_waiting_list(ncb_p np, Scsi_Cmnd *cmd)
{
	Scsi_Cmnd *wcmd;

#ifdef DEBUG_WAITING_LIST
	printf("%s: cmd %lx inserted into waiting list\n", ncr_name(np), (u_long) cmd);
#endif
	cmd->next_wcmd = 0;
	if (!(wcmd = np->waiting_list)) np->waiting_list = cmd;
	else {
		while ((wcmd->next_wcmd) != 0)
			wcmd = (Scsi_Cmnd *) wcmd->next_wcmd;
		wcmd->next_wcmd = (char *) cmd;
	}
}

static Scsi_Cmnd *retrieve_from_waiting_list(int to_remove, ncb_p np, Scsi_Cmnd *cmd)
{
	Scsi_Cmnd *wcmd;

	if (!(wcmd = np->waiting_list)) return 0;
	while (wcmd->next_wcmd) {
		if (cmd == (Scsi_Cmnd *) wcmd->next_wcmd) {
			if (to_remove) {
				wcmd->next_wcmd = cmd->next_wcmd;
				cmd->next_wcmd = 0;
			}
#ifdef DEBUG_WAITING_LIST
	printf("%s: cmd %lx retrieved from waiting list\n", ncr_name(np), (u_long) cmd);
#endif
			return cmd;
		}
	}
	return 0;
}

static void process_waiting_list(ncb_p np, int sts)
{
	Scsi_Cmnd *waiting_list, *wcmd;

	waiting_list = np->waiting_list;
	np->waiting_list = 0;

#ifdef DEBUG_WAITING_LIST
	if (waiting_list) printf("%s: waiting_list=%lx processing sts=%d\n", ncr_name(np), (u_long) waiting_list, sts);
#endif
	while ((wcmd = waiting_list) != 0) {
		waiting_list = (Scsi_Cmnd *) wcmd->next_wcmd;
		wcmd->next_wcmd = 0;
		if (sts == DID_OK) {
#ifdef DEBUG_WAITING_LIST
	printf("%s: cmd %lx trying to requeue\n", ncr_name(np), (u_long) wcmd);
#endif
			sts = ncr_queue_command(wcmd, wcmd->scsi_done);
		}
		if (sts != DID_OK) {
#ifdef DEBUG_WAITING_LIST
	printf("%s: cmd %lx done forced sts=%d\n", ncr_name(np), (u_long) wcmd, sts);
#endif
			wcmd->result = ScsiResult(sts, 0);
			wcmd->scsi_done(wcmd);
		}
	}
}

#undef next_wcmd

/*
**	Returns data transfer direction for common op-codes.
*/

static int guess_xfer_direction(int opcode)
{
	int d;

	switch(opcode) {
	case 0x12:  /*	INQUIRY				12 */
	case 0x4D:  /*	LOG SENSE			4D */
	case 0x5A:  /*	MODE SENSE(10)			5A */
	case 0x1A:  /*	MODE SENSE(6)			1A */
	case 0x3C:  /*	READ BUFFER			3C */
	case 0x1C:  /*	RECEIVE DIAGNOSTIC RESULTS	1C */
	case 0x03:  /*	REQUEST SENSE			03 */
		d = XferIn;
		break;
	case 0x39:  /*	COMPARE				39 */
	case 0x3A:  /*	COPY AND VERIFY			3A */
	case 0x18:  /*	COPY				18 */
	case 0x4C:  /*	LOG SELECT			4C */
	case 0x55:  /*	MODE SELECT(10)			55 */
	case 0x3B:  /*	WRITE BUFFER			3B */
	case 0x1D:  /*	SEND DIAGNOSTIC			1D */
	case 0x40:  /*	CHANGE DEFINITION		40 */
	case 0x15:  /*	MODE SELECT(6)			15 */
		d = XferOut;
		break;
	case 0x00:  /*	TEST UNIT READY			00 */
		d = XferNone;
		break;
	default:
		d = XferBoth;
		break;
	}

	return d;
}


#ifdef SCSI_NCR_PROC_INFO_SUPPORT

/*=========================================================================
**	Proc file system stuff
**
**	A read operation returns profile information.
**	A write operation is a control command.
**	The string is parsed in the driver code and the command is passed 
**	to the ncr_usercmd() function.
**=========================================================================
*/

#ifdef SCSI_NCR_USER_COMMAND_SUPPORT

#define is_digit(c)	((c) >= '0' && (c) <= '9')
#define digit_to_bin(c)	((c) - '0')
#define is_space(c)	((c) == ' ' || (c) == '\t')

static int skip_spaces(char *ptr, int len)
{
	int cnt, c;

	for (cnt = len; cnt > 0 && (c = *ptr++) && is_space(c); cnt--);

	return (len - cnt);
}

static int get_int_arg(char *ptr, int len, u_long *pv)
{
	int	cnt, c;
	u_long	v;

	for (v = 0, cnt = len; cnt > 0 && (c = *ptr++) && is_digit(c); cnt--) {
		v = (v * 10) + digit_to_bin(c);
	}

	if (pv)
		*pv = v;

	return (len - cnt);
}

static int is_keyword(char *ptr, int len, char *verb)
{
	int verb_len = strlen(verb);

	if (len >= strlen(verb) && !memcmp(verb, ptr, verb_len))
		return verb_len;
	else
		return 0;

}

#define SKIP_SPACES(min_spaces)						\
	if ((arg_len = skip_spaces(ptr, len)) < (min_spaces))		\
		return -EINVAL;						\
	ptr += arg_len; len -= arg_len;

#define GET_INT_ARG(v)							\
	if (!(arg_len = get_int_arg(ptr, len, &(v))))			\
		return -EINVAL;						\
	ptr += arg_len; len -= arg_len;


/*
**	Parse a control command
*/

static int ncr_user_command(ncb_p np, char *buffer, int length)
{
	char *ptr	= buffer;
	int len		= length;
	struct usrcmd	 *uc = &np->user;
	int		arg_len;
	u_long 		target;

	bzero(uc, sizeof(*uc));

	if (len > 0 && ptr[len-1] == '\n')
		--len;

	if	((arg_len = is_keyword(ptr, len, "setsync")) != 0)
		uc->cmd = UC_SETSYNC;
	else if	((arg_len = is_keyword(ptr, len, "settags")) != 0)
		uc->cmd = UC_SETTAGS;
	else if	((arg_len = is_keyword(ptr, len, "setorder")) != 0)
		uc->cmd = UC_SETORDER;
	else if	((arg_len = is_keyword(ptr, len, "setwide")) != 0)
		uc->cmd = UC_SETWIDE;
	else if	((arg_len = is_keyword(ptr, len, "setdebug")) != 0)
		uc->cmd = UC_SETDEBUG;
	else if	((arg_len = is_keyword(ptr, len, "setflag")) != 0)
		uc->cmd = UC_SETFLAG;
	else if	((arg_len = is_keyword(ptr, len, "clearprof")) != 0)
		uc->cmd = UC_CLEARPROF;
#ifdef	UC_DEBUG_ERROR_RECOVERY
	else if	((arg_len = is_keyword(ptr, len, "debug_error_recovery")) != 0)
		uc->cmd = UC_DEBUG_ERROR_RECOVERY;
#endif
	else
		arg_len = 0;

#ifdef DEBUG_PROC_INFO
printf("ncr_user_command: arg_len=%d, cmd=%ld\n", arg_len, uc->cmd);
#endif

	if (!arg_len)
		return -EINVAL;
	ptr += arg_len; len -= arg_len;

	switch(uc->cmd) {
	case UC_SETSYNC:
	case UC_SETTAGS:
	case UC_SETWIDE:
	case UC_SETFLAG:
		SKIP_SPACES(1);
		if ((arg_len = is_keyword(ptr, len, "all")) != 0) {
			ptr += arg_len; len -= arg_len;
			uc->target = ~0;
		} else {
			GET_INT_ARG(target);
			uc->target = (1<<target);
#ifdef DEBUG_PROC_INFO
printf("ncr_user_command: target=%ld\n", target);
#endif
		}
		break;
	}

	switch(uc->cmd) {
	case UC_SETSYNC:
	case UC_SETTAGS:
	case UC_SETWIDE:
		SKIP_SPACES(1);
		GET_INT_ARG(uc->data);
#ifdef DEBUG_PROC_INFO
printf("ncr_user_command: data=%ld\n", uc->data);
#endif
		break;
	case UC_SETORDER:
		SKIP_SPACES(1);
		if	((arg_len = is_keyword(ptr, len, "simple")))
			uc->data = M_SIMPLE_TAG;
		else if	((arg_len = is_keyword(ptr, len, "ordered")))
			uc->data = M_ORDERED_TAG;
		else if	((arg_len = is_keyword(ptr, len, "default")))
			uc->data = 0;
		else
			return -EINVAL;
		break;
	case UC_SETDEBUG:
		while (len > 0) {
			SKIP_SPACES(1);
			if	((arg_len = is_keyword(ptr, len, "alloc")))
				uc->data |= DEBUG_ALLOC;
			else if	((arg_len = is_keyword(ptr, len, "phase")))
				uc->data |= DEBUG_PHASE;
			else if	((arg_len = is_keyword(ptr, len, "poll")))
				uc->data |= DEBUG_POLL;
			else if	((arg_len = is_keyword(ptr, len, "queue")))
				uc->data |= DEBUG_QUEUE;
			else if	((arg_len = is_keyword(ptr, len, "result")))
				uc->data |= DEBUG_RESULT;
			else if	((arg_len = is_keyword(ptr, len, "scatter")))
				uc->data |= DEBUG_SCATTER;
			else if	((arg_len = is_keyword(ptr, len, "script")))
				uc->data |= DEBUG_SCRIPT;
			else if	((arg_len = is_keyword(ptr, len, "tiny")))
				uc->data |= DEBUG_TINY;
			else if	((arg_len = is_keyword(ptr, len, "timing")))
				uc->data |= DEBUG_TIMING;
			else if	((arg_len = is_keyword(ptr, len, "nego")))
				uc->data |= DEBUG_NEGO;
			else if	((arg_len = is_keyword(ptr, len, "tags")))
				uc->data |= DEBUG_TAGS;
			else if	((arg_len = is_keyword(ptr, len, "freeze")))
				uc->data |= DEBUG_FREEZE;
			else if	((arg_len = is_keyword(ptr, len, "restart")))
				uc->data |= DEBUG_RESTART;
			else
				return -EINVAL;
			ptr += arg_len; len -= arg_len;
		}
#ifdef DEBUG_PROC_INFO
printf("ncr_user_command: data=%ld\n", uc->data);
#endif
		break;
	case UC_SETFLAG:
		while (len > 0) {
			SKIP_SPACES(1);
			if	((arg_len = is_keyword(ptr, len, "trace")))
				uc->data |= UF_TRACE;
			else if	((arg_len = is_keyword(ptr, len, "no_disc")))
				uc->data |= UF_NODISC;
			else
				return -EINVAL;
			ptr += arg_len; len -= arg_len;
		}
		break;
#ifdef	UC_DEBUG_ERROR_RECOVERY
	case UC_DEBUG_ERROR_RECOVERY:
		SKIP_SPACES(1);
		if	((arg_len = is_keyword(ptr, len, "sge")))
			uc->data = 1;
		else if	((arg_len = is_keyword(ptr, len, "abort")))
			uc->data = 2;
		else if	((arg_len = is_keyword(ptr, len, "reset")))
			uc->data = 3;
		else if	((arg_len = is_keyword(ptr, len, "parity")))
			uc->data = 4;
		else if	((arg_len = is_keyword(ptr, len, "none")))
			uc->data = 0;
		else
			return -EINVAL;
		ptr += arg_len; len -= arg_len;
		break;
#endif
	default:
		break;
	}

	if (len)
		return -EINVAL;
	else {
		long flags;

		save_flags(flags); cli();
		ncr_usercmd (np);
		restore_flags(flags);
	}
	return length;
}

#endif	/* SCSI_NCR_USER_COMMAND_SUPPORT */

#ifdef SCSI_NCR_USER_INFO_SUPPORT

struct info_str
{
	char *buffer;
	int length;
	int offset;
	int pos;
};

static void copy_mem_info(struct info_str *info, char *data, int len)
{
	if (info->pos + len > info->length)
		len = info->length - info->pos;

	if (info->pos + len < info->offset) {
		info->pos += len;
		return;
	}
	if (info->pos < info->offset) {
		data += (info->offset - info->pos);
		len  -= (info->offset - info->pos);
	}

	if (len > 0) {
		memcpy(info->buffer + info->pos, data, len);
		info->pos += len;
	}
}

static int copy_info(struct info_str *info, char *fmt, ...)
{
	va_list args;
	char buf[81];
	int len;

	va_start(args, fmt);
	len = vsprintf(buf, fmt, args);
	va_end(args);

	copy_mem_info(info, buf, len);
	return len;
}

/*
**	Copy formatted profile information into the input buffer.
*/

#define to_ms(t) ((t) * 1000 / HZ)

static int ncr_host_info(ncb_p np, char *ptr, off_t offset, int len)
{
	struct info_str info;

	info.buffer	= ptr;
	info.length	= len;
	info.offset	= offset;
	info.pos	= 0;

	copy_info(&info, "General information:\n");
	copy_info(&info, "  Chip NCR53C%s, ",	np->chip_name);
	copy_info(&info, "device id 0x%x, ",	np->device_id);
	copy_info(&info, "revision id 0x%x\n",	np->revision_id);

	copy_info(&info, "  IO port address 0x%lx, ", (u_long) np->port);
	copy_info(&info, "IRQ number %d\n", (int) np->irq);

#ifndef NCR_IOMAPPED
	if (np->reg)
		copy_info(&info, "  Using memory mapped IO at virtual address 0x%lx\n",
		                  (u_long) np->reg);
#endif
	copy_info(&info, "  Synchronous period factor %d, ", (int) np->minsync);
	copy_info(&info, "max commands per lun %d\n", SCSI_NCR_MAX_TAGS);

	if (driver_setup.debug || driver_setup.verbose > 1) {
		copy_info(&info, "  Debug flags 0x%x, ", driver_setup.debug);
		copy_info(&info, "verbosity level %d\n", driver_setup.verbose);
	}

#ifdef SCSI_NCR_PROFILE_SUPPORT
	copy_info(&info, "Profiling information:\n");
	copy_info(&info, "  %-12s = %lu\n", "num_trans",np->profile.num_trans);
	copy_info(&info, "  %-12s = %lu\n", "num_kbytes",np->profile.num_kbytes);
	copy_info(&info, "  %-12s = %lu\n", "num_disc",	np->profile.num_disc);
	copy_info(&info, "  %-12s = %lu\n", "num_break",np->profile.num_break);
	copy_info(&info, "  %-12s = %lu\n", "num_int",	np->profile.num_int);
	copy_info(&info, "  %-12s = %lu\n", "num_fly",	np->profile.num_fly);
	copy_info(&info, "  %-12s = %lu\n", "ms_setup",	to_ms(np->profile.ms_setup));
	copy_info(&info, "  %-12s = %lu\n", "ms_data",	to_ms(np->profile.ms_data));
	copy_info(&info, "  %-12s = %lu\n", "ms_disc",	to_ms(np->profile.ms_disc));
	copy_info(&info, "  %-12s = %lu\n", "ms_post",	to_ms(np->profile.ms_post));
#endif
	
	return info.pos > info.offset? info.pos - info.offset : 0;
}

#endif /* SCSI_NCR_USER_INFO_SUPPORT */

/*
**	Entry point of the scsi proc fs of the driver.
**	- func = 0 means read  (returns profile data)
**	- func = 1 means write (parse user control command)
*/

int ncr53c8xx_proc_info(char *buffer, char **start, off_t offset,
			int length, int hostno, int func)
{
	struct Scsi_Host *host;
	struct host_data *host_data;
	ncb_p ncb = 0;
	int retv;

#ifdef DEBUG_PROC_INFO
printf("ncr53c8xx_proc_info: hostno=%d, func=%d\n", hostno, func);
#endif

	for (host = first_host; host; host = host->next) {
		if (host->hostt == the_template && host->host_no == hostno) {
			host_data = (struct host_data *) host->hostdata;
			ncb = host_data->ncb;
			break;
		}
	}

	if (!ncb)
		return -EINVAL;

	if (func) {
#ifdef	SCSI_NCR_USER_COMMAND_SUPPORT
		retv = ncr_user_command(ncb, buffer, length);
#else
		retv = -EINVAL;
#endif
	}
	else {
		if (start)
			*start = buffer;
#ifdef SCSI_NCR_USER_INFO_SUPPORT
		retv = ncr_host_info(ncb, buffer, offset, length);
#else
		retv = -EINVAL;
#endif
	}

	return retv;
}


/*=========================================================================
**	End of proc file system stuff
**=========================================================================
*/
#endif


#ifdef SCSI_NCR_NVRAM_SUPPORT

/* ---------------------------------------------------------------------
**
**	Try reading Symbios format nvram
**
** ---------------------------------------------------------------------
**
** GPOI0 - data in/data out
** GPIO1 - clock
**
**	return 0 if NVRAM data OK, 1 if NVRAM data not OK
** ---------------------------------------------------------------------
*/

#define SET_BIT 0
#define CLR_BIT 1
#define SET_CLK 2
#define CLR_CLK 3

static u_short nvram_read_data(ncr_slot *np, u_char *data, int len, u_char *gpreg, u_char *gpcntl);
static void nvram_start(ncr_slot *np, u_char *gpreg);
static void nvram_write_byte(ncr_slot *np, u_char *ack_data, u_char write_data, u_char *gpreg, u_char *gpcntl);
static void nvram_read_byte(ncr_slot *np, u_char *read_data, u_char ack_data, u_char *gpreg, u_char *gpcntl);
static void nvram_readAck(ncr_slot *np, u_char *read_bit, u_char *gpreg, u_char *gpcntl);
static void nvram_writeAck(ncr_slot *np, u_char write_bit, u_char *gpreg, u_char *gpcntl);
static void nvram_doBit(ncr_slot *np, u_char *read_bit, u_char write_bit, u_char *gpreg);
static void nvram_stop(ncr_slot *np, u_char *gpreg);
static void nvram_setBit(ncr_slot *np, u_char write_bit, u_char *gpreg, int bit_mode);

__initfunc(
static int ncr_get_Symbios_nvram (ncr_slot *np, Symbios_nvram *nvram)
)
{
	static u_char Symbios_trailer[6] = {0xfe, 0xfe, 0, 0, 0, 0};
	u_char	gpcntl, gpreg;
	u_char	old_gpcntl, old_gpreg;
	u_short	csum;
	u_char	ack_data;
	int	retv = 1;

	/* save current state of GPCNTL and GPREG */
	old_gpreg	= INB (nc_gpreg);
	old_gpcntl	= INB (nc_gpcntl);
	gpcntl		= old_gpcntl & 0xfc;

	/* set up GPREG & GPCNTL to set GPIO0 and GPIO1 in to known state */
	OUTB (nc_gpreg,  old_gpreg);
	OUTB (nc_gpcntl, gpcntl);

	/* this is to set NVRAM into a known state with GPIO0/1 both low */
	gpreg = old_gpreg;
	nvram_setBit(np, 0, &gpreg, CLR_CLK);
	nvram_setBit(np, 0, &gpreg, CLR_BIT);
		
	/* now set NVRAM inactive with GPIO0/1 both high */
	nvram_stop(np, &gpreg);
	
	/* activate NVRAM */
	nvram_start(np, &gpreg);

	/* write device code and random address MSB */
	nvram_write_byte(np, &ack_data,
		0xa0 | ((SYMBIOS_NVRAM_ADDRESS >> 7) & 0x0e), &gpreg, &gpcntl);
	if (ack_data & 0x01)
		goto out;

	/* write random address LSB */
	nvram_write_byte(np, &ack_data,
		(SYMBIOS_NVRAM_ADDRESS & 0x7f) << 1, &gpreg, &gpcntl);
	if (ack_data & 0x01)
		goto out;

	/* regenerate START state to set up for reading */
	nvram_start(np, &gpreg);
	
	/* rewrite device code and address MSB with read bit set (lsb = 0x01) */
	nvram_write_byte(np, &ack_data,
		0xa1 | ((SYMBIOS_NVRAM_ADDRESS >> 7) & 0x0e), &gpreg, &gpcntl);
	if (ack_data & 0x01)
		goto out;

	/* now set up GPIO0 for inputting data */
	gpcntl |= 0x01;
	OUTB (nc_gpcntl, gpcntl);
		
	/* input all active data - only part of total NVRAM */
	csum = nvram_read_data(np,
			(u_char *) nvram, sizeof(*nvram), &gpreg, &gpcntl);

	/* finally put NVRAM back in inactive mode */
	gpcntl &= 0xfe;
	OUTB (nc_gpcntl, gpcntl);
	nvram_stop(np, &gpreg);
	
#ifdef SCSI_NCR_DEBUG_NVRAM
printf("ncr53c8xx: NvRAM marker=%x trailer=%x %x %x %x %x %x byte_count=%d/%d checksum=%x/%x\n",
	nvram->start_marker,
	nvram->trailer[0], nvram->trailer[1], nvram->trailer[2],
	nvram->trailer[3], nvram->trailer[4], nvram->trailer[5],
	nvram->byte_count, sizeof(*nvram) - 12,
	nvram->checksum, csum);
#endif

	/* check valid NVRAM signature, verify byte count and checksum */
	if (nvram->start_marker == 0 &&
	    !memcmp(nvram->trailer, Symbios_trailer, 6) &&
	    nvram->byte_count == sizeof(*nvram) - 12 &&
	    csum == nvram->checksum)
		retv = 0;
out:
	/* return GPIO0/1 to original states after having accessed NVRAM */
	OUTB (nc_gpcntl, old_gpcntl);
	OUTB (nc_gpreg,  old_gpreg);

	return retv;
}

/*
 * Read Symbios NvRAM data and compute checksum.
 */
__initfunc(
static u_short nvram_read_data(ncr_slot *np, u_char *data, int len, u_char *gpreg, u_char *gpcntl)
)
{
	int	x;
	u_short	csum;

	for (x = 0; x < len; x++) 
		nvram_read_byte(np, &data[x], (x == (len - 1)), gpreg, gpcntl);

	for (x = 6, csum = 0; x < len - 6; x++)
		csum += data[x];

	return csum;
}

/*
 * Send START condition to NVRAM to wake it up.
 */
__initfunc(
static void nvram_start(ncr_slot *np, u_char *gpreg)
)
{
	nvram_setBit(np, 1, gpreg, SET_BIT);
	nvram_setBit(np, 0, gpreg, SET_CLK);
	nvram_setBit(np, 0, gpreg, CLR_BIT);
	nvram_setBit(np, 0, gpreg, CLR_CLK);
}

/*
 * WRITE a byte to the NVRAM and then get an ACK to see it was accepted OK,
 * GPIO0 must already be set as an output
 */
__initfunc(
static void nvram_write_byte(ncr_slot *np, u_char *ack_data, u_char write_data, u_char *gpreg, u_char *gpcntl)
)
{
	int x;
	
	for (x = 0; x < 8; x++)
		nvram_doBit(np, 0, (write_data >> (7 - x)) & 0x01, gpreg);
		
	nvram_readAck(np, ack_data, gpreg, gpcntl);
}

/*
 * READ a byte from the NVRAM and then send an ACK to say we have got it,
 * GPIO0 must already be set as an input
 */
__initfunc(
static void nvram_read_byte(ncr_slot *np, u_char *read_data, u_char ack_data, u_char *gpreg, u_char *gpcntl)
)
{
	int x;
	u_char read_bit;

	*read_data = 0;
	for (x = 0; x < 8; x++) {
		nvram_doBit(np, &read_bit, 1, gpreg);
		*read_data |= ((read_bit & 0x01) << (7 - x));
	}

	nvram_writeAck(np, ack_data, gpreg, gpcntl);
}

/*
 * Output an ACK to the NVRAM after reading,
 * change GPIO0 to output and when done back to an input
 */
__initfunc(
static void nvram_writeAck(ncr_slot *np, u_char write_bit, u_char *gpreg, u_char *gpcntl)
)
{
	OUTB (nc_gpcntl, *gpcntl & 0xfe);
	nvram_doBit(np, 0, write_bit, gpreg);
	OUTB (nc_gpcntl, *gpcntl);
}

/*
 * Input an ACK from NVRAM after writing,
 * change GPIO0 to input and when done back to an output
 */
__initfunc(
static void nvram_readAck(ncr_slot *np, u_char *read_bit, u_char *gpreg, u_char *gpcntl)
)
{
	OUTB (nc_gpcntl, *gpcntl | 0x01);
	nvram_doBit(np, read_bit, 1, gpreg);
	OUTB (nc_gpcntl, *gpcntl);
}

/*
 * Read or write a bit to the NVRAM,
 * read if GPIO0 input else write if GPIO0 output
 */
__initfunc(
static void nvram_doBit(ncr_slot *np, u_char *read_bit, u_char write_bit, u_char *gpreg)
)
{
	nvram_setBit(np, write_bit, gpreg, SET_BIT);
	nvram_setBit(np, 0, gpreg, SET_CLK);
	if (read_bit)
		*read_bit = INB (nc_gpreg);
	nvram_setBit(np, 0, gpreg, CLR_CLK);
	nvram_setBit(np, 0, gpreg, CLR_BIT);
}

/*
 * Send STOP condition to NVRAM - puts NVRAM to sleep... ZZzzzz!!
 */
__initfunc(
static void nvram_stop(ncr_slot *np, u_char *gpreg)
)
{
	nvram_setBit(np, 0, gpreg, SET_CLK);
	nvram_setBit(np, 1, gpreg, SET_BIT);
}

/*
 * Set/clear data/clock bit in GPIO0
 */
__initfunc(
static void nvram_setBit(ncr_slot *np, u_char write_bit, u_char *gpreg, int bit_mode)
)
{
	DELAY(5);
	switch (bit_mode){
	case SET_BIT:
		*gpreg |= write_bit;
		break;
	case CLR_BIT:
		*gpreg &= 0xfe;
		break;
	case SET_CLK:
		*gpreg |= 0x02;
		break;
	case CLR_CLK:
		*gpreg &= 0xfd;
		break;

	}
	OUTB (nc_gpreg, *gpreg);
	DELAY(5);
}

#undef SET_BIT 0
#undef CLR_BIT 1
#undef SET_CLK 2
#undef CLR_CLK 3


/* ---------------------------------------------------------------------
**
**	Try reading Tekram format nvram
**
** ---------------------------------------------------------------------
**
** GPOI0 - data in
** GPIO1 - data out
** GPIO2 - clock
** GPIO4 - chip select
**
**	return 0 if NVRAM data OK, 1 if NVRAM data not OK
** ---------------------------------------------------------------------
*/

static u_short Tnvram_read_data(ncr_slot *np, u_short *data, int len, u_char *gpreg);
static void Tnvram_Send_Command(ncr_slot *np, u_short write_data, u_char *read_bit, u_char *gpreg);
static void Tnvram_Read_Word(ncr_slot *np, u_short *nvram_data, u_char *gpreg);
static void Tnvram_Read_Bit(ncr_slot *np, u_char *read_bit, u_char *gpreg);
static void Tnvram_Write_Bit(ncr_slot *np, u_char write_bit, u_char *gpreg);
static void Tnvram_Stop(ncr_slot *np, u_char *gpreg);
static void Tnvram_Clk(ncr_slot *np, u_char *gpreg);

__initfunc(
static int ncr_get_Tekram_nvram (ncr_slot *np, Tekram_nvram *nvram)
)
{
	u_char gpcntl, gpreg;
	u_char old_gpcntl, old_gpreg;
	u_short csum;

	/* save current state of GPCNTL and GPREG */
	old_gpreg	= INB (nc_gpreg);
	old_gpcntl	= INB (nc_gpcntl);

	/* set up GPREG & GPCNTL to set GPIO0/1/2/4 in to known state, 0 in,
	   1/2/4 out */
	gpreg = old_gpreg & 0xe9;
	OUTB (nc_gpreg, gpreg);
	gpcntl = (old_gpcntl & 0xe9) | 0x09;
	OUTB (nc_gpcntl, gpcntl);

	/* input all of NVRAM, 64 words */
	csum = Tnvram_read_data(np, (u_short *) nvram,
			sizeof(*nvram) / sizeof(short), &gpreg);
	
	/* return GPIO0/1/2/4 to original states after having accessed NVRAM */
	OUTB (nc_gpcntl, old_gpcntl);
	OUTB (nc_gpreg,  old_gpreg);

	/* check data valid */
	if (csum != 0x1234)
		return 1;

	return 0;
}

/*
 * Read Tekram NvRAM data and compute checksum.
 */
__initfunc(
static u_short Tnvram_read_data(ncr_slot *np, u_short *data, int len, u_char *gpreg)
)
{
	u_char	read_bit;
	u_short	csum;
	int	x;

	for (x = 0, csum = 0; x < len; x++)  {

		/* output read command and address */
		Tnvram_Send_Command(np, 0x180 | x, &read_bit, gpreg);
		if (read_bit & 0x01)
			return 0; /* Force bad checksum */

		Tnvram_Read_Word(np, &data[x], gpreg);
		csum += data[x];

		Tnvram_Stop(np, gpreg);
	}

	return csum;
}

/*
 * Send read command and address to NVRAM
 */
__initfunc(
static void Tnvram_Send_Command(ncr_slot *np, u_short write_data, u_char *read_bit, u_char *gpreg)
)
{
	int x;

	/* send 9 bits, start bit (1), command (2), address (6)  */
	for (x = 0; x < 9; x++)
		Tnvram_Write_Bit(np, (u_char) (write_data >> (8 - x)), gpreg);

	*read_bit = INB (nc_gpreg);
}

/*
 * READ a byte from the NVRAM
 */
__initfunc(
static void Tnvram_Read_Word(ncr_slot *np, u_short *nvram_data, u_char *gpreg)
)
{
	int x;
	u_char read_bit;

	*nvram_data = 0;
	for (x = 0; x < 16; x++) {
		Tnvram_Read_Bit(np, &read_bit, gpreg);

		if (read_bit & 0x01)
			*nvram_data |=  (0x01 << (15 - x));
		else
			*nvram_data &= ~(0x01 << (15 - x));
	}
}

/* 
 * Read bit from NVRAM
 */
__initfunc(
static void Tnvram_Read_Bit(ncr_slot *np, u_char *read_bit, u_char *gpreg)
)
{
	DELAY(2);
	Tnvram_Clk(np, gpreg);
	*read_bit = INB (nc_gpreg);
}

/*
 * Write bit to GPIO0
 */
__initfunc(
static void Tnvram_Write_Bit(ncr_slot *np, u_char write_bit, u_char *gpreg)
)
{
	if (write_bit & 0x01)
		*gpreg |= 0x02;
	else
		*gpreg &= 0xfd;
		
	*gpreg |= 0x10;
		
	OUTB (nc_gpreg, *gpreg);
	DELAY(2);

	Tnvram_Clk(np, gpreg);
}

/*
 * Send STOP condition to NVRAM - puts NVRAM to sleep... ZZZzzz!!
 */
__initfunc(
static void Tnvram_Stop(ncr_slot *np, u_char *gpreg)
)
{
	*gpreg &= 0xef;
	OUTB (nc_gpreg, *gpreg);
	DELAY(2);

	Tnvram_Clk(np, gpreg);
}

/*
 * Pulse clock bit in GPIO0
 */
__initfunc(
static void Tnvram_Clk(ncr_slot *np, u_char *gpreg)
)
{
	OUTB (nc_gpreg, *gpreg | 0x04);
	DELAY(2);
	OUTB (nc_gpreg, *gpreg);
}

#endif	/* SCSI_NCR_NVRAM_SUPPORT */

/*
**	Module stuff
*/

#ifdef MODULE
Scsi_Host_Template driver_template = NCR53C8XX;
#include "scsi_module.c"
#endif
