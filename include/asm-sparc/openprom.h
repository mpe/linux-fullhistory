/* openprom.h:  Prom structures and defines for access to the OPENBOOT
                prom routines and data areas.

   Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
*/

/* In the v0 interface of the openboot prom we could traverse a nice
   little list structure to figure out where in vm-space the prom had
   mapped itself and how much space it was taking up. In the v2 prom
   interface we have to rely on 'magic' values. :-( Most of the machines
   I have checked on have the prom mapped here all the time though.
*/
#define	LINUX_OPPROM_BEGVM	0xffd00000
#define	LINUX_OPPROM_ENDVM	0xfff00000

#define	LINUX_OPPROM_MAGIC      0x10010407

/* The device functions structure for the v0 prom. Nice and neat, open,
   close, read & write divied up between net + block + char devices. We
   also have a seek routine only usable for block devices. The divide
   and conquer strategy of this struct becomes unnecessary for v2.

   V0 device names are limited to two characters, 'sd' for scsi-disk,
   'le' for local-ethernet, etc. Note that it is technically possible
   to boot a kernel off of a tape drive and use the tape as the root
   partition! In order to do this you have to have 'magic' formatted
   tapes from Sun supposedly :-)
*/

struct linux_dev_v0_funcs {
	int	(*v0_devopen)(char *device_str);
	int	(*v0_devclose)(int dev_desc);
	int	(*v0_rdblkdev)(int dev_desc, int num_blks, int blk_st, caddr_t buf);
	int	(*v0_wrblkdev)(int dev_desc, int num_blks, int blk_st, caddr_t buf);
	int	(*v0_wrnetdev)(int dev_desc, int num_bytes, caddr_t buf);
	int	(*v0_rdnetdev)(int dev_desc, int num_bytes, caddr_t buf);
	int	(*v0_rdchardev)(int dev_desc, int num_bytes, int dummy, caddr_t buf);
	int	(*v0_wrchardev)(int dev_desc, int num_bytes, int dummy, caddr_t buf);
	int	(*v0_seekdev)(int dev_desc, long logical_offst, int from);
};

/* The OpenBoot Prom device operations for version-2 interfaces are both
   good and bad. They now allow you to address ANY device whatsoever
   that is in the machine via these funny "device paths". They look like
   this:

     "/sbus/esp@0,0xf004002c/sd@3,1"

   You can basically reference any device on the machine this way, and
   you pass this string to the v2 dev_ops. Producing these strings all
   the time can be a pain in the rear after a while. Why v2 has memory
   allocations in here are beyond me. Perhaps they figure that if you
   are going to use only the prom's device drivers then your memory
   management is either non-existant or pretty sad. :-)
*/

struct linux_dev_v2_funcs {
	int	(*v2_aieee)(int d);	/* figure this out later... */

	/* "dumb" prom memory management routines, probably
	    only safe to use for mapping device address spaces...
        */

	caddr_t	(*v2_dumb_mem_alloc)(caddr_t va, unsigned sz);
	void	(*v2_dumb_mem_free)(caddr_t va, unsigned sz);

	/* "dumb" mmap() munmap(), copy on write? whats that? */
	caddr_t	(*v2_dumb_mmap)(caddr_t virta, int asi, unsigned prot, unsigned sz);
	void	(*v2_dumb_munmap)(caddr_t virta, unsigned size);

	/* Basic Operations, self-explanatory */
	int	(*v2_dev_open)(char *devpath);
	void	(*v2_dev_close)(int d);
	int	(*v2_dev_read)(int d, caddr_t buf, int nbytes);
	int	(*v2_dev_write)(int d, caddr_t buf, int nbytes);
	void	(*v2_dev_seek)(int d, int hi, int lo);

        /* huh? */
	void	(*v2_wheee2)();
	void	(*v2_wheee3)();
};

/* Just like the device ops, they slightly screwed up the mem-list
   from v0 to v2. Probably easier on the prom-writer dude, sucks for
   us though. See above comment about prom-vm mapped address space
   magic numbers. :-(
*/

struct linux_mlist_v0 {
	struct	linux_mlist_v0 *theres_more;
	caddr_t	start_adr;
	unsigned num_bytes;
};

/* The linux_mlist_v0's are pointer by this structure. One list
   per description. This means one list for total physical memory,
   one for prom's address mapping, and one for physical mem left after
   the kernel is loaded.
 */
struct linux_mem_v0 {
	struct	linux_mlist_v0 **v0_totphys;	/* all of physical */
	struct	linux_mlist_v0 **v0_prommap;	/* addresses map'd by prom */
	struct  linux_mlist_v0 **v0_available;	/* what phys. is left over */
};

/* Arguements sent to the kernel from the boot prompt. */

struct linux_arguments_v0 {
	char	*argv[8];		/* argv format for boot string */
	char	args[100];		/* string space */
	char	boot_dev[2];		/* e.g., "sd" for `b sd(...' */
	int	boot_dev_ctrl;		/* controller # */
	int	boot_dev_unit;		/* unit # */
	int	dev_partition;		/* partition # */
	char	*kernel_file_name;	/* kernel to boot, e.g., "vmunix" */
	void	*aieee1;		/* give me some time  :> */
};

/* Prom version-2 gives us the raw strings for boot arguments and
   boot device path. We also get the stdin and stdout file pseudo
   descriptors for use with the mungy v2 device functions.
*/
struct linux_bootargs_v2 {
	char	**bootpath;		/* V2: Path to boot device */
	char	**bootargs;		/* V2: Boot args */
	int	*fd_stdin;		/* V2: Stdin descriptor */
	int	*fd_stdout;		/* V2: Stdout descriptor */
};

/* This is the actual Prom Vector from which everything else is accessed
   via struct and function pointers, etc. The prom when it loads us into
   memory plops a pointer to this master structure in register %o0 before
   it jumps to the kernel start address. I will update this soon to cover
   the v3 semantics (cpu_start, cpu_stop and other SMP fun things). :-)
*/
struct promvec {
	/* Version numbers. */
	u_int	pv_magic;		/* Magic number */
	u_int	pv_romvec_vers;		/* interface version (0, 2) */
	u_int	pv_plugin_vers;		/* ??? */
	u_int	pv_printrev;		/* PROM rev # (* 10, e.g 1.9 = 19) */

	/* Version 0 memory descriptors (see below). */
	struct	v0mem pv_v0mem;		/* V0: Memory description lists. */

	/* Node operations (see below). */
	struct	nodeops *pv_nodeops;	/* node functions */

	char	**pv_bootstr;		/* Boot command, eg sd(0,0,0)vmunix */

	struct	v0devops pv_v0devops;	/* V0: device ops */

	/*
	 * PROMDEV_* cookies.  I fear these may vanish in lieu of fd0/fd1
	 * (see below) in future PROMs, but for now they work fine.
	 */
	char	*pv_stdin;		/* stdin cookie */
	char	*pv_stdout;		/* stdout cookie */
#define	PROMDEV_KBD	0		/* input from keyboard */
#define	PROMDEV_SCREEN	0		/* output to screen */
#define	PROMDEV_TTYA	1		/* in/out to ttya */
#define	PROMDEV_TTYB	2		/* in/out to ttyb */

	/* Blocking getchar/putchar.  NOT REENTRANT! (grr) */
	int	(*pv_getchar)(void);
	void	(*pv_putchar)(int ch);

	/* Non-blocking variants that return -1 on error. */
	int	(*pv_nbgetchar)(void);
	int	(*pv_nbputchar)(int ch);

	/* Put counted string (can be very slow). */
	void	(*pv_putstr)(char *str, int len);

	/* Miscellany. */
	void	(*pv_reboot)(char *bootstr);
	void	(*pv_printf)(const char *fmt, ...);
	void	(*pv_abort)(void);	/* L1-A abort */
	int	*pv_ticks;		/* Ticks since last reset */
	__dead void (*pv_halt)(void);	/* Halt! */
	void	(**pv_synchook)(void);	/* "sync" command hook */

	/*
	 * This eval's a FORTH string.  Unfortunately, its interface
	 * changed between V0 and V2, which gave us much pain.
	 */
	union {
		void	(*v0_eval)(int len, char *str);
		void	(*v2_eval)(char *str);
	} pv_fortheval;

	struct	v0bootargs **pv_v0bootargs;	/* V0: Boot args */

	/* Extract Ethernet address from network device. */
	u_int	(*pv_enaddr)(int d, char *enaddr);

	struct	v2bootargs pv_v2bootargs;	/* V2: Boot args + std in/out */
	struct	v2devops pv_v2devops;	/* V2: device operations */

	int	pv_spare[15];

	/*
	 * The following is machine-dependent.
	 *
	 * The sun4c needs a PROM function to set a PMEG for another
	 * context, so that the kernel can map itself in all contexts.
	 * It is not possible simply to set the context register, because
	 * contexts 1 through N may have invalid translations for the
	 * current program counter.  The hardware has a mode in which
	 * all memory references go to the PROM, so the PROM can do it
	 * easily.
	 */
	void	(*pv_setctxt)(int ctxt, caddr_t va, int pmeg);
};

/*
 * In addition to the global stuff defined in the PROM vectors above,
 * the PROM has quite a collection of `nodes'.  A node is described by
 * an integer---these seem to be internal pointers, actually---and the
 * nodes are arranged into an N-ary tree.  Each node implements a fixed
 * set of functions, as described below.  The first two deal with the tree
 * structure, allowing traversals in either breadth- or depth-first fashion.
 * The rest deal with `properties'.
 *
 * A node property is simply a name/value pair.  The names are C strings
 * (NUL-terminated); the values are arbitrary byte strings (counted strings).
 * Many values are really just C strings.  Sometimes these are NUL-terminated,
 * sometimes not, depending on the the interface version; v0 seems to
 * terminate and v2 not.  Many others are simply integers stored as four
 * bytes in machine order: you just get them and go.  The third popular
 * format is an `address', which is made up of one or more sets of three
 * integers as defined below.
 *
 * N.B.: for the `next' functions, next(0) = first, and next(last) = 0.
 * Whoever designed this part had good taste.  On the other hand, these
 * operation vectors are global, rather than per-node, yet the pointers
 * are not in the openprom vectors but rather found by indirection from
 * there.  So the taste balances out.
 */
struct openprom_addr {
	int	oa_space;		/* address space (may be relative) */
	u_int	oa_base;		/* address within space */
	u_int	oa_size;		/* extent (number of bytes) */
};

struct nodeops {
	/*
	 * Tree traversal.
	 */
	int	(*no_nextnode)(int node);	/* next(node) */
	int	(*no_child)(int node);	/* first child */

	/*
	 * Property functions.  Proper use of getprop requires calling
	 * proplen first to make sure it fits.  Kind of a pain, but no
	 * doubt more convenient for the PROM coder.
	 */
	int	(*no_proplen)(int node, caddr_t name);
	int	(*no_getprop)(int node, caddr_t name, caddr_t val);
	int	(*no_setprop)(int node, caddr_t name, caddr_t val, int len);
	caddr_t	(*no_nextprop)(int node, caddr_t name);
};
