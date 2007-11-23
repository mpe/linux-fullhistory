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
*******************************************************************************
*/

/*
**	30 August 1996, version 1.12c
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
**		53C810		(NCR BIOS in flash-bios required) 
**		53C815		(~53C810 with on board rom BIOS)
**		53C820		(Wide, NCR BIOS in flash bios required)
**		53C825		(Wide, ~53C820 with on board rom BIOS)
**		53C860		(not yet tested)
**		53C875		(not yet tested)
**
**	Other features:
**		Memory mapped IO (linux-1.3.X only)
**		Module
**		Shared IRQ (since linux-1.3.72)
*/

#define SCSI_NCR_DEBUG
#define SCSI_NCR_DEBUG_FLAGS	(0)		

#define NCR_DATE "pl23 95/09/07"

#define NCR_VERSION	(2)

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
#include <linux/bios32.h>
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
#include "linux/blk.h"
#else
#include "../block/blk.h"
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
**	Proc info and user command support
*/

#ifdef SCSI_NCR_PROC_INFO_SUPPORT
#define SCSI_NCR_PROFILE
#define SCSI_NCR_USER_COMMAND
#endif

/*
**    SCSI address of this device.
**    The boot routines should have set it.
**    If not, use this.
*/

#ifndef SCSI_NCR_MYADDR
#define SCSI_NCR_MYADDR      (7)
#endif

/*
**    The maximal synchronous frequency in kHz.
**    (0=asynchronous)
*/

#ifndef SCSI_NCR_MAX_SYNC
#define SCSI_NCR_MAX_SYNC   (10000)
#endif

/*
**    The maximal bus with (in log2 byte)
**    (0=8 bit, 1=16 bit)
*/

#ifndef SCSI_NCR_MAX_WIDE
#define SCSI_NCR_MAX_WIDE   (1)
#endif

/*
**    The maximum number of tags per logic unit.
**    Used only for disk devices that support tags.
*/

#ifndef SCSI_NCR_MAX_TAGS
#define SCSI_NCR_MAX_TAGS    (4)
#endif

/*==========================================================
**
**      Configuration and Debugging
**
**==========================================================
*/

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
**    The maximum transfer length (should be >= 64k).
**    MUST NOT be greater than (MAX_SCATTER-1) * NBPG.
*/

#if 0
#define MAX_SIZE  ((MAX_SCATTER-1) * (long) NBPG)
#endif

/*
**	other
*/

#define NCR_SNOOP_TIMEOUT (1000000)

#if defined(SCSI_NCR_IOMAPPED) || defined(__alpha__)
#define NCR_IOMAPPED
#endif

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

#define bcopy(s, d, n)	memcpy((d), (s), (n))
#define bzero(d, n)	memset((d), 0, (n))

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
**	addresses. But on Alpha architecture they are different.
**	In the original Bsd driver, vtophys() is called to translate
**	data addresses to IO bus addresses. In order to minimize
**	change, I decide to define vtophys() as virt_to_bus().
*/

#if LINUX_VERSION_CODE >= LinuxVersionCode(1,3,0)
#define vtophys(p)	virt_to_bus(p)

/*
**	Memory mapped IO
**
**	Linux 1.3.X allow to remap physical pages addresses greater than
**	the highest physical memory address to kernel virtual pages.
**	We must use ioremap() to map the page and iounmap() to unmap it.
**	The memory base of ncr chips is set by the bios at a high physical
**	address. Also we can map it, and MMIO is possible.
*/

static inline vm_offset_t remap_pci_mem(u_long base, u_long size)
{
	u_long page_base	= ((u_long) base) & PAGE_MASK;
	u_long page_offs	= ((u_long) base) - page_base;
	u_long page_remapped	= (u_long) ioremap(page_base, page_offs+size);

	return (vm_offset_t) (page_remapped ? (page_remapped + page_offs) : 0UL);
}
static inline void unmap_pci_mem(vm_offset_t vaddr, u_long size)
{
	if (vaddr) iounmap((void *) (vaddr & PAGE_MASK));
}

#else

/*
**	Linux 1.2.X assumes that addresses (virtual, physical, bus)
**	are the same.
**
**	I have not found how to do MMIO. It seems that only processes can
**	map high physical pages to virtual (Xservers can do MMIO).
*/

#define vtophys(p)	((u_long) (p))
#endif

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
**/

static inline void *m_alloc(int size)
{
	void *ptr = (void *) kmalloc(size, GFP_ATOMIC);
	if (((unsigned long) ptr) & 3)
		panic("ncr53c8xx: kmalloc returns misaligned address %lx\n", (unsigned long) ptr);
	return ptr;
}

static inline void m_free(void *ptr, int size)
	{ kfree(ptr); }

/*
**	Transfer direction
**
**	The middle scsi driver of Linux does not provide the transfer
**	direction in the command structure.
**	FreeBsd ncr driver require this information.
**
**	I spent some hours to read the scsi2 documentation to see if
**	it was possible to deduce the direction of transfer from the opcode
**	of the command. It seems that it's OK.
**	guess_xfer_direction() seems to work. If it's wrong we will
**	get a phase mismatch on some opcode.
*/

#define XferNone	0
#define XferIn		1
#define XferOut		2
#define XferBoth	3
static int guess_xfer_direction(int opcode);

/*
**	Head of list of NCR boards
**
**	Host is retrieved by its irq level.
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

#define bootverbose 1

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

#ifdef SCSI_NCR_DEBUG
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
**	Access to the controller chip.
**
**	If NCR_IOMAPPED is defined, only IO are used by the driver.
**	Else, we begins initialisations by using MMIO.
**		If cache test fails, we retry using IO mapped.
**	The flag "use_mmio" in the ncb structure is set to 1 if
**	mmio is possible.
**
**==========================================================
*/

/*
**	IO mapped input / ouput
*/

#define	IOM_INB(r)         inb (np->port + offsetof(struct ncr_reg, r))
#define	IOM_INB_OFF(o)     inb (np->port + (o))
#define	IOM_INW(r)         inw (np->port + offsetof(struct ncr_reg, r))
#define	IOM_INL(r)         inl (np->port + offsetof(struct ncr_reg, r))
#define	IOM_INL_OFF(o)     inl (np->port + (o))

#define	IOM_OUTB(r, val)     outb ((val), np->port+offsetof(struct ncr_reg,r))
#define	IOM_OUTW(r, val)     outw ((val), np->port+offsetof(struct ncr_reg,r))
#define	IOM_OUTL(r, val)     outl ((val), np->port+offsetof(struct ncr_reg,r))
#define	IOM_OUTL_OFF(o, val) outl ((val), np->port + (o))

/*
**	MEMORY mapped IO input / output
*/

#define MMIO_INB(r)        readb(&np->reg_remapped->r)
#define MMIO_INB_OFF(o)    readb((char *)np->reg_remapped + (o))
#define MMIO_INW(r)        readw(&np->reg_remapped->r)
#define MMIO_INL(r)        readl(&np->reg_remapped->r)
#define MMIO_INL_OFF(o)    readl((char *)np->reg_remapped + (o))

#define MMIO_OUTB(r, val)     writeb((val), &np->reg_remapped->r)
#define MMIO_OUTW(r, val)     writew((val), &np->reg_remapped->r)
#define MMIO_OUTL(r, val)     writel((val), &np->reg_remapped->r)
#define MMIO_OUTL_OFF(o, val) writel((val), (char *)np->reg_remapped + (o))

/*
**	IO mapped only input / output
*/

#ifdef NCR_IOMAPPED

#define INB(r)             IOM_INB(r)
#define INB_OFF(o)         IOM_INB_OFF(o)
#define INW(r)             IOM_INW(r)
#define INL(r)             IOM_INL(r)
#define INL_OFF(o)         IOM_INL_OFF(o)

#define OUTB(r, val)       IOM_OUTB(r, val)
#define OUTW(r, val)       IOM_OUTW(r, val)
#define OUTL(r, val)       IOM_OUTL(r, val)
#define OUTL_OFF(o, val)   IOM_OUTL_OFF(o, val)

/*
**	IO mapped or MEMORY mapped depending on flag "use_mmio"
*/

#else

#define	INB(r)             (np->use_mmio ? MMIO_INB(r) : IOM_INB(r))
#define	INB_OFF(o)         (np->use_mmio ? MMIO_INB_OFF(o) : IOM_INB_OFF(o))
#define	INW(r)             (np->use_mmio ? MMIO_INW(r) : IOM_INW(r))
#define	INL(r)             (np->use_mmio ? MMIO_INL(r) : IOM_INL(r))
#define	INL_OFF(o)         (np->use_mmio ? MMIO_INL_OFF(o) : IOM_INL_OFF(o))

#define	OUTB(r, val)       (np->use_mmio ? MMIO_OUTB(r, val) : IOM_OUTB(r, val))
#define	OUTW(r, val)       (np->use_mmio ? MMIO_OUTW(r, val) : IOM_OUTW(r, val))
#define	OUTL(r, val)       (np->use_mmio ? MMIO_OUTL(r, val) : IOM_OUTL(r, val))
#define	OUTL_OFF(o, val)   (np->use_mmio ? MMIO_OUTL_OFF(o, val) : IOM_OUTL_OFF(o, val))

#endif

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
#define	SIR_MAX			(12)

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
#define	MAX_TAGS	(16)		/* hard limit */

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

#define	UF_TRACE	(0x01)

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
	u_long data;
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
	**	statistical data
	*/

	u_long	transfers;
	u_long	bytes;

	/*
	**	user settable limits for sync transfer
	**	and tagged commands.
	*/

	u_char	usrsync;
	u_char	usrtags;
	u_char	usrwide;
	u_char	usrflag;

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

	u_long		savep;
	u_long		lastp;
	u_long		goalp;

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

	u_char		status[8];
};

/*
**	The status bytes are used by the host and the script processor.
**
**	The first four byte are copied to the scratchb register
**	(declared as scr0..scr3 in ncr_reg.h) just after the select/reselect,
**	and copied back just after disconnecting.
**	Inside the script the XX_REG are used.
**
**	The last four bytes are used inside the script by "COPY" commands.
**	Because source and destination must have the same alignment
**	in a longword, the fields HAVE to be at the choosen offsets.
**		xerr_st	(4)	0	(0x34)	scratcha
**		sync_st	(5)	1	(0x05)	sxfer
**		wide_st	(7)	3	(0x03)	scntl3
*/

/*
**	First four bytes (script)
*/
#define  QU_REG	scr0
#define  HS_REG	scr1
#define  HS_PRT	nc_scr1
#define  SS_REG	scr2
#define  PS_REG	scr3

/*
**	First four bytes (host)
*/
#define  actualquirks  phys.header.status[0]
#define  host_status   phys.header.status[1]
#define  scsi_status   phys.header.status[2]
#define  parity_status phys.header.status[3]

/*
**	Last four bytes (script)
*/
#define  xerr_st       header.status[4]	/* MUST be ==0 mod 4 */
#define  sync_st       header.status[5]	/* MUST be ==1 mod 4 */
#define  nego_st       header.status[6]
#define  wide_st       header.status[7]	/* MUST be ==3 mod 4 */

/*
**	Last four bytes (host)
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
};

#define CCB_PHYS(cp,lbl)	(cp->p_ccb + offsetof(struct ccb, lbl))

/*==========================================================
**
**      Declaration of structs:     NCR device descriptor
**
**==========================================================
*/

struct ncb {
	/*-----------------------------------------------
	**	Specific Linux fields
	**-----------------------------------------------
	*/
	int    unit;			/* Unit number                       */
	int    chip;			/* Chip number                       */
	struct timer_list timer;	/* Timer link header                 */
	int	ncr_cache;		/* Cache test variable               */
	int	release_stage;		/* Synchronisation stage on release  */
	Scsi_Cmnd *waiting_list;	/* Waiting list header for commands  */
					/* that we can't put into the squeue */
#ifndef NCR_IOMAPPED
	volatile struct ncr_reg*
		reg_remapped;		/* Virtual address of the memory     */
					/* base of the ncr chip              */
	int	use_mmio;		/* Indicate mmio is OK               */
#endif
	/*-----------------------------------------------
	**	Added field to support differences
	**	between ncr chips.
	**-----------------------------------------------
	*/
	u_short	device_id;
	u_char	revision_id;
#define ChipDevice	((np)->device_id)
#define ChipVersion	((np)->revision_id & 0xf0)

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

	/*
	**	pointer to the chip's registers.
	*/
	volatile
	struct ncr_reg* reg;

	/*
	**	A copy of the script, relocated for this ncb.
	*/
	struct script	*script;

	/*
	**	Physical address of this instance of ncb->script
	*/
	u_long		p_script;

	/*
	**	The SCSI address of the host adapter.
	*/
	u_char	  myaddr;

	/*
	**	timing parameters
	*/
	u_char		ns_async;
	u_char		ns_sync;
	u_char		rv_scntl3;

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
	u_long		squeue [MAX_START];
	u_short		squeueput;
	u_short		actccbs;

	/*
	**	Timeout handler
	*/
	u_long		heartbeat;
	u_short		ticks;
	u_short		latetime;
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
	u_long		disc_phys;
	u_long		disc_ref;

	/*
	**	The global header.
	**	Accessible to both the host and the
	**	script-processor.
	*/
	struct head     header;

	/*
	**	The global control block.
	**	It's used only during the configuration phase.
	**	A target control block will be created
	**	after the first successful transfer.
	*/
	struct ccb      ccb;

	/*
	**	message buffers.
	**	Should be longword aligned,
	**	because they're written with a
	**	COPY script command.
	*/
	u_char		msgout[8];
	u_char		msgin [8];
	u_long		lastmsg;

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
	u_int		port;

	/*
	**	irq level
	*/
	u_short		irq;
};

#define NCB_SCRIPT_PHYS(np,lbl)	(np->p_script + offsetof (struct script, lbl))

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

struct script {
	ncrcmd	start		[  7];
	ncrcmd	start0		[  2];
	ncrcmd	start1		[  3];
	ncrcmd  startpos	[  1];
	ncrcmd  tryloop		[MAX_START*5+2];
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
	ncrcmd  dispatch	[ 33];
	ncrcmd	no_data		[ 17];
	ncrcmd  checkatn	[ 10];
	ncrcmd  command		[ 15];
	ncrcmd  status		[ 27];
	ncrcmd  msg_in		[ 26];
	ncrcmd  msg_bad		[  6];
	ncrcmd  msg_parity	[  6];
	ncrcmd	msg_reject	[  8];
	ncrcmd	msg_ign_residue	[ 32];
	ncrcmd  msg_extended	[ 18];
	ncrcmd  msg_ext_2	[ 18];
	ncrcmd	msg_wdtr	[ 27];
	ncrcmd  msg_ext_3	[ 18];
	ncrcmd	msg_sdtr	[ 27];
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
	ncrcmd	msg_out_abort	[ 10];
	ncrcmd  getcc		[  4];
	ncrcmd  getcc1		[  5];
#ifdef NCR_GETCC_WITHMSG
	ncrcmd	getcc2		[ 33];
#else
	ncrcmd	getcc2		[ 14];
#endif
	ncrcmd	getcc3		[ 10];
	ncrcmd  badgetcc	[  6];
	ncrcmd	reselect	[ 12];
	ncrcmd	reselect2	[  6];
	ncrcmd	resel_tmp	[  5];
	ncrcmd  resel_lun	[ 18];
	ncrcmd	resel_tag	[ 24];
	ncrcmd  data_in		[MAX_SCATTER * 4 + 7];
	ncrcmd  data_out	[MAX_SCATTER * 4 + 7];
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
static	void	ncr_free_ccb	(ncb_p np, ccb_p cp);
static	void	ncr_getclock	(ncb_p np, u_char scntl3);
static	ccb_p	ncr_get_ccb	(ncb_p np, u_long t,u_long l);
static	void	ncr_init	(ncb_p np, char * msg, u_long code);
static	int	ncr_intr	(ncb_p np);
static	void	ncr_int_ma	(ncb_p np);
static	void	ncr_int_sir	(ncb_p np);
static  void    ncr_int_sto     (ncb_p np);
static	u_long	ncr_lookup	(char* id);
static	void	ncr_negotiate	(struct ncb* np, struct tcb* tp);

#ifdef SCSI_NCR_PROFILE
static	int	ncr_delta	(u_long from, u_long to);
static	void	ncb_profile	(ncb_p np, ccb_p cp);
#endif

static	void	ncr_script_copy_and_bind
				(struct script * script, ncb_p np);
static  void    ncr_script_fill (struct script * scr);
static	int	ncr_scatter	(ccb_p cp, Scsi_Cmnd *cmd);
static	void	ncr_setmaxtags	(ncb_p np, tcb_p tp, u_long usrtags);
static	void	ncr_setsync	(ncb_p np, ccb_p cp, u_char sxfer);
static	void	ncr_settags     (tcb_p tp, lcb_p lp);
static	void	ncr_setwide	(ncb_p np, ccb_p cp, u_char wide);
static	int	ncr_show_msg	(u_char * msg);
static	int	ncr_snooptest	(ncb_p np);
static	void	ncr_timeout	(ncb_p np);
static  void    ncr_wakeup      (ncb_p np, u_long code);

#ifdef SCSI_NCR_USER_COMMAND
static	void	ncr_usercmd	(ncb_p np);
#endif

static int ncr_attach (Scsi_Host_Template *tpnt, int unit, u_short device_id,
		       u_char revision_id, int chip, u_int base, u_int io_port, 
		       int irq, int bus, u_char device_fn);

static void insert_into_waiting_list(ncb_p np, Scsi_Cmnd *cmd);
static Scsi_Cmnd *remove_from_waiting_list(ncb_p np, Scsi_Cmnd *cmd);
static void process_waiting_list(ncb_p np, int sts);

#define requeue_waiting_list(np) process_waiting_list((np), DID_OK)
#define abort_waiting_list(np) process_waiting_list((np), DID_ABORT)
#define reset_waiting_list(np) process_waiting_list((np), DID_RESET)

/*==========================================================
**
**
**      Global static data.
**
**
**==========================================================
*/

#if 0
static char ident[] =
 	"\n$Id: ncr.c,v 1.67 1996/03/11 19:36:07 se Exp $\n";
static u_long	ncr_version = NCR_VERSION	* 11
	+ (u_long) sizeof (struct ncb)	*  7
	+ (u_long) sizeof (struct ccb)	*  5
	+ (u_long) sizeof (struct lcb)	*  3
	+ (u_long) sizeof (struct tcb)	*  2;
#endif

#ifdef SCSI_NCR_DEBUG
static int ncr_debug = SCSI_NCR_DEBUG_FLAGS;
#endif

/*==========================================================
**
**
**      Global static data:	auto configure
**
**
**==========================================================
*/

static char *ncr_name (ncb_p np)
{
	static char name[10];
	sprintf(name, "ncr53c%d-%d", np->chip, np->unit);
	return (name);
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
#define	RELOC_MASK	0xf0000000

#define	NADDR(label)	(RELOC_SOFTC | offsetof(struct ncb, label))
#define PADDR(label)    (RELOC_LABEL | offsetof(struct script, label))
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
static void *script_kvars[] =
	{ (void *)&jiffies };

static	struct script script0 = {
/*--------------------------< START >-----------------------*/ {
	/*
	**	Claim to be still alive ...
	*/
	SCR_COPY (sizeof (((struct ncb *)0)->heartbeat)),
		KVAR(SCRIPT_KVAR_JIFFIES),
		NADDR (heartbeat),

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
		PADDR(tryloop),
}/*-------------------------< TRYLOOP >---------------------*/,{
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
**		PADDR(tryloop),
**
**-----------------------------------------------------------
*/
0

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
	SCR_COPY (4),
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
	SCR_COPY (4),
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
		PADDR (msg_reject),
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
	SCR_RETURN ^ IFTRUE (IF (SCR_DATA_IN)),
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
		PADDR (msg_parity),
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
		PADDR (msg_extended),
	SCR_JUMP ^ IFTRUE (DATA (M_NOOP)),
		PADDR (clrack),
	SCR_JUMP ^ IFTRUE (DATA (M_REJECT)),
		PADDR (msg_reject),
	SCR_JUMP ^ IFTRUE (DATA (M_IGN_RESIDUE)),
		PADDR (msg_ign_residue),
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

}/*-------------------------< MSG_PARITY >---------------*/,{
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
		PADDR (msg_parity),
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
		PADDR (msg_parity),
	SCR_FROM_REG (scratcha),
		0,
	/*
	*/
	SCR_JUMP ^ IFTRUE (DATA (3)),
		PADDR (msg_ext_3),
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
		PADDR (msg_parity),
	SCR_FROM_REG (scratcha),
		0,
	SCR_JUMP ^ IFTRUE (DATA (M_X_WIDE_REQ)),
		PADDR (msg_wdtr),
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
		PADDR (msg_parity),
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
		PADDR (msg_parity),
	SCR_FROM_REG (scratcha),
		0,
	SCR_JUMP ^ IFTRUE (DATA (M_X_SYNC_REQ)),
		PADDR (msg_sdtr),
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
		PADDR (msg_parity),
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
	SCR_COPY (4),
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
		PADDR(getcc2),
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
	SCR_COPY (1),
		NADDR (header.savep),
		PADDR (disconnect0),
}/*-------------------------< DISCONNECT0 >--------------*/,{
/*<<<*/	SCR_JUMPR ^ IFTRUE (DATA (1)),
		20,
	/*
	**	neither this
	*/
	SCR_COPY (1),
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
		PADDR (msg_out_abort),
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
	SCR_COPY (4),
		RADDR (dsa),
		PADDR (getcc1),
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
		PADDR(getcc3),
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

}/*------------------------< BADGETCC >---------------------*/,{
	/*
	**	If SIGP was set, clear it and try again.
	*/
	SCR_FROM_REG (ctest2),
		0,
	SCR_JUMP ^ IFTRUE (MASK (CSIGP,CSIGP)),
		PADDR (getcc2),
	SCR_INT,
		SIR_SENSE_FAILED,
}/*-------------------------< RESELECT >--------------------*/,{
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
	SCR_REG_SFBR (ssid, SCR_AND, 0x87),
		0,
	SCR_TO_REG (ctest0),
		0,
	SCR_JUMP,
		NADDR (jump_tcb),
}/*-------------------------< RESELECT2 >-------------------*/,{
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

}/*-------------------------< DATA_IN >--------------------*/,{
/*
**	Because the size depends on the
**	#define MAX_SCATTER parameter,
**	it is filled in at runtime.
**
**	SCR_JUMP ^ IFFALSE (WHEN (SCR_DATA_IN)),
**		PADDR (no_data),
**	SCR_COPY (sizeof (u_long)),
**		KVAR(SCRIPT_KVAR_JIFFIES),
**		NADDR (header.stamp.data),
**	SCR_MOVE_TBL ^ SCR_DATA_IN,
**		offsetof (struct dsb, data[ 0]),
**
**  ##===========< i=1; i<MAX_SCATTER >=========
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
}/*-------------------------< DATA_OUT >-------------------*/,{
/*
**	Because the size depends on the
**	#define MAX_SCATTER parameter,
**	it is filled in at runtime.
**
**	SCR_JUMP ^ IFFALSE (WHEN (SCR_DATA_IN)),
**		PADDR (no_data),
**	SCR_COPY (sizeof (u_long)),
**		KVAR(SCRIPT_KVAR_JIFFIES),
**		NADDR (header.stamp.data),
**	SCR_MOVE_TBL ^ SCR_DATA_OUT,
**		offsetof (struct dsb, data[ 0]),
**
**  ##===========< i=1; i<MAX_SCATTER >=========
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
0	/* was (u_long)&ident ? */

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

void ncr_script_fill (struct script * scr)
{
	int	i;
	ncrcmd	*p;

	p = scr->tryloop;
	for (i=0; i<MAX_START; i++) {
		*p++ =SCR_COPY (4);
		*p++ =NADDR (squeue[i]);
		*p++ =RADDR (dsa);
		*p++ =SCR_CALL;
		*p++ =PADDR (trysel);
	};
	*p++ =SCR_JUMP;
	*p++ =PADDR(tryloop);

	assert ((u_long)p == (u_long)&scr->tryloop + sizeof (scr->tryloop));

	p = scr->data_in;

	*p++ =SCR_JUMP ^ IFFALSE (WHEN (SCR_DATA_IN));
	*p++ =PADDR (no_data);
	*p++ =SCR_COPY (sizeof (u_long));
	*p++ =KVAR(SCRIPT_KVAR_JIFFIES);
	*p++ =NADDR (header.stamp.data);
	*p++ =SCR_MOVE_TBL ^ SCR_DATA_IN;
	*p++ =offsetof (struct dsb, data[ 0]);

	for (i=1; i<MAX_SCATTER; i++) {
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

	p = scr->data_out;

	*p++ =SCR_JUMP ^ IFFALSE (WHEN (SCR_DATA_OUT));
	*p++ =PADDR (no_data);
	*p++ =SCR_COPY (sizeof (u_long));
	*p++ =KVAR(SCRIPT_KVAR_JIFFIES);
	*p++ =NADDR (header.stamp.data);
	*p++ =SCR_MOVE_TBL ^ SCR_DATA_OUT;
	*p++ =offsetof (struct dsb, data[ 0]);

	for (i=1; i<MAX_SCATTER; i++) {
		*p++ =SCR_CALL ^ IFFALSE (WHEN (SCR_DATA_OUT));
		*p++ =PADDR (dispatch);
		*p++ =SCR_MOVE_TBL ^ SCR_DATA_OUT;
		*p++ =offsetof (struct dsb, data[i]);
	};

	*p++ =SCR_CALL;
	*p++ =PADDR (dispatch);
	*p++ =SCR_JUMP;
	*p++ =PADDR (no_data);

	assert ((u_long)p == (u_long)&scr->data_out + sizeof (scr->data_out));
}

/*==========================================================
**
**
**	Copy and rebind a script.
**
**
**==========================================================
*/

static void ncr_script_copy_and_bind (struct script *script, ncb_p np)
{
	ncrcmd  opcode, new, old, tmp1, tmp2;
	ncrcmd	*src, *dst, *start, *end;
	int relocs;

	np->p_script = vtophys(np->script);

	src = script->start;
	dst = np->script->start;

	start = src;
	end = src + (sizeof (struct script) / 4);

	while (src < end) {

		*dst++ = opcode = *src++;

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
					new = (old & ~RELOC_MASK) + np->paddr;
					break;
				case RELOC_LABEL:
					new = (old & ~RELOC_MASK) + np->p_script;
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

				*dst++ = new;
			}
		} else
			*dst++ = *src++;

	};
}

/*==========================================================
**
**
**      Auto configuration:  attach and init a host adapter.
**
**
**==========================================================
*/

#define	MIN_ASYNC_PD	40
#define	MIN_SYNC_PD	20


/*
**	Linux host data structure
**
**	The script area is allocated in the host data structure
**	because kmalloc() returns NULL during scsi initialisations
**	with Linux 1.2.X
*/

struct host_data {
     struct ncb ncb_data;
     struct script script_data;
};

/*
**	Print something which allow to retreive the controler type, unit,
**	target, lun concerned by a kernel message.
*/

#define PRINT_LUN(np, target, lun) \
printf("%s-<target %d, lun %d>: ", ncr_name(np), (int) (target), (int) (lun))

static inline void PRINT_ADDR(Scsi_Cmnd *cmd)
{
	struct host_data *host_data = (struct host_data *) cmd->host->hostdata;
	ncb_p np                    = &host_data->ncb_data;
	if (np) PRINT_LUN(np, cmd->target, cmd->lun);
}


/*
**	Host attach and initialisations.
**
**	Allocate host data and ncb structure.
**	Request IO region and remap MMIO region.
**	Do chip initialization.
**	Try with mmio.
**	If mmio not possible (misconfigured cache),
**	retry with io mapped.
**	If all is OK, install interrupt handling and
**	start the timer daemon.
*/

static int ncr_attach (Scsi_Host_Template *tpnt, int unit, ushort device_id,
		       u_char revision_id, int chip, u_int base, u_int io_port, 
		       int irq, int bus, u_char device_fn)

{
        struct host_data *host_data;
	ncb_p np;
        struct Scsi_Host *instance = 0;
	u_long flags = 0;

printf("ncr_attach: unit=%d chip=%d base=%x, io_port=%x, irq=%d\n", unit, chip, base, io_port, irq);

	/*
	**	Allocate host_data structure
	*/
        if (!(instance = scsi_register(tpnt, sizeof(*host_data))))
	        goto attach_error;

	/*
	**	Initialize structure.
	*/
	instance->irq = irq;
	host_data = (struct host_data *) instance->hostdata;

	np        = &host_data->ncb_data;
	bzero (np, sizeof (*np));
	np->unit = unit;
	np->chip = chip;
	np->device_id	= device_id;
	np->revision_id	= revision_id;
	np->script = &host_data->script_data;

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

	np->paddr = base;
	np->vaddr = base;

#ifndef NCR_IOMAPPED
	np->reg_remapped = (struct ncr_reg *) remap_pci_mem((u_long) base, (u_long) 128);
	if (!np->reg_remapped) {
		printf("%s: can't map memory mapped IO region\n", ncr_name(np));
		np->use_mmio = 0;
	}
	printf("%s: using memory mapped IO at virtual address 0x%lx\n", ncr_name(np), (u_long) np->reg_remapped);
		np->use_mmio = 1;
#endif
	/*
	**	Try to map the controller chip into iospace.
	*/

	request_region(io_port, 128, "ncr53c8xx");
	np->port = io_port;

	/*
	**	Do chip dependent initialization.
	*/

	switch (device_id) {
	case PCI_DEVICE_ID_NCR_53C825:
	case PCI_DEVICE_ID_NCR_53C875:
		np->maxwide = 1;
		break;
	default:
		np->maxwide = 0;
		break;
	}

	/*
	**	Fill Linux host instance structure
	*/
#if LINUX_VERSION_CODE >= LinuxVersionCode(1,3,0)
	instance->max_channel	= 0;
	instance->max_id	= np->maxwide ? 16 : 8;
	instance->max_lun	= SCSI_NCR_MAX_LUN;
#endif
#ifndef NCR_IOMAPPED
	instance->base		= (char *) np->reg_remapped;
#endif
	instance->io_port	= io_port;
	instance->n_io_port	= 128;
	instance->dma_channel	= 0;
#if LINUX_VERSION_CODE >= LinuxVersionCode(2,0,0)
	instance->select_queue_depths = ncr53c8xx_select_queue_depths;
#endif

	/*
	**	Patch script to physical addresses
	*/
	ncr_script_fill (&script0);
	ncr_script_copy_and_bind (&script0, np);
	np->ccb.p_ccb		= vtophys (&np->ccb);

	/*
	**	init data structure
	*/

	np->jump_tcb.l_cmd	= SCR_JUMP;
	np->jump_tcb.l_paddr	= NCB_SCRIPT_PHYS (np, abort);

	/*
	**	Make the controller's registers available.
	**	Now the INB INW INL OUTB OUTW OUTL macros
	**	can be used safely.
	*/

	np->reg = (struct ncr_reg*) np->vaddr;

#ifndef NCR_IOMAPPED
retry_chip_init:
#endif

	/*
	**  Get SCSI addr of host adapter (set by bios?).
	*/

	np->myaddr = INB(nc_scid) & 0x07;
	if (!np->myaddr) np->myaddr = SCSI_NCR_MYADDR;

	/*
	**	Get the value of the chip's clock.
	**	Find the right value for scntl3.
	*/

	ncr_getclock (np, INB(nc_scntl3));

	/*
	**	Reset chip.
	*/

	OUTW (nc_sien , 0);	/* Disable scsi interrupts */
	OUTB (nc_dien , 0);	/* Disable dma interrupts */

	OUTB (nc_istat,  SRST);
	DELAY (1000);
	OUTB (nc_istat,  0   );

	/*
	**	Reset chip, once again.
	*/

	OUTB (nc_istat,  SRST);
	DELAY (1000);
	OUTB (nc_istat,  0   );

	/*
	**	Now check the cache handling of the pci chipset.
	*/

	if (ncr_snooptest (np)) {
#ifndef NCR_IOMAPPED
		if (np->use_mmio) {
printf("%s: cache misconfigured, retrying with IO mapped at 0x%lx\n",
	ncr_name(np), (u_long) np->port);
			np->use_mmio = 0;
			goto retry_chip_init;
		}
#endif
		printf ("CACHE INCORRECTLY CONFIGURED.\n");
		goto attach_error;
	};

	/*
	**	Install the interrupt handler.
	*/
#if LINUX_VERSION_CODE >= LinuxVersionCode(1,3,70)
#   ifdef SCSI_NCR_SHARE_IRQ
	printf("%s: requesting shared irq %d (dev_id=0x%lx)\n",
	        ncr_name(np), irq, (u_long) np);
	if (request_irq(irq, ncr53c8xx_intr, SA_INTERRUPT|SA_SHIRQ, "53c8xx", np)) {
#   else
	if (request_irq(irq, ncr53c8xx_intr, SA_INTERRUPT, "53c8xx", NULL)) {
#   endif
#else
	if (request_irq(irq, ncr53c8xx_intr, SA_INTERRUPT, "53c8xx")) {
#endif
		printf("%s: request irq %d failure\n", ncr_name(np), irq);
		goto attach_error;
	}
	np->irq = irq;

	/*
	**	After SCSI devices have been opened, we cannot
	**	reset the bus safely, so we do it here.
	**	Interrupt handler does the real work.
	*/

	OUTB (nc_scntl1, CRST);
	DELAY (1000);

	/*
	**	Process the reset exception,
	**	if interrupts are not enabled yet.
	**	Then enable disconnects.
	*/
	save_flags(flags); cli();
	ncr_exception (np);
	restore_flags(flags);

#ifndef SCSI_NCR_NO_DISCONNECT
	np->disc = 1;
#endif

	/*
	**	The middle-level SCSI driver does not
	**	wait devices to settle.
	*/
#ifdef SCSI_NCR_SETTLE_TIME
#if    SCSI_NCR_SETTLE_TIME > 2
	printf("%s: waiting for scsi devices to settle...\n", ncr_name(np));
#endif
#if    SCSI_NCR_SETTLE_TIME > 0
	DELAY(SCSI_NCR_SETTLE_TIME*1000000);
#endif
#endif

	/*
	**	Now let the generic SCSI driver
	**	look for the SCSI devices on the bus ..
	*/

	/*
	**	start the timeout daemon
	*/
	ncr_timeout (np);
	np->lasttime=0;

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
#ifndef NCR_IOMAPPED
	if (np->reg_remapped) {
		printf("%s: releasing memory mapped IO region %lx[%d]\n", ncr_name(np), (u_long) np->reg_remapped, 128);
		unmap_pci_mem((vm_offset_t) np->reg_remapped, (u_long) 128);
	}
#endif
	if (np->port) {
		printf("%s: releasing IO region %x[%d]\n", ncr_name(np), np->port, 128);
		release_region(np->port, 128);
	}
	scsi_unregister(instance);

        return -1;
 }

/*==========================================================
**
**
**	Process pending device interrupts.
**
**
**==========================================================
*/
int ncr_intr(np)
	ncb_p np;
{
	int n = 0;
	u_long flags;

	save_flags(flags); cli();

	if (DEBUG_FLAGS & DEBUG_TINY) printf ("[");

#ifdef SCSI_NCR_PARANOIA
	if (INB(nc_istat) & (INTF|SIP|DIP)) {
		/*
		**	Repeat until no outstanding ints
		*/
		do {
#endif
			ncr_exception (np);
#ifdef SCSI_NCR_PARANOIA
		} while (INB(nc_istat) & (INTF|SIP|DIP));

		n=1;
		np->ticks = 5 * HZ;
	};
#endif

	if (DEBUG_FLAGS & DEBUG_TINY) printf ("]\n");

	restore_flags(flags);

	return (n);
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
	ncb_p np                      = &host_data->ncb_data;
	tcb_p tp                      = &np->target[cmd->target];

	ccb_p cp;
	lcb_p lp;

	int	segments;
	u_char	qidx, nego, idmsg, *msgptr;
	u_long  msglen, msglen2;
	u_long	flags;
	int	xfer_direction;

	cmd->scsi_done     = done;
	cmd->host_scribble = NULL;
	cmd->SCp.ptr       = NULL;
	cmd->SCp.buffer    = NULL;

	/*---------------------------------------------
	**
	**   Reset SCSI bus
	**
	**	Interrupt handler does the real work.
	**
	**---------------------------------------------
	*/
#if 0
	if (flags & SCSI_RESET) {
		OUTB (nc_scntl1, CRST);
		DELAY (1000);
		return(COMPLETE);
	}
#endif

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


	if (DEBUG_FLAGS & DEBUG_TINY) {
		PRINT_ADDR(cmd);
		printf ("CMD=%x ", cmd->cmnd[0]);
	}

	/*---------------------------------------------------
	**
	**	Assign a ccb / bind cmd
	**	If no free ccb, insert cmd into the waiting list.
	**
	**----------------------------------------------------
	*/
	save_flags(flags); cli();

        if (!(cp=ncr_get_ccb (np, cmd->target, cmd->lun))) {
		insert_into_waiting_list(np, cmd);
		restore_flags(flags);
		return(DID_OK);
	}
	cp->cmd = cmd;

	/*---------------------------------------------------
	**
	**	Enable tagged queue if asked by user
	**
	**----------------------------------------------------
	*/
#ifdef SCSI_NCR_TAGGED_QUEUE_DISABLED
 	if (cmd->device && cmd->device->tagged_queue &&
	    (lp = tp->lp[cmd->lun]) && (!lp->usetags)) {
		ncr_setmaxtags (np, tp, SCSI_NCR_MAX_TAGS);
	}
#endif

	/*---------------------------------------------------
	**
	**	timestamp
	**
	**----------------------------------------------------
	*/

	bzero (&cp->phys.header.stamp, sizeof (struct tstamp));
	cp->phys.header.stamp.start = jiffies;

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

	if (cmd->lun == 0 && (tp->inqdata[2] & 0x7) >= 2 && tp->inqdata[7]) {
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
			if (SCSI_NCR_MAX_SYNC 
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

	if ((cp!=&np->ccb) && (np->disc))
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
		ncr_free_ccb(np, cp);
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
		xfer_direction = XferIn;
		break;
	case 0x0A:  /*	WRITE(6)			0A */
	case 0x2A:  /*	WRITE(10)			2A */
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

	switch (xfer_direction) {
	default:
	case XferIn:
	     cp->phys.header.savep = NCB_SCRIPT_PHYS (np, data_in);
	     cp->phys.header.goalp = cp->phys.header.savep +20 +segments*16;
	     break;
	case XferOut:
	     cp->phys.header.savep = NCB_SCRIPT_PHYS (np, data_out);
	     cp->phys.header.goalp = cp->phys.header.savep +20 +segments*16;
	     break;
	case XferNone:
	     cp->phys.header.savep = NCB_SCRIPT_PHYS (np, no_data);
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
	cp->phys.header.launch.l_paddr	= NCB_SCRIPT_PHYS (np, select);
	cp->phys.header.launch.l_cmd	= SCR_JUMP;
	/*
	**	select
	*/
	cp->phys.select.sel_id		= cmd->target;
	cp->phys.select.sel_scntl3	= tp->wval;
	cp->phys.select.sel_sxfer	= tp->sval;
	/*
	**	message
	*/
	cp->phys.smsg.addr		= CCB_PHYS (cp, scsi_smsg);
	cp->phys.smsg.size		= msglen;

	cp->phys.smsg2.addr		= CCB_PHYS (cp, scsi_smsg2);
	cp->phys.smsg2.size		= msglen2;
	/*
	**	command
	*/
	cp->phys.cmd.addr		= vtophys (&cmd->cmnd[0]);
	cp->phys.cmd.size		= cmd->cmd_len;
	/*
	**	sense command
	*/
	cp->phys.scmd.addr		= CCB_PHYS (cp, sensecmd);
	cp->phys.scmd.size		= 6;
	/*
	**	patch requested size into sense command
	*/
	cp->sensecmd[0]			= 0x03;
	cp->sensecmd[1]			= cmd->lun << 5;
	cp->sensecmd[4]			= sizeof(cmd->sense_buffer);
	/*
	**	sense data
	*/
	cp->phys.sense.addr		= vtophys (&cmd->sense_buffer[0]);
	cp->phys.sense.size		= sizeof(cmd->sense_buffer);
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

	cp->jump_ccb.l_cmd	= (SCR_JUMP ^ IFFALSE (DATA (cp->tag)));
	/* Compute a time limit bigger than the middle-level driver one */
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
	np->squeue [qidx	 ] = NCB_SCRIPT_PHYS (np, idle);
	np->squeue [np->squeueput] = CCB_PHYS (cp, phys);
	np->squeueput = qidx;

	if(DEBUG_FLAGS & DEBUG_QUEUE)
		printf ("%s: queuepos=%d tryoffset=%d.\n", ncr_name (np),
		np->squeueput,
		(unsigned)(np->script->startpos[0]- 
			   (NCB_SCRIPT_PHYS (np, tryloop))));

	/*
	**	Script processor may be waiting for reselect.
	**	Wake it up.
	*/
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
**	Reset the SCSI BUS.
**	This is called from the generic SCSI driver.
**
**
**==========================================================
*/
int ncr_reset_bus (Scsi_Cmnd *cmd)
{
        struct Scsi_Host   *host      = cmd->host;
/*	Scsi_Device        *device    = cmd->device; */
	struct host_data   *host_data = (struct host_data *) host->hostdata;
	ncb_p np                      = &host_data->ncb_data;
	u_long flags;

	save_flags(flags); cli();

	reset_waiting_list(np);
	ncr_init(np, "scsi bus reset", HS_RESET);

#ifndef SCSI_NCR_NO_DISCONNECT
	np->disc = 1;
#endif

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
int ncr_abort_command (Scsi_Cmnd *cmd)
{
        struct Scsi_Host   *host      = cmd->host;
/*	Scsi_Device        *device    = cmd->device; */
	struct host_data   *host_data = (struct host_data *) host->hostdata;
	ncb_p np                      = &host_data->ncb_data;
	ccb_p cp;
	u_long flags;
	int found;
	int retv;

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
	for (found=0, cp=&np->ccb; cp; cp=cp->link_ccb) {
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

	/*
	**	Disable reselect.
	**      Remove it from startqueue.
	*/
	cp->jump_ccb.l_cmd = (SCR_JUMP);
	if (cp->phys.header.launch.l_paddr == NCB_SCRIPT_PHYS (np, select)) {
		printf ("%s: abort ccb=%p (skip)\n", ncr_name (np), cp);
		cp->phys.header.launch.l_paddr = NCB_SCRIPT_PHYS (np, skip);
	}

	switch (cp->host_status) {
	case HS_BUSY:
	case HS_NEGOTIATE:
		/*
		** still in start queue ?
		*/
		if (cp->phys.header.launch.l_paddr == NCB_SCRIPT_PHYS (np, skip)) {
			retv = SCSI_ABORT_BUSY;
			break;
		}
	/* fall through */
	case HS_DISCONNECT:
		cp->host_status=HS_ABORTED;
		cp->tag = 0;
		/*
		**	wakeup this ccb.
		*/
		ncr_complete (np, cp);
		retv = SCSI_ABORT_SUCCESS;
		break;
	default:
		cp->tag = 0;
		/*
		**	wakeup this ccb.
		*/
		ncr_complete (np, cp);
		retv = SCSI_ABORT_SUCCESS;
		break;
	}

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
static int ncr_detach(ncb_p np, int irq)
{
	ccb_p cp;
	tcb_p tp;
	lcb_p lp;
	int target, lun;
	int i;
	u_char scntl3;

	printf("%s: releasing host resources\n", ncr_name(np));

/*
**	Stop the ncr_timeout process
**	Set release_stage to 1 and wait that ncr_timeout() set it to 2.
*/

#ifdef DEBUG
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

#ifdef DEBUG
	printf("%s: disabling chip interrupts\n", ncr_name(np));
#endif
	OUTW (nc_sien , 0);
	OUTB (nc_dien , 0);

/*
**	Free irq
*/

#ifdef DEBUG
	printf("%s: freeing irq %d\n", ncr_name(np), irq);
#endif
#if LINUX_VERSION_CODE >= LinuxVersionCode(1,3,70)
#   ifdef SCSI_NCR_SHARE_IRQ
	free_irq(irq, np);
#   else
	free_irq(irq, NULL);
#   endif
#else
	free_irq(irq);
#endif

	/*
	**	Reset NCR chip
	**	Preserve scntl3 for automatic clock detection.
	*/

	printf("%s: resetting chip\n", ncr_name(np));
	scntl3 = INB (nc_scntl3);
	OUTB (nc_istat,  SRST);
	DELAY (1000);
	OUTB (nc_istat,  0   );
	OUTB (nc_scntl3, scntl3);

	/*
	**	Release Memory mapped IO region and IO mapped region
	*/

#ifndef NCR_IOMAPPED
#ifdef DEBUG
	printf("%s: releasing memory mapped IO region %lx[%d]\n", ncr_name(np), (u_long) np->reg_remapped, 128);
#endif
	unmap_pci_mem((vm_offset_t) np->reg_remapped, (u_long) 128);
#endif

#ifdef DEBUG
	printf("%s: releasing IO region %x[%d]\n", ncr_name(np), np->port, 128);
#endif
	release_region(np->port, 128);

	/*
	**	Free allocated ccb(s)
	*/

	while ((cp=np->ccb.link_ccb) != NULL) {
		np->ccb.link_ccb = cp->link_ccb;
		if (cp->host_status) {
		printf("%s: shall free an active ccb (host_status=%d)\n",
			ncr_name(np), cp->host_status);
		}
#ifdef DEBUG
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
#ifdef DEBUG
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
	cp->jump_ccb.l_cmd = (SCR_JUMP);

	/*
	**	No starting.
	*/
	cp->phys.header.launch.l_paddr= NCB_SCRIPT_PHYS (np, idle);

	/*
	**	timestamp
	**	Optional, spare some CPU time
	*/
#ifdef SCSI_NCR_PROFILE
	ncb_profile (np, cp);
#endif

	if (DEBUG_FLAGS & DEBUG_TINY)
		printf ("CCB=%lx STAT=%x/%x\n", (unsigned long)cp & 0xfff,
			cp->host_status,cp->scsi_status);

	cmd = cp->cmd;
	cp->cmd = NULL;
	tp = &np->target[cmd->target];

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
		&& (cp->scsi_status == S_GOOD)) {

		/*
		**	All went well.
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
#ifdef SCSI_NCR_FORCE_SYNC_NEGO
				((char *) cmd->request_buffer)[7] |= INQ7_SYNC;
#endif
				((char *) cmd->request_buffer)[7] &=
				(target_capabilities[np->unit].and_map[cmd->target]);
			}
			bcopy (	cmd->request_buffer,
				&tp->inqdata,
				sizeof (tp->inqdata));

			/*
			**	set number of tags
			*/
			lp = tp->lp[cmd->lun];
#ifndef SCSI_NCR_TAGGED_QUEUE_DISABLED
			if (lp && !lp->usetags) {
				ncr_setmaxtags (np, tp, SCSI_NCR_MAX_TAGS);
			}
#endif
			/*
			**	prepare negotiation of synch and wide.
			*/
			ncr_negotiate (np, tp);

			/*
			**	force quirks update before next command start
			*/
			tp->quirks |= QUIRK_UPDATE;
		}

		tp->bytes     += cp->data_len;
		tp->transfers ++;
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
		&& (cp->scsi_status == S_BUSY)) {

		/*
		**   Target is busy.
		*/
		cmd->result = ScsiResult(DID_OK, cp->scsi_status);

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
	ncr_free_ccb (np, cp);

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

	ccb_p cp = &np->ccb;
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
	u_long	usrsync;
	u_char	usrwide;
#if 0
	u_char	burstlen;
#endif

	/*
	**	Reset chip.
	*/

	OUTB (nc_istat,  SRST);
	DELAY (10000);

	/*
	**	Message.
	*/

	if (msg) printf ("%s: restart (%s).\n", ncr_name (np), msg);

	/*
	**	Clear Start Queue
	*/
	for (i=0;i<MAX_START;i++)
		np -> squeue [i] = NCB_SCRIPT_PHYS (np, idle);

	/*
	**	Start at first entry.
	*/

	np->squeueput = 0;
	np->script->startpos[0] = NCB_SCRIPT_PHYS (np, tryloop);
	np->script->start0  [0] = SCR_INT ^ IFFALSE (0);

	/*
	**	Wakeup all pending jobs.
	*/
	ncr_wakeup (np, code);

	/*
	**	Remove Reset, abort ...
	*/
	OUTB (nc_istat,  0      );

	/*
	**	Init chip.
	*/
/**	NCR53C810			**/
	if (ChipDevice == PCI_DEVICE_ID_NCR_53C810 && ChipVersion == 0) {
		OUTB(nc_dmode, 0x80);	/* Set 8-transfer burst */
	}
	else
/**	NCR53C815			**/
	if (ChipDevice == PCI_DEVICE_ID_NCR_53C815) {
		OUTB(nc_dmode, 0x80);	/* Set 8-transfer burst */
	}
	else
/**	NCR53C825			**/
	if (ChipDevice == PCI_DEVICE_ID_NCR_53C825 && ChipVersion == 0) {
		OUTB(nc_dmode, 0x80);	/* Set 8-transfer burst */
	}
	else
/**	NCR53C810A or NCR53C860		**/
	if ((ChipDevice == PCI_DEVICE_ID_NCR_53C810 && ChipVersion >= 0x10) ||
	    ChipDevice == PCI_DEVICE_ID_NCR_53C860) {
		OUTB(nc_dmode, 0xc0);	/* Set 16-transfer burst */
#if 0
		OUTB(nc_ctest3, 0x01);	/* Set write and invalidate */
		OUTB(nc_dcntl, 0xa1);	/* Cache line size enable, */
					/* pre-fetch enable and 700 comp */
#endif
	}
	else
/**	NCR53C825A or NCR53C875		**/
	if ((ChipDevice == PCI_DEVICE_ID_NCR_53C825 && ChipVersion >= 0x10) ||
	    ChipDevice == PCI_DEVICE_ID_NCR_53C875) {
		OUTB(nc_dmode, 0xc0);	/* Set 16-transfer burst */
#if 0
		OUTB(nc_ctest5, 0x04);	/* Set DMA FIFO to 88 */
		OUTB(nc_ctest5, 0x24);	/* Set DMA FIFO to 536 */
		OUTB(nc_dmode, 0x40);	/* Set 64-transfer burst */
		OUTB(nc_ctest3, 0x01);	/* Set write and invalidate */
		OUTB(nc_dcntl, 0x81);	/* Cache line size enable and 700 comp*/
#endif
	}
/**	OTHERS				**/
	else {
		OUTB(nc_dmode, 0xc0);	/* Set 16-transfer burst */
	}
#if 0
	burstlen = 0xc0;
#endif

#ifdef SCSI_NCR_DISABLE_PARITY_CHECK
	OUTB (nc_scntl0, 0xc0   );      /*  full arb., (no parity)           */
#else
	OUTB (nc_scntl0, 0xca   );      /*  full arb., ena parity, par->ATN  */
#endif

	OUTB (nc_scntl1, 0x00	);	/*  odd parity, and remove CRST!!    */
	OUTB (nc_scntl3, np->rv_scntl3);/*  timing prescaler		     */
	OUTB (nc_scid  , RRE|np->myaddr);/*  host adapter SCSI address       */
	OUTW (nc_respid, 1ul<<np->myaddr);/*  id to respond to		     */
	OUTB (nc_istat , SIGP	);	/*  Signal Process		     */
#if 0
	OUTB (nc_dmode , burstlen);	/*  Burst length = 2 .. 16 transfers */
#endif
	OUTB (nc_dcntl , NOCOM  );      /*  no single step mode, protect SFBR*/

#ifdef SCSI_NCR_DISABLE_MPARITY_CHECK
	OUTB (nc_ctest4, 0x00	);	/*  disable master parity checking   */
#else
	OUTB (nc_ctest4, 0x08	);	/*  enable master parity checking    */
#endif

	OUTB (nc_stest2, EXT    );	/*  Extended Sreq/Sack filtering     */
	OUTB (nc_stest3, TE     );	/*  TolerANT enable		     */
	OUTB (nc_stime0, 0x0d	);	/*  HTH = disable  STO = 0.4 sec.    */
					/*  0.25 sec recommended for scsi 1  */

	/*
	**	Reinitialize usrsync.
	**	Have to renegotiate synch mode.
	*/

	usrsync = 255;

#ifndef SCSI_NCR_FORCE_ASYNCHRONOUS
	if (SCSI_NCR_MAX_SYNC) {
		u_long period;
		period =1000000/SCSI_NCR_MAX_SYNC; /* ns = 10e6 / kHz */
		if (period <= 11 * np->ns_sync) {
			if (period < 4 * np->ns_sync)
				usrsync = np->ns_sync;
			else
				usrsync = period / 4;
		};
	};
#endif

	/*
	**	Reinitialize usrwide.
	**	Have to renegotiate wide mode.
	*/

	usrwide = (SCSI_NCR_MAX_WIDE);
	if (usrwide > np->maxwide) usrwide=np->maxwide;

	/*
	**	Disable disconnects.
	*/

	np->disc = 0;

	/*
	**	Fill in target structure.
	*/

	for (i=0;i<MAX_TARGET;i++) {
		tcb_p tp = &np->target[i];

		tp->sval    = 0;
		tp->wval    = np->rv_scntl3;

		tp->usrsync = usrsync;
		tp->usrwide = usrwide;

		ncr_negotiate (np, tp);
	}

	/*
	**      enable ints
	*/

	OUTW (nc_sien , STO|HTH|MA|SGE|UDC|RST);
	OUTB (nc_dien , MDPE|BF|ABRT|SSI|SIR|IID);

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

	if (minsync < 25) minsync=25;

	/*
	**	if not scsi 2
	**	don't believe FAST!
	*/

	if ((minsync < 50) && (tp->inqdata[2] & 0x0f) < 2)
		minsync=50;

	/*
	**	our limit ..
	*/

	if (minsync < np->ns_sync)
		minsync = np->ns_sync;

	/*
	**	divider limit
	*/

	if (minsync > (np->ns_sync * 11) / 4)
		minsync = 255;

	tp->minsync = minsync;
	tp->maxoffs = (minsync<255 ? 8 : 0);

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
**	Switch sync mode for current job and it's target
**
**==========================================================
*/

static void ncr_setsync (ncb_p np, ccb_p cp, u_char sxfer)
{
	Scsi_Cmnd *cmd;
	tcb_p tp;
	u_char target = INB (nc_ctest0)&7;

	assert (cp);
	if (!cp) return;

	cmd = cp->cmd;
	assert (cmd);
	if (!cmd) return;
	assert (target == (cmd->target & 0xf));

	tp = &np->target[target];
	tp->period= sxfer&0xf ? ((sxfer>>5)+4) * np->ns_sync : 0xffff;

	if (tp->sval == sxfer) return;
	tp->sval = sxfer;

	/*
	**	Bells and whistles   ;-)
	*/
	PRINT_ADDR(cmd);
	if (sxfer & 0x0f) {
		/*
		**  Disable extended Sreq/Sack filtering
		*/
		if (tp->period <= 200) OUTB (nc_stest2, 0);

		printf ("%s%dns (%d Mb/sec) offset %d.\n",
			tp->period<200 ? "FAST SCSI-2 ":"",
			tp->period,
			(((tp->wval & EWS)? 2:1)*1000+tp->period/2)/tp->period,
			sxfer & 0x0f);
	} else  printf ("asynchronous.\n");

	/*
	**	set actual value and sync_status
	*/
	OUTB (nc_sxfer, sxfer);
	np->sync_st = sxfer;

	/*
	**	patch ALL ccbs of this target.
	*/
	for (cp = &np->ccb; cp; cp = cp->link_ccb) {
		if (!cp->cmd) continue;
		if (cp->cmd->target != target) continue;
		cp->sync_status = sxfer;
	};
}

/*==========================================================
**
**	Switch wide mode for current job and it's target
**
**==========================================================
*/

static void ncr_setwide (ncb_p np, ccb_p cp, u_char wide)
{
	Scsi_Cmnd *cmd;
	u_short target = INB (nc_ctest0)&7;
	tcb_p tp;
	u_char	scntl3 = np->rv_scntl3 | (wide ? EWS : 0);

	assert (cp);
	if (!cp) return;

	cmd = cp->cmd;
	assert (cmd);
	if (!cmd) return;
	assert (target == (cmd->target & 0xf));

	tp = &np->target[target];
	tp->widedone  =  wide+1;
	if (tp->wval == scntl3) return;
	tp->wval = scntl3;

	/*
	**	Bells and whistles   ;-)
	*/
	PRINT_ADDR(cmd);
	if (scntl3 & EWS)
		printf ("WIDE SCSI (16 bit) enabled.\n");
	else
		printf ("WIDE SCSI disabled.\n");

	/*
	**	set actual value and sync_status
	*/
	OUTB (nc_scntl3, scntl3);
	np->wide_st = scntl3;

	/*
	**	patch ALL ccbs of this target.
	*/
	for (cp = &np->ccb; cp; cp = cp->link_ccb) {
		if (!cp->cmd) continue;
		if (cp->cmd->target != target) continue;
		cp->wide_status = scntl3;
	};
}

/*==========================================================
**
**	Switch tagged mode for a target.
**
**==========================================================
*/

static void ncr_setmaxtags (ncb_p np, tcb_p tp, u_long usrtags)
{
	int l;
	tp->usrtags = usrtags;
	for (l=0; l<MAX_LUN; l++) {
		lcb_p lp;
		if (!tp) break;
		lp=tp->lp[l];
		if (!lp) continue;
		ncr_settags (tp, lp);
		if (lp->usetags > 0) {
			PRINT_LUN(np, tp - np->target, l);
			printf("using tagged command queueing, up to %d cmds/lun\n", lp->usetags);
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
		&& tp->usrtags) {
		reqtags = tp->usrtags;
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

#ifdef SCSI_NCR_USER_COMMAND

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
			ncr_setmaxtags (np, &np->target[t], np->user.data);
		};
		np->disc = 1;
		break;

	case UC_SETDEBUG:
#ifdef SCSI_NCR_DEBUG
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
	}
	np->user.cmd=0;
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
	long signed   t;
	ccb_p cp;
	u_long flags;

	/*
	**	If release process in progress, let's go
	**	Set the release stage from 1 to 2 to synchronize
	**	with the release process.
	**/

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

	if (np->lasttime + HZ < thistime) {
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

		t = (thistime - np->heartbeat) / HZ;

		if (t<2) np->latetime=0; else np->latetime++;
		if (np->latetime>5) {
			/*
			**      If there are no requests, the script
			**      processor will sleep on SEL_WAIT_RESEL.
			**      But we have to check whether it died.
			**      Let's wake it up.
			*/
			OUTB (nc_istat, SIGP);
		}
		if (np->latetime>10) {
			/*
			**	Although we tried to wake it up,
			**	the script processor didn't respond.
			**
			**	May be a target is hanging,
			**	or another initator lets a tape device
			**	rewind with disconnect disabled :-(
			**
			**	We won't accept that.
			*/
			if (INB (nc_sbcl) & CBSY)
				OUTB (nc_scntl1, CRST);
			DELAY (1000);
			ncr_init (np, "ncr dead ?", HS_TIMEOUT);
#ifndef SCSI_NCR_NO_DISCONNECT
			np->disc = 1;
#endif
			np->heartbeat = thistime;
		}

		/*----------------------------------------------------
		**
		**	should handle ccb timeouts
		**	Let the middle scsi driver manage timeouts.
		**----------------------------------------------------
		*/

		for (cp=&np->ccb; cp; cp=cp->link_ccb) {
			/*
			**	look for timed out ccbs.
			*/
			if (!cp->host_status) continue;
			count++;
			/*
			**	Have to force ordered tag to avoid timeouts
			*/
			if (cp->cmd && cp->tlimit <= 
				thistime + NCR_TIMEOUT_INCREASE + SCSI_NCR_TIMEOUT_ALERT) {
				lcb_p lp;
				lp = np->target[cp->cmd->target].lp[cp->cmd->lun];
				if (lp && !lp->force_ordered_tag) {
					lp->force_ordered_tag = 1;
				}
			}
/*
**	Let the middle scsi driver manage timeouts
*/
#if 0
			if (cp->tlimit > thistime) continue;

			/*
			**	Disable reselect.
			**      Remove it from startqueue.
			*/
			cp->jump_ccb.l_cmd = (SCR_JUMP);
			if (cp->phys.header.launch.l_paddr ==
				NCB_SCRIPT_PHYS (np, select)) {
				printf ("%s: timeout ccb=%p (skip)\n",
					ncr_name (np), cp);
				cp->phys.header.launch.l_paddr
				= NCB_SCRIPT_PHYS (np, skip);
			};

			switch (cp->host_status) {

			case HS_BUSY:
			case HS_NEGOTIATE:
				/*
				** still in start queue ?
				*/
				if (cp->phys.header.launch.l_paddr ==
					NCB_SCRIPT_PHYS (np, skip))
					continue;

				/* fall through */
			case HS_DISCONNECT:
				cp->host_status=HS_TIMEOUT;
			};
			cp->tag = 0;

			/*
			**	wakeup this ccb.
			*/
			ncr_complete (np, cp);
#endif
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
**
**	ncr chip exception handler.
**
**
**==========================================================
*/

void ncr_exception (ncb_p np)
{
	u_char	istat, dstat;
	u_short	sist;
	u_int32	dsp, dsa;
	int	script_ofs;
	int	i;

	/*
	**	interrupt on the fly ?
	*/
	while ((istat = INB (nc_istat)) & INTF) {
		if (DEBUG_FLAGS & DEBUG_TINY) printf ("F");
		OUTB (nc_istat, (istat & SIGP) | INTF);
		np->profile.num_fly++;
		ncr_wakeup (np, 0);
	};

	if (!(istat & (SIP|DIP))) return;

	/*
	**	Steinbach's Guideline for Systems Programming:
	**	Never test for an error condition you don't know how to handle.
	*/

	dstat = INB (nc_dstat);
	sist  = INW (nc_sist) ;
	np->profile.num_int++;

	if (DEBUG_FLAGS & DEBUG_TINY)
		printf ("<%d|%x:%x|%x:%x>",
			(int)INB(nc_scr0),
			dstat,sist,
			(unsigned)INL(nc_dsp),
			(unsigned)INL(nc_dbc));
	if ((dstat==DFE) && (sist==PAR)) return;

/*==========================================================
**
**	First the normal cases.
**
**==========================================================
*/
	/*-------------------------------------------
	**	SCSI reset
	**-------------------------------------------
	*/

	if (sist & RST) {
		ncr_init (np, bootverbose ? "scsi reset" : NULL, HS_RESET);
		return;
	};

	/*-------------------------------------------
	**	selection timeout
	**
	**	IID excluded from dstat mask!
	**	(chip bug)
	**-------------------------------------------
	*/

	if ((sist  & STO) &&
		!(sist  & (GEN|HTH|MA|SGE|UDC|RST|PAR)) &&
		!(dstat & (MDPE|BF|ABRT|SIR))) {
		ncr_int_sto (np);
		return;
	};

	/*-------------------------------------------
	**      Phase mismatch.
	**-------------------------------------------
	*/

	if ((sist  & MA) &&
		!(sist  & (STO|GEN|HTH|SGE|UDC|RST|PAR)) &&
		!(dstat & (MDPE|BF|ABRT|SIR|IID))) {
		ncr_int_ma (np);
		return;
	};

	/*----------------------------------------
	**	move command with length 0
	**----------------------------------------
	*/

	if ((dstat & IID) &&
		!(sist  & (STO|GEN|HTH|MA|SGE|UDC|RST|PAR)) &&
		!(dstat & (MDPE|BF|ABRT|SIR)) &&
		((INL(nc_dbc) & 0xf8000000) == SCR_MOVE_TBL)) {
		/*
		**      Target wants more data than available.
		**	The "no_data" script will do it.
		*/
		OUTL (nc_dsp, NCB_SCRIPT_PHYS (np, no_data));
		return;
	};

	/*-------------------------------------------
	**	Programmed interrupt
	**-------------------------------------------
	*/

	if ((dstat & SIR) &&
		!(sist  & (STO|GEN|HTH|MA|SGE|UDC|RST|PAR)) &&
		!(dstat & (MDPE|BF|ABRT|IID)) &&
		(INB(nc_dsps) <= SIR_MAX)) {
		ncr_int_sir (np);
		return;
	};

	/*========================================
	**	do the register dump
	**========================================
	*/
	if (jiffies - np->regtime > 10*HZ) {
		int i;
		np->regtime = jiffies;
		for (i=0; i<sizeof(np->regdump); i++)
			((char*)&np->regdump)[i] = INB_OFF(i);
		np->regdump.nc_dstat = dstat;
		np->regdump.nc_sist  = sist;
	};

	/*=========================================
	**	log message for real hard errors
	**=========================================

	"ncr0 targ 0?: ERROR (ds:si) (so-si-sd) (sxfer/scntl3) @ (dsp:dbc)."
	"	      reg: r0 r1 r2 r3 r4 r5 r6 ..... rf."

	exception register:
		ds:	dstat
		si:	sist

	SCSI bus lines:
		so:	control lines as driver by NCR.
		si:	control lines as seen by NCR.
		sd:	scsi data lines as seen by NCR.

	wide/fastmode:
		sxfer:	(see the manual)
		scntl3:	(see the manual)

	current script command:
		dsp:	script adress (relative to start of script).
		dbc:	first word of script command.

	First 16 register of the chip:
		r0..rf

	=============================================
	*/

	dsp = (unsigned) INL (nc_dsp);
	dsa = (unsigned) INL (nc_dsa);

	script_ofs = dsp - np->p_script;

	printf ("%s:%d: ERROR (%x:%x) (%x-%x-%x) (%x/%x) @ (%x:%08x).\n",
		ncr_name (np), (unsigned)INB (nc_ctest0)&0x0f, dstat, sist,
		(unsigned)INB (nc_socl), (unsigned)INB (nc_sbcl), (unsigned)INB (nc_sbdl),
		(unsigned)INB (nc_sxfer),(unsigned)INB (nc_scntl3), script_ofs,
		(unsigned) INL (nc_dbc));

	if (((script_ofs & 3) == 0) &&
	    (unsigned)script_ofs < sizeof(struct script)) {
		printf ("\tscript cmd = %08x\n", 
			(int) *(ncrcmd *)((char*)np->script +script_ofs));
	}

        printf ("\treg:\t");
        for (i=0; i<16;i++)
            printf (" %02x", (unsigned)INB_OFF(i));
        printf (".\n");

	/*----------------------------------------
	**	clean up the dma fifo
	**----------------------------------------
	*/

	if ( (INB(nc_sstat0) & (ILF|ORF|OLF)   ) ||
	     (INB(nc_sstat1) & (FF3210)	) ||
	     (INB(nc_sstat2) & (ILF1|ORF1|OLF1)) ||	/* wide .. */
	     !(dstat & DFE)) {
		printf ("%s: have to clear fifos.\n", ncr_name (np));
		OUTB (nc_stest3, TE|CSF);	/* clear scsi fifo */
		OUTB (nc_ctest3, CLF);		/* clear dma fifo  */
	}

	/*----------------------------------------
	**	handshake timeout
	**----------------------------------------
	*/

	if (sist & HTH) {
		printf ("%s: handshake timeout\n", ncr_name(np));
		OUTB (nc_scntl1, CRST);
		DELAY (1000);
		OUTB (nc_scntl1, 0x00);
		OUTB (nc_scr0, HS_FAIL);
		OUTL (nc_dsp, NCB_SCRIPT_PHYS (np, cleanup));
		return;
	}

	/*----------------------------------------
	**	unexpected disconnect
	**----------------------------------------
	*/

	if ((sist  & UDC) &&
		!(sist  & (STO|GEN|HTH|MA|SGE|RST|PAR)) &&
		!(dstat & (MDPE|BF|ABRT|SIR|IID))) {
		OUTB (nc_scr0, HS_UNEXPECTED);
		OUTL (nc_dsp, NCB_SCRIPT_PHYS (np, cleanup));
		return;
	};

	/*----------------------------------------
	**	cannot disconnect
	**----------------------------------------
	*/

	if ((dstat & IID) &&
		!(sist  & (STO|GEN|HTH|MA|SGE|UDC|RST|PAR)) &&
		!(dstat & (MDPE|BF|ABRT|SIR)) &&
		((INL(nc_dbc) & 0xf8000000) == SCR_WAIT_DISC)) {
		/*
		**      Unexpected data cycle while waiting for disconnect.
		*/
		if (INB(nc_sstat2) & LDSC) {
			/*
			**	It's an early reconnect.
			**	Let's continue ...
			*/
			OUTB (nc_dcntl, (STD|NOCOM));
			/*
			**	info message
			*/
			printf ("%s: INFO: LDSC while IID.\n",
				ncr_name (np));
			return;
		};
		printf ("%s: target %d doesn't release the bus.\n",
			ncr_name (np), (int)INB (nc_ctest0)&0x0f);
		/*
		**	return without restarting the NCR.
		**	timeout will do the real work.
		*/
		return;
	};

	/*----------------------------------------
	**	single step
	**----------------------------------------
	*/

	if ((dstat & SSI) &&
		!(sist  & (STO|GEN|HTH|MA|SGE|UDC|RST|PAR)) &&
		!(dstat & (MDPE|BF|ABRT|SIR|IID))) {
		OUTB (nc_dcntl, (STD|NOCOM));
		return;
	};

/*
**	@RECOVER@ HTH, SGE, ABRT.
**
**	We should try to recover from these interrupts.
**	They may occur if there are problems with synch transfers, or 
**	if targets are switched on or off while the driver is running.
*/

	if (sist & SGE) {
		OUTB (nc_ctest3, CLF);		/* clear scsi offsets */
	}

	/*
	**	Freeze controller to be able to read the messages.
	*/

	if (DEBUG_FLAGS & DEBUG_FREEZE) {
		unsigned char val;
		for (i=0; i<0x60; i++) {
			switch (i%16) {

			case 0:
				printf ("%s: reg[%d0]: ",
					ncr_name(np),i/16);
				break;
			case 4:
			case 8:
			case 12:
				printf (" ");
				break;
			};
			val = INB_OFF(i);
			printf (" %x%x", val/16, val%16);
			if (i%16==15) printf (".\n");
		}

		del_timer(&np->timer);

		printf ("%s: halted!\n", ncr_name(np));
		/*
		**	don't restart controller ...
		*/
		OUTB (nc_istat,  SRST);
		return;
	};

#ifdef NCR_FREEZE
	/*
	**	Freeze system to be able to read the messages.
	*/
	printf ("ncr: fatal error: system halted - press reset to reboot ...");
	cli();
	for (;;);
#endif

	/*
	**	sorry, have to kill ALL jobs ...
	*/

	ncr_init (np, "fatal error", HS_FAIL);
#ifndef SCSI_NCR_NO_DISCONNECT
	np->disc = 1;
#endif
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
	cp = &np->ccb;
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
	diff = scratcha - NCB_SCRIPT_PHYS (np, tryloop);

/*	assert ((diff <= MAX_START * 20) && !(diff % 20));*/

	if ((diff <= MAX_START * 20) && !(diff % 20)) {
		np->script->startpos[0] = scratcha;
		OUTL (nc_dsp, NCB_SCRIPT_PHYS (np, start));
		return;
	};
	ncr_init (np, "selection timeout", HS_FAIL);
#ifndef SCSI_NCR_NO_DISCONNECT
	np->disc = 1;
#endif
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
	u_int32	dsa;
	u_int32	dsp;
	u_int32	nxtdsp;
	u_int32	*vdsp;
	u_int32	oadr, olen;
	u_int32	*tblp;
        ncrcmd *newcmd;
	u_char	cmd, sbcl, delta, ss0, ss2;
	ccb_p	cp;

	dsp = INL (nc_dsp);
	dsa = INL (nc_dsa);
	dbc = INL (nc_dbc);
	ss0 = INB (nc_sstat0);
	ss2 = INB (nc_sstat2);
	sbcl= INB (nc_sbcl);

	cmd = dbc >> 24;
	rest= dbc & 0xffffff;
	delta=(INB (nc_dfifo) - rest) & 0x7f;

	/*
	**	The data in the dma fifo has not been transfered to
	**	the target -> add the amount to the rest
	**	and clear the data.
	**	Check the sstat2 register in case of wide transfer.
	*/

	if (! (INB(nc_dstat) & DFE)) rest += delta;
	if (ss0 & OLF) rest++;
	if (ss0 & ORF) rest++;
	if (INB(nc_scntl3) & EWS) {
		if (ss2 & OLF1) rest++;
		if (ss2 & ORF1) rest++;
	};
	OUTB (nc_ctest3, CLF   );	/* clear dma fifo  */
	OUTB (nc_stest3, TE|CSF);	/* clear scsi fifo */

	/*
	**	locate matching cp
	*/
	dsa = INL (nc_dsa);
	cp = &np->ccb;
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
		nxtdsp = vdsp[3];
	} else if (dsp == vtophys (&cp->patch[6])) {
		vdsp = &cp->patch[4];
		nxtdsp = vdsp[3];
	} else {
		vdsp = (u_int32 *) ((char*)np->script - np->p_script + dsp -8);
		nxtdsp = dsp;
	};

	/*
	**	log the information
	*/
	if (DEBUG_FLAGS & (DEBUG_TINY|DEBUG_PHASE)) {
		printf ("P%x%x ",cmd&7, sbcl&7);
		printf ("RL=%d D=%d SS0=%x ",
			(unsigned) rest, (unsigned) delta, ss0);
	};
	if (DEBUG_FLAGS & DEBUG_PHASE) {
		printf ("\nCP=%p CP2=%p DSP=%x NXT=%x VDSP=%p CMD=%x ",
			cp, np->header.cp,
			(unsigned)dsp,
			(unsigned)nxtdsp, vdsp, cmd);
	};

	/*
	**	get old startaddress and old length.
	*/

	oadr = vdsp[1];

	if (cmd & 0x10) {	/* Table indirect */
		tblp = (u_int32 *) ((char*) &cp->phys + oadr);
		olen = tblp[0];
		oadr = tblp[1];
	} else {
		tblp = (u_int32 *) 0;
		olen = vdsp[0] & 0xffffff;
	};

	if (DEBUG_FLAGS & DEBUG_PHASE) {
		printf ("OCMD=%x\nTBLP=%p OLEN=%x OADR=%x\n",
			(unsigned) (vdsp[0] >> 24),
			tblp,
			(unsigned) olen,
			(unsigned) oadr);
	};

	/*
	**	if old phase not dataphase, leave here.
	*/

	if (cmd != (vdsp[0] >> 24)) {
		PRINT_ADDR(cp->cmd);
		printf ("internal error: cmd=%02x != %02x=(vdsp[0] >> 24)\n",
			(unsigned)cmd, (unsigned)vdsp[0] >> 24);
		
		return;
	}
	if (cmd & 0x06) {
		PRINT_ADDR(cp->cmd);
		printf ("phase change %x-%x %d@%08x resid=%d.\n",
			cmd&7, sbcl&7, (unsigned)olen,
			(unsigned)oadr, (unsigned)rest);

		OUTB (nc_dcntl, (STD|NOCOM));
		return;
	};

	/*
	**	choose the correct patch area.
	**	if savep points to one, choose the other.
	*/

	newcmd = cp->patch;
	if (cp->phys.header.savep == vtophys (newcmd)) newcmd+=4;

	/*
	**	fillin the commands
	*/

	newcmd[0] = ((cmd & 0x0f) << 24) | rest;
	newcmd[1] = oadr + olen - rest;
	newcmd[2] = SCR_JUMP;
	newcmd[3] = nxtdsp;

	if (DEBUG_FLAGS & DEBUG_PHASE) {
		PRINT_ADDR(cp->cmd);
		printf ("newcmd[%d] %x %x %x %x.\n",
			(int) (newcmd - cp->patch),
			(unsigned)newcmd[0],
			(unsigned)newcmd[1],
			(unsigned)newcmd[2],
			(unsigned)newcmd[3]);
	}
	/*
	**	fake the return address (to the patch).
	**	and restart script processor at dispatcher.
	*/
	np->profile.num_break++;
	OUTL (nc_temp, vtophys (newcmd));
	OUTL (nc_dsp, NCB_SCRIPT_PHYS (np, dispatch));
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
	u_char chg, ofs, per, fak, wide;
	u_char num = INB (nc_dsps);
	ccb_p	cp=0;
	u_long	dsa;
	u_char	target = INB (nc_ctest0) & 7;
	tcb_p	tp     = &np->target[target];
	int     i;
	if (DEBUG_FLAGS & DEBUG_TINY) printf ("I#%d", num);

	switch (num) {
	case SIR_SENSE_RESTART:
	case SIR_STALL_RESTART:
		break;

	default:
		/*
		**	lookup the ccb
		*/
		dsa = INL (nc_dsa);
		cp = &np->ccb;
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
			OUTL (nc_dsp, NCB_SCRIPT_PHYS (np, getcc));
			return;
		};

		/*
		**	no job, resume normal processing
		*/
		if (DEBUG_FLAGS & DEBUG_RESTART) printf (" -- remove trap\n");
		np->script->start0[0] =  SCR_INT ^ IFFALSE (0);
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
		np->script->start0[0] =  SCR_INT;
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
			ncr_setsync (np, cp, 0xe0);
			break;

		case NS_WIDE:
			ncr_setwide (np, cp, 0);
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

		if (per < np->ns_sync)
			{chg = 1; per = np->ns_sync;}
		if (per < tp->minsync)
			{chg = 1; per = tp->minsync;}
		if (ofs > tp->maxoffs)
			{chg = 1; ofs = tp->maxoffs;}

		/*
		**	Check against controller limits.
		*/
		if (ofs != 0) {
			fak = (4ul * per - 1) / np->ns_sync - 3;
			if (fak>7) {
				chg = 1;
				ofs = 0;
			}
		}
		if (ofs == 0) {
			fak = 7;
			per = 0;
			tp->minsync = 0;
		}

		if (DEBUG_FLAGS & DEBUG_NEGO) {
			PRINT_ADDR(cp->cmd);
			printf ("sync: per=%d ofs=%d fak=%d chg=%d.\n",
				per, ofs, fak, chg);
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
					ncr_setsync (np, cp, 0xe0);
					OUTL (nc_dsp, NCB_SCRIPT_PHYS (np, msg_bad));
				} else {
					/*
					**	Answer is ok.
					*/
					ncr_setsync (np, cp, (fak<<5)|ofs);
					OUTL (nc_dsp, NCB_SCRIPT_PHYS (np, clrack));
				};
				return;

			case NS_WIDE:
				ncr_setwide (np, cp, 0);
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

		ncr_setsync (np, cp, (fak<<5)|ofs);

		np->msgout[0] = M_EXTENDED;
		np->msgout[1] = 3;
		np->msgout[2] = M_X_SYNC_REQ;
		np->msgout[3] = per;
		np->msgout[4] = ofs;

		cp->nego_status = NS_SYNC;

		if (DEBUG_FLAGS & DEBUG_NEGO) {
			PRINT_ADDR(cp->cmd);
			printf ("sync msgout: ");
			(void) ncr_show_msg (np->msgin);
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
					ncr_setwide (np, cp, 0);
					OUTL (nc_dsp, NCB_SCRIPT_PHYS (np, msg_bad));
				} else {
					/*
					**	Answer is ok.
					*/
					ncr_setwide (np, cp, wide);
					OUTL (nc_dsp, NCB_SCRIPT_PHYS (np, clrack));
				};
				return;

			case NS_SYNC:
				ncr_setsync (np, cp, 0xe0);
				break;
			};
		};

		/*
		**	It was a request, set value and
		**      prepare an answer message
		*/

		ncr_setwide (np, cp, wide);

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
			(unsigned)np->lastmsg, np->msgout[0]);
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
		printf ("M_DISCONNECT received, but datapointer not saved:\n"
			"\tdata=%x save=%x goal=%x.\n",
			(unsigned) INL (nc_temp),
			(unsigned) np->header.savep,
			(unsigned) np->header.goalp);
		break;

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

		np->script->start1[0] =  SCR_INT;

		/*
		**	For the moment tagged transfers cannot be disabled.
		*/
#if 0
		/*
		**	Try to disable tagged transfers.
		*/
		ncr_setmaxtags (np, &np->target[target], 0);
#endif

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
		cp = &np->ccb;
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
		np->script->start1[0] =  SCR_INT ^ IFFALSE (0);
		break;
	};

out:
	OUTB (nc_dcntl, (STD|NOCOM));
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
	if (lp) {
		cp = lp->next_ccb;

		/*
		**	Look for free CCB
		*/

		while (cp && cp->magic) cp = cp->next_ccb;
	}

	/*
	**	if nothing available, take the default.
	**	DANGEROUS, because this ccb is not suitable for
	**	reselection.
	**	If lp->actccbs > 0 wait for a suitable ccb to be free.
	*/
	if ((!cp) && lp && lp->actccbs > 0)
		return ((ccb_p) 0);

	if (!cp) cp = &np->ccb;

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

void ncr_free_ccb (ncb_p np, ccb_p cp)
{
	/*
	**    sanity
	*/

	assert (cp != NULL);

	cp -> host_status = HS_IDLE;
	cp -> magic = 0;
#if 0
	if (cp == &np->ccb)
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
		tp->jump_tcb.l_cmd   = (SCR_JUMP^IFFALSE (DATA (0x80 + target)));
		tp->jump_tcb.l_paddr = np->jump_tcb.l_paddr;

		tp->getscr[0] = SCR_COPY (1);
		tp->getscr[1] = vtophys (&tp->sval);
		tp->getscr[2] = np->paddr + offsetof (struct ncr_reg, nc_sxfer);
		tp->getscr[3] = SCR_COPY (1);
		tp->getscr[4] = vtophys (&tp->wval);
		tp->getscr[5] = np->paddr + offsetof (struct ncr_reg, nc_scntl3);

		assert (( (offsetof(struct ncr_reg, nc_sxfer) ^
			offsetof(struct tcb    , sval    )) &3) == 0);
		assert (( (offsetof(struct ncr_reg, nc_scntl3) ^
			offsetof(struct tcb    , wval    )) &3) == 0);

		tp->call_lun.l_cmd   = (SCR_CALL);
		tp->call_lun.l_paddr = NCB_SCRIPT_PHYS (np, resel_lun);

		tp->jump_lcb.l_cmd   = (SCR_JUMP);
		tp->jump_lcb.l_paddr = NCB_SCRIPT_PHYS (np, abort);
		np->jump_tcb.l_paddr = vtophys (&tp->jump_tcb);
	}

	/*
	**	Logic unit control block
	*/
	lp = tp->lp[lun];
	if (!lp) {
		/*
		**	Allocate a lcb
		*/
		lp = (lcb_p) m_alloc (sizeof (struct lcb));
		if (!lp) return;

		if (DEBUG_FLAGS & DEBUG_ALLOC) {
			PRINT_LUN(np, target, lun);
			printf ("new lcb @%p.\n", lp);
		}

		/*
		**	Initialize it
		*/
		bzero (lp, sizeof (*lp));
		lp->jump_lcb.l_cmd   = (SCR_JUMP ^ IFFALSE (DATA (lun)));
		lp->jump_lcb.l_paddr = tp->jump_lcb.l_paddr;

		lp->call_tag.l_cmd   = (SCR_CALL);
		lp->call_tag.l_paddr = NCB_SCRIPT_PHYS (np, resel_tag);

		lp->jump_ccb.l_cmd   = (SCR_JUMP);
		lp->jump_ccb.l_paddr = NCB_SCRIPT_PHYS (np, aborttag);

		lp->actlink = 1;

		/*
		**   Chain into LUN list
		*/
		tp->jump_lcb.l_paddr = vtophys (&lp->jump_lcb);
		tp->lp[lun] = lp;

#ifndef SCSI_NCR_TAGGED_QUEUE_DISABLED
		if (!lp->usetags) {
			ncr_setmaxtags (np, tp, SCSI_NCR_MAX_TAGS);
		}
#endif
	}

	/*
	**	Allocate ccbs up to lp->reqccbs.
	**
	**	This modification will be reworked in a future release.
	*/

loop_alloc_ccb:

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
	cp = (ccb_p) m_alloc (sizeof (struct ccb));
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
	cp->jump_ccb.l_cmd   = SCR_JUMP;
	cp->jump_ccb.l_paddr = lp->jump_ccb.l_paddr;
	lp->jump_ccb.l_paddr = CCB_PHYS (cp, jump_ccb);
	cp->call_tmp.l_cmd   = SCR_CALL;
	cp->call_tmp.l_paddr = NCB_SCRIPT_PHYS (np, resel_tmp);

	/*
	**	Chain into wakeup list
	*/
	cp->link_ccb      = np->ccb.link_ccb;
	np->ccb.link_ccb  = cp;

	/*
	**	Chain into CCB list
	*/
	cp->next_ccb	= lp->next_ccb;
	lp->next_ccb	= cp;

goto loop_alloc_ccb;
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

/*	FreeBSD driver important comments
**	---------------------------------
**	We try to reduce the number of interrupts caused
**	by unexpected phase changes due to disconnects.
**	A typical harddisk may disconnect before ANY block.
**	If we wanted to avoid unexpected phase changes at all
**	we had to use a break point every 512 bytes.
**	Of course the number of scatter/gather blocks is
**	limited.
*/

/*
**	The scatterlist passed by the linux middle-level scsi drivers
**	may contain blocks of any size (Generaly < 1024 bytes blocks,
**	can be 4096 with a 4K fs).
*/

#if defined(SCSI_NCR_SEGMENT_SIZE)
static	int	ncr_scatter(ccb_p cp, Scsi_Cmnd *cmd)
{
	struct scatterlist *scatter;
	struct dsb *phys;
	register u_short segment = 0;
	register u_short o_segment = 0;
	u_short chunk, chunk_min;
	u_long segaddr;
	int segsize;
	int datalen;

	phys = &cp->phys;
	cp->data_len = 0;

	/*
	**	Compute a good value for chunk size
	**	If SCSI_NCR_SEGMENT_SIZE is OK, we will try to use it. 
	*/

	if (!cmd->use_sg)
		cp->data_len	= cmd->request_bufflen;
	else {
		scatter = (struct scatterlist *)cmd->buffer;
		for (segment = 0 ; segment < cmd->use_sg ; segment++)
			cp->data_len += scatter[segment].length;
	}


	if (!cp->data_len) {
		bzero (&phys->data, sizeof (phys->data));
		return 0;
	}

	chunk_min	= cp->data_len / MAX_SCATTER;
	for (chunk = SCSI_NCR_SEGMENT_SIZE ; chunk < chunk_min ; chunk += chunk);

	/*
	**	If the linux scsi command is not a scatterlist,
	**	the computed chunk size is OK.
	*/

	if (!cmd->use_sg) {
		bzero (&phys->data, sizeof (phys->data));
		datalen = cmd->request_bufflen;
		segaddr = vtophys(cmd->request_buffer);
		segsize = chunk;
		o_segment = 0;

if (DEBUG_FLAGS & DEBUG_SCATTER)
	printf("ncr53c8xx: re-scattering physical=0x%x size=%d chunk=%d.\n",
	(unsigned) segaddr, (int) datalen, (int) chunk);

		while (datalen && (o_segment < MAX_SCATTER)) {
			if (segsize > datalen) segsize	= datalen;
			phys->data[o_segment].addr	= segaddr;
			phys->data[o_segment].size	= segsize;

			datalen -= segsize;

if(DEBUG_FLAGS & DEBUG_SCATTER)
	printf ("ncr53c8xx:     seg #%d  addr=%lx  size=%d  (rest=%d).\n",
	o_segment, segaddr, (int) segsize, (int) datalen);

			segaddr	+= segsize;
			o_segment++;
		}

		return datalen ? -1 : o_segment;
	}

	/*
	**	Else, the computed chunk size is not so good
	**	and we have to iterate.
	**	Rescatter the Linux scatterlist into the data block descriptor.
	**	Loop if necessary, beginning with the not so good chunk size and
	**	doubling it if the scatter process fails.
	*/

	scatter = (struct scatterlist *)cmd->buffer;
	for (segment = 0; segment < cmd->use_sg; chunk += chunk) {
		o_segment	= 0;
		bzero (&phys->data, sizeof (phys->data));
		for (segment = 0 ; segment < cmd->use_sg ; segment++) {
			datalen = scatter[segment].length;
			segaddr = vtophys(scatter[segment].address);
			segsize = chunk;

if (DEBUG_FLAGS & DEBUG_SCATTER)
	printf("ncr53c8xx: re-scattering physical=0x%x size=%d chunk=%d.\n",
	(unsigned) segaddr, (int) datalen, (int) chunk);

			while (datalen && (o_segment < MAX_SCATTER)) {
				if (segsize > datalen) segsize	= datalen;
				phys->data[o_segment].addr	= segaddr;
				phys->data[o_segment].size	= segsize;

				datalen -= segsize;

if(DEBUG_FLAGS & DEBUG_SCATTER)
	printf ("ncr53c8xx:     seg #%d  addr=%lx  size=%d  (rest=%d).\n",
	o_segment, segaddr, (int) segsize, (int) datalen);

				segaddr	+= segsize;
				o_segment++;
			}

			if (datalen) break;
		}
	}

	return segment < cmd->use_sg ? -1 : o_segment;
}

#else /* !defined SCSI_NCR_SEGMENT_SIZE */

static	int	ncr_scatter(ccb_p cp, Scsi_Cmnd *cmd)
{
	struct dsb *phys = &cp->phys;
	u_short	segment  = 0;

	cp->data_len = 0;
	bzero (&phys->data, sizeof (phys->data));

	if (!cmd->use_sg) {
	     phys->data[segment].addr = vtophys(cmd->request_buffer);
	     phys->data[segment].size = cmd->request_bufflen;
	     cp->data_len            += phys->data[segment].size;
	     segment++;
	     return segment;
	}

	while (segment < cmd->use_sg && segment < MAX_SCATTER) {
	     struct scatterlist *scatter = (struct scatterlist *)cmd->buffer;

	     phys->data[segment].addr = vtophys(scatter[segment].address);
	     phys->data[segment].size = scatter[segment].length;
	     cp->data_len            += phys->data[segment].size;
	     ++segment;
	}

	return segment < cmd->use_sg ? -1 : segment;
}
#endif /* SCSI_NCR_SEGMENT_SIZE */

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
static int ncr_regtest (struct ncb* np)
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

static int ncr_snooptest (struct ncb* np)
{
	u_long	ncr_rd, ncr_wr, ncr_bk, host_rd, host_wr, pc, err=0;
	int	i;
#ifndef NCR_IOMAPPED
	if (np->use_mmio) {
            err |= ncr_regtest (np);
            if (err) return (err);
	}
#endif
	/*
	**	init
	*/
	pc  = NCB_SCRIPT_PHYS (np, snooptest);
	host_wr = 1;
	ncr_wr  = 2;
	/*
	**	Set memory and register.
	*/
	np->ncr_cache = host_wr;
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
	host_rd = np->ncr_cache;
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
	if (pc != NCB_SCRIPT_PHYS (np, snoopend)+8) {
		printf ("CACHE TEST FAILED: script execution failed.\n");
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

/*
**	Compute the difference in milliseconds.
**/

#ifdef SCSI_NCR_PROFILE

static	int ncr_delta (u_long from, u_long to)
{
	if (!from) return (-1);
	if (!to) return (-2);
	return ((to  - from) * 1000 / HZ );
}

#define PROFILE  cp->phys.header.stamp
static	void ncb_profile (ncb_p np, ccb_p cp)
{
	int co, da, st, en, di, se, post,work,disc;
	u_long diff;

	PROFILE.end = jiffies;

	st = ncr_delta (PROFILE.start,PROFILE.status);
	if (st<0) return;	/* status  not reached  */

	da = ncr_delta (PROFILE.start,PROFILE.data);
	if (da<0) return;	/* No data transfer phase */

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

	diff = (np->disc_phys - np->disc_ref) & 0xff;
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

#endif /* SCSI_NCR_PROFILE */

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
**	This is important for the negotiation
**	of the synchronous transfer rate.
**
**==========================================================
**
**	Note: we have to return the correct value.
**	THERE IS NO SAVE DEFAULT VALUE.
**
**	We assume that all NCR based boards are delivered
**	with a 40Mhz clock. Because we have to divide
**	by an integer value greater than 3, only clock
**	frequencies of 40Mhz (/4) or 50MHz (/5) permit
**	the FAST-SCSI rate of 10MHz.
**
**----------------------------------------------------------
*/

#ifndef NCR_CLOCK
#	define NCR_CLOCK 40
#endif /* NCR_CLOCK */


static void ncr_getclock (ncb_p np, u_char scntl3)
{
#if 0
	u_char	tbl[5] = {6,2,3,4,6};
	u_char	f;
	u_char	ns_clock = (1000/NCR_CLOCK);

	/*
	**	Compute the best value for scntl3.
	*/

	f = (2 * MIN_SYNC_PD - 1) / ns_clock;
	if (!f ) f=1;
	if (f>4) f=4;
	np -> ns_sync = (ns_clock * tbl[f]) / 2;
	np -> rv_scntl3 = f<<4;

	f = (2 * MIN_ASYNC_PD - 1) / ns_clock;
	if (!f ) f=1;
	if (f>4) f=4;
	np -> ns_async = (ns_clock * tbl[f]) / 2;
	np -> rv_scntl3 |= f;
	if (DEBUG_FLAGS & DEBUG_TIMING)
		printf ("%s: sclk=%d async=%d sync=%d (ns) scntl3=0x%x\n",
		ncr_name (np), ns_clock, np->ns_async, np->ns_sync, np->rv_scntl3);
#else
	/*
	 *	For now just preserve the BIOS setting ...
	 */

	if ((scntl3 & 7) < 3) {
		printf ("%s: assuming 40MHz clock\n", ncr_name(np));
		scntl3 = 3; /* assume 40MHz if no value supplied by BIOS */
	}

	np->ns_sync   = 25;
	np->ns_async  = 50;
	np->rv_scntl3 = ((scntl3 & 0x7) << 4) -0x20 + (scntl3 & 0x7);

	if (bootverbose) {
		printf ("%s: initial value of SCNTL3 = %02x, final = %02x\n",
			ncr_name(np), scntl3, np->rv_scntl3);
	}
#endif
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

static int ncr53c8xx_pci_init(Scsi_Host_Template *tpnt, int unit, int board, int chip,
	     uchar bus, uchar device_fn, int options);

/*
**   NCR53C8XX devices description table
*/

static struct {
     ushort pci_device_id;
     int chip;
     int max_revision;
     int min_revision;
} pci_chip_ids[] = { 
     {PCI_DEVICE_ID_NCR_53C810,   810, -1, -1}, 
/*   {PCI_DEVICE_ID_NCR_53C810AP, 810, -1, -1}, */
     {PCI_DEVICE_ID_NCR_53C815,   815, -1, -1},
     {PCI_DEVICE_ID_NCR_53C820,   820, -1, -1},
     {PCI_DEVICE_ID_NCR_53C825,   825, -1, -1},
     {PCI_DEVICE_ID_NCR_53C860,   860, -1, -1},
     {PCI_DEVICE_ID_NCR_53C875,   875, -1, -1}
};

#define NPCI_CHIP_IDS (sizeof (pci_chip_ids) / sizeof(pci_chip_ids[0]))

/*
**   Linux entry point for NCR53C8XX devices detection routine.
**
**   Called by the middle-level scsi drivers at initialization time,
**   or at module installation.
**
**   Read the PCI configuration and try to attach each
**   detected NCR board.
**
**   Returns the number of boards successfully attached.
*/

int ncr53c8xx_detect(Scsi_Host_Template *tpnt)
{
     int i;
     int count = 0;			/* Number of boards detected */
     uchar pci_bus, pci_device_fn;
     short pci_index;	/* Device index to PCI BIOS calls */

#if LINUX_VERSION_CODE >= LinuxVersionCode(1,3,0)
     tpnt->proc_dir = &proc_scsi_ncr53c8xx;
# ifdef SCSI_NCR_PROC_INFO_SUPPORT
     tpnt->proc_info = ncr53c8xx_proc_info;
# endif
#endif

     if (pcibios_present()) {
	  for (i = 0; i < NPCI_CHIP_IDS; ++i) 
	       for (pci_index = 0;
		    !pcibios_find_device(PCI_VENDOR_ID_NCR, 
					 pci_chip_ids[i].pci_device_id, pci_index, &pci_bus, 
					 &pci_device_fn);
                    ++pci_index)
		    if (!ncr53c8xx_pci_init(tpnt, count, 0, pci_chip_ids[i].chip, 
			      pci_bus, pci_device_fn, /* no options */ 0))
		    ++count;
     }

     return count;
}


/*
**   Read the PCI configuration of a found NCR board and
**   try yo attach it.
*/

static int ncr53c8xx_pci_init(Scsi_Host_Template *tpnt, int unit, int board, int chip,
		    uchar bus, uchar device_fn, int options)
{
     ushort vendor_id, device_id, command;
#if LINUX_VERSION_CODE >= LinuxVersionCode(1,3,0)
     uint base, io_port; 
#else
     ulong base, io_port; 
#endif
     uchar irq, revision;
     int error, expected_chip;
     int expected_id = -1, max_revision = -1, min_revision = -1;
     int i;

     printk("ncr53c8xx : at PCI bus %d, device %d, function %d\n",
	    bus, (int) (device_fn & 0xf8) >> 3, (int) device_fn & 7);

     if (!pcibios_present()) {
	  printk("ncr53c8xx : not initializing due to lack of PCI BIOS,\n");
	  return -1;
     }

     if ((error = pcibios_read_config_word( bus, device_fn, PCI_VENDOR_ID,      &vendor_id)) ||
	 (error = pcibios_read_config_word( bus, device_fn, PCI_DEVICE_ID,      &device_id)) ||
	 (error = pcibios_read_config_word( bus, device_fn, PCI_COMMAND,        &command))   ||
	 (error = pcibios_read_config_dword(bus, device_fn, PCI_BASE_ADDRESS_0, &io_port))   || 
	 (error = pcibios_read_config_dword(bus, device_fn, PCI_BASE_ADDRESS_1, &base))      ||
	 (error = pcibios_read_config_byte (bus, device_fn, PCI_CLASS_REVISION, &revision))  ||
	 (error = pcibios_read_config_byte (bus, device_fn, PCI_INTERRUPT_LINE, &irq))) {
	  printk("ncr53c8xx : error %s not initializing due to error reading configuration space\n",
		 pcibios_strerror(error));
	  return -1;
     }

     if (vendor_id != PCI_VENDOR_ID_NCR) {
	  printk("ncr53c8xx : not initializing, 0x%04x is not NCR vendor ID\n", (int) vendor_id);
	  return -1;
     }


     if (command & PCI_COMMAND_IO) { 
	  if ((io_port & 3) != 1) {
	       printk("ncr53c8xx : disabling I/O mapping since base address 0 (0x%x)\n"
		      "            bits 0..1 indicate a non-IO mapping\n", (int) io_port);
	       io_port = 0;
	  }
	  else
	       io_port &= PCI_BASE_ADDRESS_IO_MASK;
     }
     else
	  io_port = 0;

     if (command & PCI_COMMAND_MEMORY) {
	  if ((base & PCI_BASE_ADDRESS_SPACE) != PCI_BASE_ADDRESS_SPACE_MEMORY) {
	       printk("ncr53c8xx : disabling memory mapping since base address 1\n"
		      "            contains a non-memory mapping\n");
	       base = 0;
	  }
	  else 
	       base &= PCI_BASE_ADDRESS_MEM_MASK;
     }
     else
	  base = 0;
	
     if (!io_port && !base) {
	  printk("ncr53c8xx : not initializing, both I/O and memory mappings disabled\n");
	  return -1;
     }
	
     if (!(command & PCI_COMMAND_MASTER)) {
	  printk ("ncr53c8xx : not initializing, BUS MASTERING was disabled\n");
	  return -1;
     }

     for (i = 0; i < NPCI_CHIP_IDS; ++i) {
	  if (device_id == pci_chip_ids[i].pci_device_id) {
	       max_revision  = pci_chip_ids[i].max_revision;
	       min_revision  = pci_chip_ids[i].min_revision;
	       expected_chip = pci_chip_ids[i].chip;
	  }
	  if (chip == pci_chip_ids[i].chip)
	       expected_id = pci_chip_ids[i].pci_device_id;
     }

     if (chip && device_id != expected_id) 
	  printk("ncr53c8xx : warning : device id of 0x%04x doesn't\n"
		 "            match expected 0x%04x\n",
		  (unsigned int) device_id, (unsigned int) expected_id );
    
     if (max_revision != -1 && revision > max_revision) 
	  printk("ncr53c8xx : warning : revision %d is greater than expected.\n",
		 (int) revision);
     else if (min_revision != -1 && revision < min_revision)
	  printk("ncr53c8xx : warning : revision %d is lower than expected.\n",
		 (int) revision);

     if (io_port && check_region (io_port, 128)) {
	  printk("ncr53c8xx : IO region 0x%x to 0x%x is in use\n",
		 (int) io_port, (int) (io_port + 127));
	  return -1;
     }

     return ncr_attach (tpnt, unit, device_id, revision, chip, base, io_port, 
		       (int) irq, bus, (uchar) device_fn);
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
			if (device->tagged_supported) {
				device->queue_depth = SCSI_NCR_MAX_TAGS;
			}
			else {
				device->queue_depth = 1;
			}
#ifdef DEBUG
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
#ifdef DEBUG
printk("ncr53c8xx_queue_command\n");
#endif

     if ((sts = ncr_queue_command(cmd, done)) != DID_OK) {
	  cmd->result = ScsiResult(sts, 0);
	  done(cmd);
#ifdef DEBUG
printk("ncr53c8xx : command not queued - result=%d\n", sts);
#endif
          return sts;
     }
#ifdef DEBUG
printk("ncr53c8xx : command successfully queued\n");
#endif
     return sts;
}

/*
**   Linux entry point of the interrupt handler
*/

#if LINUX_VERSION_CODE >= LinuxVersionCode(1,3,70)
static void ncr53c8xx_intr(int irq, void *dev_id, struct pt_regs * regs)
#else
static void ncr53c8xx_intr(int irq, struct pt_regs * regs)
#endif
{
     struct Scsi_Host *host;
     struct host_data *host_data;

#ifdef DEBUG
printk("ncr53c8xx : interrupt received\n");
#endif

     for (host = first_host; host; host = host->next) {
	  if (host->hostt == the_template && host->irq == irq) {
	       host_data = (struct host_data *) host->hostdata;
#if LINUX_VERSION_CODE >= LinuxVersionCode(1,3,70)
#   ifdef SCSI_NCR_SHARE_IRQ
               if (dev_id == &host_data->ncb_data)
#   endif
#endif
	       ncr_intr(&host_data->ncb_data);
	  }
     }
}

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

#if	LINUX_VERSION_CODE >= LinuxVersionCode(1,3,98)
int ncr53c8xx_reset(Scsi_Cmnd *cmd, unsigned int reset_flags)
#else
int ncr53c8xx_reset(Scsi_Cmnd *cmd)
#endif
{
#ifdef DEBUG
printk("ncr53c8xx_reset : reset call\n");
#endif
	return ncr_reset_bus(cmd);
}

/*
**   Linux entry point of abort() function
*/

int ncr53c8xx_abort(Scsi_Cmnd *cmd)
{
printk("ncr53c8xx_abort : abort call\n");
	return ncr_abort_command(cmd);
}

#ifdef MODULE
int ncr53c8xx_release(struct Scsi_Host *host)
{
     struct host_data *host_data;
#ifdef DEBUG
printk("ncr53c8xx : release\n");
#endif

     for (host = first_host; host; host = host->next) {
	  if (host->hostt == the_template) {
	       host_data = (struct host_data *) host->hostdata;
	       ncr_detach(&host_data->ncb_data, host->irq);
	  }
     }

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

static Scsi_Cmnd *remove_from_waiting_list(ncb_p np, Scsi_Cmnd *cmd)
{
	Scsi_Cmnd *wcmd;

	if (!(wcmd = np->waiting_list)) return 0;
	while (wcmd->next_wcmd) {
		if (cmd == (Scsi_Cmnd *) wcmd->next_wcmd) {
			wcmd->next_wcmd = cmd->next_wcmd;
			cmd->next_wcmd = 0;
#ifdef DEBUG_WAITING_LIST
	printf("%s: cmd %lx removed from waiting list\n", ncr_name(np), (u_long) cmd);
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
**	In order to patch the SCSI script for SAVE/RESTORE DATA POINTER,
**	we need the direction of transfer.
**	Linux middle-level scsi driver does not provide this information.
**	So we have to guess it.
**	My documentation about SCSI-II standard is old. Probably some opcode
**	are missing.
**	If I do'nt know the command code, I assume input transfer direction.
*/

static int guess_xfer_direction(int opcode)
{
	int d;

	switch(opcode) {
	case 0x00:  /*	TEST UNIT READY			00 */
	case 0x08:  /*	READ(6)				08 */
	case 0x12:  /*	INQUIRY				12 */
	case 0x4D:  /*	LOG SENSE			4D */
	case 0x5A:  /*	MODE SENSE(10)			5A */
	case 0x1A:  /*	MODE SENSE(6)			1A */
	case 0x28:  /*	READ(10)			28 */
	case 0xA8:  /*	READ(12)			A8 */
	case 0x3C:  /*	READ BUFFER			3C */
	case 0x1C:  /*	RECEIVE DIAGNOSTIC RESULTS	1C */
	case 0xB7:  /*	READ DEFECT DATA(12)		B7 */
	case 0xB8:  /*	READ ELEMENT STATUS		B8 */
	            /*	GET WINDOW			25 */
	case 0x25:  /*	READ CAPACITY			25 */
	case 0x29:  /*	READ GENERATION			29 */
	case 0x3E:  /*	READ LONG			3E */
	            /*	GET DATA BUFFER STATUS		34 */
	            /*	PRE-FETCH			34 */
	case 0x34:  /*	READ POSITION			34 */
	case 0x03:  /*	REQUEST SENSE			03 */
	case 0x05:  /*	READ BLOCK LIMITS		05 */
	case 0x0F:  /*	READ REVERSE			0F */
	case 0x14:  /*	RECOVER BUFFERED DATA		14 */
	case 0x2D:  /*	READ UPDATED BLOCK		2D */
	case 0x37:  /*	READ DEFECT DATA(10)		37 */
	case 0x42:  /*	READ SUB-CHANNEL		42 */
	case 0x43:  /*	READ TOC			43 */
	case 0x44:  /*	READ HEADER			44 */
	case 0xC7:  /*  ???                  ???        C7 */
		d = XferIn;
		break;
	case 0x39:  /*	COMPARE				39 */
	case 0x3A:  /*	COPY AND VERIFY			3A */
	            /*	PRINT				0A */
	            /*	SEND MESSAGE(6)			0A */
	case 0x0A:  /*	WRITE(6)			0A */
	case 0x18:  /*	COPY				18 */
	case 0x4C:  /*	LOG SELECT			4C */
	case 0x55:  /*	MODE SELECT(10)			55 */
	case 0x3B:  /*	WRITE BUFFER			3B */
	case 0x1D:  /*	SEND DIAGNOSTIC			1D */
	case 0x40:  /*	CHANGE DEFINITION		40 */
	            /*	SEND MESSAGE(12)		AA */
	case 0xAA:  /*	WRITE(12)			AA */
	case 0xB6:  /*	SEND VOLUME TAG			B6 */
	case 0x3F:  /*	WRITE LONG			3F */
	case 0x04:  /*	FORMAT UNIT			04 */
		    /*	INITIALIZE ELEMENT STATUS	07 */
	case 0x07:  /*	REASSIGN BLOCKS			07 */
	case 0x15:  /*	MODE SELECT(6)			15 */
	case 0x24:  /*	SET WINDOW			24 */
	case 0x2A:  /*	WRITE(10)			2A */
	case 0x2E:  /*	WRITE AND VERIFY(10)		2E */
	case 0xAE:  /*	WRITE AND VERIFY(12)		AE */
	case 0xB0:  /*	SEARCH DATA HIGH(12)		B0 */
	case 0xB1:  /*	SEARCH DATA EQUAL(12)		B1 */
	case 0xB2:  /*	SEARCH DATA LOW(12)		B2 */
	            /*	OBJECT POSITION			31 */
	case 0x30:  /*	SEARCH DATA HIGH(10)		30 */
	case 0x31:  /*	SEARCH DATA EQUAL(10)		31 */
	case 0x32:  /*	SEARCH DATA LOW(10)		32 */
	case 0x38:  /*	MEDIUM SCAN			38 */
	case 0x3D:  /*	UPDATE BLOCK			3D */
	case 0x41:  /*	WRITE SAME			41 */
	            /*	LOAD UNLOAD			1B */
	            /*	SCAN				1B */
	case 0x1B:  /*	START STOP UNIT			1B */
		d = XferOut;
		break;
	case 0x01:  /*	REZERO UNIT			01 */
	            /*	SEEK(6)				0B */
	case 0x0B:  /*	SLEW AND PRINT			0B */
	            /*	SYNCHRONIZE BUFFER		10 */
	case 0x10:  /*	WRITE FILEMARKS			10 */
	case 0x11:  /*	SPACE				11 */
	case 0x13:  /*	VERIFY				13 */
	case 0x16:  /*	RESERVE UNIT			16 */
	case 0x17:  /*	RELEASE UNIT			17 */
	case 0x19:  /*	ERASE				19 */
	            /*	LOCATE				2B */
	            /*	POSITION TO ELEMENT		2B */
	case 0x2B:  /*	SEEK(10)			2B */
	case 0x1E:  /*	PREVENT ALLOW MEDIUM REMOVAL	1E */
	case 0x2C:  /*	ERASE(10)			2C */
	case 0xAC:  /*	ERASE(12)			AC */
	case 0x2F:  /*	VERIFY(10)			2F */
	case 0xAF:  /*	VERIFY(12)			AF */
	case 0x33:  /*	SET LIMITS(10)			33 */
	case 0xB3:  /*	SET LIMITS(12)			B3 */
	case 0x35:  /*	SYNCHRONIZE CACHE		35 */
	case 0x36:  /*	LOCK UNLOCK CACHE		36 */
	case 0x45:  /*	PLAY AUDIO(10)			45 */
	case 0x47:  /*	PLAY AUDIO MSF			47 */
	case 0x48:  /*	PLAY AUDIO TRACK/INDEX		48 */
	case 0x49:  /*	PLAY TRACK RELATIVE(10)		49 */
	case 0xA9:  /*	PLAY TRACK RELATIVE(12)		A9 */
	case 0x4B:  /*	PAUSE/RESUME			4B */
	            /*	MOVE MEDIUM			A5 */
	case 0xA5:  /*	PLAY AUDIO(12)			A5 */
	case 0xA6:  /*	EXCHANGE MEDIUM			A6 */
	case 0xB5:  /*	REQUEST VOLUME ELEMENT ADDRESS	B5 */
		d = XferNone;
		break;
	default:
		d = XferIn;
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
		GET_INT_ARG(target);
#ifdef DEBUG_PROC_INFO
printf("ncr_user_command: target=%ld\n", target);
#endif
		if (target > MAX_TARGET)
			return -EINVAL;
		uc->target = (1<<target);
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
			else
				return -EINVAL;
			ptr += arg_len; len -= arg_len;
		}
		break;
	default:
		break;
	}

	/*
	** Not allow to disable tagged queue
	*/ 
	if (uc->cmd == UC_SETTAGS && uc->data < 1)
		return -EINVAL;

	if (len)
		return -EINVAL;
#ifdef SCSI_NCR_USER_COMMAND
	else {
		long flags;

		save_flags(flags); cli();
		ncr_usercmd (np);
		restore_flags(flags);
	}
#endif
	return length;
}

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

static int ncr_host_info(ncb_p np, char *ptr, off_t offset, int len)
{
	struct info_str info;

	info.buffer	= ptr;
	info.length	= len;
	info.offset	= offset;
	info.pos	= 0;

	copy_info(&info, "General information:\n");
	copy_info(&info, "  Chip NCR53C%03d, ",	np->chip);
	copy_info(&info, "device id 0x%x, ",	np->device_id);
	copy_info(&info, "revision id 0x%x\n",	np->revision_id);

	copy_info(&info, "  IO port address 0x%lx, ", (u_long) np->port);
	copy_info(&info, "IRQ number %d\n", (int) np->irq);

#ifndef NCR_IOMAPPED
	if (np->use_mmio)
		copy_info(&info, "  Using memory mapped IO at virtual address 0x%lx\n",
		                  (u_long) np->reg_remapped);
#endif

#ifdef SCSI_NCR_PROFILE
	copy_info(&info, "Profiling information:\n");
	copy_info(&info, "  %-12s = %lu\n", "num_trans",np->profile.num_trans);
	copy_info(&info, "  %-12s = %lu\n", "num_kbytes",np->profile.num_kbytes);
	copy_info(&info, "  %-12s = %lu\n", "num_disc",	np->profile.num_disc);
	copy_info(&info, "  %-12s = %lu\n", "num_break",np->profile.num_break);
	copy_info(&info, "  %-12s = %lu\n", "num_int",	np->profile.num_int);
	copy_info(&info, "  %-12s = %lu\n", "num_fly",	np->profile.num_fly);
	copy_info(&info, "  %-12s = %lu\n", "ms_setup",	np->profile.ms_setup);
	copy_info(&info, "  %-12s = %lu\n", "ms_data",	np->profile.ms_data);
	copy_info(&info, "  %-12s = %lu\n", "ms_disc",	np->profile.ms_disc);
	copy_info(&info, "  %-12s = %lu\n", "ms_post",	np->profile.ms_post);
#endif
	
	return info.pos > info.offset? info.pos - info.offset : 0;
}

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
			ncb = &host_data->ncb_data;
			break;
		}
	}

	if (!ncb)
		return -EINVAL;

	if (func) {
		retv = ncr_user_command(ncb, buffer, length);
#ifdef DEBUG_PROC_INFO
printf("ncr_user_command: retv=%d\n", retv);
#endif
	}
	else {
		if (start)
			*start = buffer;
		retv = ncr_host_info(ncb, buffer, offset, length);
	}

	return retv;
}

/*=========================================================================
**	End of proc file system stuff
**=========================================================================
*/
#endif

/*
**	Module stuff
*/

#ifdef MODULE
Scsi_Host_Template driver_template = NCR53C8XX;
#include "scsi_module.c"
#endif
