/*
 * sysctl.h: General linux system control interface
 *
 * Begun 24 March 1995, Stephen Tweedie
 */

#include <linux/lists.h>

#ifndef _LINUX_SYSCTL_H
#define _LINUX_SYSCTL_H

#define CTL_MAXNAME 10

struct __sysctl_args {
	int *name;
	int nlen;
	void *oldval;
	size_t *oldlenp;
	void *newval;
	size_t newlen;
	unsigned long __unused[4];
};

/* Define sysctl names first */

/* Top-level names: */

/* For internal pattern-matching use only: */
#ifdef __KERNEL__
#define CTL_ANY		-1	/* Matches any name */
#define CTL_NONE		0
#endif

#define CTL_KERN	1	/* General kernel info and control */
#define CTL_VM		2	/* VM management */
#define CTL_NET		3	/* Networking */
#define CTL_PROC	4	/* Process info */
#define CTL_FS		5	/* Filesystems */
#define CTL_DEBUG	6	/* Debugging */
#define CTL_DEV		7	/* Devices */
#define CTL_MAXID	8

/* CTL_KERN names: */
#define KERN_OSTYPE	1	/* string: system version */
#define KERN_OSRELEASE	2	/* string: system release */
#define KERN_OSREV	3	/* int: system revision */
#define KERN_VERSION	4	/* string: compile time info */
#define KERN_SECUREMASK 5	/* struct: maximum rights mask */
#define KERN_PROF 	6	/* table: profiling information */
#define KERN_NODENAME   7
#define KERN_DOMAINNAME 8
#define KERN_NRINODE	9
#define KERN_MAXINODE	10
#define KERN_NRFILE	11
#define KERN_MAXFILE	12
#define KERN_MAXID	13
#define KERN_SECURELVL	14	/* int: system security level */
#define KERN_PANIC	15	/* int: panic timeout */
#define KERN_REALROOTDEV 16	/* real root device to mount after initrd */
#define KERN_NFSRNAME	17	/* NFS root name */
#define KERN_NFSRADDRS	18	/* NFS root addresses */
#define KERN_JAVA_INTERPRETER 19 /* path to Java(tm) interpreter */
#define KERN_JAVA_APPLETVIEWER 20 /* path to Java(tm) appletviewer */

/* CTL_VM names: */
#define VM_SWAPCTL	1	/* struct: Set vm swapping control */
#define VM_KSWAPD	2	/* struct: control background pageout */
#define VM_FREEPG	3	/* struct: Set free page thresholds */
#define VM_BDFLUSH	4	/* struct: Control buffer cache flushing */
#define VM_MAXID	5

/* CTL_NET names: */
#define NET_CORE        1
#define NET_ETHER       2
#define NET_802         3
#define NET_UNIX        4
#define NET_IPV4        5
#define NET_IPX         6
#define NET_ATALK       7
#define NET_NETROM      8
#define NET_AX25        9
#define NET_BRIDGE	10

/* /proc/sys/net/core */

/* /proc/sys/net/ethernet */

/* /proc/sys/net/802 */

/* /proc/sys/net/unix */

/* /proc/sys/net/ipv4 */
#define NET_IPV4_ARP_RES_TIME           1
#define NET_IPV4_ARP_DEAD_RES_TIME      2
#define NET_IPV4_ARP_MAX_TRIES          3
#define NET_IPV4_ARP_TIMEOUT            4
#define NET_IPV4_ARP_CHECK_INTERVAL     5
#define NET_IPV4_ARP_CONFIRM_INTERVAL   6
#define NET_IPV4_ARP_CONFIRM_TIMEOUT	7

/* /proc/sys/net/ipx */

/* /proc/sys/net/appletalk */

/* /proc/sys/net/netrom */

/* /proc/sys/net/ax25 */

/* CTL_PROC names: */

/* CTL_FS names: */

/* CTL_DEBUG names: */

/* CTL_DEV names: */

#ifdef __KERNEL__

extern asmlinkage int sys_sysctl(struct __sysctl_args *);
extern void init_sysctl(void);

typedef struct ctl_table ctl_table;

typedef int ctl_handler (ctl_table *table, int *name, int nlen,
			 void *oldval, size_t *oldlenp,
			 void *newval, size_t newlen, 
			 void **context);

typedef int proc_handler (ctl_table *ctl, int write, struct file * filp,
			  void *buffer, size_t *lenp);

extern int proc_dostring(ctl_table *, int, struct file *,
			 void *, size_t *);
extern int proc_dointvec(ctl_table *, int, struct file *,
			 void *, size_t *);
extern int proc_dointvec_minmax(ctl_table *, int, struct file *,
				void *, size_t *);

extern int do_sysctl (int *name, int nlen,
		      void *oldval, size_t *oldlenp,
		      void *newval, size_t newlen);

extern int do_sysctl_strategy (ctl_table *table, 
			       int *name, int nlen,
			       void *oldval, size_t *oldlenp,
			       void *newval, size_t newlen, void ** context);

extern ctl_handler sysctl_string;
extern ctl_handler sysctl_intvec;

extern int do_string (
	void *oldval, size_t *oldlenp, void *newval, size_t newlen,
	int rdwr, char *data, size_t max);
extern int do_int (
	void *oldval, size_t *oldlenp, void *newval, size_t newlen,
	int rdwr, int *data);
extern int do_struct (
	void *oldval, size_t *oldlenp, void *newval, size_t newlen,
	int rdwr, void *data, size_t len);


/*
 * Register a set of sysctl names by calling register_sysctl_table
 * with an initialised array of ctl_table's.  An entry with zero
 * ctl_name terminates the table.  table->de will be set up by the
 * registration and need not be initialised in advance.
 *
 * sysctl names can be mirrored automatically under /proc/sys.  The
 * procname supplied controls /proc naming.
 *
 * The table's mode will be honoured both for sys_sysctl(2) and
 * proc-fs access.
 *
 * Leaf nodes in the sysctl tree will be represented by a single file
 * under /proc; non-leaf nodes will be represented by directories.  A
 * null procname disables /proc mirroring at this node.
 * 
 * sysctl(2) can automatically manage read and write requests through
 * the sysctl table.  The data and maxlen fields of the ctl_table
 * struct enable minimal validation of the values being written to be
 * performed, and the mode field allows minimal authentication.
 * 
 * More sophisticated management can be enabled by the provision of a
 * strategy routine with the table entry.  This will be called before
 * any automatic read or write of the data is performed.
 * 
 * The strategy routine may return:
 * <0: Error occurred (error is passed to user process)
 * 0:  OK - proceed with automatic read or write.
 * >0: OK - read or write has been done by the strategy routine, so 
 *     return immediately.
 * 
 * There must be a proc_handler routine for any terminal nodes
 * mirrored under /proc/sys (non-terminals are handled by a built-in
 * directory handler).  Several default handlers are available to
 * cover common cases.
 */

/* A sysctl table is an array of struct ctl_table: */
struct ctl_table 
{
	int ctl_name;			/* Binary ID */
	const char *procname;		/* Text ID for /proc/sys, or zero */
	void *data;
	int maxlen;
	mode_t mode;
	ctl_table *child;
	proc_handler *proc_handler;	/* Callback for text formatting */
	ctl_handler *strategy;		/* Callback function for all r/w */
	struct proc_dir_entry *de;	/* /proc control block */
	void *extra1;
	void *extra2;
};

/* struct ctl_table_header is used to maintain dynamic lists of
   ctl_table trees. */
struct ctl_table_header
{
	ctl_table *ctl_table;
	DLNODE(struct ctl_table_header) ctl_entry;	
};

struct ctl_table_header * register_sysctl_table(ctl_table * table, 
						int insert_at_head);
void unregister_sysctl_table(struct ctl_table_header * table);

#else /* __KERNEL__ */

#endif /* __KERNEL__ */

#endif /* _LINUX_SYSCTL_H */
