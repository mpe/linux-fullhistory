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
#define CTL_NONE	0
#endif

enum
{
	CTL_KERN=1,		/* General kernel info and control */
	CTL_VM,			/* VM management */
	CTL_NET,		/* Networking */
	CTL_PROC,		/* Process info */
	CTL_FS,			/* Filesystems */
	CTL_DEBUG,		/* Debugging */
	CTL_DEV,		/* Devices */
};


/* CTL_KERN names: */
enum
{
	KERN_OSTYPE=1,		/* string: system version */
	KERN_OSRELEASE,		/* string: system release */
	KERN_OSREV,		/* int: system revision */
	KERN_VERSION,		/* string: compile time info */
	KERN_SECUREMASK,	/* struct: maximum rights mask */
	KERN_PROF,		/* table: profiling information */
	KERN_NODENAME,
	KERN_DOMAINNAME,
	KERN_NRINODE,
	KERN_MAXINODE,
	KERN_NRFILE,
	KERN_MAXFILE,
	KERN_SECURELVL,		/* int: system security level */
	KERN_PANIC,		/* int: panic timeout */
	KERN_REALROOTDEV,	/* real root device to mount after initrd */
	KERN_NFSRNAME,		/* NFS root name */
	KERN_NFSRADDRS,		/* NFS root addresses */
	KERN_JAVA_INTERPRETER,	/* path to Java(tm) interpreter */
	KERN_JAVA_APPLETVIEWER,	/* path to Java(tm) appletviewer */
	KERN_SPARC_REBOOT,	/* reboot command on Sparc */
	KERN_CTLALTDEL,		/* int: allow ctl-alt-del to reboot */
	KERN_PRINTK,            /* sturct: control printk logging parameters */
	KERN_NAMETRANS,		/* Name translation */
	KERN_STATINODE,
	KERN_DENTRY,		/* dentry statistics */
	KERN_MODPROBE,
	KERN_KMOD_UNLOAD_DELAY
};


/* CTL_VM names: */
enum
{
	VM_SWAPCTL=1,		/* struct: Set vm swapping control */
	VM_SWAPOUT,		/* int: Background pageout interval */
	VM_FREEPG,		/* struct: Set free page thresholds */
	VM_BDFLUSH,		/* struct: Control buffer cache flushing */
	VM_OVERCOMMIT_MEMORY,	/* Turn off the virtual memory safety limit */
	VM_BUFFERMEM,		/* struct: Set buffer memory thresholds */
	VM_PAGECACHE		/* struct: Set cache memory thresholds */
};


/* CTL_NET names: */
enum
{
	NET_CORE=1,
	NET_ETHER,
	NET_802,
	NET_UNIX,
	NET_IPV4,
	NET_IPX,
	NET_ATALK,
	NET_NETROM,
	NET_AX25,
	NET_BRIDGE,
	NET_IPV6,
	NET_ROSE,
	NET_X25,
	NET_TR,
	NET_DECNET
};


/* /proc/sys/net/core */
enum
{
	NET_CORE_WMEM_MAX=1,
	NET_CORE_RMEM_MAX,
	NET_CORE_WMEM_DEFAULT,
	NET_CORE_RMEM_DEFAULT,
	NET_CORE_DESTROY_DELAY,
	NET_CORE_MAX_BACKLOG,
	NET_CORE_FASTROUTE,
	NET_CORE_MSG_COST,
	NET_CORE_MSG_BURST,
	NET_CORE_OPTMEM_MAX,
};

/* /proc/sys/net/ethernet */

/* /proc/sys/net/802 */

/* /proc/sys/net/unix */

enum
{
	NET_UNIX_DESTROY_DELAY=1,
	NET_UNIX_DELETE_DELAY,
};

/* /proc/sys/net/ipv4 */
enum
{
	/* v2.0 compatibile variables */
	NET_IPV4_FORWARD = 8,
	NET_IPV4_DYNADDR = 9,

	NET_IPV4_CONF = 16,
	NET_IPV4_NEIGH = 17,
	NET_IPV4_ROUTE = 18,
	NET_IPV4_FIB_HASH = 19,

	NET_IPV4_TCP_HOE_RETRANSMITS=32,
	NET_IPV4_TCP_TIMESTAMPS,
	NET_IPV4_TCP_WINDOW_SCALING,
	NET_IPV4_TCP_SACK,
	NET_IPV4_DEFAULT_TTL,
	NET_IPV4_AUTOCONFIG,
	NET_IPV4_NO_PMTU_DISC,
	NET_IPV4_TCP_SYN_RETRIES,
	NET_IPV4_IPFRAG_HIGH_THRESH,
	NET_IPV4_IPFRAG_LOW_THRESH,
	NET_IPV4_IPFRAG_TIME,
	NET_IPV4_TCP_MAX_KA_PROBES,
	NET_IPV4_TCP_KEEPALIVE_TIME,
	NET_IPV4_TCP_KEEPALIVE_PROBES,
	NET_IPV4_TCP_RETRIES1,
	NET_IPV4_TCP_RETRIES2,
	NET_IPV4_TCP_MAX_DELAY_ACKS,
	NET_IPV4_TCP_FIN_TIMEOUT,
	NET_IPV4_IP_MASQ_DEBUG,
	NET_TCP_SYNCOOKIES,
	NET_TCP_STDURG,
	NET_TCP_RFC1337,
	NET_TCP_SYN_TAILDROP,
	NET_TCP_MAX_SYN_BACKLOG,
	NET_IPV4_LOCAL_PORT_RANGE,
	NET_IPV4_ICMP_ECHO_IGNORE_ALL,
	NET_IPV4_ICMP_ECHO_IGNORE_BROADCASTS,
	NET_IPV4_ICMP_SOURCEQUENCH_RATE,
	NET_IPV4_ICMP_DESTUNREACH_RATE,
	NET_IPV4_ICMP_TIMEEXCEED_RATE,
	NET_IPV4_ICMP_PARAMPROB_RATE,
	NET_IPV4_ICMP_ECHOREPLY_RATE,
};

enum {
	NET_IPV4_ROUTE_FLUSH = 1,
	NET_IPV4_ROUTE_MIN_DELAY,
	NET_IPV4_ROUTE_MAX_DELAY,
	NET_IPV4_ROUTE_GC_THRESH,
	NET_IPV4_ROUTE_MAX_SIZE,
	NET_IPV4_ROUTE_GC_MIN_INTERVAL,
	NET_IPV4_ROUTE_GC_TIMEOUT,
	NET_IPV4_ROUTE_GC_INTERVAL,
	NET_IPV4_ROUTE_REDIRECT_LOAD,
	NET_IPV4_ROUTE_REDIRECT_NUMBER,
	NET_IPV4_ROUTE_REDIRECT_SILENCE,
	NET_IPV4_ROUTE_ERROR_COST,
	NET_IPV4_ROUTE_ERROR_BURST,
	NET_IPV4_ROUTE_GC_ELASTICITY,
};

enum
{
	NET_PROTO_CONF_ALL = -2,
	NET_PROTO_CONF_DEFAULT = -3,

	/* And device ifindices ... */
};

enum
{
	NET_IPV4_CONF_FORWARDING = 1,
	NET_IPV4_CONF_MC_FORWARDING,
	NET_IPV4_CONF_PROXY_ARP,
	NET_IPV4_CONF_ACCEPT_REDIRECTS,
	NET_IPV4_CONF_SECURE_REDIRECTS,
	NET_IPV4_CONF_SEND_REDIRECTS,
	NET_IPV4_CONF_SHARED_MEDIA,
	NET_IPV4_CONF_RP_FILTER,
	NET_IPV4_CONF_ACCEPT_SOURCE_ROUTE,
	NET_IPV4_CONF_BOOTP_RELAY,
	NET_IPV4_CONF_LOG_MARTIANS,
};

/* /proc/sys/net/ipv6 */
enum {
	NET_IPV6_CONF = 16,
	NET_IPV6_NEIGH = 17,
	NET_IPV6_ROUTE = 18,
};

enum {
	NET_IPV6_ROUTE_FLUSH = 1,
	NET_IPV6_ROUTE_GC_THRESH,
	NET_IPV6_ROUTE_MAX_SIZE,
	NET_IPV6_ROUTE_GC_MIN_INTERVAL,
	NET_IPV6_ROUTE_GC_TIMEOUT,
	NET_IPV6_ROUTE_GC_INTERVAL,
	NET_IPV6_ROUTE_GC_ELASTICITY,
};

enum {
	NET_IPV6_FORWARDING = 1,
	NET_IPV6_HOP_LIMIT,
	NET_IPV6_MTU,
	NET_IPV6_ACCEPT_RA,
	NET_IPV6_ACCEPT_REDIRECTS,
	NET_IPV6_AUTOCONF,
	NET_IPV6_DAD_TRANSMITS,
	NET_IPV6_RTR_SOLICITS,
	NET_IPV6_RTR_SOLICIT_INTERVAL,
	NET_IPV6_RTR_SOLICIT_DELAY,
};

/* /proc/sys/net/<protocol>/neigh/<dev> */
enum {
	NET_NEIGH_MCAST_SOLICIT=1,
	NET_NEIGH_UCAST_SOLICIT,
	NET_NEIGH_APP_SOLICIT,
	NET_NEIGH_RETRANS_TIME,
	NET_NEIGH_REACHABLE_TIME,
	NET_NEIGH_DELAY_PROBE_TIME,
	NET_NEIGH_GC_STALE_TIME,
	NET_NEIGH_UNRES_QLEN,
	NET_NEIGH_PROXY_QLEN,
	NET_NEIGH_ANYCAST_DELAY,
	NET_NEIGH_PROXY_DELAY,
	NET_NEIGH_LOCKTIME,
	NET_NEIGH_GC_INTERVAL,
	NET_NEIGH_GC_THRESH1,
	NET_NEIGH_GC_THRESH2,
	NET_NEIGH_GC_THRESH3
};

/* /proc/sys/net/ipx */


/* /proc/sys/net/appletalk */
enum {
	NET_ATALK_AARP_EXPIRY_TIME = 1,
	NET_ATALK_AARP_TICK_TIME,
	NET_ATALK_AARP_RETRANSMIT_LIMIT,
	NET_ATALK_AARP_RESOLVE_TIME,
};


/* /proc/sys/net/netrom */
enum {
	NET_NETROM_DEFAULT_PATH_QUALITY = 1,
	NET_NETROM_OBSOLESCENCE_COUNT_INITIALISER,
	NET_NETROM_NETWORK_TTL_INITIALISER,
	NET_NETROM_TRANSPORT_TIMEOUT,
	NET_NETROM_TRANSPORT_MAXIMUM_TRIES,
	NET_NETROM_TRANSPORT_ACKNOWLEDGE_DELAY,
	NET_NETROM_TRANSPORT_BUSY_DELAY,
	NET_NETROM_TRANSPORT_REQUESTED_WINDOW_SIZE,
	NET_NETROM_TRANSPORT_NO_ACTIVITY_TIMEOUT,
	NET_NETROM_ROUTING_CONTROL,
	NET_NETROM_LINK_FAILS_COUNT
};

/* /proc/sys/net/ax25 */
enum {
	NET_AX25_IP_DEFAULT_MODE = 1,
	NET_AX25_DEFAULT_MODE,
	NET_AX25_BACKOFF_TYPE,
	NET_AX25_CONNECT_MODE,
	NET_AX25_STANDARD_WINDOW,
	NET_AX25_EXTENDED_WINDOW,
	NET_AX25_T1_TIMEOUT,
	NET_AX25_T2_TIMEOUT,
	NET_AX25_T3_TIMEOUT,
	NET_AX25_IDLE_TIMEOUT,
	NET_AX25_N2,
	NET_AX25_PACLEN,
	NET_AX25_PROTOCOL,
	NET_AX25_DAMA_SLAVE_TIMEOUT
};

/* /proc/sys/net/rose */
enum {
	NET_ROSE_RESTART_REQUEST_TIMEOUT = 1,
	NET_ROSE_CALL_REQUEST_TIMEOUT,
	NET_ROSE_RESET_REQUEST_TIMEOUT,
	NET_ROSE_CLEAR_REQUEST_TIMEOUT,
	NET_ROSE_NO_ACTIVITY_TIMEOUT,
	NET_ROSE_ACK_HOLD_BACK_TIMEOUT,
	NET_ROSE_ROUTING_CONTROL,
	NET_ROSE_LINK_FAIL_TIMEOUT,
	NET_ROSE_MAX_VCS,
	NET_ROSE_WINDOW_SIZE
};

/* /proc/sys/net/x25 */
enum {
	NET_X25_RESTART_REQUEST_TIMEOUT = 1,
	NET_X25_CALL_REQUEST_TIMEOUT,
	NET_X25_RESET_REQUEST_TIMEOUT,
	NET_X25_CLEAR_REQUEST_TIMEOUT,
	NET_X25_ACK_HOLD_BACK_TIMEOUT
};

/* /proc/sys/net/token-ring */
enum
{
	NET_TR_RIF_TIMEOUT=1
};

/* /proc/sys/net/decnet */
enum {
	NET_DECNET_DEF_T3_BROADCAST = 1,
	NET_DECNET_DEF_T3_POINTTOPOINT,
	NET_DECNET_DEF_T1,
	NET_DECNET_DEF_BCT1,
	NET_DECNET_CACHETIMEOUT,
	NET_DECNET_DEBUG_LEVEL
};

/* CTL_PROC names: */

/* CTL_FS names: */

/* CTL_DEBUG names: */

/* CTL_DEV names: */
enum {
	DEV_CDROM = 1,
};

/* /proc/sys/dev/cdrom */
enum {
	DEV_CDROM_INFO = 1,
};

#ifdef __KERNEL__

extern asmlinkage int sys_sysctl(struct __sysctl_args *);
extern void sysctl_init(void);

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
extern int proc_dointvec_jiffies(ctl_table *, int, struct file *,
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
