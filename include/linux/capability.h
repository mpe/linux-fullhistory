/*
 * This is <linux/capability.h>
 *
 * Andrew G. Morgan <morgan@transmeta.com>
 * Alexander Kjeldaas <astor@guardian.no>
 * with help from Aleph1, Roland Buresund and Andrew Main.
 */ 

#ifndef _LINUX_CAPABILITY_H
#define _LINUX_CAPABILITY_H

#include <linux/types.h>
#include <linux/fs.h>

/* User-level do most of the mapping between kernel and user
   capabilities based on the version tag given by the kernel. The
   kernel might be somewhat backwards compatible, but don't bet on
   it. */

#define _LINUX_CAPABILITY_VERSION  0x19980330

typedef struct _user_cap_struct {
	__u32 version;
	__u32 size;
	__u8  cap[1];
} *cap_t;

#ifdef __KERNEL__

typedef struct kernel_cap_struct {
	int cap;
} kernel_cap_t;

#endif


/**
 ** POSIX-draft defined capabilities. 
 **/

/* In a system with the [_POSIX_CHOWN_RESTRICTED] option defined, this
   overrides the restriction of changing file ownership and group
   ownership. */

#define CAP_CHOWN            0

/* Override all DAC access, including ACL execute access if
   [_POSIX_ACL] is defined. Excluding DAC access covered by
   CAP_LINUX_IMMUTABLE */

#define CAP_DAC_OVERRIDE     1

/* Overrides all DAC restrictions regarding read and search on files
   and directories, including ACL restrictions if [_POSIX_ACL] is
   defined. Excluding DAC access covered by CAP_LINUX_IMMUTABLE */

#define CAP_DAC_READ_SEARCH  2
    
/* Overrides all restrictions about allowed operations on files, where
   file owner ID must be equal to the user ID, except where CAP_FSETID
   is applicable. It doesn't override MAC and DAC restrictions. */

#define CAP_FOWNER           3

/* Overrides the following restrictions that the effective user ID
   shall match the file owner ID when setting the S_ISUID and S_ISGID
   bits on that file; that the effective group ID (or one of the
   supplementary group IDs shall match the file owner ID when setting
   the S_ISGID bit on that file; that the S_ISUID and S_ISGID bits are
   cleared on successful return from chown(2). */

#define CAP_FSETID           4

/* Used to decide between falling back on the old suser() or fsuser(). */

#define CAP_FS_MASK          0x1f

/* Overrides the restriction that the real or effective user ID of a
   process sending a signal must match the real or effective user ID
   of the process receiving the signal. */

#define CAP_KILL             5

/* Allows setgid(2) manipulation */

#define CAP_SETGID           6

/* Allows setuid(2) manipulation */

#define CAP_SETUID           7


/**
 ** Linux-specific capabilities
 **/

/* Transfer any capability in your permitted set to any pid,
   remove any capability in your permitted set from any pid */

#define CAP_SETPCAP          8

/* Allow modification of S_IMMUTABLE and S_APPEND file attributes */

#define CAP_LINUX_IMMUTABLE  9

/* Allows binding to TCP/UDP sockets below 1024 */

#define CAP_NET_BIND_SERVICE 10

/* Allow broadcasting, listen to multicast */

#define CAP_NET_BROADCAST    11

/* Allow interface configuration */
/* Allow configuring of firewall stuff */
/* Allow setting debug option on sockets */
/* Allow modification of routing tables */

#define CAP_NET_ADMIN        12

/* Allow use of RAW sockets */
/* Allow use of PACKET sockets */

#define CAP_NET_RAW          13

/* Allow locking of segments in memory */

#define CAP_IPC_LOCK         14

/* Override IPC ownership checks */

#define CAP_IPC_OWNER        15

/* Insert and remove kernel modules */

#define CAP_SYS_MODULE       16

/* Allow ioperm/iopl access */

#define CAP_SYS_RAWIO        17

/* Allow use of chroot() */

#define CAP_SYS_CHROOT       18

/* Allow ptrace() of any process */

#define CAP_SYS_PTRACE       19

/* Allow configuration of process accounting */

#define CAP_SYS_PACCT        20

/* Allow configuration of the secure attention key */
/* Allow administration of the random device */
/* Allow device administration */
/* Allow examination and configuration of disk quotas */
/* System Admin functions: mount et al */

#define CAP_SYS_ADMIN        21

/* Allow use of reboot() */

#define CAP_SYS_BOOT         22

/* Allow use of renice() on others, and raising of priority */

#define CAP_SYS_NICE         23

/* Override resource limits */

#define CAP_SYS_RESOURCE     24

/* Allow manipulation of system clock */

#define CAP_SYS_TIME         25

/* Allow configuration of tty devices */

#define CAP_SYS_TTY_CONFIG   26

#ifdef __KERNEL__

/*
 * Internal kernel functions only
 */

#define CAP_EMPTY_SET       {  0 }
#define CAP_FULL_SET        { ~0 }

#define CAP_TO_MASK(x) (1 << (x))
#define cap_raise(c, flag)   (c.cap |=  CAP_TO_MASK(flag))
#define cap_lower(c, flag)   (c.cap &= ~CAP_TO_MASK(flag))
#define cap_raised(c, flag)  (c.cap &   CAP_TO_MASK(flag))

#define cap_isclear(c) (!c.cap)

#define cap_copy(dest,src) do { (dest).cap = (src).cap; } while(0)
#define cap_clear(c)       do {  c.cap =  0; } while(0)
#define cap_set_full(c)    do {  c.cap = ~0; } while(0)

#define cap_is_fs_cap(c)     ((c) & CAP_FS_MASK)

#endif /* __KERNEL__ */

#endif /* !_LINUX_CAPABILITY_H */
