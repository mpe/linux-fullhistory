#ifndef _LINUX_PROC_FS_H
#define _LINUX_PROC_FS_H

#include <linux/config.h>
#include <linux/fs.h>

/*
 * The proc filesystem constants/structures
 */

enum root_directory_inos {
	PROC_ROOT_INO = 1,
	PROC_LOADAVG,
	PROC_UPTIME,
	PROC_MEMINFO,
	PROC_KMSG,
	PROC_VERSION,
	PROC_CPUINFO,
	PROC_PCI,
	PROC_SELF,	/* will change inode # */
	PROC_NET,
        PROC_SCSI,
#ifdef CONFIG_DEBUG_MALLOC
	PROC_MALLOC,
#endif
	PROC_KCORE,
	PROC_MODULES,
	PROC_STAT,
	PROC_DEVICES,
	PROC_INTERRUPTS,
	PROC_FILESYSTEMS,
	PROC_KSYMS,
	PROC_DMA,	
	PROC_IOPORTS,
	PROC_PROFILE /* whether enabled or not */
};

enum pid_directory_inos {
	PROC_PID_INO = 2,
	PROC_PID_MEM,
	PROC_PID_CWD,
	PROC_PID_ROOT,
	PROC_PID_EXE,
	PROC_PID_FD,
	PROC_PID_ENVIRON,
	PROC_PID_CMDLINE,
	PROC_PID_STAT,
	PROC_PID_STATM,
	PROC_PID_MAPS
};

enum pid_subdirectory_inos {
	PROC_PID_FD_DIR = 1
};

enum net_directory_inos {
	PROC_NET_UNIX = 128,
#ifdef CONFIG_INET
	PROC_NET_ARP,
	PROC_NET_ROUTE,
	PROC_NET_DEV,
	PROC_NET_RAW,
	PROC_NET_TCP,
	PROC_NET_UDP,
	PROC_NET_SNMP,
#ifdef CONFIG_INET_RARP
	PROC_NET_RARP,
#endif
#ifdef CONFIG_IP_MULTICAST
	PROC_NET_IGMP,
#endif
#ifdef CONFIG_IP_FIREWALL
	PROC_NET_IPFWFWD,
	PROC_NET_IPFWBLK,
#endif
#ifdef CONFIG_IP_ACCT
	PROC_NET_IPACCT,
#endif
#ifdef CONFIG_IP_MASQUERADE
	PROC_NET_IPMSQHST,
#endif
#if	defined(CONFIG_WAVELAN)
	PROC_NET_WAVELAN,
#endif	/* defined(CONFIG_WAVELAN) */
#endif
#ifdef CONFIG_IPX
	PROC_NET_IPX_INTERFACE,
	PROC_NET_IPX_ROUTE,
	PROC_NET_IPX,
#endif
#ifdef CONFIG_ATALK
	PROC_NET_ATALK,
	PROC_NET_AT_ROUTE,
	PROC_NET_ATIF,
#endif
#ifdef CONFIG_AX25
	PROC_NET_AX25_ROUTE,
	PROC_NET_AX25,
#ifdef CONFIG_NETROM
	PROC_NET_NR_NODES,
	PROC_NET_NR_NEIGH,
	PROC_NET_NR,
#endif
#endif
	PROC_NET_SOCKSTAT,
	PROC_NET_LAST
};

enum scsi_directory_inos {
	PROC_SCSI_SCSI = 256,
	PROC_SCSI_EATA,
	PROC_SCSI_EATA_PIO,
	PROC_SCSI_AHA152X,
	PROC_SCSI_AHA1542,
	PROC_SCSI_AHA1740,
	PROC_SCSI_AIC7XXX,
	PROC_SCSI_BUSLOGIC,
	PROC_SCSI_U14_34F,
	PROC_SCSI_FUTURE_DOMAIN,
	PROC_SCSI_GENERIC_NCR5380,
	PROC_SCSI_IN2000,
	PROC_SCSI_PAS16,
	PROC_SCSI_QLOGIC,
	PROC_SCSI_SEAGATE,
	PROC_SCSI_T128,
	PROC_SCSI_NCR53C7xx,
	PROC_SCSI_ULTRASTOR,
	PROC_SCSI_7000FASST,
	PROC_SCSI_SCSI_DEBUG,	
	PROC_SCSI_NOT_PRESENT,
	PROC_SCSI_FILE,                        /* I'm asuming here that we */
	PROC_SCSI_LAST = (PROC_SCSI_FILE + 16) /* won't ever see more than */
};                                             /* 16 HBAs in one machine   */

#define PROC_SUPER_MAGIC 0x9fa0

struct proc_dir_entry {
	unsigned short low_ino;
	unsigned short namelen;
	char * name;
};

extern struct super_block *proc_read_super(struct super_block *,void *,int);
extern void proc_put_inode(struct inode *);
extern void proc_put_super(struct super_block *);
extern void proc_statfs(struct super_block *, struct statfs *, int);
extern void proc_read_inode(struct inode *);
extern void proc_write_inode(struct inode *);
extern int proc_match(int, const char *, struct proc_dir_entry *);

extern struct inode_operations proc_root_inode_operations;
extern struct inode_operations proc_base_inode_operations;
extern struct inode_operations proc_net_inode_operations;
extern struct inode_operations proc_scsi_inode_operations;
extern struct inode_operations proc_mem_inode_operations;
extern struct inode_operations proc_array_inode_operations;
extern struct inode_operations proc_arraylong_inode_operations;
extern struct inode_operations proc_kcore_inode_operations;
extern struct inode_operations proc_profile_inode_operations;
extern struct inode_operations proc_kmsg_inode_operations;
extern struct inode_operations proc_link_inode_operations;
extern struct inode_operations proc_fd_inode_operations;

#endif
