/*****************************************************************************/

/*
 *	istallion.c  -- stallion intelligent multiport serial driver.
 *
 *	Copyright (C) 1994,1995  Greg Ungerer (gerg@stallion.oz.au).
 *
 *	This code is loosely based on the Linux serial driver, written by
 *	Linus Torvalds, Theodore T'so and others.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*****************************************************************************/

#ifdef MODULE
#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>
#endif

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/interrupt.h>
#include <linux/termios.h>
#include <linux/fcntl.h>
#include <linux/tty_driver.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/cdk.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <asm/io.h>

/*****************************************************************************/

/*
 *	Define different board types. Not all of the following board types
 *	are supported by this driver. But I will use the standard "assigned"
 *	board numbers. Currently supported boards are abbreviated as:
 *	ECP = EasyConnection 8/64, ONB = ONboard, BBY = Brumby and
 *	STAL = Stallion.
 */
#define	BRD_UNKNOWN	0
#define	BRD_STALLION	1
#define	BRD_BRUMBY4	2
#define	BRD_ONBOARD2	3
#define	BRD_ONBOARD	4
#define	BRD_BRUMBY8	5
#define	BRD_BRUMBY16	6
#define	BRD_ONBOARDE	7
#define	BRD_ONBOARD32	9
#define	BRD_ONBOARD2_32	10
#define	BRD_ONBOARDRS	11
#define	BRD_EASYIO	20
#define	BRD_ECH		21
#define	BRD_ECHMC	22
#define	BRD_ECP		23
#define BRD_ECPE	24
#define	BRD_ECPMC	25
#define	BRD_ECHPCI	26

#define	BRD_BRUMBY	BRD_BRUMBY4

/*
 *	Define a configuration structure to hold the board configuration.
 *	Need to set this up in the code (for now) with the boards that are
 *	to be configured into the system. This is what needs to be modified
 *	when adding/removing/modifying boards. Each line entry in the
 *	stli_brdconf[] array is a board. Each line contains io/irq/memory
 *	ranges for that board (as well as what type of board it is).
 *	Some examples:
 *		{ BRD_ECP, 0x2a0, 0, 0xcc000, 0, 0 },
 *	This line will configure an EasyConnection 8/64 at io address 2a0,
 *	and shared memory address of cc000. Multiple EasyConnection 8/64
 *	boards can share the same shared memory address space. No interrupt
 *	is required for this board type.
 *	Another example:
 *		{ BRD_ONBOARD, 0x240, 0, 0xd0000, 0, 0 },
 *	This line will configure an ONboard (ISA type) at io address 240,
 *	and shared memory address of d0000. Multiple ONboards can share
 *	the same shared memory address space. No interrupt required.
 *	Another example:
 *		{ BRD_BRUMBY4, 0x360, 0, 0xc8000, 0, 0 },
 *	This line will configure a Brumby board (any number of ports!) at
 *	io address 360 and shared memory address of c8000. All Brumby boards
 *	configured into a system must have their own separate io and memory
 *	addresses. No interrupt is required.
 *	Another example:
 *		{ BRD_STALLION, 0x330, 0, 0xd0000, 0, 0 },
 *	This line will configure an original Stallion board at io address 330
 *	and shared memory address d0000 (this would only be valid for a "V4.0"
 *	or Rev.O Stallion board). All Stallion boards configured into the
 *	system must have their own separate io and memory addresses. No
 *	interrupt is required.
 */

typedef struct {
	int		brdtype;
	int		ioaddr1;
	int		ioaddr2;
	unsigned long	memaddr;
	int		irq;
	int		irqtype;
} stlconf_t;

static stlconf_t	stli_brdconf[] = {
 	{ BRD_ECP, 0x2a0, 0, 0xcc000, 0, 0 },
};

static int	stli_nrbrds = sizeof(stli_brdconf) / sizeof(stlconf_t);

/*
 *	Code support is offered for boards to use the above 1Mb memory
 *	ranges for those boards which support this (supported on the ONboard
 *	and ECP-EI hardware). The following switch should be enabled. The only
 *	catch is that the kernel functions required to do this are not
 *	normally exported symbols, so you will have to do some extra work
 *	for this to be used in the loadable module form of the driver.
 *	Unfortunately this doesn't work either if you linke the driver into
 *	the kernel, sincethe memory management code is not set up early
 *	enough (before our initialization routine is run).
 */
#define	STLI_HIMEMORY	0

#if STLI_HIMEMORY
#include <asm/page.h>
#include <asm/pgtable.h>
#endif

/*****************************************************************************/

/*
 *	Define some important driver characteristics. Device major numbers
 *	allocated as per Linux Device Registery.
 */
#ifndef	STL_SIOMEMMAJOR
#define	STL_SIOMEMMAJOR		28
#endif
#ifndef	STL_SERIALMAJOR
#define	STL_SERIALMAJOR		24
#endif
#ifndef	STL_CALLOUTMAJOR
#define	STL_CALLOUTMAJOR	25
#endif

#define	STL_DRVTYPSERIAL	1
#define	STL_DRVTYPCALLOUT	2

#define	STL_MAXBRDS		4
#define	STL_MAXPANELS		4
#define	STL_MAXPORTS		64
#define	STL_MAXCHANS		(STL_MAXPORTS + 1)
#define	STL_MAXDEVS		(STL_MAXBRDS * STL_MAXPORTS)

/*****************************************************************************/

/*
 *	Define our local driver identity first. Set up stuff to deal with
 *	all the local structures required by a serial tty driver.
 */
static char	*stli_drvname = "Stallion Intelligent Multiport Serial Driver";
static char	*stli_drvversion = "1.0.0";
static char	*stli_serialname = "ttyE";
static char	*stli_calloutname = "cue";

static struct tty_driver	stli_serial;
static struct tty_driver	stli_callout;
static struct tty_struct	*stli_ttys[STL_MAXDEVS];
static struct termios		*stli_termios[STL_MAXDEVS];
static struct termios		*stli_termioslocked[STL_MAXDEVS];
static int			stli_refcount;

/*
 *	We will need to allocate a temporary write buffer for chars that
 *	come direct from user space. The problem is that a copy from user
 *	space might cause a page fault (typically on a system that is
 *	swapping!). All ports will share one buffer - since if the system
 *	is already swapping a shared buffer won't make things any worse.
 */
static char			*stli_tmpwritebuf = (char *) NULL;
static struct semaphore		stli_tmpwritesem = MUTEX;

#define	STLI_TXBUFSIZE		4096

/*
 *	Use a fast local buffer for cooked characters. Typically a whole
 *	bunch of cooked characters come in for a port, 1 at a time. So we
 *	save those up into a local buffer, then write out the whole lot
 *	with a large memcpy. Just use 1 buffer for all ports, since its
 *	use it is only need for short periods of time by each port.
 */
static char			*stli_txcookbuf = (char *) NULL;
static int			stli_txcooksize = 0;
static int			stli_txcookrealsize = 0;
static struct tty_struct	*stli_txcooktty = (struct tty_struct *) NULL;

/*
 *	Define a local default termios struct. All ports will be created
 *	with this termios initially. Basically all it defines is a raw port
 *	at 9600 baud, 8 data bits, no parity, 1 stop bit.
 */
static struct termios		stli_deftermios = {
	0,
	0,
	(B9600 | CS8 | CREAD | HUPCL | CLOCAL),
	0,
	0,
	INIT_C_CC
};

/*****************************************************************************/

/*
 *	Define a set of structures to hold all the board/panel/port info
 *	for our ports. These will be dynamically allocated as required at
 *	driver initialization time.
 */

/*
 *	Port and board structures to hold status info about each object.
 *	The board structure contains pointers to structures for each port
 *	connected to it. Panels are not distinguished here, since
 *	communication with the slave board will always be on a per port
 *	basis.
 */
typedef struct {
	int			portnr;
	int			panelnr;
	int			brdnr;
	unsigned long		state;
	int			devnr;
	int			flags;
	int			baud_base;
	int			custom_divisor;
	int			close_delay;
	int			closing_wait;
	int			refcount;
	int			openwaitcnt;
	int			rc;
	int			argsize;
	void			*argp;
	long			session;
	long			pgrp;
	unsigned int		rxmarkmsk;
	struct tty_struct	*tty;
	struct wait_queue	*open_wait;
	struct wait_queue	*close_wait;
	struct wait_queue	*raw_wait;
	struct tq_struct	tqhangup;
	struct termios		normaltermios;
	struct termios		callouttermios;
	asysigs_t		asig;
	unsigned long		addr;
	unsigned long		rxoffset;
	unsigned long		txoffset;
	unsigned int		rxsize;
	unsigned int		txsize;
	unsigned long		sigs;
	unsigned char		reqbit;
	unsigned char		portidx;
	unsigned char		portbit;
} stliport_t;

/*
 *	Use a structure of function pointers to do board level operations.
 *	These include, enable/disable, paging shared memory, interrupting, etc.
 */
typedef struct stlbrd {
	int		brdnr;
	int		brdtype;
	int		state;
	int		nrpanels;
	int		nrports;
	int		nrdevs;
	unsigned int	iobase;
	void		*membase;
	int		memsize;
	int		pagesize;
	int		hostoffset;
	int		slaveoffset;
	int		bitsize;
	int		panels[STL_MAXPANELS];
	void		(*init)(struct stlbrd *brdp);
	void		(*enable)(struct stlbrd *brdp);
	void		(*reenable)(struct stlbrd *brdp);
	void		(*disable)(struct stlbrd *brdp);
	char		*(*getmemptr)(struct stlbrd *brdp, unsigned long offset, int line);
	void		(*intr)(struct stlbrd *brdp);
	void		(*reset)(struct stlbrd *brdp);
	stliport_t	*ports[STL_MAXPORTS];
} stlibrd_t;

static stlibrd_t	*stli_brds;

static int		stli_shared = 0;

/*
 *	Per board state flags. Used with the state field of the board struct.
 *	Not really much here... All we need to do is keep track of whether
 *	the board has been detected, and whether it is actully running a slave
 *	or not.
 */
#define	BST_FOUND	0x1
#define	BST_STARTED	0x2

/*
 *	Define the set of port state flags. These are marked for internal
 *	state purposes only, usually to do with the state of communications
 *	with the slave. Most of them need to be updated atomically, so always
 *	use the bit setting operations (unless protected by cli/sti).
 */
#define	ST_INITIALIZING	1
#define	ST_OPENING	2
#define	ST_CLOSING	3
#define	ST_CMDING	4
#define	ST_TXBUSY	5
#define	ST_RXING	6
#define	ST_DOFLUSHRX	7
#define	ST_DOFLUSHTX	8
#define	ST_DOSIGS	9
#define	ST_RXSTOP	10
#define	ST_GETSIGS	11

/*
 *	Define an array of board names as printable strings. Handy for
 *	referencing boards when printing trace and stuff.
 */
static char	*stli_brdnames[] = {
	"Unknown",
	"Stallion",
	"Brumby",
	"ONboard-MC",
	"ONboard",
	"Brumby",
	"Brumby",
	"ONboard-EI",
	(char *) NULL,
	"ONboard",
	"ONboard-MC",
	"ONboard-MC",
	(char *) NULL,
	(char *) NULL,
	(char *) NULL,
	(char *) NULL,
	(char *) NULL,
	(char *) NULL,
	(char *) NULL,
	(char *) NULL,
	"EasyIO",
	"EC8/32-AT",
	"EC8/32-MC",
	"EC8/64-AT",
	"EC8/64-EI",
	"EC8/64-MC",
	"EC8/32-PCI",
};

/*****************************************************************************/

/*
 *	Hardware configuration info for ECP boards. These defines apply
 *	to the directly accessable io ports of the ECP. There is a set of
 *	defines for each ECP board type, ISA, EISA and MCA.
 */
#define	ECP_IOSIZE	4
#define	ECP_MEMSIZE	(128 * 1024)
#define	ECP_ATPAGESIZE	(4 * 1024)
#define	ECP_EIPAGESIZE	(64 * 1024)
#define	ECP_MCPAGESIZE	(4 * 1024)

/*
 *	Important defines for the ISA class of ECP board.
 */
#define	ECP_ATIREG	0
#define	ECP_ATCONFR	1
#define	ECP_ATMEMAR	2
#define	ECP_ATMEMPR	3
#define	ECP_ATSTOP	0x1
#define	ECP_ATINTENAB	0x10
#define	ECP_ATENABLE	0x20
#define	ECP_ATDISABLE	0x00
#define	ECP_ATADDRMASK	0x3f000
#define	ECP_ATADDRSHFT	12

/*
 *	Important defines for the EISA class of ECP board.
 */
#define	ECP_EIIREG	0
#define	ECP_EIMEMARL	1
#define	ECP_EICONFR	2
#define	ECP_EIMEMARH	3
#define	ECP_EIENABLE	0x1
#define	ECP_EIDISABLE	0x0
#define	ECP_EISTOP	0x4
#define	ECP_EIEDGE	0x00
#define	ECP_EILEVEL	0x80
#define	ECP_EIADDRMASKL	0x00ff0000
#define	ECP_EIADDRSHFTL	16
#define	ECP_EIADDRMASKH	0xff000000
#define	ECP_EIADDRSHFTH	24
#define	ECP_EIBRDENAB	0xc84

/*
 *	Important defines for the Micro-channel class of ECP board.
 *	(It has a lot in common with the ISA boards.)
 */
#define	ECP_MCIREG	0
#define	ECP_MCCONFR	1
#define	ECP_MCSTOP	0x20
#define	ECP_MCENABLE	0x80
#define	ECP_MCDISABLE	0x00

/*
 *	Hardware configuration info for ONboard and Brumby boards. These
 *	defines apply to the directly accessable io ports of these boards.
 */
#define	ONB_IOSIZE	16
#define	ONB_MEMSIZE	(64 * 1024)
#define	ONB_ATPAGESIZE	(64 * 1024)
#define	ONB_MCPAGESIZE	(64 * 1024)
#define	ONB_EIMEMSIZE	(128 * 1024)
#define	ONB_EIPAGESIZE	(64 * 1024)

/*
 *	Important defines for the ISA class of ONboard board.
 */
#define	ONB_ATIREG	0
#define	ONB_ATMEMAR	1
#define	ONB_ATCONFR	2
#define	ONB_ATSTOP	0x4
#define	ONB_ATENABLE	0x01
#define	ONB_ATDISABLE	0x00
#define	ONB_ATADDRMASK	0xff0000
#define	ONB_ATADDRSHFT	16

#if STLI_HIMEMORY
#define	ONB_HIMEMENAB	0x02
#else
#define	ONB_HIMEMENAB	0
#endif

/*
 *	Important defines for the EISA class of ONboard board.
 */
#define	ONB_EIIREG	0
#define	ONB_EIMEMARL	1
#define	ONB_EICONFR	2
#define	ONB_EIMEMARH	3
#define	ONB_EIENABLE	0x1
#define	ONB_EIDISABLE	0x0
#define	ONB_EISTOP	0x4
#define	ONB_EIEDGE	0x00
#define	ONB_EILEVEL	0x80
#define	ONB_EIADDRMASKL	0x00ff0000
#define	ONB_EIADDRSHFTL	16
#define	ONB_EIADDRMASKH	0xff000000
#define	ONB_EIADDRSHFTH	24
#define	ONB_EIBRDENAB	0xc84

/*
 *	Important defines for the Brumby boards. They are pretty simple,
 *	there is not much that is programmably configurable.
 */
#define	BBY_IOSIZE	16
#define	BBY_MEMSIZE	(64 * 1024)
#define	BBY_PAGESIZE	(16 * 1024)

#define	BBY_ATIREG	0
#define	BBY_ATCONFR	1
#define	BBY_ATSTOP	0x4

/*
 *	Important defines for the Stallion boards. They are pretty simple,
 *	there is not much that is programmably configurable.
 */
#define	STAL_IOSIZE	16
#define	STAL_MEMSIZE	(64 * 1024)
#define	STAL_PAGESIZE	(64 * 1024)

/*
 *	Define the set of status register values for EasyConnection panels.
 *	The signature will return with the status value for each panel. From
 *	this we can determine what is attached to the board - before we have
 *	actually down loaded any code to it.
 */
#define	ECH_PNLSTATUS	2
#define	ECH_PNL16PORT	0x20
#define	ECH_PNLIDMASK	0x07
#define	ECH_PNLINTRPEND	0x80

/*
 *	Define some macros to do things to the board. Even those these boards
 *	are somewhat related there is often significantly different ways of
 *	doing some operation on it (like enable, paging, reset, etc). So each
 *	board class has a set of functions which do the commonly required
 *	operations. The macros below basically just call these functions,
 *	generally checking for a NULL function - which means that the board
 *	needs nothing done to it to achieve this operation!
 */
#define	EBRDINIT(brdp)						\
	if (brdp->init != NULL)					\
		(* brdp->init)(brdp)

#define	EBRDENABLE(brdp)					\
	if (brdp->enable != NULL)				\
		(* brdp->enable)(brdp);

#define	EBRDDISABLE(brdp)					\
	if (brdp->disable != NULL)				\
		(* brdp->disable)(brdp);

#define	EBRDINTR(brdp)						\
	if (brdp->intr != NULL)					\
		(* brdp->intr)(brdp);

#define	EBRDRESET(brdp)						\
	if (brdp->reset != NULL)				\
		(* brdp->reset)(brdp);

#define	EBRDGETMEMPTR(brdp,offset)				\
	(* brdp->getmemptr)(brdp, offset, __LINE__)

/*
 *	Define the maximal baud rate, and he default baud base for ports.
 */
#define	STL_MAXBAUD	230400
#define	STL_BAUDBASE	115200
#define	STL_CLOSEDELAY	50

/*****************************************************************************/

/*
 *	Define macros to extract a brd or port number from a minor number.
 */
#define	MKDEV2BRD(min)		(((min) & 0xc0) >> 6)
#define	MKDEV2PORT(min)		((min) & 0x3f)

/*
 *	Define a baud rate table that converts termios baud rate selector
 *	into the actual baud rate value. All baud rate calculations are based
 *	on the actual baud rate required.
 */
static unsigned int	stli_baudrates[] = {
	0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800,
	9600, 19200, 38400, 57600, 115200, 230400
};

/*****************************************************************************/

/*
 *	Define some handy local macros...
 */
#define	MIN(a,b)		(((a) <= (b)) ? (a) : (b))

/*****************************************************************************/

/*
 *	Prototype all functions in this driver!
 */

#ifdef MODULE
int		init_module(void);
void		cleanup_module(void);
#endif
static void	*stli_memalloc(int len);

int		stli_init(void);
static int	stli_open(struct tty_struct *tty, struct file *filp);
static void	stli_close(struct tty_struct *tty, struct file *filp);
static int	stli_write(struct tty_struct *tty, int from_user, const unsigned char *buf, int count);
static void	stli_putchar(struct tty_struct *tty, unsigned char ch);
static void	stli_flushchars(struct tty_struct *tty);
static int	stli_writeroom(struct tty_struct *tty);
static int	stli_charsinbuffer(struct tty_struct *tty);
static int	stli_ioctl(struct tty_struct *tty, struct file *file, unsigned int cmd, unsigned long arg);
static void	stli_settermios(struct tty_struct *tty, struct termios *old);
static void	stli_throttle(struct tty_struct *tty);
static void	stli_unthrottle(struct tty_struct *tty);
static void	stli_stop(struct tty_struct *tty);
static void	stli_start(struct tty_struct *tty);
static void	stli_flushbuffer(struct tty_struct *tty);
static void	stli_hangup(struct tty_struct *tty);

static int	stli_brdinit(void);
static int	stli_initecp(stlibrd_t *brdp, stlconf_t *confp);
static int	stli_initonb(stlibrd_t *brdp, stlconf_t *confp);
static int	stli_initports(stlibrd_t *brdp);
static int	stli_startbrd(stlibrd_t *brdp);
static int	stli_memread(struct inode *ip, struct file *fp, char *buf, int count);
static int	stli_memwrite(struct inode *ip, struct file *fp, const char *buf, int count);
static int	stli_memioctl(struct inode *ip, struct file *fp, unsigned int cmd, unsigned long arg);
static void	stli_poll(unsigned long arg);
static int	stli_hostcmd(stlibrd_t *brdp, int channr);
static int	stli_initopen(stlibrd_t *brdp, stliport_t *portp);
static int	stli_rawopen(stlibrd_t *brdp, stliport_t *portp, unsigned long arg, int wait);
static int	stli_rawclose(stlibrd_t *brdp, stliport_t *portp, unsigned long arg, int wait);
static int	stli_waitcarrier(stlibrd_t *brdp, stliport_t *portp, struct file *filp);
static void	stli_dohangup(void *arg);
static void	stli_delay(int len);
static int	stli_setport(stliport_t *portp);
static int	stli_cmdwait(stlibrd_t *brdp, stliport_t *portp, unsigned long cmd, void *arg, int size, int copyback);
static void	stli_sendcmd(stlibrd_t *brdp, stliport_t *portp, unsigned long cmd, void *arg, int size, int copyback);
static void	stli_dodelaycmd(stliport_t *portp, volatile cdkctrl_t *cp);
static void	stli_mkasyport(stliport_t *portp, asyport_t *pp, struct termios *tiosp);
static void	stli_mkasysigs(asysigs_t *sp, int dtr, int rts);
static long	stli_mktiocm(unsigned long sigvalue);
static void	stli_read(stlibrd_t *brdp, stliport_t *portp);
static void	stli_getserial(stliport_t *portp, struct serial_struct *sp);
static int	stli_setserial(stliport_t *portp, struct serial_struct *sp);

static void	stli_ecpinit(stlibrd_t *brdp);
static void	stli_ecpenable(stlibrd_t *brdp);
static void	stli_ecpdisable(stlibrd_t *brdp);
static char	*stli_ecpgetmemptr(stlibrd_t *brdp, unsigned long offset, int line);
static void	stli_ecpreset(stlibrd_t *brdp);
static void	stli_ecpintr(stlibrd_t *brdp);
static void	stli_ecpeiinit(stlibrd_t *brdp);
static void	stli_ecpeienable(stlibrd_t *brdp);
static void	stli_ecpeidisable(stlibrd_t *brdp);
static char	*stli_ecpeigetmemptr(stlibrd_t *brdp, unsigned long offset, int line);
static void	stli_ecpeireset(stlibrd_t *brdp);
static void	stli_ecpmcenable(stlibrd_t *brdp);
static void	stli_ecpmcdisable(stlibrd_t *brdp);
static char	*stli_ecpmcgetmemptr(stlibrd_t *brdp, unsigned long offset, int line);
static void	stli_ecpmcreset(stlibrd_t *brdp);

static void	stli_onbinit(stlibrd_t *brdp);
static void	stli_onbenable(stlibrd_t *brdp);
static void	stli_onbdisable(stlibrd_t *brdp);
static char	*stli_onbgetmemptr(stlibrd_t *brdp, unsigned long offset, int line);
static void	stli_onbreset(stlibrd_t *brdp);
static void	stli_onbeinit(stlibrd_t *brdp);
static void	stli_onbeenable(stlibrd_t *brdp);
static void	stli_onbedisable(stlibrd_t *brdp);
static char	*stli_onbegetmemptr(stlibrd_t *brdp, unsigned long offset, int line);
static void	stli_onbereset(stlibrd_t *brdp);
static void	stli_bbyinit(stlibrd_t *brdp);
static char	*stli_bbygetmemptr(stlibrd_t *brdp, unsigned long offset, int line);
static void	stli_bbyreset(stlibrd_t *brdp);
static void	stli_stalinit(stlibrd_t *brdp);
static char	*stli_stalgetmemptr(stlibrd_t *brdp, unsigned long offset, int line);
static void	stli_stalreset(stlibrd_t *brdp);

#if STLI_HIMEMORY
static void *stli_mapbrdmem(unsigned long physaddr, unsigned int size);
#endif

/*****************************************************************************/

/*
 *	Define the driver info for a user level shared memory device. This
 *	device will work sort of like the /dev/kmem device - except that it
 *	will give access to the shared memory on the Stallion intelligent
 *	board. This is also a very useful debugging tool.
 */
static struct file_operations	stli_fsiomem = {
	NULL,
	stli_memread,
	stli_memwrite,
	NULL,
	NULL,
	stli_memioctl,
	NULL,
	NULL,
	NULL,
	NULL
};

/*****************************************************************************/

/*
 *	Define a timer_list entry for our poll routine. The slave board
 *	is polled every so often to see if anything needs doing. This is
 *	much cheaper on host cpu than using interrupts. It turns out to
 *	not increase character latency by much either...
 */
static struct timer_list	stli_timerlist = {
	NULL, NULL, 0, 0, stli_poll
};

static int	stli_timeron = 0;

/*
 *	This is hack to allow for the kernel changes made to add_timer
 *	in the newer 1.3.X kernels (changed around 1.3.1X).
 */
#ifdef LINUX_1_2_X_COMPAT
#define	STLI_TIMEOUT	0
#else
#define	STLI_TIMEOUT	(jiffies + 1)
#endif

/*****************************************************************************/

#ifdef MODULE

/*
 *	Include kernel version number for modules.
 */
char	kernel_version[] = UTS_RELEASE;

int init_module()
{
	unsigned long	flags;

#if DEBUG
	printk("init_module()\n");
#endif

	save_flags(flags);
	cli();
	stli_init();
	restore_flags(flags);

	return(0);
}

/*****************************************************************************/

void cleanup_module()
{
	stlibrd_t	*brdp;
	stliport_t	*portp;
	unsigned long	flags;
	int		i, j;

#if DEBUG
	printk("cleanup_module()\n");
#endif

	printk("Unloading %s: version %s\n", stli_drvname, stli_drvversion);

	save_flags(flags);
	cli();

/*
 *	Free up all allocated resources used by the ports. This includes
 *	memory and interrupts.
 */
	if (stli_timeron) {
		stli_timeron = 0;
		del_timer(&stli_timerlist);
	}

	i = tty_unregister_driver(&stli_serial);
	j = tty_unregister_driver(&stli_callout);
	if (i || j) {
		printk("STALLION: failed to un-register tty driver, errno=%d,%d\n", -i, -j);
		restore_flags(flags);
		return;
	}
	if ((i = unregister_chrdev(STL_SIOMEMMAJOR, "staliomem")))
		printk("STALLION: failed to un-register serial memory device, errno=%d\n", -i);

	if (stli_tmpwritebuf != (char *) NULL)
		kfree_s(stli_tmpwritebuf, STLI_TXBUFSIZE);
	if (stli_txcookbuf != (char *) NULL)
		kfree_s(stli_txcookbuf, STLI_TXBUFSIZE);

	for (i = 0; (i < stli_nrbrds); i++) {
		brdp = &stli_brds[i];
		for (j = 0; (j < STL_MAXPORTS); j++) {
			portp = brdp->ports[j];
			if (portp != (stliport_t *) NULL) {
				if (portp->tty != (struct tty_struct *) NULL)
					tty_hangup(portp->tty);
				kfree_s(portp, sizeof(stliport_t));
			}
		}

#if STLI_HIMEMORY
		if (((unsigned long) brdp->membase) >= 0x100000)
			vfree(brdp->membase);
#endif
		if ((brdp->brdtype == BRD_ECP) || (brdp->brdtype == BRD_ECPE) || (brdp->brdtype == BRD_ECPMC))
			release_region(brdp->iobase, ECP_IOSIZE);
		else
			release_region(brdp->iobase, ONB_IOSIZE);
	}
	kfree_s(stli_brds, (sizeof(stlibrd_t) * stli_nrbrds));

	restore_flags(flags);
}

#endif

/*****************************************************************************/

/*
 *	Local memory allocation routines. These are used so we can deal with
 *	memory allocation at init time and during run-time in a consistent
 *	way. Everbody just calls the stli_memalloc routine to allocate
 *	memory and it will do the right thing. There is no common memory
 *	deallocation code - since this is only done is special cases, all of
 *	which are tightly controlled.
 */

static void *stli_memalloc(int len)
{
	return (void *) kmalloc(len, GFP_KERNEL);
}

/*****************************************************************************/

static int stli_open(struct tty_struct *tty, struct file *filp)
{
	stlibrd_t	*brdp;
	stliport_t	*portp;
	unsigned int	minordev;
	int		brdnr, portnr, rc;

#if DEBUG
	printk("stli_open(tty=%x,filp=%x): device=%x\n", (int) tty, (int) filp, tty->device);
#endif

	minordev = MINOR(tty->device);
	brdnr = MKDEV2BRD(minordev);
	if (brdnr >= stli_nrbrds)
		return(-ENODEV);
	if (stli_brds == (stlibrd_t *) NULL)
		return(-ENODEV);
	brdp = &stli_brds[brdnr];
	if ((brdp->state & BST_STARTED) == 0)
		return(-ENODEV);
	portnr = MKDEV2PORT(minordev);
	if ((portnr < 0) || (portnr > brdp->nrports))
		return(-ENODEV);

	portp = brdp->ports[portnr];
	if (portp == (stliport_t *) NULL)
		return(-ENODEV);
	if (portp->devnr < 1)
		return(-ENODEV);

/*
 *	Check if this port is in the middle of closing. If so then wait
 *	until it is closed then return error status based on flag settings.
 *	The sleep here does not need interrupt protection since the wakeup
 *	for it is done with the same context.
 */
	if (portp->flags & ASYNC_CLOSING) {
		interruptible_sleep_on(&portp->close_wait);
		if (portp->flags & ASYNC_HUP_NOTIFY)
			return(-EAGAIN);
		return(-ERESTARTSYS);
	}

/*
 *	On the first open of the device setup the port hardware, and
 *	initialize the per port data structure. Since initializing the port
 *	requires serval commands to the board we will need to wait for any
 *	other open that is already initializing the port.
 */
	portp->tty = tty;
	tty->driver_data = portp;
	portp->refcount++;

	while (test_bit(ST_INITIALIZING, &portp->state)) {
		if (current->signal & ~current->blocked)
			return(-ERESTARTSYS);
		interruptible_sleep_on(&portp->raw_wait);
	}

	if ((portp->flags & ASYNC_INITIALIZED) == 0) {
		set_bit(ST_INITIALIZING, &portp->state);
		if ((rc = stli_initopen(brdp, portp)) >= 0) {
			portp->flags |= ASYNC_INITIALIZED;
			clear_bit(TTY_IO_ERROR, &tty->flags);
		}
		clear_bit(ST_INITIALIZING, &portp->state);
		wake_up_interruptible(&portp->open_wait);
		if (rc < 0)
			return(rc);
	}

/*
 *	Check if this port is in the middle of closing. If so then wait
 *	until it is closed then return error status, based on flag settings.
 *	The sleep here does not need interrupt protection since the wakeup
 *	for it is done with the same context.
 */
	if (portp->flags & ASYNC_CLOSING) {
		interruptible_sleep_on(&portp->close_wait);
		if (portp->flags & ASYNC_HUP_NOTIFY)
			return(-EAGAIN);
		return(-ERESTARTSYS);
	}

/*
 *	Based on type of open being done check if it can overlap with any
 *	previous opens still in effect. If we are a normal serial device
 *	then also we might have to wait for carrier.
 */
	if (tty->driver.subtype == STL_DRVTYPCALLOUT) {
		if (portp->flags & ASYNC_NORMAL_ACTIVE)
			return(-EBUSY);
		if (portp->flags & ASYNC_CALLOUT_ACTIVE) {
			if ((portp->flags & ASYNC_SESSION_LOCKOUT) &&
					(portp->session != current->session))
				return(-EBUSY);
			if ((portp->flags & ASYNC_PGRP_LOCKOUT) &&
					(portp->pgrp != current->pgrp))
				return(-EBUSY);
		}
		portp->flags |= ASYNC_CALLOUT_ACTIVE;
	} else {
		if (filp->f_flags & O_NONBLOCK) {
			if (portp->flags & ASYNC_CALLOUT_ACTIVE)
				return(-EBUSY);
		} else {
			if ((rc = stli_waitcarrier(brdp, portp, filp)) != 0)
				return(rc);
		}
		portp->flags |= ASYNC_NORMAL_ACTIVE;
	}

	if ((portp->refcount == 1) && (portp->flags & ASYNC_SPLIT_TERMIOS)) {
		if (tty->driver.subtype == STL_DRVTYPSERIAL)
			*tty->termios = portp->normaltermios;
		else
			*tty->termios = portp->callouttermios;
		stli_setport(portp);
	}

	portp->session = current->session;
	portp->pgrp = current->pgrp;
	return(0);
}

/*****************************************************************************/

static void stli_close(struct tty_struct *tty, struct file *filp)
{
	stlibrd_t	*brdp;
	stliport_t	*portp;
	unsigned long	flags;

#if DEBUG
	printk("stli_close(tty=%x,filp=%x)\n", (int) tty, (int) filp);
#endif

	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return;

	save_flags(flags);
	cli();
	if (tty_hung_up_p(filp)) {
		restore_flags(flags);
		return;
	}
	if (portp->refcount-- > 1) {
		restore_flags(flags);
		return;
	}

	portp->flags |= ASYNC_CLOSING;

	if (portp->flags & ASYNC_NORMAL_ACTIVE)
		portp->normaltermios = *tty->termios;
	if (portp->flags & ASYNC_CALLOUT_ACTIVE)
		portp->callouttermios = *tty->termios;

/*
 *	May want to wait for data to drain before closing. The BUSY flag
 *	keeps track of whether we are still transmitting or not. It is
 *	updated by messages from the slave - indicating when all chars
 *	really have drained.
 */
	if (tty == stli_txcooktty)
		stli_flushchars(tty);
	tty->closing = 1;
	if (test_bit(ST_TXBUSY, &portp->state)) {
		if (portp->closing_wait != ASYNC_CLOSING_WAIT_NONE)
			tty_wait_until_sent(tty, portp->closing_wait);
	}

	portp->flags &= ~ASYNC_INITIALIZED;
	brdp = &stli_brds[portp->brdnr];
	stli_rawclose(brdp, portp, 0, 1);
	if (tty->termios->c_cflag & HUPCL) {
		stli_mkasysigs(&portp->asig, 0, 0);
		stli_cmdwait(brdp, portp, A_SETSIGNALS, &portp->asig, sizeof(asysigs_t), 0);
	}
	clear_bit(ST_TXBUSY, &portp->state);
	clear_bit(ST_RXSTOP, &portp->state);
	set_bit(TTY_IO_ERROR, &tty->flags);
	if (tty->ldisc.flush_buffer)
		(tty->ldisc.flush_buffer)(tty);
	set_bit(ST_DOFLUSHRX, &portp->state);
	stli_flushbuffer(tty);

	tty->closing = 0;
	tty->driver_data = (void *) NULL;
	portp->tty = (struct tty_struct *) NULL;

	if (portp->openwaitcnt) {
		if (portp->close_delay)
			stli_delay(portp->close_delay);
		wake_up_interruptible(&portp->open_wait);
	}

	portp->flags &= ~(ASYNC_CALLOUT_ACTIVE | ASYNC_NORMAL_ACTIVE | ASYNC_CLOSING);
	wake_up_interruptible(&portp->close_wait);
	restore_flags(flags);
}

/*****************************************************************************/

/*
 *	Carry out first open operations on a port. This involves a number of
 *	commands to be sent to the slave. We need to open the port, set the
 *	notification events, set the initial port settings, get and set the
 *	initial signal values. We sleep and wait in between each one. But
 *	this still all happens pretty quickly.
 */

static int stli_initopen(stlibrd_t *brdp, stliport_t *portp)
{
	struct tty_struct	*tty;
	asynotify_t		nt;
	asyport_t		aport;
	int			rc;

#if DEBUG
	printk("stli_initopen(brdp=%x,portp=%x)\n", (int) brdp, (int) portp);
#endif

	if ((rc = stli_rawopen(brdp, portp, 0, 1)) < 0)
		return(rc);

	memset(&nt, 0, sizeof(asynotify_t));
	nt.data = (DT_TXLOW | DT_TXEMPTY | DT_RXBUSY | DT_RXBREAK);
	nt.signal = SG_DCD;
	if ((rc = stli_cmdwait(brdp, portp, A_SETNOTIFY, &nt, sizeof(asynotify_t), 0)) < 0)
		return(rc);

	tty = portp->tty;
	if (tty == (struct tty_struct *) NULL)
		return(-ENODEV);
	stli_mkasyport(portp, &aport, tty->termios);
	if ((rc = stli_cmdwait(brdp, portp, A_SETPORT, &aport, sizeof(asyport_t), 0)) < 0)
		return(rc);

	set_bit(ST_GETSIGS, &portp->state);
	if ((rc = stli_cmdwait(brdp, portp, A_GETSIGNALS, &portp->asig, sizeof(asysigs_t), 1)) < 0)
		return(rc);
	if (clear_bit(ST_GETSIGS, &portp->state))
		portp->sigs = stli_mktiocm(portp->asig.sigvalue);
	stli_mkasysigs(&portp->asig, 1, 1);
	if ((rc = stli_cmdwait(brdp, portp, A_SETSIGNALS, &portp->asig, sizeof(asysigs_t), 0)) < 0)
		return(rc);

	return(0);
}

/*****************************************************************************/

/*
 *	Send an open message to the slave. This will sleep waiting for the
 *	acknowledgement, so must have user context. We need to co-ordinate
 *	with close events here, since we don't want open and close events
 *	to overlap.
 */

static int stli_rawopen(stlibrd_t *brdp, stliport_t *portp, unsigned long arg, int wait)
{
	volatile cdkhdr_t	*hdrp;
	volatile cdkctrl_t	*cp;
	volatile unsigned char	*bits;
	unsigned long		flags;
	int			rc;

#if DEBUG
	printk("stli_rawopen(brdp=%x,portp=%x,arg=%x,wait=%d)\n", (int) brdp, (int) portp, (int) arg, wait);
#endif

/*
 *	Send a message to the slave to open this port.
 */
	save_flags(flags);
	cli();

/*
 *	Slave is already closing this port. This can happen if a hangup
 *	occurs on this port. So we must wait until it is complete. The
 *	order of opens and closes may not be preserved across shared
 *	memory, so we must wait until it is complete.
 */
	while (test_bit(ST_CLOSING, &portp->state)) {
		if (current->signal & ~current->blocked) {
			restore_flags(flags);
			return(-ERESTARTSYS);
		}
		interruptible_sleep_on(&portp->raw_wait);
	}

/*
 *	Everything is ready now, so write the open message into shared
 *	memory. Once the message is in set the service bits to say that
 *	this port wants service.
 */
	EBRDENABLE(brdp);
	cp = &((volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr))->ctrl;
	cp->openarg = arg;
	cp->open = 1;
	hdrp = (volatile cdkhdr_t *) EBRDGETMEMPTR(brdp, CDK_CDKADDR);
	hdrp->slavereq |= portp->reqbit;
	bits = ((volatile unsigned char *) hdrp) + brdp->slaveoffset + portp->portidx;
	*bits |= portp->portbit;
	EBRDDISABLE(brdp);

	if (wait == 0) {
		restore_flags(flags);
		return(0);
	}

/*
 *	Slave is in action, so now we must wait for the open acknowledgment
 *	to come back.
 */
	rc = 0;
	set_bit(ST_OPENING, &portp->state);
	while (test_bit(ST_OPENING, &portp->state)) {
		if (current->signal & ~current->blocked) {
			rc = -ERESTARTSYS;
			break;
		}
		interruptible_sleep_on(&portp->raw_wait);
	}
	restore_flags(flags);

	if ((rc == 0) && (portp->rc != 0))
		rc = -EIO;
	return(rc);
}

/*****************************************************************************/

/*
 *	Send a close message to the slave. Normally this will sleep waiting
 *	for the acknowledgement, but if wait parameter is 0 it will not. If
 *	wait is true then must have user context (to sleep).
 */

static int stli_rawclose(stlibrd_t *brdp, stliport_t *portp, unsigned long arg, int wait)
{
	volatile cdkhdr_t	*hdrp;
	volatile cdkctrl_t	*cp;
	volatile unsigned char	*bits;
	unsigned long		flags;
	int			rc;

#if DEBUG
	printk("stli_rawclose(brdp=%x,portp=%x,arg=%x,wait=%d)\n", (int) brdp, (int) portp, (int) arg, wait);
#endif

	save_flags(flags);
	cli();

/*
 *	Slave is already closing this port. This can happen if a hangup
 *	occurs on this port.
 */
	if (wait) {
		while (test_bit(ST_CLOSING, &portp->state)) {
			if (current->signal & ~current->blocked) {
				restore_flags(flags);
				return(-ERESTARTSYS);
			}
			interruptible_sleep_on(&portp->raw_wait);
		}
	}

/*
 *	Write the close command into shared memory.
 */
	EBRDENABLE(brdp);
	cp = &((volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr))->ctrl;
	cp->closearg = arg;
	cp->close = 1;
	hdrp = (volatile cdkhdr_t *) EBRDGETMEMPTR(brdp, CDK_CDKADDR);
	hdrp->slavereq |= portp->reqbit;
	bits = ((volatile unsigned char *) hdrp) + brdp->slaveoffset + portp->portidx;
	*bits |= portp->portbit;
	EBRDDISABLE(brdp);

	set_bit(ST_CLOSING, &portp->state);
	if (wait == 0) {
		restore_flags(flags);
		return(0);
	}

/*
 *	Slave is in action, so now we must wait for the open acknowledgment
 *	to come back.
 */
	rc = 0;
	while (test_bit(ST_CLOSING, &portp->state)) {
		if (current->signal & ~current->blocked) {
			rc = -ERESTARTSYS;
			break;
		}
		interruptible_sleep_on(&portp->raw_wait);
	}
	restore_flags(flags);

	if ((rc == 0) && (portp->rc != 0))
		rc = -EIO;
	return(rc);
}

/*****************************************************************************/

/*
 *	Send a command to the slave and wait for the response. This must
 *	have user context (it sleeps). This routine is generic in that it
 *	can send any type of command. Its purpose is to wait for that command
 *	to complete (as opposed to initiating the command then returning).
 */

static int stli_cmdwait(stlibrd_t *brdp, stliport_t *portp, unsigned long cmd, void *arg, int size, int copyback)
{
	unsigned long	flags;

#if DEBUG
	printk("stli_cmdwait(brdp=%x,portp=%x,cmd=%x,arg=%x,size=%d,copyback=%d)\n", (int) brdp, (int) portp, (int) cmd, (int) arg, size, copyback);
#endif

	save_flags(flags);
	cli();
	while (test_bit(ST_CMDING, &portp->state)) {
		if (current->signal & ~current->blocked) {
			restore_flags(flags);
			return(-ERESTARTSYS);
		}
		interruptible_sleep_on(&portp->raw_wait);
	}

	stli_sendcmd(brdp, portp, cmd, arg, size, copyback);

	while (test_bit(ST_CMDING, &portp->state)) {
		if (current->signal & ~current->blocked) {
			restore_flags(flags);
			return(-ERESTARTSYS);
		}
		interruptible_sleep_on(&portp->raw_wait);
	}
	restore_flags(flags);

	if (portp->rc != 0)
		return(-EIO);
	return(0);
}

/*****************************************************************************/

/*
 *	Send the termios settings for this port to the slave. This sleeps
 *	waiting for the command to complete - so must have user context.
 */

static int stli_setport(stliport_t *portp)
{
	stlibrd_t	*brdp;
	asyport_t	aport;

#if DEBUG
	printk("stli_setport(portp=%x)\n", (int) portp);
#endif

	if (portp == (stliport_t *) NULL)
		return(-ENODEV);
	if (portp->tty == (struct tty_struct *) NULL)
		return(-ENODEV);
	if ((portp->brdnr < 0) && (portp->brdnr >= stli_nrbrds))
		return(-ENODEV);
	brdp = &stli_brds[portp->brdnr];

	stli_mkasyport(portp, &aport, portp->tty->termios);
	return(stli_cmdwait(brdp, portp, A_SETPORT, &aport, sizeof(asyport_t), 0));
}

/*****************************************************************************/

/*
 *	Wait for a specified delay period, this is not a busy-loop. It will
 *	give up the processor while waiting. Unfortunately this has some
 *	rather intimate knowledge of the process management stuff.
 */

static void stli_delay(int len)
{
#if DEBUG
	printk("stl_delay(len=%d)\n", len);
#endif
	if (len > 0) {
		current->state = TASK_INTERRUPTIBLE;
		current->timeout = jiffies + len;
		schedule();
	}
}

/*****************************************************************************/

/*
 *	Possibly need to wait for carrier (DCD signal) to come high. Say
 *	maybe because if we are clocal then we don't need to wait...
 */

static int stli_waitcarrier(stlibrd_t *brdp, stliport_t *portp, struct file *filp)
{
	unsigned long	flags;
	int		rc;

#if DEBUG
	printk("stli_waitcarrier(brdp=%x,portp=%x,filp=%x)\n", (int) brdp, (int) portp, (int) filp);
#endif

	rc = 0;

	save_flags(flags);
	cli();
	portp->openwaitcnt++;
	if (portp->refcount > 0)
		portp->refcount--;

	for (;;) {
		if ((portp->flags & ASYNC_CALLOUT_ACTIVE) == 0) {
			stli_mkasysigs(&portp->asig, 1, 1);
			if ((rc = stli_cmdwait(brdp, portp, A_SETSIGNALS, &portp->asig, sizeof(asysigs_t), 0)) < 0)
				break;
		}
		if (tty_hung_up_p(filp) || ((portp->flags & ASYNC_INITIALIZED) == 0)) {
			if (portp->flags & ASYNC_HUP_NOTIFY)
				rc = -EBUSY;
			else
				rc = -ERESTARTSYS;
			break;
		}
		if (((portp->flags & ASYNC_CALLOUT_ACTIVE) == 0) &&
				((portp->flags & ASYNC_CLOSING) == 0) &&
				((portp->tty->termios->c_cflag & CLOCAL) ||
				(portp->sigs & TIOCM_CD))) {
			break;
		}
		if (current->signal & ~current->blocked) {
			rc = -ERESTARTSYS;
			break;
		}
		interruptible_sleep_on(&portp->open_wait);
	}

	if (! tty_hung_up_p(filp))
		portp->refcount++;
	portp->openwaitcnt--;
	restore_flags(flags);

	return(rc);
}

/*****************************************************************************/

/*
 *	Write routine. Take the data and put it in the shared memory ring
 *	queue. If port is not already sending chars then need to mark the
 *	service bits for this port.
 */

static int stli_write(struct tty_struct *tty, int from_user, const unsigned char *buf, int count)
{
	volatile cdkasy_t	*ap;
	volatile cdkhdr_t	*hdrp;
	volatile unsigned char	*bits;
	unsigned char		*shbuf, *chbuf;
	stliport_t		*portp;
	stlibrd_t		*brdp;
	unsigned int		len, stlen, head, tail, size;
	unsigned long		flags;

#if DEBUG
	printk("stli_write(tty=%x,from_user=%d,buf=%x,count=%d)\n", (int) tty, from_user, (int) buf, count);
#endif

	if ((tty == (struct tty_struct *) NULL) || (stli_tmpwritebuf == (char *) NULL))
		return(0);
	if (tty == stli_txcooktty)
		stli_flushchars(tty);
	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return(0);
	if ((portp->brdnr < 0) || (portp->brdnr >= stli_nrbrds))
		return(0);
	brdp = &stli_brds[portp->brdnr];
	chbuf = (unsigned char *) buf;

/*
 *	If copying direct from user space we need to be able to handle page
 *	faults while we are copying. To do this copy as much as we can now
 *	into a kernel buffer. From there we copy it into shared memory. The
 *	big problem is that we do not want shared memory enabled when we are
 *	sleeping (other boards may be serviced while asleep). Something else
 *	to note here is the reading of the tail twice. Since the boards
 *	shared memory can be on an 8-bit bus then we need to be very carefull
 *	reading 16 bit quantities - since both the board (slave) and host
 *	cound be writing and reading at the same time.
 */
	if (from_user) {
		save_flags(flags);
		cli();
		EBRDENABLE(brdp);
		ap = (volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr);
		head = (unsigned int) ap->txq.head;
		tail = (unsigned int) ap->txq.tail;
		if (tail != ((unsigned int) ap->txq.tail))
			tail = (unsigned int) ap->txq.tail;
		len = (head >= tail) ? (portp->txsize - (head - tail) - 1) : (tail - head - 1);
		count = MIN(len, count);
		EBRDDISABLE(brdp);

		down(&stli_tmpwritesem);
		memcpy_fromfs(stli_tmpwritebuf, chbuf, count);
		up(&stli_tmpwritesem);
		chbuf = &stli_tmpwritebuf[0];
		restore_flags(flags);
	}

/*
 *	All data is now local, shove as much as possible into shared memory.
 */
	save_flags(flags);
	cli();
	EBRDENABLE(brdp);
	ap = (volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr);
	head = (unsigned int) ap->txq.head;
	tail = (unsigned int) ap->txq.tail;
	if (tail != ((unsigned int) ap->txq.tail))
		tail = (unsigned int) ap->txq.tail;
	size = portp->txsize;
	if (head >= tail) {
		len = size - (head - tail) - 1;
		stlen = size - head;
	} else {
		len = tail - head - 1;
		stlen = len;
	}

	len = MIN(len, count);
	count = 0;
	shbuf = (char *) EBRDGETMEMPTR(brdp, portp->txoffset);

	while (len > 0) {
		stlen = MIN(len, stlen);
		memcpy((shbuf + head), chbuf, stlen);
		chbuf += stlen;
		len -= stlen;
		count += stlen;
		head += stlen;
		if (head >= size) {
			head = 0;
			stlen = tail;
		}
	}

	ap = (volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr);
	ap->txq.head = head;
	if (test_bit(ST_TXBUSY, &portp->state)) {
		if (ap->changed.data & DT_TXEMPTY)
			ap->changed.data &= ~DT_TXEMPTY;
	}
	hdrp = (volatile cdkhdr_t *) EBRDGETMEMPTR(brdp, CDK_CDKADDR);
	hdrp->slavereq |= portp->reqbit;
	bits = ((volatile unsigned char *) hdrp) + brdp->slaveoffset + portp->portidx;
	*bits |= portp->portbit;
	set_bit(ST_TXBUSY, &portp->state);

	EBRDDISABLE(brdp);
	restore_flags(flags);

	return(count);
}

/*****************************************************************************/

/*
 *	Output a single character. We put it into a temporary local buffer
 *	(for speed) then write out that buffer when the flushchars routine
 *	is called. There is a safety catch here so that if some other port
 *	writes chars before the current buffer has been, then we write them
 *	first them do the new ports.
 */

static void stli_putchar(struct tty_struct *tty, unsigned char ch)
{
#if DEBUG
	printk("stli_putchar(tty=%x,ch=%x)\n", (int) tty, (int) ch);
#endif

	if (tty == (struct tty_struct *) NULL)
		return;
	if (tty != stli_txcooktty) {
		if (stli_txcooktty != (struct tty_struct *) NULL)
			stli_flushchars(stli_txcooktty);
		stli_txcooktty = tty;
	}

	stli_txcookbuf[stli_txcooksize++] = ch;
}

/*****************************************************************************/

/*
 *	Transfer characters from the local TX cooking buffer to the board.
 *	We sort of ignore the tty that gets passed in here. We rely on the
 *	info stored with the TX cook buffer to tell us which port to flush
 *	the data on. In any case we clean out the TX cook buffer, for re-use
 *	by someone else.
 */

static void stli_flushchars(struct tty_struct *tty)
{
	volatile cdkhdr_t	*hdrp;
	volatile unsigned char	*bits;
	volatile cdkasy_t	*ap;
	struct tty_struct	*cooktty;
	stliport_t		*portp;
	stlibrd_t		*brdp;
	unsigned int		len, stlen, head, tail, size, count, cooksize;
	unsigned char		*buf, *shbuf;
	unsigned long		flags;

#if DEBUG
	printk("stli_flushchars(tty=%x)\n", (int) tty);
#endif

	cooksize = stli_txcooksize;
	cooktty = stli_txcooktty;
	stli_txcooksize = 0;
	stli_txcookrealsize = 0;
	stli_txcooktty = (struct tty_struct *) NULL;

	if (tty == (struct tty_struct *) NULL)
		return;
	if (cooktty == (struct tty_struct *) NULL)
		return;
	if (tty != cooktty)
		tty = cooktty;
	if (cooksize == 0)
		return;

	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return;
	if ((portp->brdnr < 0) || (portp->brdnr >= stli_nrbrds))
		return;
	brdp = &stli_brds[portp->brdnr];

	save_flags(flags);
	cli();
	EBRDENABLE(brdp);

	ap = (volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr);
	head = (unsigned int) ap->txq.head;
	tail = (unsigned int) ap->txq.tail;
	if (tail != ((unsigned int) ap->txq.tail))
		tail = (unsigned int) ap->txq.tail;
	size = portp->txsize;
	if (head >= tail) {
		len = size - (head - tail) - 1;
		stlen = size - head;
	} else {
		len = tail - head - 1;
		stlen = len;
	}

	len = MIN(len, cooksize);
	count = 0;
	shbuf = (char *) EBRDGETMEMPTR(brdp, portp->txoffset);
	buf = stli_txcookbuf;

	while (len > 0) {
		stlen = MIN(len, stlen);
		memcpy((shbuf + head), buf, stlen);
		buf += stlen;
		len -= stlen;
		count += stlen;
		head += stlen;
		if (head >= size) {
			head = 0;
			stlen = tail;
		}
	}

	ap = (volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr);
	ap->txq.head = head;

	if (test_bit(ST_TXBUSY, &portp->state)) {
		if (ap->changed.data & DT_TXEMPTY)
			ap->changed.data &= ~DT_TXEMPTY;
	}
	hdrp = (volatile cdkhdr_t *) EBRDGETMEMPTR(brdp, CDK_CDKADDR);
	hdrp->slavereq |= portp->reqbit;
	bits = ((volatile unsigned char *) hdrp) + brdp->slaveoffset + portp->portidx;
	*bits |= portp->portbit;
	set_bit(ST_TXBUSY, &portp->state);

	EBRDDISABLE(brdp);
	restore_flags(flags);
}

/*****************************************************************************/

static int stli_writeroom(struct tty_struct *tty)
{
	volatile cdkasyrq_t	*rp;
	stliport_t		*portp;
	stlibrd_t		*brdp;
	unsigned int		head, tail, len;
	unsigned long		flags;

#if DEBUG
	printk("stli_writeroom(tty=%x)\n", (int) tty);
#endif

	if (tty == (struct tty_struct *) NULL)
		return(0);
	if (tty == stli_txcooktty) {
		if (stli_txcookrealsize != 0) {
			len = stli_txcookrealsize - stli_txcooksize;
			return(len);
		}
	}

	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return(0);
	if ((portp->brdnr < 0) || (portp->brdnr >= stli_nrbrds))
		return(0);
	brdp = &stli_brds[portp->brdnr];

	save_flags(flags);
	cli();
	EBRDENABLE(brdp);
	rp = &((volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr))->txq;
	head = (unsigned int) rp->head;
	tail = (unsigned int) rp->tail;
	if (tail != ((unsigned int) rp->tail))
		tail = (unsigned int) rp->tail;
	len = (head >= tail) ? (portp->txsize - (head - tail)) : (tail - head);
	len--;
	EBRDDISABLE(brdp);
	restore_flags(flags);

	if (tty == stli_txcooktty) {
		stli_txcookrealsize = len;
		len -= stli_txcooksize;
	}
	return(len);
}

/*****************************************************************************/

/*
 *	Return the number of characters in the transmit buffer. Normally we
 *	will return the number of chars in the shared memory ring queue.
 *	We need to kludge around the case where the shared memory buffer is
 *	empty but not all characters have drained yet, for this case just
 *	return that there is 1 character in the buffer!
 */

static int stli_charsinbuffer(struct tty_struct *tty)
{
	volatile cdkasyrq_t	*rp;
	stliport_t		*portp;
	stlibrd_t		*brdp;
	unsigned int		head, tail, len;
	unsigned long		flags;

#if DEBUG
	printk("stli_charsinbuffer(tty=%x)\n", (int) tty);
#endif

	if (tty == (struct tty_struct *) NULL)
		return(0);
	if (tty == stli_txcooktty)
		stli_flushchars(tty);
	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return(0);
	if ((portp->brdnr < 0) || (portp->brdnr >= stli_nrbrds))
		return(0);
	brdp = &stli_brds[portp->brdnr];

	save_flags(flags);
	cli();
	EBRDENABLE(brdp);
	rp = &((volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr))->txq;
	head = (unsigned int) rp->head;
	tail = (unsigned int) rp->tail;
	if (tail != ((unsigned int) rp->tail))
		tail = (unsigned int) rp->tail;
	len = (head >= tail) ? (head - tail) : (portp->txsize - (tail - head));
	if ((len == 0) && test_bit(ST_TXBUSY, &portp->state))
		len = 1;
	EBRDDISABLE(brdp);
	restore_flags(flags);

	return(len);
}

/*****************************************************************************/

/*
 *	Generate the serial struct info.
 */

static void stli_getserial(stliport_t *portp, struct serial_struct *sp)
{
	struct serial_struct	sio;

#if DEBUG
	printk("stli_getserial(portp=%x,sp=%x)\n", (int) portp, (int) sp);
#endif

	memset(&sio, 0, sizeof(struct serial_struct));
	sio.type = PORT_UNKNOWN;
	sio.line = portp->portnr;
	sio.port = stli_brdconf[portp->brdnr].ioaddr1;
	sio.irq = stli_brdconf[portp->brdnr].irq;
	sio.flags = portp->flags;
	sio.baud_base = portp->baud_base;
	sio.close_delay = portp->close_delay;
	sio.closing_wait = portp->closing_wait;
	sio.custom_divisor = portp->custom_divisor;
	sio.xmit_fifo_size = 0;
	sio.hub6 = 0;
	memcpy_tofs(sp, &sio, sizeof(struct serial_struct));
}

/*****************************************************************************/

/*
 *	Set port according to the serial struct info.
 *	At this point we do not do any auto-configure stuff, so we will
 *	just quietly ignore any requests to change irq, etc.
 */

static int stli_setserial(stliport_t *portp, struct serial_struct *sp)
{
	struct serial_struct	sio;
	int			rc;

#if DEBUG
	printk("stli_setserial(portp=%x,sp=%x)\n", (int) portp, (int) sp);
#endif

	memcpy_fromfs(&sio, sp, sizeof(struct serial_struct));
	if (!suser()) {
		if ((sio.baud_base != portp->baud_base) ||
				(sio.close_delay != portp->close_delay) ||
				((sio.flags & ~ASYNC_USR_MASK) != (portp->flags & ~ASYNC_USR_MASK)))
			return(-EPERM);
	} 

	portp->flags = (portp->flags & ~ASYNC_USR_MASK) | (sio.flags & ASYNC_USR_MASK);
	portp->baud_base = sio.baud_base;
	portp->close_delay = sio.close_delay;
	portp->closing_wait = sio.closing_wait;
	portp->custom_divisor = sio.custom_divisor;

	if ((rc = stli_setport(portp)) < 0)
		return(rc);
	return(0);
}

/*****************************************************************************/

static int stli_ioctl(struct tty_struct *tty, struct file *file, unsigned int cmd, unsigned long arg)
{
	stliport_t	*portp;
	stlibrd_t	*brdp;
	unsigned long	val;
	int		rc;

#if DEBUG
	printk("stli_ioctl(tty=%x,file=%x,cmd=%x,arg=%x)\n", (int) tty, (int) file, cmd, (int) arg);
#endif

	if (tty == (struct tty_struct *) NULL)
		return(-ENODEV);
	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return(-ENODEV);
	if ((portp->brdnr < 0) || (portp->brdnr >= stli_nrbrds))
		return(0);
	brdp = &stli_brds[portp->brdnr];

	rc = 0;

	switch (cmd) {
	case TCSBRK:
		if ((rc = tty_check_change(tty)) == 0) {
			tty_wait_until_sent(tty, 0);
			if (! arg) {
				val = 250;
				rc = stli_cmdwait(brdp, portp, A_BREAK, &val, sizeof(unsigned long), 0);
			}
		}
		break;
	case TCSBRKP:
		if ((rc = tty_check_change(tty)) == 0) {
			tty_wait_until_sent(tty, 0);
			val = (arg ? (arg * 100) : 250);
			rc = stli_cmdwait(brdp, portp, A_BREAK, &val, sizeof(unsigned long), 0);
		}
		break;
	case TIOCGSOFTCAR:
		if ((rc = verify_area(VERIFY_WRITE, (void *) arg, sizeof(long))) == 0)
			put_fs_long(((tty->termios->c_cflag & CLOCAL) ? 1 : 0), (unsigned long *) arg);
		break;
	case TIOCSSOFTCAR:
		if ((rc = verify_area(VERIFY_READ, (void *) arg, sizeof(long))) == 0) {
			arg = get_fs_long((unsigned long *) arg);
			tty->termios->c_cflag = (tty->termios->c_cflag & ~CLOCAL) | (arg ? CLOCAL : 0);
		}
		break;
	case TIOCMGET:
		if ((rc = verify_area(VERIFY_WRITE, (void *) arg, sizeof(unsigned int))) == 0) {
			if ((rc = stli_cmdwait(brdp, portp, A_GETSIGNALS, &portp->asig, sizeof(asysigs_t), 1)) < 0)
				return(rc);
			val = stli_mktiocm(portp->asig.sigvalue);
			put_fs_long(val, (unsigned long *) arg);
		}
		break;
	case TIOCMBIS:
		if ((rc = verify_area(VERIFY_READ, (void *) arg, sizeof(long))) == 0) {
			arg = get_fs_long((unsigned long *) arg);
			stli_mkasysigs(&portp->asig, ((arg & TIOCM_DTR) ? 1 : -1), ((arg & TIOCM_RTS) ? 1 : -1));
			rc = stli_cmdwait(brdp, portp, A_SETSIGNALS, &portp->asig, sizeof(asysigs_t), 0);
		}
		break;
	case TIOCMBIC:
		if ((rc = verify_area(VERIFY_READ, (void *) arg, sizeof(long))) == 0) {
			arg = get_fs_long((unsigned long *) arg);
			stli_mkasysigs(&portp->asig, ((arg & TIOCM_DTR) ? 0 : -1), ((arg & TIOCM_RTS) ? 0 : -1));
			rc = stli_cmdwait(brdp, portp, A_SETSIGNALS, &portp->asig, sizeof(asysigs_t), 0);
		}
		break;
	case TIOCMSET:
		if ((rc = verify_area(VERIFY_READ, (void *) arg, sizeof(long))) == 0) {
			arg = get_fs_long((unsigned long *) arg);
			stli_mkasysigs(&portp->asig, ((arg & TIOCM_DTR) ? 1 : 0), ((arg & TIOCM_RTS) ? 1 : 0));
			rc = stli_cmdwait(brdp, portp, A_SETSIGNALS, &portp->asig, sizeof(asysigs_t), 0);
		}
		break;
	case TIOCGSERIAL:
		if ((rc = verify_area(VERIFY_WRITE, (void *) arg, sizeof(struct serial_struct))) == 0)
			stli_getserial(portp, (struct serial_struct *) arg);
		break;
	case TIOCSSERIAL:
		if ((rc = verify_area(VERIFY_READ, (void *) arg, sizeof(struct serial_struct))) == 0)
			rc = stli_setserial(portp, (struct serial_struct *) arg);
		break;
	case TIOCSERCONFIG:
	case TIOCSERGWILD:
	case TIOCSERSWILD:
	case TIOCSERGETLSR:
	case TIOCSERGSTRUCT:
	case TIOCSERGETMULTI:
	case TIOCSERSETMULTI:
	default:
		rc = -ENOIOCTLCMD;
		break;
	}

	return(rc);
}

/*****************************************************************************/

/*
 *	This routine assumes that we have user context and can sleep.
 *	Looks like it is true for the current ttys implementation..!!
 */

static void stli_settermios(struct tty_struct *tty, struct termios *old)
{
	stliport_t	*portp;
	stlibrd_t	*brdp;
	struct termios	*tiosp;
	asyport_t	aport;

#if DEBUG
	printk("stli_settermios(tty=%x,old=%x)\n", (int) tty, (int) old);
#endif

	if (tty == (struct tty_struct *) NULL)
		return;
	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return;
	if ((portp->brdnr < 0) || (portp->brdnr >= stli_nrbrds))
		return;
	brdp = &stli_brds[portp->brdnr];

	tiosp = tty->termios;
	if ((tiosp->c_cflag == old->c_cflag) && (tiosp->c_iflag == old->c_iflag))
		return;

	stli_mkasyport(portp, &aport, tiosp);
	stli_cmdwait(brdp, portp, A_SETPORT, &aport, sizeof(asyport_t), 0);
	stli_mkasysigs(&portp->asig, ((tiosp->c_cflag & CBAUD) ? 1 : 0), -1);
	stli_cmdwait(brdp, portp, A_SETSIGNALS, &portp->asig, sizeof(asysigs_t), 0);
	if ((old->c_cflag & CRTSCTS) && ((tiosp->c_cflag & CRTSCTS) == 0))
		tty->hw_stopped = 0;
	if (((old->c_cflag & CLOCAL) == 0) && (tiosp->c_cflag & CLOCAL))
		wake_up_interruptible(&portp->open_wait);
}

/*****************************************************************************/

/*
 *	Attempt to flow control who ever is sending us data. We won't really
 *	do any flow control action here. We can't directly, and even if we
 *	wanted to we would have to send a command to the slave. The slave
 *	knows how to flow control, and will do so when its buffers reach its
 *	internal high water marks. So what we will do is set a local state
 *	bit that will stop us sending any RX data up from the poll routine
 *	(which is the place where RX data from the slave is handled).
 */

static void stli_throttle(struct tty_struct *tty)
{
	stliport_t	*portp;

#if DEBUG
	printk("stli_throttle(tty=%x)\n", (int) tty);
#endif

	if (tty == (struct tty_struct *) NULL)
		return;
	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return;

	set_bit(ST_RXSTOP, &portp->state);
}

/*****************************************************************************/

/*
 *	Unflow control the device sending us data... That means that all
 *	we have to do is clear the RXSTOP state bit. The next poll call
 *	will then be able to pass the RX data back up.
 */

static void stli_unthrottle(struct tty_struct *tty)
{
	stliport_t	*portp;

#if DEBUG
	printk("stli_unthrottle(tty=%x)\n", (int) tty);
#endif

	if (tty == (struct tty_struct *) NULL)
		return;
	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return;

	clear_bit(ST_RXSTOP, &portp->state);
}

/*****************************************************************************/

/*
 *	Stop the transmitter. Basically to do this we will just turn TX
 *	interrupts off.
 */

static void stli_stop(struct tty_struct *tty)
{
	stlibrd_t	*brdp;
	stliport_t	*portp;
	asyctrl_t	actrl;

#if DEBUG
	printk("stli_stop(tty=%x)\n", (int) tty);
#endif

	if (tty == (struct tty_struct *) NULL)
		return;
	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return;
	if ((portp->brdnr < 0) || (portp->brdnr >= stli_nrbrds))
		return;
	brdp = &stli_brds[portp->brdnr];

	memset(&actrl, 0, sizeof(asyctrl_t));
	actrl.txctrl = CT_STOPFLOW;
#if 0
	stli_cmdwait(brdp, portp, A_PORTCTRL, &actrl, sizeof(asyctrl_t));
#endif
}

/*****************************************************************************/

/*
 *	Start the transmitter again. Just turn TX interrupts back on.
 */

static void stli_start(struct tty_struct *tty)
{
	stliport_t	*portp;
	stlibrd_t	*brdp;
	asyctrl_t	actrl;

#if DEBUG
	printk("stli_start(tty=%x)\n", (int) tty);
#endif

	if (tty == (struct tty_struct *) NULL)
		return;
	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return;
	if ((portp->brdnr < 0) || (portp->brdnr >= stli_nrbrds))
		return;
	brdp = &stli_brds[portp->brdnr];

	memset(&actrl, 0, sizeof(asyctrl_t));
	actrl.txctrl = CT_STARTFLOW;
#if 0
	stli_cmdwait(brdp, portp, A_PORTCTRL, &actrl, sizeof(asyctrl_t));
#endif
}

/*****************************************************************************/

/*
 *	Scheduler called hang up routine. This is called from the scheduler,
 *	not direct from the driver "poll" routine. We can't call it there
 *	since the real local hangup code will enable/disable the board and
 *	other things that we can't do while handling the poll. Much easier
 *	to deal with it some time later (don't really care when, hangups
 *	aren't that time critical).
 */

static void stli_dohangup(void *arg)
{
	stliport_t	*portp;

#if DEBUG
	printk("stli_dohangup(portp=%x)\n", (int) arg);
#endif

	portp = (stliport_t *) arg;
	if (portp == (stliport_t *) NULL)
		return;
	if (portp->tty == (struct tty_struct *) NULL)
		return;
	tty_hangup(portp->tty);
}

/*****************************************************************************/

/*
 *	Hangup this port. This is pretty much like closing the port, only
 *	a little more brutal. No waiting for data to drain. Shutdown the
 *	port and maybe drop signals. This is rather tricky really. We want
 *	to close the port as well.
 */

static void stli_hangup(struct tty_struct *tty)
{
	stliport_t	*portp;
	stlibrd_t	*brdp;
	unsigned long	flags;

#if DEBUG
	printk("stli_hangup(tty=%x)\n", (int) tty);
#endif

	if (tty == (struct tty_struct *) NULL)
		return;
	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return;
	if ((portp->brdnr < 0) || (portp->brdnr >= stli_nrbrds))
		return;
	brdp = &stli_brds[portp->brdnr];

	portp->flags &= ~ASYNC_INITIALIZED;

	save_flags(flags);
	cli();
	if (! test_bit(ST_CLOSING, &portp->state))
		stli_rawclose(brdp, portp, 0, 0);
	if (tty->termios->c_cflag & HUPCL) {
		stli_mkasysigs(&portp->asig, 0, 0);
		if (test_bit(ST_CMDING, &portp->state)) {
			set_bit(ST_DOSIGS, &portp->state);
			set_bit(ST_DOFLUSHTX, &portp->state);
			set_bit(ST_DOFLUSHRX, &portp->state);
		} else {
			stli_sendcmd(brdp, portp, A_SETSIGNALSF, &portp->asig, sizeof(asysigs_t), 0);
		}
	}
	restore_flags(flags);

	clear_bit(ST_TXBUSY, &portp->state);
	clear_bit(ST_RXSTOP, &portp->state);
	set_bit(TTY_IO_ERROR, &tty->flags);
	tty->driver_data = (void *) NULL;
	portp->tty = (struct tty_struct *) NULL;
	portp->flags &= ~(ASYNC_NORMAL_ACTIVE | ASYNC_CALLOUT_ACTIVE);
	portp->refcount = 0;
	wake_up_interruptible(&portp->open_wait);
}

/*****************************************************************************/

/*
 *	Flush characters from the lower buffer. We may not have user context
 *	so we cannot sleep waiting for it to complete. Also we need to check
 *	if there is chars for this port in the TX cook buffer, and flush them
 *	as well.
 */

static void stli_flushbuffer(struct tty_struct *tty)
{
	stliport_t	*portp;
	stlibrd_t	*brdp;
	unsigned long	ftype, flags;

#if DEBUG
	printk("stli_flushbuffer(tty=%x)\n", (int) tty);
#endif

	if (tty == (struct tty_struct *) NULL)
		return;
	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return;
	if ((portp->brdnr < 0) || (portp->brdnr >= stli_nrbrds))
		return;
	brdp = &stli_brds[portp->brdnr];

	save_flags(flags);
	cli();
	if (tty == stli_txcooktty) {
		stli_txcooktty = (struct tty_struct *) NULL;
		stli_txcooksize = 0;
		stli_txcookrealsize = 0;
	}
	if (test_bit(ST_CMDING, &portp->state)) {
		set_bit(ST_DOFLUSHTX, &portp->state);
	} else {
		ftype = FLUSHTX;
		if (test_bit(ST_DOFLUSHRX, &portp->state)) {
			ftype |= FLUSHRX;
			clear_bit(ST_DOFLUSHRX, &portp->state);
		}
		stli_sendcmd(brdp, portp, A_FLUSH, &ftype, sizeof(unsigned long), 0);
	}
	restore_flags(flags);

	wake_up_interruptible(&tty->write_wait);
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) && tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);
}

/*****************************************************************************/

/*
 *	Generic send command routine. This will send a message to the slave,
 *	of the specified type with the specified argument. Must be very
 *	carefull of data that will be copied out from shared memory -
 *	containing command results. The command completion is all done from
 *	a poll routine that does not have user coontext. Therefore you cannot
 *	copy back directly into user space, or to the kernel stack. This
 *	routine does not sleep, so can be called from anywhere.
 */

static void stli_sendcmd(stlibrd_t *brdp, stliport_t *portp, unsigned long cmd, void *arg, int size, int copyback)
{
	volatile cdkhdr_t	*hdrp;
	volatile cdkctrl_t	*cp;
	volatile unsigned char	*bits;
	unsigned long		flags;

#if DEBUG
	printk("stli_sendcmd(brdp=%x,portp=%x,cmd=%x,arg=%x,size=%d,copyback=%d)\n", (int) brdp, (int) portp, (int) cmd, (int) arg, size, copyback);
#endif

	if (test_bit(ST_CMDING, &portp->state)) {
		printk("STALLION: command already busy, cmd=%x!\n", (int) cmd);
		return;
	}

	save_flags(flags);
	cli();
	EBRDENABLE(brdp);
	cp = &((volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr))->ctrl;
	if (size > 0) {
		memcpy((void *) &(cp->args[0]), arg, size);
		if (copyback) {
			portp->argp = arg;
			portp->argsize = size;
		}
	}
	cp->status = 0;
	cp->cmd = cmd;
	hdrp = (volatile cdkhdr_t *) EBRDGETMEMPTR(brdp, CDK_CDKADDR);
	hdrp->slavereq |= portp->reqbit;
	bits = ((volatile unsigned char *) hdrp) + brdp->slaveoffset + portp->portidx;
	*bits |= portp->portbit;
	set_bit(ST_CMDING, &portp->state);
	EBRDDISABLE(brdp);
	restore_flags(flags);
}

/*****************************************************************************/

/*
 *	Read data from shared memory. This assumes that the shared memory
 *	is enabled and that interrupts are off. Basically we just empty out
 *	the shared memory buffer into the tty buffer. Must be carefull to
 *	handle the case where we fill up the tty buffer, but still have
 *	more chars to unload.
 */

static inline void stli_read(stlibrd_t *brdp, stliport_t *portp)
{
	volatile cdkasyrq_t	*rp;
	volatile char		*shbuf;
	struct tty_struct	*tty;
	unsigned int		head, tail, size;
	unsigned int		len, stlen;

#if DEBUG
	printk("stli_read(brdp=%x,portp=%d)\n", (int) brdp, (int) portp);
#endif

	if (test_bit(ST_RXSTOP, &portp->state))
		return;
	tty = portp->tty;
	if (tty == (struct tty_struct *) NULL)
		return;

	rp = &((volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr))->rxq;
	head = (unsigned int) rp->head;
	if (head != ((unsigned int) rp->head))
		head = (unsigned int) rp->head;
	tail = (unsigned int) rp->tail;
	size = portp->rxsize;
	if (head >= tail) {
		len = head - tail;
		stlen = len;
	} else {
		len = size - (tail - head);
		stlen = size - tail;
	}

	len = MIN(len, (TTY_FLIPBUF_SIZE - tty->flip.count));
	shbuf = (volatile char *) EBRDGETMEMPTR(brdp, portp->rxoffset);

	while (len > 0) {
		stlen = MIN(len, stlen);
		memcpy(tty->flip.char_buf_ptr, (char *) (shbuf + tail), stlen);
		memset(tty->flip.flag_buf_ptr, 0, stlen);
		tty->flip.char_buf_ptr += stlen;
		tty->flip.flag_buf_ptr += stlen;
		tty->flip.count += stlen;

		len -= stlen;
		tail += stlen;
		if (tail >= size) {
			tail = 0;
			stlen = head;
		}
	}
	rp = &((volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr))->rxq;
	rp->tail = tail;

	if (head != tail)
		set_bit(ST_RXING, &portp->state);

	tty_schedule_flip(tty);
}

/*****************************************************************************/

/*
 *	Set up and carry out any delayed commands. There is only a small set
 *	of slave commands that can be done "off-level". So it is not too
 *	difficult to deal with them here.
 */

static inline void stli_dodelaycmd(stliport_t *portp, volatile cdkctrl_t *cp)
{
	int	cmd;

	if (test_bit(ST_DOSIGS, &portp->state)) {
		if (test_bit(ST_DOFLUSHTX, &portp->state) && test_bit(ST_DOFLUSHRX, &portp->state))
			cmd = A_SETSIGNALSF;
		else if (test_bit(ST_DOFLUSHTX, &portp->state))
			cmd = A_SETSIGNALSFTX;
		else if (test_bit(ST_DOFLUSHRX, &portp->state))
			cmd = A_SETSIGNALSFRX;
		else
			cmd = A_SETSIGNALS;
		clear_bit(ST_DOFLUSHTX, &portp->state);
		clear_bit(ST_DOFLUSHRX, &portp->state);
		clear_bit(ST_DOSIGS, &portp->state);
		memcpy((void *) &(cp->args[0]), (void *) &portp->asig, sizeof(asysigs_t));
		cp->status = 0;
		cp->cmd = cmd;
		set_bit(ST_CMDING, &portp->state);
	} else if (test_bit(ST_DOFLUSHTX, &portp->state) || test_bit(ST_DOFLUSHRX, &portp->state)) {
		cmd = ((test_bit(ST_DOFLUSHTX, &portp->state)) ? FLUSHTX : 0);
		cmd |= ((test_bit(ST_DOFLUSHRX, &portp->state)) ? FLUSHRX : 0);
		clear_bit(ST_DOFLUSHTX, &portp->state);
		clear_bit(ST_DOFLUSHRX, &portp->state);
		memcpy((void *) &(cp->args[0]), (void *) &cmd, sizeof(int));
		cp->status = 0;
		cp->cmd = A_FLUSH;
		set_bit(ST_CMDING, &portp->state);
	}
}

/*****************************************************************************/

/*
 *	Host command service checking. This handles commands or messages
 *	coming from the slave to the host. Must have board shared memory
 *	enabled and interrupts off when called. Notice that by servicing the
 *	read data last we don't need to change the shared memory pointer
 *	during processing (which is a slow IO operation).
 */

static inline int stli_hostcmd(stlibrd_t *brdp, int channr)
{
	volatile cdkasy_t	*ap;
	volatile cdkctrl_t	*cp;
	struct tty_struct	*tty;
	asynotify_t		nt;
	stliport_t		*portp;
	unsigned long		oldsigs;
	int			rc, donerx;

#if DEBUG
	printk("stli_hostcmd(brdp=%x,channr=%d)\n", (int) brdp, channr);
#endif

	portp = brdp->ports[(channr - 1)];
	ap = (volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr);
	cp = &ap->ctrl;

/*
 *	Check if we are waiting for an open completion message.
 */
	if (test_bit(ST_OPENING, &portp->state)) {
		rc = (int) cp->openarg;
		if ((cp->open == 0) && (rc != 0)) {
			if (rc > 0)
				rc--;
			cp->openarg = 0;
			portp->rc = rc;
			clear_bit(ST_OPENING, &portp->state);
			wake_up_interruptible(&portp->raw_wait);
		}
	}

/*
 *	Check if we are waiting for a close completion message.
 */
	if (test_bit(ST_CLOSING, &portp->state)) {
		rc = (int) cp->closearg;
		if ((cp->close == 0) && (rc != 0)) {
			if (rc > 0)
				rc--;
			cp->closearg = 0;
			portp->rc = rc;
			clear_bit(ST_CLOSING, &portp->state);
			wake_up_interruptible(&portp->raw_wait);
		}
	}

/*
 *	Check if we are waiting for a command completion message. We may
 *	need to copy out the command results associated with this command.
 */
	if (test_bit(ST_CMDING, &portp->state)) {
		rc = cp->status;
		if ((cp->cmd == 0) && (rc != 0)) {
			if (rc > 0)
				rc--;
			if (portp->argp != (void *) NULL) {
				memcpy(portp->argp, (void *) &(cp->args[0]), portp->argsize);
				portp->argp = (void *) NULL;
			}
			cp->status = 0;
			portp->rc = rc;
			clear_bit(ST_CMDING, &portp->state);
			stli_dodelaycmd(portp, cp);
			wake_up_interruptible(&portp->raw_wait);
		}
	}

/*
 *	Check for any notification messages ready. This includes lots of
 *	different types of events - RX chars ready, RX break received,
 *	TX data low or empty in the slave, modem signals changed state.
 */
	donerx = 0;

	if (ap->notify) {
		nt = ap->changed;
		ap->notify = 0;
		tty = portp->tty;

		if (nt.signal & SG_DCD) {
			oldsigs = portp->sigs;
			portp->sigs = stli_mktiocm(nt.sigvalue);
			clear_bit(ST_GETSIGS, &portp->state);
			if ((portp->sigs & TIOCM_CD) && ((oldsigs & TIOCM_CD) == 0))
				wake_up_interruptible(&portp->open_wait);
			if ((oldsigs & TIOCM_CD) && ((portp->sigs & TIOCM_CD) == 0)) {
				if (! ((portp->flags & ASYNC_CALLOUT_ACTIVE) &&
						(portp->flags & ASYNC_CALLOUT_NOHUP))) {
					if (tty != (struct tty_struct *) NULL)
						queue_task_irq_off(&portp->tqhangup, &tq_scheduler);
				}
			}
		}

		if (nt.data & DT_TXEMPTY)
			clear_bit(ST_TXBUSY, &portp->state);
		if (nt.data & (DT_TXEMPTY | DT_TXLOW)) {
			if (tty != (struct tty_struct *) NULL) {
				if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) && tty->ldisc.write_wakeup)
					(tty->ldisc.write_wakeup)(tty);
				wake_up_interruptible(&tty->write_wait);
			}
		}

		if ((nt.data & DT_RXBREAK) && (portp->rxmarkmsk & BRKINT)) {
			if (tty != (struct tty_struct *) NULL) {
				if (tty->flip.count < TTY_FLIPBUF_SIZE) {
					tty->flip.count++;
					*tty->flip.flag_buf_ptr++ = TTY_BREAK;
					*tty->flip.char_buf_ptr++ = 0;
#ifndef MODULE
					if (portp->flags & ASYNC_SAK)
						do_SAK(tty);
#endif
					tty_schedule_flip(tty);
				}
			}
		}

		if (nt.data & DT_RXBUSY) {
			donerx++;
			stli_read(brdp, portp);
		}
	}

/*
 *	It might seem odd that we are checking for more RX chars here.
 *	But, we need to handle the case where the tty buffer was previously
 *	filled, but we had more characters to pass up. The slave will not
 *	send any more RX notify messages until the RX buffer has been emptied.
 *	But it will leave the service bits on (since the buffer is not empty).
 *	So from here we can try to process more RX chars.
 */
	if ((!donerx) && test_bit(ST_RXING, &portp->state)) {
		clear_bit(ST_RXING, &portp->state);
		stli_read(brdp, portp);
	}

	return(0);
}

/*****************************************************************************/

/*
 *	Driver poll routine. This routine polls the boards in use and passes
 *	messages back up to host when neccesary. This is actually very
 *	CPU efficient, since we will always have the kernel poll clock, it
 *	adds only a few cycles when idle (since board service can be
 *	determined very easily), but when loaded generates no interrupts
 *	(with their expensive associated context change).
 */

static void stli_poll(unsigned long arg)
{
	volatile cdkhdr_t	*hdrp;
	unsigned char		bits[(STL_MAXCHANS / 8) + 1];
	unsigned char		hostreq, slavereq;
	stliport_t		*portp;
	stlibrd_t		*brdp;
	int			bitpos, bitat, bitsize;
	int 			brdnr, channr, nrdevs;

	stli_timerlist.expires = STLI_TIMEOUT;
	add_timer(&stli_timerlist);

/*
 *	Check each board and do any servicing required.
 */
	for (brdnr = 0; (brdnr < stli_nrbrds); brdnr++) {
		brdp = &stli_brds[brdnr];
		if ((brdp->state & BST_STARTED) == 0)
			continue;

		EBRDENABLE(brdp);
		hdrp = (volatile cdkhdr_t *) EBRDGETMEMPTR(brdp, CDK_CDKADDR);
		hostreq = hdrp->hostreq;
		slavereq = hdrp->slavereq;
		bitsize = brdp->bitsize;
		nrdevs = brdp->nrdevs;

/*
 *		Check if slave wants any service. Basically we try to do as
 *		little work as possible here. There are 2 levels of service
 *		bits. So if there is nothing to do we bail early. We check
 *		8 service bits at a time in the inner loop, so we can bypass
 *		the lot if none of them want service.
 */
		if (hostreq) {
			memcpy(&bits[0], (((unsigned char *) hdrp) + brdp->hostoffset), bitsize);

			for (bitpos = 0; (bitpos < bitsize); bitpos++) {
				if (bits[bitpos] == 0)
					continue;
				channr = bitpos * 8;
				for (bitat = 0x1; (channr < nrdevs); channr++, bitat <<= 1) {
					if (bits[bitpos] & bitat) {
						stli_hostcmd(brdp, channr);
					}
				}
			}
		}

/*
 *		Check if any of the out-standing host commands have completed.
 *		It is a bit unfortunate that we need to check stuff that we
 *		initiated!  This ain't pretty, but it needs to be fast.
 */
		if (slavereq) {
			slavereq = 0;
			hostreq = 0;
			memcpy(&bits[0], (((unsigned char *) hdrp) + brdp->slaveoffset), bitsize);

			for (bitpos = 0; (bitpos < bitsize); bitpos++) {
				if (bits[bitpos] == 0)
					continue;
				channr = bitpos * 8;
				for (bitat = 0x1; (channr < nrdevs); channr++, bitat <<= 1) {
					if (bits[bitpos] & bitat) {
						portp = brdp->ports[(channr - 1)];
						if (test_bit(ST_OPENING, &portp->state) ||
								test_bit(ST_CLOSING, &portp->state) ||
								test_bit(ST_CMDING, &portp->state) ||
								test_bit(ST_TXBUSY, &portp->state)) {
							slavereq |= portp->reqbit;
						} else {
							bits[bitpos] &= ~bitat;
							hostreq++;
						}
					}
				}
			}
			hdrp->slavereq = slavereq;
			if (hostreq)
				memcpy((((unsigned char *) hdrp) + brdp->slaveoffset), &bits[0], bitsize);
		}

		EBRDDISABLE(brdp);
	}
}

/*****************************************************************************/

/*
 *	Translate the termios settings into the port setting structure of
 *	the slave.
 */

static void stli_mkasyport(stliport_t *portp, asyport_t *pp, struct termios *tiosp)
{
#if DEBUG
	printk("stli_mkasyport(portp=%x,pp=%x,tiosp=%d)\n", (int) portp, (int) pp, (int) tiosp);
#endif

	memset(pp, 0, sizeof(asyport_t));

/*
 *	Start of by setting the baud, char size, parity and stop bit info.
 */
	pp->baudout = tiosp->c_cflag & CBAUD;
	if (pp->baudout & CBAUDEX) {
		pp->baudout &= ~CBAUDEX;
		if ((pp->baudout < 1) || (pp->baudout > 2))
			tiosp->c_cflag &= ~CBAUDEX;
		else
			pp->baudout += 15;
	}
	pp->baudout = stli_baudrates[pp->baudout];
	if ((tiosp->c_cflag & CBAUD) == B38400) {
		if ((portp->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
			pp->baudout = 57600;
		else if ((portp->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
			pp->baudout = 115200;
		else if ((portp->flags & ASYNC_SPD_MASK) == ASYNC_SPD_CUST)
			pp->baudout = (portp->baud_base / portp->custom_divisor);
	}
	if (pp->baudout > STL_MAXBAUD)
		pp->baudout = STL_MAXBAUD;
	pp->baudin = pp->baudout;

	switch (tiosp->c_cflag & CSIZE) {
	case CS5:
		pp->csize = 5;
		break;
	case CS6:
		pp->csize = 6;
		break;
	case CS7:
		pp->csize = 7;
		break;
	default:
		pp->csize = 8;
		break;
	}

	if (tiosp->c_cflag & CSTOPB)
		pp->stopbs = PT_STOP2;
	else
		pp->stopbs = PT_STOP1;

	if (tiosp->c_cflag & PARENB) {
		if (tiosp->c_cflag & PARODD)
			pp->parity = PT_ODDPARITY;
		else
			pp->parity = PT_EVENPARITY;
	} else {
		pp->parity = PT_NOPARITY;
	}

/*
 *	Set up any flow control options enabled.
 */
	if (tiosp->c_iflag & IXON) {
		pp->flow |= F_IXON;
		if (tiosp->c_iflag & IXANY)
			pp->flow |= F_IXANY;
	}
	if (tiosp->c_cflag & CRTSCTS)
		pp->flow |= (F_RTSFLOW | F_CTSFLOW);

	pp->startin = tiosp->c_cc[VSTART];
	pp->stopin = tiosp->c_cc[VSTOP];
	pp->startout = tiosp->c_cc[VSTART];
	pp->stopout = tiosp->c_cc[VSTOP];

/*
 *	Set up the RX char marking mask with those RX error types we must
 *	catch. We can get the slave to help us out a little here, it will
 *	ignore parity errors and breaks for us, and mark parity errors in
 *	the data stream.
 */
	if (tiosp->c_iflag & IGNPAR)
		pp->iflag |= FI_IGNRXERRS;
	if (tiosp->c_iflag & IGNBRK)
		pp->iflag |= FI_IGNBREAK;

	portp->rxmarkmsk = 0;
	if (tiosp->c_iflag & (INPCK | PARMRK))
		pp->iflag |= FI_1MARKRXERRS;
	if (tiosp->c_iflag & BRKINT)
		portp->rxmarkmsk |= BRKINT;
}

/*****************************************************************************/

/*
 *	Construct a slave signals structure for setting the DTR and RTS
 *	signals as specified.
 */

static void stli_mkasysigs(asysigs_t *sp, int dtr, int rts)
{
#if DEBUG
	printk("stli_mkasysigs(sp=%x,dtr=%d,rts=%d)\n", (int) sp, dtr, rts);
#endif

	memset(sp, 0, sizeof(asysigs_t));
	if (dtr >= 0) {
		sp->signal |= SG_DTR;
		sp->sigvalue |= ((dtr > 0) ? SG_DTR : 0);
	}
	if (rts >= 0) {
		sp->signal |= SG_RTS;
		sp->sigvalue |= ((rts > 0) ? SG_RTS : 0);
	}
}

/*****************************************************************************/

/*
 *	Convert the signals returned from the slave into a local TIOCM type
 *	signals value. We keep them localy in TIOCM format.
 */

static long stli_mktiocm(unsigned long sigvalue)
{
	long	tiocm;

#if DEBUG
	printk("stli_mktiocm(sigvalue=%x)\n", (int) sigvalue);
#endif

	tiocm = 0;
	tiocm |= ((sigvalue & SG_DCD) ? TIOCM_CD : 0);
	tiocm |= ((sigvalue & SG_CTS) ? TIOCM_CTS : 0);
	tiocm |= ((sigvalue & SG_RI) ? TIOCM_RI : 0);
	tiocm |= ((sigvalue & SG_DSR) ? TIOCM_DSR : 0);
	tiocm |= ((sigvalue & SG_DTR) ? TIOCM_DTR : 0);
	tiocm |= ((sigvalue & SG_RTS) ? TIOCM_RTS : 0);
	return(tiocm);
}

/*****************************************************************************/

/*
 *	All panels and ports actually attached have been worked out. All
 *	we need to do here is set up the appropriate per port data structures.
 */

static int stli_initports(stlibrd_t *brdp)
{
	stliport_t	*portp;
	int		i, panelnr, panelport;

#if DEBUG
	printk("stli_initports(brdp=%x)\n", (int) brdp);
#endif

	for (i = 0, panelnr = 0, panelport = 0; (i < brdp->nrports); i++) {
		portp = (stliport_t *) stli_memalloc(sizeof(stliport_t));
		if (portp == (stliport_t *) NULL) {
			printk("STALLION: failed to allocate port structure\n");
			continue;
		}

		memset(portp, 0, sizeof(stliport_t));
		portp->portnr = i;
		portp->brdnr = brdp->brdnr;
		portp->panelnr = panelnr;
		portp->baud_base = STL_BAUDBASE;
		portp->close_delay = STL_CLOSEDELAY;
		portp->closing_wait = 30 * HZ;
		portp->tqhangup.routine = stli_dohangup;
		portp->tqhangup.data = portp;
		portp->normaltermios = stli_deftermios;
		portp->callouttermios = stli_deftermios;
		panelport++;
		if (panelport >= brdp->panels[panelnr]) {
			panelport = 0;
			panelnr++;
		}
		brdp->ports[i] = portp;
	}

	return(0);
}

/*****************************************************************************/

/*
 *	All the following routines are board specific hardware operations.
 */

static void stli_ecpinit(stlibrd_t *brdp)
{
	unsigned long	memconf;

#if DEBUG
	printk("stli_ecpinit(brdp=%d)\n", (int) brdp);
#endif

	outb(ECP_ATSTOP, (brdp->iobase + ECP_ATCONFR));
	udelay(10);
	outb(ECP_ATDISABLE, (brdp->iobase + ECP_ATCONFR));
	udelay(100);

	memconf = (((unsigned long) brdp->membase) & ECP_ATADDRMASK) >> ECP_ATADDRSHFT;
	outb(memconf, (brdp->iobase + ECP_ATMEMAR));
}

/*****************************************************************************/

static void stli_ecpenable(stlibrd_t *brdp)
{	
#if DEBUG
	printk("stli_ecpenable(brdp=%x)\n", (int) brdp);
#endif
	outb(ECP_ATENABLE, (brdp->iobase + ECP_ATCONFR));
}

/*****************************************************************************/

static void stli_ecpdisable(stlibrd_t *brdp)
{	
#if DEBUG
	printk("stli_ecpdisable(brdp=%x)\n", (int) brdp);
#endif
	outb(ECP_ATDISABLE, (brdp->iobase + ECP_ATCONFR));
}

/*****************************************************************************/

static char *stli_ecpgetmemptr(stlibrd_t *brdp, unsigned long offset, int line)
{	
	void		*ptr;
	unsigned char	val;

#if DEBUG
	printk("stli_ecpgetmemptr(brdp=%x,offset=%x)\n", (int) brdp, (int) offset);
#endif

	if (offset > brdp->memsize) {
		printk("STALLION: shared memory pointer=%x out of range at line=%d(%d), brd=%d\n", (int) offset, line, __LINE__, brdp->brdnr);
		ptr = 0;
		val = 0;
	} else {
		ptr = brdp->membase + (offset % ECP_ATPAGESIZE);
		val = (unsigned char) (offset / ECP_ATPAGESIZE);
	}
	outb(val, (brdp->iobase + ECP_ATMEMPR));
	return(ptr);
}

/*****************************************************************************/

static void stli_ecpreset(stlibrd_t *brdp)
{	
#if DEBUG
	printk("stli_ecpreset(brdp=%x)\n", (int) brdp);
#endif

	outb(ECP_ATSTOP, (brdp->iobase + ECP_ATCONFR));
	udelay(10);
	outb(ECP_ATDISABLE, (brdp->iobase + ECP_ATCONFR));
	udelay(500);
}

/*****************************************************************************/

static void stli_ecpintr(stlibrd_t *brdp)
{	
#if DEBUG
	printk("stli_ecpintr(brdp=%x)\n", (int) brdp);
#endif
	outb(0x1, brdp->iobase);
}

/*****************************************************************************/

/*
 *	The following set of functions act on ECP EISA boards.
 */

static void stli_ecpeiinit(stlibrd_t *brdp)
{
	unsigned long	memconf;

#if DEBUG
	printk("stli_ecpeiinit(brdp=%x)\n", (int) brdp);
#endif

	outb(0x1, (brdp->iobase + ECP_EIBRDENAB));
	outb(ECP_EISTOP, (brdp->iobase + ECP_EICONFR));
	udelay(10);
	outb(ECP_EIDISABLE, (brdp->iobase + ECP_EICONFR));
	udelay(500);

	memconf = (((unsigned long) brdp->membase) & ECP_EIADDRMASKL) >> ECP_EIADDRSHFTL;
	outb(memconf, (brdp->iobase + ECP_EIMEMARL));
	memconf = (((unsigned long) brdp->membase) & ECP_EIADDRMASKH) >> ECP_EIADDRSHFTH;
	outb(memconf, (brdp->iobase + ECP_EIMEMARH));
}

/*****************************************************************************/

static void stli_ecpeienable(stlibrd_t *brdp)
{	
	outb(ECP_EIENABLE, (brdp->iobase + ECP_EICONFR));
}

/*****************************************************************************/

static void stli_ecpeidisable(stlibrd_t *brdp)
{	
	outb(ECP_EIDISABLE, (brdp->iobase + ECP_EICONFR));
}

/*****************************************************************************/

static char *stli_ecpeigetmemptr(stlibrd_t *brdp, unsigned long offset, int line)
{	
	void		*ptr;
	unsigned char	val;

#if DEBUG
	printk("stli_ecpeigetmemptr(brdp=%x,offset=%x,line=%d)\n", (int) brdp, (int) offset, line);
#endif

	if (offset > brdp->memsize) {
		printk("STALLION: shared memory pointer=%x out of range at line=%d(%d), brd=%d\n", (int) offset, line, __LINE__, brdp->brdnr);
		ptr = 0;
		val = 0;
	} else {
		ptr = brdp->membase + (offset % ECP_EIPAGESIZE);
		if (offset < ECP_EIPAGESIZE)
			val = ECP_EIENABLE;
		else
			val = ECP_EIENABLE | 0x40;
	}
	outb(val, (brdp->iobase + ECP_EICONFR));
	return(ptr);
}

/*****************************************************************************/

static void stli_ecpeireset(stlibrd_t *brdp)
{	
	outb(ECP_EISTOP, (brdp->iobase + ECP_EICONFR));
	udelay(10);
	outb(ECP_EIDISABLE, (brdp->iobase + ECP_EICONFR));
	udelay(500);
}

/*****************************************************************************/

/*
 *	The following set of functions act on ECP MCA boards.
 */

static void stli_ecpmcenable(stlibrd_t *brdp)
{	
	outb(ECP_MCENABLE, (brdp->iobase + ECP_MCCONFR));
}

/*****************************************************************************/

static void stli_ecpmcdisable(stlibrd_t *brdp)
{	
	outb(ECP_MCDISABLE, (brdp->iobase + ECP_MCCONFR));
}

/*****************************************************************************/

static char *stli_ecpmcgetmemptr(stlibrd_t *brdp, unsigned long offset, int line)
{	
	void		*ptr;
	unsigned char	val;

	if (offset > brdp->memsize) {
		printk("STALLION: shared memory pointer=%x out of range at line=%d(%d), brd=%d\n", (int) offset, line, __LINE__, brdp->brdnr);
		ptr = 0;
		val = 0;
	} else {
		ptr = brdp->membase + (offset % ECP_MCPAGESIZE);
		val = ((unsigned char) (offset / ECP_MCPAGESIZE)) | ECP_MCENABLE;
	}
	outb(val, (brdp->iobase + ECP_MCCONFR));
	return(ptr);
}

/*****************************************************************************/

static void stli_ecpmcreset(stlibrd_t *brdp)
{	
	outb(ECP_MCSTOP, (brdp->iobase + ECP_MCCONFR));
	udelay(10);
	outb(ECP_MCDISABLE, (brdp->iobase + ECP_MCCONFR));
	udelay(500);
}

/*****************************************************************************/

/*
 *	The following routines act on ONboards.
 */

static void stli_onbinit(stlibrd_t *brdp)
{
	unsigned long	memconf;
	int		i;

#if DEBUG
	printk("stli_onbinit(brdp=%d)\n", (int) brdp);
#endif

	outb(ONB_ATSTOP, (brdp->iobase + ONB_ATCONFR));
	udelay(10);
	outb(ONB_ATDISABLE, (brdp->iobase + ONB_ATCONFR));
	for (i = 0; (i < 100); i++)
		udelay(1000);

	memconf = (((unsigned long) brdp->membase) & ONB_ATADDRMASK) >> ONB_ATADDRSHFT;
	outb(memconf, (brdp->iobase + ONB_ATMEMAR));
	outb(0x1, brdp->iobase);
	udelay(1000);
}

/*****************************************************************************/

static void stli_onbenable(stlibrd_t *brdp)
{	
#if DEBUG
	printk("stli_onbenable(brdp=%x)\n", (int) brdp);
#endif
	outb((ONB_ATENABLE | ONB_HIMEMENAB), (brdp->iobase + ONB_ATCONFR));
}

/*****************************************************************************/

static void stli_onbdisable(stlibrd_t *brdp)
{	
#if DEBUG
	printk("stli_onbdisable(brdp=%x)\n", (int) brdp);
#endif
	outb(ONB_ATDISABLE, (brdp->iobase + ONB_ATCONFR));
}

/*****************************************************************************/

static char *stli_onbgetmemptr(stlibrd_t *brdp, unsigned long offset, int line)
{	
	void	*ptr;

#if DEBUG
	printk("stli_onbgetmemptr(brdp=%x,offset=%x)\n", (int) brdp, (int) offset);
#endif

	if (offset > brdp->memsize) {
		printk("STALLION: shared memory pointer=%x out of range at line=%d(%d), brd=%d\n", (int) offset, line, __LINE__, brdp->brdnr);
		ptr = 0;
	} else {
		ptr = brdp->membase + (offset % ONB_ATPAGESIZE);
	}
	return(ptr);
}

/*****************************************************************************/

static void stli_onbreset(stlibrd_t *brdp)
{	
	int	i;

#if DEBUG
	printk("stli_onbreset(brdp=%x)\n", (int) brdp);
#endif

	outb(ONB_ATSTOP, (brdp->iobase + ONB_ATCONFR));
	udelay(10);
	outb(ONB_ATDISABLE, (brdp->iobase + ONB_ATCONFR));
	for (i = 0; (i < 100); i++)
		udelay(1000);
}

/*****************************************************************************/

/*
 *	The following routines act on ONboard EISA.
 */

static void stli_onbeinit(stlibrd_t *brdp)
{
	unsigned long	memconf;
	int		i;

#if DEBUG
	printk("stli_onbeinit(brdp=%d)\n", (int) brdp);
#endif

	outb(0x1, (brdp->iobase + ONB_EIBRDENAB));
	outb(ONB_EISTOP, (brdp->iobase + ONB_EICONFR));
	udelay(10);
	outb(ONB_EIDISABLE, (brdp->iobase + ONB_EICONFR));
	for (i = 0; (i < 100); i++)
		udelay(1000);

	memconf = (((unsigned long) brdp->membase) & ONB_EIADDRMASKL) >> ONB_EIADDRSHFTL;
	outb(memconf, (brdp->iobase + ONB_EIMEMARL));
	memconf = (((unsigned long) brdp->membase) & ONB_EIADDRMASKH) >> ONB_EIADDRSHFTH;
	outb(memconf, (brdp->iobase + ONB_EIMEMARH));
	outb(0x1, brdp->iobase);
	udelay(1000);
}

/*****************************************************************************/

static void stli_onbeenable(stlibrd_t *brdp)
{	
#if DEBUG
	printk("stli_onbeenable(brdp=%x)\n", (int) brdp);
#endif
	outb(ONB_EIENABLE, (brdp->iobase + ONB_EICONFR));
}

/*****************************************************************************/

static void stli_onbedisable(stlibrd_t *brdp)
{	
#if DEBUG
	printk("stli_onbedisable(brdp=%x)\n", (int) brdp);
#endif
	outb(ONB_EIDISABLE, (brdp->iobase + ONB_EICONFR));
}

/*****************************************************************************/

static char *stli_onbegetmemptr(stlibrd_t *brdp, unsigned long offset, int line)
{	
	void		*ptr;
	unsigned char	val;

#if DEBUG
	printk("stli_onbegetmemptr(brdp=%x,offset=%x,line=%d)\n", (int) brdp, (int) offset, line);
#endif

	if (offset > brdp->memsize) {
		printk("STALLION: shared memory pointer=%x out of range at line=%d(%d), brd=%d\n", (int) offset, line, __LINE__, brdp->brdnr);
		ptr = 0;
		val = 0;
	} else {
		ptr = brdp->membase + (offset % ONB_EIPAGESIZE);
		if (offset < ONB_EIPAGESIZE)
			val = ONB_EIENABLE;
		else
			val = ONB_EIENABLE | 0x40;
	}
	outb(val, (brdp->iobase + ONB_EICONFR));
	return(ptr);
}

/*****************************************************************************/

static void stli_onbereset(stlibrd_t *brdp)
{	
	int	i;

#if DEBUG
	printk("stli_onbereset(brdp=%x)\n", (int) brdp);
#endif

	outb(ONB_EISTOP, (brdp->iobase + ONB_EICONFR));
	udelay(10);
	outb(ONB_EIDISABLE, (brdp->iobase + ONB_EICONFR));
	for (i = 0; (i < 100); i++)
		udelay(1000);
}

/*****************************************************************************/

/*
 *	The following routines act on Brumby boards.
 */

static void stli_bbyinit(stlibrd_t *brdp)
{
	int	i;

#if DEBUG
	printk("stli_bbyinit(brdp=%d)\n", (int) brdp);
#endif

	outb(BBY_ATSTOP, (brdp->iobase + BBY_ATCONFR));
	udelay(10);
	outb(0, (brdp->iobase + BBY_ATCONFR));
	for (i = 0; (i < 500); i++)
		udelay(1000);
	outb(0x1, brdp->iobase);
	udelay(1000);
}

/*****************************************************************************/

static char *stli_bbygetmemptr(stlibrd_t *brdp, unsigned long offset, int line)
{	
	void		*ptr;
	unsigned char	val;

#if DEBUG
	printk("stli_bbygetmemptr(brdp=%x,offset=%x)\n", (int) brdp, (int) offset);
#endif

	if (offset > brdp->memsize) {
		printk("STALLION: shared memory pointer=%x out of range at line=%d(%d), brd=%d\n", (int) offset, line, __LINE__, brdp->brdnr);
		ptr = 0;
		val = 0;
	} else {
		ptr = brdp->membase + (offset % BBY_PAGESIZE);
		val = (unsigned char) (offset / BBY_PAGESIZE);
	}
	outb(val, (brdp->iobase + BBY_ATCONFR));
	return(ptr);
}

/*****************************************************************************/

static void stli_bbyreset(stlibrd_t *brdp)
{	
	int	i;

#if DEBUG
	printk("stli_bbyreset(brdp=%x)\n", (int) brdp);
#endif

	outb(BBY_ATSTOP, (brdp->iobase + BBY_ATCONFR));
	udelay(10);
	outb(0, (brdp->iobase + BBY_ATCONFR));
	for (i = 0; (i < 100); i++)
		udelay(1000);
}

/*****************************************************************************/

/*
 *	The following routines act on original old Stallion boards.
 */

static void stli_stalinit(stlibrd_t *brdp)
{
	int	i;

#if DEBUG
	printk("stli_stalinit(brdp=%d)\n", (int) brdp);
#endif

	outb(0x1, brdp->iobase);
	for (i = 0; (i < 100); i++)
		udelay(1000);
}

/*****************************************************************************/

static char *stli_stalgetmemptr(stlibrd_t *brdp, unsigned long offset, int line)
{	
	void	*ptr;

#if DEBUG
	printk("stli_stalgetmemptr(brdp=%x,offset=%x)\n", (int) brdp, (int) offset);
#endif

	if (offset > brdp->memsize) {
		printk("STALLION: shared memory pointer=%x out of range at line=%d(%d), brd=%d\n", (int) offset, line, __LINE__, brdp->brdnr);
		ptr = 0;
	} else {
		ptr = brdp->membase + (offset % STAL_PAGESIZE);
	}
	return(ptr);
}

/*****************************************************************************/

static void stli_stalreset(stlibrd_t *brdp)
{	
	volatile unsigned long	*vecp;
	int			i;

#if DEBUG
	printk("stli_stalreset(brdp=%x)\n", (int) brdp);
#endif

	vecp = (volatile unsigned long *) (brdp->membase + 0x30);
	*vecp = 0xffff0000;
	outb(0, brdp->iobase);
	for (i = 0; (i < 500); i++)
		udelay(1000);
}

/*****************************************************************************/

#if STLI_HIMEMORY
 
#define	PAGE_IOMEM	__pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_ACCESSED | _PAGE_PCD)

/*
 *	To support shared memory addresses outside of the lower 1 Mb region
 *	we will need to pull some tricks with memory management to map the
 *	higher range into kernel virtual address space... Radical stuff...
 */

static void *stli_mapbrdmem(unsigned long physaddr, unsigned int size)
{
	void	*virtaddr;
	int	rc;

#if DEBUG
	printk("stli_mapbrdmem(physaddr=%x,size=%x)\n", (int) physaddr, size);
#endif

	if ((virtaddr = vmalloc(size)) == (char *) NULL) {
		printk("STALLION: failed to allocate virtual address space, size=%x\n", size);
		return((void *) NULL);
	}
	if ((rc = remap_page_range((TASK_SIZE + ((unsigned long) virtaddr)), physaddr, size, PAGE_IOMEM))) {
		printk("STALLION: failed to map phyiscal address=%x, errno=%d\n", (int) physaddr, rc);
		return((void *) NULL);
	}
	return(virtaddr);
}

#endif

/*****************************************************************************/

/*
 *	Try to find an ECP board and initialize it. This handles only ECP
 *	board types.
 */

static int stli_initecp(stlibrd_t *brdp, stlconf_t *confp)
{
	cdkecpsig_t	sig;
	cdkecpsig_t	*sigsp;
	unsigned int	status, nxtid;
	int		panelnr;

#if DEBUG
	printk("stli_initecp(brdp=%x,confp=%x)\n", (int) brdp, (int) confp);
#endif

/*
 *	Based on the specific board type setup the common vars to access
 *	and enable shared memory. Set all board specific information now
 *	as well.
 */
	switch (brdp->brdtype) {
	case BRD_ECP:
		brdp->iobase = confp->ioaddr1;
		brdp->membase = (void *) confp->memaddr;
		brdp->memsize = ECP_MEMSIZE;
		brdp->pagesize = ECP_ATPAGESIZE;
		brdp->init = stli_ecpinit;
		brdp->enable = stli_ecpenable;
		brdp->reenable = stli_ecpenable;
		brdp->disable = stli_ecpdisable;
		brdp->getmemptr = stli_ecpgetmemptr;
		brdp->intr = stli_ecpintr;
		brdp->reset = stli_ecpreset;
		break;

	case BRD_ECPE:
		brdp->iobase = confp->ioaddr1;
		brdp->membase = (void *) confp->memaddr;
		brdp->memsize = ECP_MEMSIZE;
		brdp->pagesize = ECP_EIPAGESIZE;
		brdp->init = stli_ecpeiinit;
		brdp->enable = stli_ecpeienable;
		brdp->reenable = stli_ecpeienable;
		brdp->disable = stli_ecpeidisable;
		brdp->getmemptr = stli_ecpeigetmemptr;
		brdp->intr = stli_ecpintr;
		brdp->reset = stli_ecpeireset;
		break;

	case BRD_ECPMC:
		brdp->memsize = ECP_MEMSIZE;
		brdp->membase = (void *) confp->memaddr;
		brdp->pagesize = ECP_MCPAGESIZE;
		brdp->iobase = confp->ioaddr1;
		brdp->init = NULL;
		brdp->enable = stli_ecpmcenable;
		brdp->reenable = stli_ecpmcenable;
		brdp->disable = stli_ecpmcdisable;
		brdp->getmemptr = stli_ecpmcgetmemptr;
		brdp->intr = stli_ecpintr;
		brdp->reset = stli_ecpmcreset;
		break;

	default:
		return(-EINVAL);
	}

/*
 *	The per-board operations structure is all setup, so now lets go
 *	and get the board operational. Firstly initialize board configuration
 *	registers. Then if we are using the higher 1Mb support then set up
 *	the memory mapping info so we can get at the boards shared memory.
 */
	EBRDINIT(brdp);

#if STLI_HIMEMORY
	if (confp->memaddr > 0x100000) {
		brdp->membase = stli_mapbrdmem(confp->memaddr, brdp->memsize);
		if (brdp->membase == (void *) NULL)
			return(-ENOMEM);
	}
#endif

/*
 *	Now that all specific code is set up, enable the shared memory and
 *	look for the a signature area that will tell us exactly what board
 *	this is, and what is connected to it.
 */
	EBRDENABLE(brdp);
	sigsp = (cdkecpsig_t *) EBRDGETMEMPTR(brdp, CDK_SIGADDR);
	memcpy(&sig, sigsp, sizeof(cdkecpsig_t));
	EBRDDISABLE(brdp);

#if 0
	printk("%s(%d): sig-> magic=%x romver=%x panel=%x,%x,%x,%x,%x,%x,%x,%x\n",
		__FILE__, __LINE__, (int) sig.magic, sig.romver, sig.panelid[0],
		(int) sig.panelid[1], (int) sig.panelid[2], (int) sig.panelid[3],
		(int) sig.panelid[4], (int) sig.panelid[5], (int) sig.panelid[6],
		(int) sig.panelid[7]);
#endif

	if (sig.magic != ECP_MAGIC)
		return(-ENODEV);

/*
 *	Scan through the signature looking at the panels connected to the
 *	board. Calculate the total number of ports as we go.
 */
	for (panelnr = 0, nxtid = 0; (panelnr < STL_MAXPANELS); panelnr++) {
		status = sig.panelid[nxtid];
		if ((status & ECH_PNLIDMASK) != nxtid)
			break;
		if (status & ECH_PNL16PORT) {
			brdp->panels[panelnr] = 16;
			brdp->nrports += 16;
			nxtid += 2;
		} else {
			brdp->panels[panelnr] = 8;
			brdp->nrports += 8;
			nxtid++;
		}
		brdp->nrpanels++;
	}

	request_region(brdp->iobase, ECP_IOSIZE, "serial(ECP)");
	brdp->state |= BST_FOUND;
	return(0);
}

/*****************************************************************************/

/*
 *	Try to find an ONboard, Brumby or Stallion board and initialize it.
 *	This handles only these board types.
 */

static int stli_initonb(stlibrd_t *brdp, stlconf_t *confp)
{
	cdkonbsig_t	sig;
	cdkonbsig_t	*sigsp;
	int		i;

#if DEBUG
	printk("stli_initonb(brdp=%x,confp=%x)\n", (int) brdp, (int) confp);
#endif

/*
 *	Based on the specific board type setup the common vars to access
 *	and enable shared memory. Set all board specific information now
 *	as well.
 */
	switch (brdp->brdtype) {
	case BRD_ONBOARD:
	case BRD_ONBOARD32:
	case BRD_ONBOARD2:
	case BRD_ONBOARD2_32:
	case BRD_ONBOARDRS:
		brdp->iobase = confp->ioaddr1;
		brdp->membase = (void *) confp->memaddr;
		brdp->memsize = ONB_MEMSIZE;
		brdp->pagesize = ONB_ATPAGESIZE;
		brdp->init = stli_onbinit;
		brdp->enable = stli_onbenable;
		brdp->reenable = stli_onbenable;
		brdp->disable = stli_onbdisable;
		brdp->getmemptr = stli_onbgetmemptr;
		brdp->intr = stli_ecpintr;
		brdp->reset = stli_onbreset;
		break;

	case BRD_ONBOARDE:
		brdp->iobase = confp->ioaddr1;
		brdp->membase = (void *) confp->memaddr;
		brdp->memsize = ONB_EIMEMSIZE;
		brdp->pagesize = ONB_EIPAGESIZE;
		brdp->init = stli_onbeinit;
		brdp->enable = stli_onbeenable;
		brdp->reenable = stli_onbeenable;
		brdp->disable = stli_onbedisable;
		brdp->getmemptr = stli_onbegetmemptr;
		brdp->intr = stli_ecpintr;
		brdp->reset = stli_onbereset;
		break;

	case BRD_BRUMBY4:
	case BRD_BRUMBY8:
	case BRD_BRUMBY16:
		brdp->iobase = confp->ioaddr1;
		brdp->membase = (void *) confp->memaddr;
		brdp->memsize = BBY_MEMSIZE;
		brdp->pagesize = BBY_PAGESIZE;
		brdp->init = stli_bbyinit;
		brdp->enable = NULL;
		brdp->reenable = NULL;
		brdp->disable = NULL;
		brdp->getmemptr = stli_bbygetmemptr;
		brdp->intr = stli_ecpintr;
		brdp->reset = stli_bbyreset;
		break;

	case BRD_STALLION:
		brdp->iobase = confp->ioaddr1;
		brdp->membase = (void *) confp->memaddr;
		brdp->memsize = STAL_MEMSIZE;
		brdp->pagesize = STAL_PAGESIZE;
		brdp->init = stli_stalinit;
		brdp->enable = NULL;
		brdp->reenable = NULL;
		brdp->disable = NULL;
		brdp->getmemptr = stli_stalgetmemptr;
		brdp->intr = stli_ecpintr;
		brdp->reset = stli_stalreset;
		break;

	default:
		return(-EINVAL);
	}

/*
 *	The per-board operations structure is all setup, so now lets go
 *	and get the board operational. Firstly initialize board configuration
 *	registers. Then if we are using the higher 1Mb support then set up
 *	the memory mapping info so we can get at the boards shared memory.
 */
	EBRDINIT(brdp);

#if STLI_HIMEMORY
	if (confp->memaddr > 0x100000) {
		brdp->membase = stli_mapbrdmem(confp->memaddr, brdp->memsize);
		if (brdp->membase == (void *) NULL)
			return(-ENOMEM);
	}
#endif

/*
 *	Now that all specific code is set up, enable the shared memory and
 *	look for the a signature area that will tell us exactly what board
 *	this is, and how many ports.
 */
	EBRDENABLE(brdp);
	sigsp = (cdkonbsig_t *) EBRDGETMEMPTR(brdp, CDK_SIGADDR);
	memcpy(&sig, sigsp, sizeof(cdkonbsig_t));
	EBRDDISABLE(brdp);

#if 0
	printk("%s(%d): sig-> magic=%x:%x:%x:%x romver=%x amask=%x:%x:%x\n",
		__FILE__, __LINE__, sig.magic0, sig.magic1, sig.magic2,
		sig.magic3, sig.romver, sig.amask0, sig.amask1, sig.amask2);
#endif

	if ((sig.magic0 != ONB_MAGIC0) || (sig.magic1 != ONB_MAGIC1) ||
			(sig.magic2 != ONB_MAGIC2) || (sig.magic3 != ONB_MAGIC3))
		return(-ENODEV);

/*
 *	Scan through the signature alive mask and calculate how many ports
 *	there are on this board.
 */
	brdp->nrpanels = 1;
	if (sig.amask1) {
		brdp->nrports = 32;
	} else {
		for (i = 0; (i < 16); i++) {
			if (((sig.amask0 << i) & 0x8000) == 0)
				break;
		}
		brdp->nrports = i;
	}

	request_region(brdp->iobase, ONB_IOSIZE, "serial(ONB/BBY)");
	brdp->state |= BST_FOUND;
	return(0);
}

/*****************************************************************************/

/*
 *	Start up a running board. This routine is only called after the
 *	code has been down loaded to the board and is operational. It will
 *	read in the memory map, and get the show on the road...
 */

static int stli_startbrd(stlibrd_t *brdp)
{
	volatile cdkhdr_t	*hdrp;
	volatile cdkmem_t	*memp;
	volatile cdkasy_t	*ap;
	unsigned long		flags;
	stliport_t		*portp;
	int			portnr, nrdevs, i, rc;

#if DEBUG
	printk("stli_startbrd(brdp=%x)\n", (int) brdp);
#endif

	rc = 0;

	save_flags(flags);
	cli();
	EBRDENABLE(brdp);
	hdrp = (volatile cdkhdr_t *) EBRDGETMEMPTR(brdp, CDK_CDKADDR);
	nrdevs = hdrp->nrdevs;

#if 0
	printk("%s(%d): CDK version %d.%d.%d --> nrdevs=%d memp=%x hostp=%x slavep=%x\n",
		 __FILE__, __LINE__, hdrp->ver_release, hdrp->ver_modification,
		 hdrp->ver_fix, nrdevs, (int) hdrp->memp, (int) hdrp->hostp,
		 (int) hdrp->slavep);
#endif

	if (nrdevs < (brdp->nrports + 1)) {
		printk("STALLION: slave failed to allocate memory for all devices, devices=%d\n", nrdevs);
		brdp->nrports = nrdevs - 1;
	}
	brdp->nrdevs = nrdevs;
	brdp->hostoffset = hdrp->hostp - CDK_CDKADDR;
	brdp->slaveoffset = hdrp->slavep - CDK_CDKADDR;
	brdp->bitsize = (nrdevs + 7) / 8;
	memp = (volatile cdkmem_t *) hdrp->memp;
	if (((unsigned long) memp) > brdp->memsize) {
		printk("STALLION: corrupted shared memory region?\n");
		rc = -EIO;
		goto stli_donestartup;
	}
	memp = (volatile cdkmem_t *) EBRDGETMEMPTR(brdp, (unsigned long) memp);
	if (memp->dtype != TYP_ASYNCTRL) {
		printk("STALLION: no slave control device found\n");
		goto stli_donestartup;
	}
	memp++;

/*
 *	Cycle through memory allocation of each port. We are guaranteed to
 *	have all ports inside the first page of slave window, so no need to
 *	change pages while reading memory map.
 */
	for (i = 1, portnr = 0; (i < nrdevs); i++, portnr++, memp++) {
		if (memp->dtype != TYP_ASYNC)
			break;
		portp = brdp->ports[portnr];
		if (portp == (stliport_t *) NULL)
			break;
		portp->devnr = i;
		portp->addr = memp->offset;
		portp->reqbit = (unsigned char) (0x1 << (i * 8 / nrdevs));
		portp->portidx = (unsigned char) (i / 8);
		portp->portbit = (unsigned char) (0x1 << (i % 8));
	}

/*
 *	For each port setup a local copy of the RX and TX buffer offsets
 *	and sizes. We do this separate from the above, because we need to
 *	move the shared memory page...
 */
	for (i = 1, portnr = 0; (i < nrdevs); i++, portnr++) {
		portp = brdp->ports[portnr];
		if (portp == (stliport_t *) NULL)
			break;
		if (portp->addr == 0)
			break;
		ap = (volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr);
		if (ap != (volatile cdkasy_t *) NULL) {
			portp->rxsize = ap->rxq.size;
			portp->txsize = ap->txq.size;
			portp->rxoffset = ap->rxq.offset;
			portp->txoffset = ap->txq.offset;
		}
	}

stli_donestartup:
	EBRDDISABLE(brdp);
	restore_flags(flags);

	if (rc == 0)
		brdp->state |= BST_STARTED;

	if (! stli_timeron) {
		stli_timeron++;
		stli_timerlist.expires = STLI_TIMEOUT;
		add_timer(&stli_timerlist);
	}

	return(rc);
}

/*****************************************************************************/

/*
 *	Scan through all the boards in the configuration and see what we
 *	can find.
 */

static int stli_brdinit()
{
	stlibrd_t	*brdp;
	stlconf_t	*confp;
	int		i, j;

#if DEBUG
	printk("stli_brdinit()\n");
#endif

	if (stli_nrbrds > STL_MAXBRDS)
		return(-EINVAL);

	stli_brds = (stlibrd_t *) stli_memalloc((sizeof(stlibrd_t) * stli_nrbrds));
	if (stli_brds == (stlibrd_t *) NULL) {
		printk("STALLION: failed to allocate board structures\n");
		return(-ENOMEM);
	}
	memset(stli_brds, 0, (sizeof(stlibrd_t) * stli_nrbrds));

	for (i = 0; (i < stli_nrbrds); i++) {
		brdp = &stli_brds[i];
		confp = &stli_brdconf[i];
		brdp->brdnr = i;
		brdp->brdtype = confp->brdtype;

		switch (confp->brdtype) {
		case BRD_ECP:
		case BRD_ECPE:
		case BRD_ECPMC:
			stli_initecp(brdp, confp);
			break;
		case BRD_ONBOARD:
		case BRD_ONBOARDE:
		case BRD_ONBOARD2:
		case BRD_ONBOARD32:
		case BRD_ONBOARD2_32:
		case BRD_ONBOARDRS:
		case BRD_BRUMBY4:
		case BRD_BRUMBY8:
		case BRD_BRUMBY16:
		case BRD_STALLION:
			stli_initonb(brdp, confp);
			break;
		case BRD_EASYIO:
		case BRD_ECH:
		case BRD_ECHMC:
		case BRD_ECHPCI:
			printk("STALLION: %s board type not supported in this driver\n", stli_brdnames[brdp->brdtype]);
			break;
		default:
			printk("STALLION: unit=%d is unknown board type=%d\n", i, confp->brdtype);
			break;
		}

		if ((brdp->state & BST_FOUND) == 0) {
			printk("STALLION: %s board not found, unit=%d io=%x mem=%x\n", stli_brdnames[brdp->brdtype], i, confp->ioaddr1, (int) confp->memaddr);
			continue;
		}

		stli_initports(brdp);
		printk("STALLION: %s found, unit=%d io=%x mem=%x nrpanels=%d nrports=%d\n", stli_brdnames[brdp->brdtype], i, confp->ioaddr1, (int) confp->memaddr, brdp->nrpanels, brdp->nrports);
	}

/*
 *	All found boards are initialized. Now for a little optimization, if
 *	no boards are sharing the "shared memory" regions then we can just
 *	leave them all enabled. This is in fact the usual case.
 */
	stli_shared = 0;
	if (stli_nrbrds > 1) {
		for (i = 0; (i < stli_nrbrds); i++) {
			for (j = i + 1; (j < stli_nrbrds); j++) {
				brdp = &stli_brds[i];
				if ((brdp->membase >= stli_brds[j].membase) &&
						(brdp->membase <= (stli_brds[j].membase + stli_brds[j].memsize - 1))) {
					stli_shared++;
					break;
				}
			}
		}
	}

	if (stli_shared == 0) {
		for (i = 0; (i < stli_nrbrds); i++) {
			brdp = &stli_brds[i];
			if (brdp->state & BST_FOUND) {
				EBRDENABLE(brdp);
				brdp->enable = NULL;
				brdp->disable = NULL;
			}
		}
	}

	return(0);
}

/*****************************************************************************/

/*
 *	Code to handle an "staliomem" read operation. This device is the 
 *	contents of the board shared memory. It is used for down loading
 *	the slave image (and debugging :-)
 */

static int stli_memread(struct inode *ip, struct file *fp, char *buf, int count)
{
	unsigned long	flags;
	void		*memptr;
	stlibrd_t	*brdp;
	int		brdnr, size, n;

#if DEBUG
	printk("stli_memread(ip=%x,fp=%x,buf=%x,count=%d)\n", (int) ip, (int) fp, (int) buf, count);
#endif

	brdnr = MINOR(ip->i_rdev);
	if (brdnr >= stli_nrbrds)
		return(-ENODEV);
	brdp = &stli_brds[brdnr];
	if (brdp->state == 0)
		return(-ENODEV);
	if (fp->f_pos >= brdp->memsize)
		return(0);

	size = MIN(count, (brdp->memsize - fp->f_pos));

	save_flags(flags);
	cli();
	EBRDENABLE(brdp);
	while (size > 0) {
		memptr = (void *) EBRDGETMEMPTR(brdp, fp->f_pos);
		n = MIN(size, (brdp->pagesize - (((unsigned long) fp->f_pos) % brdp->pagesize)));
		memcpy_tofs(buf, memptr, n);
		fp->f_pos += n;
		buf += n;
		size -= n;
	}
	EBRDDISABLE(brdp);
	restore_flags(flags);

	return(count);
}

/*****************************************************************************/

/*
 *	Code to handle an "staliomem" write operation. This device is the 
 *	contents of the board shared memory. It is used for down loading
 *	the slave image (and debugging :-)
 */

static int stli_memwrite(struct inode *ip, struct file *fp, const char *buf, int count)
{
	unsigned long	flags;
	void		*memptr;
	stlibrd_t	*brdp;
	char		*chbuf;
	int		brdnr, size, n;

#if DEBUG
	printk("stli_memwrite(ip=%x,fp=%x,buf=%x,count=%x)\n", (int) ip, (int) fp, (int) buf, count);
#endif

	brdnr = MINOR(ip->i_rdev);
	if (brdnr >= stli_nrbrds)
		return(-ENODEV);
	brdp = &stli_brds[brdnr];
	if (brdp->state == 0)
		return(-ENODEV);
	if (fp->f_pos >= brdp->memsize)
		return(0);

	chbuf = (char *) buf;
	size = MIN(count, (brdp->memsize - fp->f_pos));

	save_flags(flags);
	cli();
	EBRDENABLE(brdp);
	while (size > 0) {
		memptr = (void *) EBRDGETMEMPTR(brdp, fp->f_pos);
		n = MIN(size, (brdp->pagesize - (((unsigned long) fp->f_pos) % brdp->pagesize)));
		memcpy_fromfs(memptr, chbuf, n);
		fp->f_pos += n;
		chbuf += n;
		size -= n;
	}
	EBRDDISABLE(brdp);
	restore_flags(flags);

	return(count);
}

/*****************************************************************************/

/*
 *	The "staliomem" device is also required to do some special operations on
 *	the board. We need to be able to send an interrupt to the board,
 *	reset it, and start/stop it.
 */

static int stli_memioctl(struct inode *ip, struct file *fp, unsigned int cmd, unsigned long arg)
{
	stlibrd_t	*brdp;
	int		brdnr, rc;

#if DEBUG
	printk("stli_memioctl(ip=%x,fp=%x,cmd=%x,arg=%x)\n", (int) ip, (int) fp, cmd, (int) arg);
#endif

	brdnr = MINOR(ip->i_rdev);
	if (brdnr >= stli_nrbrds)
		return(-ENODEV);
	brdp = &stli_brds[brdnr];
	if (brdp->state == 0)
		return(-ENODEV);

	rc = 0;

	switch (cmd) {
	case STL_BINTR:
		EBRDINTR(brdp);
		break;
	case STL_BSTART:
		rc = stli_startbrd(brdp);
		break;
	case STL_BSTOP:
		brdp->state &= ~BST_STARTED;
		break;
	case STL_BRESET:
		brdp->state &= ~BST_STARTED;
		EBRDRESET(brdp);
		if (stli_shared == 0) {
			if (brdp->reenable != NULL)
				(* brdp->reenable)(brdp);
		}
		break;
	default:
		rc = -ENOIOCTLCMD;
		break;
	}

	return(rc);
}

/*****************************************************************************/

int stli_init(void)
{
	printk("%s: version %s\n", stli_drvname, stli_drvversion);

	stli_brdinit();

/*
 *	Allocate a temporary write buffer.
 */
	stli_tmpwritebuf = (char *) stli_memalloc(STLI_TXBUFSIZE);
	if (stli_tmpwritebuf == (char *) NULL)
		printk("STALLION: failed to allocate memory (size=%d)\n", STLI_TXBUFSIZE);
	stli_txcookbuf = (char *) stli_memalloc(STLI_TXBUFSIZE);
	if (stli_txcookbuf == (char *) NULL)
		printk("STALLION: failed to allocate memory (size=%d)\n", STLI_TXBUFSIZE);

/*
 *	Set up a character driver for the shared memory region. We need this
 *	to down load the slave code image. Also it is a useful debugging tool.
 */
	if (register_chrdev(STL_SIOMEMMAJOR, "staliomem", &stli_fsiomem))
		printk("STALLION: failed to register serial memory device\n");

/*
 *	Set up the tty driver structure and register us as a driver.
 *	Also setup the callout tty device.
 */
	memset(&stli_serial, 0, sizeof(struct tty_driver));
	stli_serial.magic = TTY_DRIVER_MAGIC;
	stli_serial.name = stli_serialname;
	stli_serial.major = STL_SERIALMAJOR;
	stli_serial.minor_start = 0;
	stli_serial.num = STL_MAXBRDS * STL_MAXPORTS;
	stli_serial.type = TTY_DRIVER_TYPE_SERIAL;
	stli_serial.subtype = STL_DRVTYPSERIAL;
	stli_serial.init_termios = stli_deftermios;
	stli_serial.flags = TTY_DRIVER_REAL_RAW;
	stli_serial.refcount = &stli_refcount;
	stli_serial.table = stli_ttys;
	stli_serial.termios = stli_termios;
	stli_serial.termios_locked = stli_termioslocked;
	
	stli_serial.open = stli_open;
	stli_serial.close = stli_close;
	stli_serial.write = stli_write;
	stli_serial.put_char = stli_putchar;
	stli_serial.flush_chars = stli_flushchars;
	stli_serial.write_room = stli_writeroom;
	stli_serial.chars_in_buffer = stli_charsinbuffer;
	stli_serial.ioctl = stli_ioctl;
	stli_serial.set_termios = stli_settermios;
	stli_serial.throttle = stli_throttle;
	stli_serial.unthrottle = stli_unthrottle;
	stli_serial.stop = stli_stop;
	stli_serial.start = stli_start;
	stli_serial.hangup = stli_hangup;
	stli_serial.flush_buffer = stli_flushbuffer;

	stli_callout = stli_serial;
	stli_callout.name = stli_calloutname;
	stli_callout.major = STL_CALLOUTMAJOR;
	stli_callout.subtype = STL_DRVTYPCALLOUT;

	if (tty_register_driver(&stli_serial))
		printk("STALLION: failed to register serial driver\n");
	if (tty_register_driver(&stli_callout))
		printk("STALLION: failed to register callout driver\n");

	return 0;
}

/*****************************************************************************/
