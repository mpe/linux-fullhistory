/*
 *  proc.c
 *
 *  Copyright (C) 1995, 1996 by Paal-Kr. Engstad and Volker Lendecke
 *  Copyright (C) 1997 by Volker Lendecke
 *
 *  28/06/96 - Fixed long file name support (smb_proc_readdir_long) by Yuri Per
 *  28/09/97 - Fixed smb_d_path [now smb_build_path()] to be non-recursive
 *             by Riccardo Facchetti
 */

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/dcache.h>
#include <linux/dirent.h>
#include <linux/smb_fs.h>
#include <linux/smbno.h>
#include <linux/smb_mount.h>

#include <asm/string.h>

#define SMBFS_PARANOIA 1
/* #define SMBFS_DEBUG_VERBOSE 1 */
/* #define pr_debug printk */

#define SMB_VWV(packet)  ((packet) + SMB_HEADER_LEN)
#define SMB_CMD(packet)  (*(packet+8))
#define SMB_WCT(packet)  (*(packet+SMB_HEADER_LEN - 1))
#define SMB_BCC(packet)  smb_bcc(packet)
#define SMB_BUF(packet)  ((packet) + SMB_HEADER_LEN + SMB_WCT(packet) * 2 + 2)

#define SMB_DIRINFO_SIZE 43
#define SMB_STATUS_SIZE  21

static inline int
min(int a, int b)
{
	return a < b ? a : b;
}

static void
str_upper(char *name, int len)
{
	while (len--)
	{
		if (*name >= 'a' && *name <= 'z')
			*name -= ('a' - 'A');
		name++;
	}
}

static void
str_lower(char *name, int len)
{
	while (len--)
	{
		if (*name >= 'A' && *name <= 'Z')
			*name += ('a' - 'A');
		name++;
	}
}

static void reverse_string(char *buf, int len) {
	char c;
	char *end = buf+len-1;

	while(buf < end) {
		c = *buf;
		*(buf++) = *end;
		*(end--) = c;
	}
}

/*****************************************************************************/
/*                                                                           */
/*  Encoding/Decoding section                                                */
/*                                                                           */
/*****************************************************************************/

__u8 *
smb_encode_smb_length(__u8 * p, __u32 len)
{
	*p = 0;
	*(p+1) = 0;
	*(p+2) = (len & 0xFF00) >> 8;
	*(p+3) = (len & 0xFF);
	if (len > 0xFFFF)
	{
		*(p+1) = 1;
	}
	return p + 4;
}

/*
 * smb_build_path: build the path to entry and name storing it in buf.
 * The path returned will have the trailing '\0'.
 */
static int smb_build_path(struct dentry * entry, struct qstr * name, char * buf)
{
	char *path = buf;

	if (entry == NULL)
		goto test_name_and_out;

	/*
	 * If IS_ROOT, we have to do no walking at all.
	 */
	if (IS_ROOT(entry)) {
		*(path++) = '\\';
		if (name != NULL)
			goto name_and_out;
		goto out;
	}

	/*
	 * Build the path string walking the tree backward from end to ROOT
	 * and store it in reversed order [see reverse_string()]
	 */
	for (;;) {
		memcpy(path, entry->d_name.name, entry->d_name.len);
		reverse_string(path, entry->d_name.len);
		path += entry->d_name.len;

		*(path++) = '\\';

		entry = entry->d_parent;

		if (IS_ROOT(entry))
			break;
	}

	reverse_string(buf, path-buf);

test_name_and_out:
	if (name != NULL) {
		*(path++) = '\\';
name_and_out:
		memcpy(path, name->name, name->len);
		path += name->len;
	}
out:
	*(path++) = '\0';
	return (path-buf);
}

static char *smb_encode_path(struct smb_sb_info *server, char *buf,
			     struct dentry *dir, struct qstr *name)
{
	char *start = buf;

	buf += smb_build_path(dir, name, buf);

	if (server->opt.protocol <= SMB_PROTOCOL_COREPLUS)
		str_upper(start, buf - start);

	return buf;
}

/* The following are taken directly from msdos-fs */

/* Linear day numbers of the respective 1sts in non-leap years. */

static int day_n[] =
{0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 0, 0, 0, 0};
		  /* JanFebMarApr May Jun Jul Aug Sep Oct Nov Dec */


extern struct timezone sys_tz;

static int
utc2local(int time)
{
	return time - sys_tz.tz_minuteswest * 60;
}

static int
local2utc(int time)
{
	return time + sys_tz.tz_minuteswest * 60;
}

/* Convert a MS-DOS time/date pair to a UNIX date (seconds since 1 1 70). */

static int
date_dos2unix(unsigned short time, unsigned short date)
{
	int month, year, secs;

	month = ((date >> 5) & 15) - 1;
	year = date >> 9;
	secs = (time & 31) * 2 + 60 * ((time >> 5) & 63) + (time >> 11) * 3600 + 86400 *
	    ((date & 31) - 1 + day_n[month] + (year / 4) + year * 365 - ((year & 3) == 0 &&
						   month < 2 ? 1 : 0) + 3653);
	/* days since 1.1.70 plus 80's leap day */
	return local2utc(secs);
}


/* Convert linear UNIX date to a MS-DOS time/date pair. */

static void
date_unix2dos(int unix_date, __u8 * date, __u8 * time)
{
	int day, year, nl_day, month;

	unix_date = utc2local(unix_date);
	WSET(time, 0,
	     (unix_date % 60) / 2 + (((unix_date / 60) % 60) << 5) +
	     (((unix_date / 3600) % 24) << 11));
	day = unix_date / 86400 - 3652;
	year = day / 365;
	if ((year + 3) / 4 + 365 * year > day)
		year--;
	day -= (year + 3) / 4 + 365 * year;
	if (day == 59 && !(year & 3))
	{
		nl_day = day;
		month = 2;
	} else
	{
		nl_day = (year & 3) || day <= 59 ? day : day - 1;
		for (month = 0; month < 12; month++)
			if (day_n[month] > nl_day)
				break;
	}
	WSET(date, 0,
	     nl_day - day_n[month - 1] + 1 + (month << 5) + (year << 9));
}



/*****************************************************************************/
/*                                                                           */
/*  Support section.                                                         */
/*                                                                           */
/*****************************************************************************/

__u32
smb_len(__u8 * p)
{
	return ((*(p+1) & 0x1) << 16L) | (*(p+2) << 8L) | *(p+3);
}

static __u16
smb_bcc(__u8 * packet)
{
	int pos = SMB_HEADER_LEN + SMB_WCT(packet) * sizeof(__u16);
	return WVAL(packet, pos);
}

/* smb_valid_packet: We check if packet fulfills the basic
   requirements of a smb packet */

static int
smb_valid_packet(__u8 * packet)
{
	return (packet[4] == 0xff
		&& packet[5] == 'S'
		&& packet[6] == 'M'
		&& packet[7] == 'B'
		&& (smb_len(packet) + 4 == SMB_HEADER_LEN
		    + SMB_WCT(packet) * 2 + SMB_BCC(packet)));
}

/* smb_verify: We check if we got the answer we expected, and if we
   got enough data. If bcc == -1, we don't care. */

static int
smb_verify(__u8 * packet, int command, int wct, int bcc)
{
	return (SMB_CMD(packet) == command &&
		SMB_WCT(packet) >= wct &&
		(bcc == -1 || SMB_BCC(packet) >= bcc)) ? 0 : -EIO;
}

/*
 * Returns the maximum read or write size for the current packet size
 * and max_xmit value.
 * N.B. Since this value is usually computed before locking the server,
 * the server's packet size must never be decreased!
 */
static int
smb_get_xmitsize(struct smb_sb_info *server, int overhead)
{
	int size = server->packet_size;

	/*
	 * Start with the smaller of packet size and max_xmit ...
	 */
	if (size > server->opt.max_xmit)
		size = server->opt.max_xmit;
	return size - overhead;
}

/*
 * Calculate the maximum read size
 */
int
smb_get_rsize(struct smb_sb_info *server)
{
	int overhead = SMB_HEADER_LEN + 5 * sizeof(__u16) + 2 + 1 + 2;
	int size = smb_get_xmitsize(server, overhead);
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_get_rsize: packet=%d, xmit=%d, size=%d\n",
server->packet_size, server->opt.max_xmit, size);
#endif
	return size;
}

/*
 * Calculate the maximum write size
 */
int
smb_get_wsize(struct smb_sb_info *server)
{
	int overhead = SMB_HEADER_LEN + 5 * sizeof(__u16) + 2 + 1 + 2;
	int size = smb_get_xmitsize(server, overhead);
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_get_wsize: packet=%d, xmit=%d, size=%d\n",
server->packet_size, server->opt.max_xmit, size);
#endif
	return size;
}

static int
smb_errno(int errcls, int error)
{
	if (errcls == ERRDOS)
		switch (error)
		{
		case ERRbadfunc:
			return EINVAL;
		case ERRbadfile:
			return ENOENT;
		case ERRbadpath:
			return ENOENT;
		case ERRnofids:
			return EMFILE;
		case ERRnoaccess:
			return EACCES;
		case ERRbadfid:
			return EBADF;
		case ERRbadmcb:
			return EREMOTEIO;
		case ERRnomem:
			return ENOMEM;
		case ERRbadmem:
			return EFAULT;
		case ERRbadenv:
			return EREMOTEIO;
		case ERRbadformat:
			return EREMOTEIO;
		case ERRbadaccess:
			return EACCES;
		case ERRbaddata:
			return E2BIG;
		case ERRbaddrive:
			return ENXIO;
		case ERRremcd:
			return EREMOTEIO;
		case ERRdiffdevice:
			return EXDEV;
		case ERRnofiles:
			return 0;
		case ERRbadshare:
			return ETXTBSY;
		case ERRlock:
			return EDEADLK;
		case ERRfilexists:
			return EEXIST;
		case 87:
			return 0;	/* Unknown error!! */
		case 123:		/* Invalid name?? e.g. .tmp* */
			return ENOENT;
			/* This next error seems to occur on an mv when
			 * the destination exists */
		case 183:
			return EEXIST;
		default:
			printk("smb_errno: ERRDOS code %d, returning EIO\n",
				error);
			return EIO;
	} else if (errcls == ERRSRV)
		switch (error)
		{
		case ERRerror:
			return ENFILE;
		case ERRbadpw:
			return EINVAL;
		case ERRbadtype:
			return EIO;
		case ERRaccess:
			return EACCES;
		default:
			printk("smb_errno: ERRSRV code %d, returning EIO\n",
				error);
			return EIO;
	} else if (errcls == ERRHRD)
		switch (error)
		{
		case ERRnowrite:
			return EROFS;
		case ERRbadunit:
			return ENODEV;
		case ERRnotready:
			return EUCLEAN;
		case ERRbadcmd:
			return EIO;
		case ERRdata:
			return EIO;
		case ERRbadreq:
			return ERANGE;
		case ERRbadshare:
			return ETXTBSY;
		case ERRlock:
			return EDEADLK;
		default:
			printk("smb_errno: ERRHRD code %d, returning EIO\n",
				error);
			return EIO;
	} else if (errcls == ERRCMD)
		{
		printk("smb_errno: ERRCMD code %d, returning EIO\n", error);
		return EIO;
		}
	return 0;
}

static inline void
smb_lock_server(struct smb_sb_info *server)
{
	down(&(server->sem));
}

static inline void
smb_unlock_server(struct smb_sb_info *server)
{
	up(&(server->sem));
}

/*
 * smb_retry: This function should be called when smb_request_ok has
   indicated an error. If the error was indicated because the
   connection was killed, we try to reconnect. If smb_retry returns 0,
   the error was indicated for another reason, so a retry would not be
   of any use.
 * N.B. The server must be locked for this call.
 */

static int
smb_retry(struct smb_sb_info *server)
{
	struct wait_queue wait = { current, NULL };
	unsigned long timeout;
	int result = 0;

	if (server->state != CONN_INVALID)
		goto out;

	smb_close_socket(server);

	if (server->conn_pid == 0)
	{
		printk("smb_retry: no connection process\n");
		server->state = CONN_RETRIED;
		goto out;
	}

	kill_proc(server->conn_pid, SIGUSR1, 0);
#if 0
	server->conn_pid = 0;
#endif

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_retry: signalled pid %d, waiting for new connection\n",
server->conn_pid);
#endif
	/*
	 * Wait here for a new connection.
	 */
	timeout = jiffies + 10*HZ;
	add_wait_queue(&server->wait, &wait);
	while (1)
	{
		current->state = TASK_INTERRUPTIBLE;
		current->timeout = jiffies + HZ;
		if (server->state != CONN_INVALID)
			break;
		if (jiffies > timeout)
		{
			printk("smb_retry: timed out, try again later\n");
			break;
		}
		if (signal_pending(current))
		{
			printk("smb_retry: caught signal\n");
			break;
		}
		schedule();
	}
	remove_wait_queue(&server->wait, &wait);
	current->timeout = 0;
	current->state = TASK_RUNNING;

	if (server->state == CONN_VALID)
	{
#ifdef SMBFS_PARANOIA
printk("smb_retry: new connection pid=%d\n", server->conn_pid);
#endif
		result = 1;
	}

out:
	return result;
}

/* smb_request_ok: We expect the server to be locked. Then we do the
   request and check the answer completely. When smb_request_ok
   returns 0, you can be quite sure that everything went well. When
   the answer is <=0, the returned number is a valid unix errno. */

static int
smb_request_ok(struct smb_sb_info *s, int command, int wct, int bcc)
{
	int result = 0;

	s->rcls = 0;
	s->err = 0;

	/* Make sure we have a connection */
	if (s->state != CONN_VALID && !smb_retry(s))
	{
		result = -EIO;
	} else if (smb_request(s) < 0)
	{
		pr_debug("smb_request failed\n");
		result = -EIO;
	} else if (smb_valid_packet(s->packet) != 0)
	{
		pr_debug("not a valid packet!\n");
		result = -EIO;
	} else if (s->rcls != 0)
	{
		result = -smb_errno(s->rcls, s->err);
	} else if (smb_verify(s->packet, command, wct, bcc) != 0)
	{
		pr_debug("smb_verify failed\n");
		result = -EIO;
	}
	return result;
}

/*
 * This is called with the server locked after a successful smb_newconn().
 * It installs the new connection pid, sets server->state to CONN_VALID,
 * and wakes up the process waiting for the new connection.
 * N.B. The first call is made without locking the server -- need to fix!
 */
int
smb_offerconn(struct smb_sb_info *server)
{
	int error;

	error = -EACCES;
	if ((current->uid != server->mnt->mounted_uid) && !suser()) 
		goto out;
	if (atomic_read(&server->sem.count) == 1)
	{
		printk("smb_offerconn: server not locked, count=%d\n",
			atomic_read(&server->sem.count));
#if 0
		goto out;
#endif
	}

	server->conn_pid = current->pid;
	server->state = CONN_VALID;
	wake_up_interruptible(&server->wait);
#ifdef SMBFS_PARANOIA
printk("smb_offerconn: state valid, pid=%d\n", server->conn_pid);
#endif
	error = 0;

out:
	return error;
}

/*
 * This must be called with the server locked.
 * N.B. The first call is made without locking the server -- need to fix!
 */
int
smb_newconn(struct smb_sb_info *server, struct smb_conn_opt *opt)
{
	struct file *filp;
	int error;

	error = -EBADF;
	if (opt->fd >= NR_OPEN || !(filp = current->files->fd[opt->fd]))
		goto out;
	if (!smb_valid_socket(filp->f_dentry->d_inode))
		goto out;

	error = -EACCES;
	if ((current->uid != server->mnt->mounted_uid) && !suser())
		goto out;
	if (atomic_read(&server->sem.count) == 1)
	{
		printk("smb_newconn: server not locked, count=%d\n",
			atomic_read(&server->sem.count));
#if 0
		goto out;
#endif
	}

	/*
	 * Make sure the old socket is closed
	 */
	smb_close_socket(server);

	filp->f_count += 1;
	server->sock_file = filp;
	smb_catch_keepalive(server);
	server->opt = *opt;
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_newconn: protocol=%d, max_xmit=%d\n",
server->opt.protocol, server->opt.max_xmit);
#endif
	server->generation += 1;
	error = 0;

out:
	return error;
}

/* smb_setup_header: We completely set up the packet. You only have to
   insert the command-specific fields */

__u8 *
smb_setup_header(struct smb_sb_info * server, __u8 command, __u16 wct, __u16 bcc)
{
	__u32 xmit_len = SMB_HEADER_LEN + wct * sizeof(__u16) + bcc + 2;
	__u8 *p = server->packet;
	__u8 *buf = server->packet;

if (xmit_len > server->packet_size)
printk("smb_setup_header: Aieee, xmit len > packet! len=%d, size=%d\n",
xmit_len, server->packet_size);

	p = smb_encode_smb_length(p, xmit_len - 4);

	*p++ = 0xff;
	*p++ = 'S';
	*p++ = 'M';
	*p++ = 'B';
	*p++ = command;

	memset(p, '\0', 19);
	p += 19;
	p += 8;

	WSET(buf, smb_tid, server->opt.tid);
	WSET(buf, smb_pid, 1);
	WSET(buf, smb_uid, server->opt.server_uid);
	WSET(buf, smb_mid, 1);

	if (server->opt.protocol > SMB_PROTOCOL_CORE)
	{
		*(buf+smb_flg) = 0x8;
		WSET(buf, smb_flg2, 0x3);
	}
	*p++ = wct;		/* wct */
	p += 2 * wct;
	WSET(p, 0, bcc);
	return p + 2;
}

static void
smb_setup_bcc(struct smb_sb_info *server, __u8 * p)
{
	__u8 *packet = server->packet;
	__u8 *pbcc = packet + SMB_HEADER_LEN + 2 * SMB_WCT(packet);
	__u16 bcc = p - (pbcc + 2);

	WSET(pbcc, 0, bcc);
	smb_encode_smb_length(packet,
			      SMB_HEADER_LEN + 2 * SMB_WCT(packet) - 2 + bcc);
}

/*
 * We're called with the server locked, and we leave it that way.
 * Set the permissions to be consistent with the desired access.
 */

static int
smb_proc_open(struct smb_sb_info *server, struct dentry *dir, int wish)
{
	struct inode *ino = dir->d_inode;
	int mode, read_write = 0x42, read_only = 0x40;
	int error;
	char *p;

	mode = read_write;
#if 0
	if (!(wish & (O_WRONLY | O_RDWR)))
		mode = read_only;
#endif

      retry:
	p = smb_setup_header(server, SMBopen, 2, 0);
	WSET(server->packet, smb_vwv0, mode);
	WSET(server->packet, smb_vwv1, aSYSTEM | aHIDDEN | aDIR);
	*p++ = 4;
	p = smb_encode_path(server, p, dir, NULL);
	smb_setup_bcc(server, p);

	error = smb_request_ok(server, SMBopen, 7, 0);
	if (error != 0)
	{
		if (smb_retry(server))
			goto retry;

		if (mode == read_write &&
		    (error == -EACCES || error == -ETXTBSY || error == -EROFS))
		{
#ifdef SMBFS_PARANOIA
printk("smb_proc_open: %s/%s open failed, error=%d, retrying R/O\n",
dir->d_parent->d_name.name, dir->d_name.name, error);
#endif
			mode = read_only;
			goto retry;
		}
	}
	/* We should now have data in vwv[0..6]. */

	ino->u.smbfs_i.fileid = WVAL(server->packet, smb_vwv0);
	ino->u.smbfs_i.attr   = WVAL(server->packet, smb_vwv1);
	/* smb_vwv2 has mtime */
	/* smb_vwv4 has size  */
	ino->u.smbfs_i.access = WVAL(server->packet, smb_vwv6);
	ino->u.smbfs_i.access &= 3;

	/* N.B. Suppose the open failed?? */
	ino->u.smbfs_i.open = server->generation;

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_proc_open: error=%d, access=%d\n", error, ino->u.smbfs_i.access);
#endif
	return error;
}

int
smb_open(struct dentry *dentry, int wish)
{
	struct inode *i = dentry->d_inode;
	int result;

	result = -ENOENT;
	if (!i)
	{
		printk("smb_open: no inode for dentry %s/%s\n",
			dentry->d_parent->d_name.name, dentry->d_name.name);
		goto out;
	}

	/*
	 * Note: If the caller holds an active dentry and the file is
	 * currently open, we can be sure that the file isn't about
	 * to be closed. (See smb_close_dentry() below.)
	 */
	if (!smb_is_open(i))
	{
		struct smb_sb_info *server = SMB_SERVER(i);
		smb_lock_server(server);
		result = 0;
		if (!smb_is_open(i))
			result = smb_proc_open(server, dentry, wish);
		smb_unlock_server(server);
		if (result)
		{
#ifdef SMBFS_PARANOIA
printk("smb_open: %s/%s open failed, result=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, result);
#endif
			goto out;
		}
		/*
		 * A successful open means the path is still valid ...
		 */
		smb_renew_times(dentry);
	}

	result = -EACCES;
	if (((wish == O_RDONLY) && ((i->u.smbfs_i.access == O_RDONLY)
				     || (i->u.smbfs_i.access == O_RDWR)))
	    || ((wish == O_WRONLY) && ((i->u.smbfs_i.access == O_WRONLY)
					|| (i->u.smbfs_i.access == O_RDWR)))
	    || ((wish == O_RDWR) && (i->u.smbfs_i.access == O_RDWR)))
		result = 0;

out:
	return result;
}

/* We're called with the server locked */

static int 
smb_proc_close(struct smb_sb_info *server, __u16 fileid, __u32 mtime)
{
	smb_setup_header(server, SMBclose, 3, 0);
	WSET(server->packet, smb_vwv0, fileid);
	DSET(server->packet, smb_vwv1, utc2local(mtime));
	return smb_request_ok(server, SMBclose, 0, 0);
}

/*
 * Called with the server locked
 */
static int 
smb_proc_close_inode(struct smb_sb_info *server, struct inode * ino)
{
	int result = 0;
	if (smb_is_open(ino))
	{
		/*
		 * We clear the open flag in advance, in case another
 		 * process observes the value while we block below.
		 */
		ino->u.smbfs_i.open = 0;
		result = smb_proc_close(server, ino->u.smbfs_i.fileid,
						ino->i_mtime);
	}
	return result;
}

int
smb_close(struct inode *ino)
{
	int result = 0;

	if (smb_is_open(ino))
	{
		struct smb_sb_info *server = SMB_SERVER(ino);
		smb_lock_server(server);
		result = smb_proc_close_inode(server, ino);
		smb_unlock_server(server);
	}
	return result;
}

/*
 * This routine is called from dput() when d_count is going to 0.
 * We use this to close the file so that cached dentries don't
 * keep too many files open.
 *
 * There are some tricky race conditions here: the dentry may go
 * back into use while we're closing the file, and we don't want
 * the new user to be confused as to the open status.
 */
void
smb_close_dentry(struct dentry * dentry)
{
	struct inode *ino = dentry->d_inode;

	if (ino)
	{
		if (smb_is_open(ino))
		{
			struct smb_sb_info *server = SMB_SERVER(ino);
			smb_lock_server(server);
			/*
			 * Check whether the dentry is back in use.
			 */
			if (dentry->d_count <= 1)
			{
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_close_dentry: closing %s/%s, count=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, dentry->d_count);
#endif
				smb_proc_close_inode(server, ino);
			}
			smb_unlock_server(server);
		}
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_close_dentry: closed %s/%s, count=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, dentry->d_count);
#endif
	}
}

/* In smb_proc_read and smb_proc_write we do not retry, because the
   file-id would not be valid after a reconnection. */

int
smb_proc_read(struct inode *ino, off_t offset, int count, char *data)
{
	struct smb_sb_info *server = SMB_SERVER(ino);
	__u16 returned_count, data_len;
	char *buf;
	int result;

	smb_lock_server(server);
	smb_setup_header(server, SMBread, 5, 0);
	buf = server->packet;
	WSET(buf, smb_vwv0, ino->u.smbfs_i.fileid);
	WSET(buf, smb_vwv1, count);
	DSET(buf, smb_vwv2, offset);
	WSET(buf, smb_vwv4, 0);

	result = smb_request_ok(server, SMBread, 5, -1);
	if (result < 0)
		goto out;
	returned_count = WVAL(server->packet, smb_vwv0);

	buf = SMB_BUF(server->packet);
	data_len = WVAL(buf, 1);
	memcpy(data, buf+3, data_len);

	if (returned_count != data_len)
	{
		printk(KERN_NOTICE "smb_proc_read: returned != data_len\n");
		printk(KERN_NOTICE "smb_proc_read: ret_c=%d, data_len=%d\n",
		       returned_count, data_len);
	}
	result = data_len;

out:
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_proc_read: file %s/%s, count=%d, result=%d\n",
((struct dentry *) ino->u.smbfs_i.dentry)->d_parent->d_name.name, 
((struct dentry *) ino->u.smbfs_i.dentry)->d_name.name, count, result);
#endif
	smb_unlock_server(server);
	return result;
}

int
smb_proc_write(struct inode *ino, off_t offset, int count, const char *data)
{
	struct smb_sb_info *server = SMB_SERVER(ino);
	int result;
	__u8 *p;

	smb_lock_server(server);
#if SMBFS_DEBUG_VERBOSE
printk("smb_proc_write: file %s/%s, count=%d@%ld, packet_size=%d\n",
((struct dentry *)ino->u.smbfs_i.dentry)->d_parent->d_name.name, 
((struct dentry *)ino->u.smbfs_i.dentry)->d_name.name, 
count, offset, server->packet_size);
#endif
	p = smb_setup_header(server, SMBwrite, 5, count + 3);
	WSET(server->packet, smb_vwv0, ino->u.smbfs_i.fileid);
	WSET(server->packet, smb_vwv1, count);
	DSET(server->packet, smb_vwv2, offset);
	WSET(server->packet, smb_vwv4, 0);

	*p++ = 1;
	WSET(p, 0, count);
	memcpy(p+2, data, count);

	result = smb_request_ok(server, SMBwrite, 1, 0);
	if (result >= 0)
		result = WVAL(server->packet, smb_vwv0);

	smb_unlock_server(server);
	return result;
}

int
smb_proc_create(struct dentry *dir, struct qstr *name,
		__u16 attr, time_t ctime)
{
	struct smb_sb_info *server;
	int error;
	char *p;

	server = server_from_dentry(dir);
	smb_lock_server(server);

      retry:
	p = smb_setup_header(server, SMBcreate, 3, 0);
	WSET(server->packet, smb_vwv0, attr);
	DSET(server->packet, smb_vwv1, utc2local(ctime));
	*p++ = 4;
	p = smb_encode_path(server, p, dir, name);
	smb_setup_bcc(server, p);

	if ((error = smb_request_ok(server, SMBcreate, 1, 0)) < 0)
	{
		if (smb_retry(server))
			goto retry;
		goto out;
	}
	smb_proc_close(server, WVAL(server->packet, smb_vwv0), CURRENT_TIME);
	error = 0;

out:
	smb_unlock_server(server);
	return error;
}

int
smb_proc_mv(struct dentry *odir, struct qstr *oname,
	    struct dentry *ndir, struct qstr *nname)
{
	struct smb_sb_info *server;
	char *p;
	int result;

	server = server_from_dentry(odir);
	smb_lock_server(server);

      retry:
	p = smb_setup_header(server, SMBmv, 1, 0);
	WSET(server->packet, smb_vwv0, aSYSTEM | aHIDDEN);
	*p++ = 4;
	p = smb_encode_path(server, p, odir, oname);
	*p++ = 4;
	p = smb_encode_path(server, p, ndir, nname);
	smb_setup_bcc(server, p);

	if ((result = smb_request_ok(server, SMBmv, 0, 0)) < 0)
	{
		if (smb_retry(server))
			goto retry;
		goto out;
	}
	result = 0;
out:
	smb_unlock_server(server);
	return result;
}

int
smb_proc_mkdir(struct dentry *dir, struct qstr *name)
{
	struct smb_sb_info *server;
	char *p;
	int result;

	server = server_from_dentry(dir);
	smb_lock_server(server);

      retry:
	p = smb_setup_header(server, SMBmkdir, 0, 0);
	*p++ = 4;
	p = smb_encode_path(server, p, dir, name);
	smb_setup_bcc(server, p);

	if ((result = smb_request_ok(server, SMBmkdir, 0, 0)) < 0)
	{
		if (smb_retry(server))
			goto retry;
		goto out;
	}
	result = 0;
out:
	smb_unlock_server(server);
	return result;
}

int
smb_proc_rmdir(struct dentry *dir, struct qstr *name)
{
	struct smb_sb_info *server;
	char *p;
	int result;

	server = server_from_dentry(dir);
	smb_lock_server(server);

      retry:
	p = smb_setup_header(server, SMBrmdir, 0, 0);
	*p++ = 4;
	p = smb_encode_path(server, p, dir, name);
	smb_setup_bcc(server, p);

	if ((result = smb_request_ok(server, SMBrmdir, 0, 0)) < 0)
	{
		if (smb_retry(server))
			goto retry;
		goto out;
	}
	result = 0;
out:
	smb_unlock_server(server);
	return result;
}

int
smb_proc_unlink(struct dentry *dir, struct qstr *name)
{
	struct smb_sb_info *server;
	char *p;
	int result;

	server = server_from_dentry(dir);
	smb_lock_server(server);

      retry:
	p = smb_setup_header(server, SMBunlink, 1, 0);
	WSET(server->packet, smb_vwv0, aSYSTEM | aHIDDEN);
	*p++ = 4;
	p = smb_encode_path(server, p, dir, name);
	smb_setup_bcc(server, p);

	if ((result = smb_request_ok(server, SMBunlink, 0, 0)) < 0)
	{
		if (smb_retry(server))
			goto retry;
		goto out;
	}
	result = 0;
out:
	smb_unlock_server(server);
	return result;
}

int
smb_proc_trunc(struct smb_sb_info *server, __u16 fid, __u32 length)
{
	char *p;
	int result;

	smb_lock_server(server);

      retry:
	p = smb_setup_header(server, SMBwrite, 5, 0);
	WSET(server->packet, smb_vwv0, fid);
	WSET(server->packet, smb_vwv1, 0);
	DSET(server->packet, smb_vwv2, length);
	WSET(server->packet, smb_vwv4, 0);
	*p++ = 4;
	*p++ = 0;
	smb_setup_bcc(server, p);

	if ((result = smb_request_ok(server, SMBwrite, 1, 0)) < 0)
	{
		if (smb_retry(server))
			goto retry;
		goto out;
	}
	result = 0;
out:
	smb_unlock_server(server);
	return result;
}

static void
smb_init_dirent(struct smb_sb_info *server, struct smb_fattr *fattr)
{
	memset(fattr, 0, sizeof(*fattr));

	fattr->f_nlink = 1;
	fattr->f_uid = server->mnt->uid;
	fattr->f_gid = server->mnt->gid;
	fattr->f_blksize = 512;
}

static void
smb_finish_dirent(struct smb_sb_info *server, struct smb_fattr *fattr)
{
	fattr->f_mode = server->mnt->file_mode;
	if (fattr->attr & aDIR)
	{
		fattr->f_mode = server->mnt->dir_mode;
		fattr->f_size = 512;
	}

	fattr->f_blocks = 0; /* already set to zero? */
	if ((fattr->f_blksize != 0) && (fattr->f_size != 0))
	{
		fattr->f_blocks =
		    (fattr->f_size - 1) / fattr->f_blksize + 1;
	}
	return;
}

void
smb_init_root_dirent(struct smb_sb_info *server, struct smb_fattr *fattr)
{
	smb_init_dirent(server, fattr);
	fattr->attr = aDIR;
	fattr->f_ino = 1;
	fattr->f_mtime = CURRENT_TIME;
	smb_finish_dirent(server, fattr);
}

/*
 * Note that we are now returning the name as a reference to avoid
 * an extra copy, and that the upper/lower casing is done in place.
 */
static __u8 *
smb_decode_dirent(struct smb_sb_info *server, __u8 *p, 
			struct cache_dirent *entry)
{
	int len;

	/*
	 * SMB doesn't have a concept of inode numbers ...
	 */
	entry->ino = 0;

	p += SMB_STATUS_SIZE;	/* reserved (search_status) */
	entry->name = p + 9;
	len = strlen(entry->name);
	if (len > 12)
	{
		len = 12;
	}
	/*
	 * Trim trailing blanks for Pathworks servers
	 */
	while (len > 2 && entry->name[len-1] == ' ')
		len--;
	entry->len = len;

	switch (server->opt.case_handling)
	{
	case SMB_CASE_UPPER:
		str_upper(entry->name, len);
		break;
	case SMB_CASE_LOWER:
		str_lower(entry->name, len);
		break;
	default:
		break;
	}
	pr_debug("smb_decode_dirent: len=%d, name=%s\n", len, entry->name);
	return p + 22;
}

/* This routine is used to read in directory entries from the network.
   Note that it is for short directory name seeks, i.e.: protocol <
   SMB_PROTOCOL_LANMAN2 */

static int
smb_proc_readdir_short(struct smb_sb_info *server, struct dentry *dir, int fpos,
		       void *cachep)
{
	char *p;
	int result;
	int i, first, entries_seen, entries;
	int entries_asked = (server->opt.max_xmit - 100) / SMB_DIRINFO_SIZE;
	__u16 bcc;
	__u16 count;
	char status[SMB_STATUS_SIZE];
	static struct qstr mask = { "*.*", 3, 0 };

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_proc_readdir_short: %s/%s, pos=%d\n",
dir->d_parent->d_name.name, dir->d_name.name, fpos);
#endif

	smb_lock_server(server);

	/* N.B. We need to reinitialize the cache to restart */
      retry:
	smb_init_dircache(cachep);
	first = 1;
	entries = 0;
	entries_seen = 2; /* implicit . and .. */

	while (1)
	{
		p = smb_setup_header(server, SMBsearch, 2, 0);
		WSET(server->packet, smb_vwv0, entries_asked);
		WSET(server->packet, smb_vwv1, aDIR);
		*p++ = 4;
		if (first == 1)
		{
			p = smb_encode_path(server, p, dir, &mask);
			*p++ = 5;
			WSET(p, 0, 0);
			p += 2;
			first = 0;
		} else
		{
			*p++ = 0;
			*p++ = 5;
			WSET(p, 0, SMB_STATUS_SIZE);
			p += 2;
			memcpy(p, status, SMB_STATUS_SIZE);
			p += SMB_STATUS_SIZE;
		}

		smb_setup_bcc(server, p);

		result = smb_request_ok(server, SMBsearch, 1, -1);
		if (result < 0)
		{
			if ((server->rcls == ERRDOS) && 
			    (server->err  == ERRnofiles))
				break;
			if (smb_retry(server))
				goto retry;
			goto unlock_return;
		}
		p = SMB_VWV(server->packet);
		count = WVAL(p, 0);
		if (count <= 0)
			break;

		result = -EIO;
		bcc = WVAL(p, 2);
		if (bcc != count * SMB_DIRINFO_SIZE + 3)
			goto unlock_return;
		p += 7;

		/* Read the last entry into the status field. */
		memcpy(status,
		       SMB_BUF(server->packet) + 3 +
		       (count - 1) * SMB_DIRINFO_SIZE,
		       SMB_STATUS_SIZE);

		/* Now we are ready to parse smb directory entries. */

		for (i = 0; i < count; i++)
		{
			struct cache_dirent this_ent, *entry = &this_ent;

			p = smb_decode_dirent(server, p, entry);
			if (entries_seen == 2 && entry->name[0] == '.')
			{
				if (entry->len == 1)
					continue;
				if (entry->name[1] == '.' && entry->len == 2)
					continue;
			}
			if (entries_seen >= fpos)
			{
				pr_debug("smb_proc_readdir: fpos=%u\n", 
					entries_seen);
				smb_add_to_cache(cachep, entry, entries_seen);
				entries++;
			} else
			{
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_proc_readdir: skipped, seen=%d, i=%d, fpos=%d\n",
entries_seen, i, fpos);
#endif
			}
			entries_seen++;
		}
	}
	result = entries;

    unlock_return:
	smb_unlock_server(server);
	return result;
}

/*
 * Interpret a long filename structure using the specified info level:
 *   level 1   -- Win NT, Win 95, OS/2
 *   level 259 -- File name and length only, Win NT, Win 95
 * There seem to be numerous inconsistencies and bugs in implementation.
 *
 * We return a reference to the name string to avoid copying, and perform
 * any needed upper/lower casing in place.  Note!! Level 259 entries may
 * not have any space beyond the name, so don't try to write a null byte!
 */
static char *
smb_decode_long_dirent(struct smb_sb_info *server, char *p,
			struct cache_dirent *entry, int level)
{
	char *result;
	unsigned int len = 0;

	/*
	 * SMB doesn't have a concept of inode numbers ...
	 */
	entry->ino = 0;

	switch (level)
	{
	case 1:
		len = *((unsigned char *) p + 26);
		entry->len = len;
		entry->name = p + 27;
		result = p + 28 + len;
		break;

	case 259: /* SMB_FIND_FILE_NAMES_INFO = 0x103 */
		result = p + DVAL(p, 0);
		/* DVAL(p, 4) should be resume key? Seems to be 0 .. */
		len = DVAL(p, 8);
		if (len > 255)
			len = 255;
		entry->name = p + 12;
		/*
		 * Kludge alert: Win NT 4.0 adds a trailing null byte and
		 * counts it in the name length, but Win 95 doesn't.  Hence
		 * we test for a trailing null and decrement the length ...
		 */
		if (len && entry->name[len-1] == '\0')
			len--;
		entry->len = len;
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_decode_long_dirent: info 259 at %p, len=%d, name=%s\n",
p, len, entry->name);
#endif
		break;

	default:
		printk("smb_decode_long_dirent: Unknown level %d\n", level);
		result = p + WVAL(p, 0);
	}

	switch (server->opt.case_handling)
	{
	case SMB_CASE_UPPER:
		str_upper(entry->name, len);
		break;
	case SMB_CASE_LOWER:
		str_lower(entry->name, len);
		break;
	default:
		break;
	}

	return result;
}

static int
smb_proc_readdir_long(struct smb_sb_info *server, struct dentry *dir, int fpos,
		      void *cachep)
{
	/* Both NT and OS/2 accept info level 1 (but see note below). */
	int info_level = 1;
	const int max_matches = 512;

	char *p, *mask, *lastname;
	int first, entries, entries_seen;

	unsigned char *resp_data = NULL;
	unsigned char *resp_param = NULL;
	int resp_data_len = 0;
	int resp_param_len = 0;

	__u16 command;

	int ff_resume_key = 0; /* this isn't being used */
	int ff_searchcount = 0;
	int ff_eos = 0;
	int ff_lastname = 0;
	int ff_dir_handle = 0;
	int loop_count = 0;
	int mask_len, i, result;

	char param[12 + SMB_MAXPATHLEN + 2]; /* too long for the stack! */
	static struct qstr star = { "*", 1, 0 };

	/*
	 * Check whether to change the info level.  There appears to be
	 * a bug in Win NT 4.0's handling of info level 1, whereby it
	 * truncates the directory scan for certain patterns of files.
	 * Hence we use level 259 for NT. (And Win 95 as well ...)
	 */
	if (server->opt.protocol >= SMB_PROTOCOL_NT1)
		info_level = 259;

	smb_lock_server(server);

      retry:
	/*
	 * Encode the initial path
	 */
	mask = &(param[12]);
	mask_len = smb_encode_path(server, mask, dir, &star) - mask;
	first = 1;
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_proc_readdir_long: starting fpos=%d, mask=%s\n", fpos, mask);
#endif
	/*
	 * We must reinitialize the dircache when retrying.
	 */
	smb_init_dircache(cachep);
	entries = 0;
	entries_seen = 2;
	ff_eos = 0;

	while (ff_eos == 0)
	{
		loop_count += 1;
		if (loop_count > 200)
		{
			printk(KERN_WARNING "smb_proc_readdir_long: "
			       "Looping in FIND_NEXT??\n");
			entries = -EIO;
			break;
		}

		if (first != 0)
		{
			command = TRANSACT2_FINDFIRST;
			WSET(param, 0, aSYSTEM | aHIDDEN | aDIR);
			WSET(param, 2, max_matches);	/* max count */
			WSET(param, 4, 8 + 4 + 2);	/* resume required +
							   close on end +
							   continue */
			WSET(param, 6, info_level);
			DSET(param, 8, 0);
		} else
		{
			command = TRANSACT2_FINDNEXT;
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_proc_readdir_long: handle=0x%X, resume=%d, lastname=%d, mask=%s\n",
ff_dir_handle, ff_resume_key, ff_lastname, mask);
#endif
			WSET(param, 0, ff_dir_handle);	/* search handle */
			WSET(param, 2, max_matches);	/* max count */
			WSET(param, 4, info_level);
			DSET(param, 6, ff_resume_key);	/* ff_resume_key */
			WSET(param, 10, 8 + 4 + 2);	/* resume required +
							   close on end +
							   continue */
			if (server->mnt->version & 1)
			{
				/* Windows 95 is not able to deliver answers
				 * to FIND_NEXT fast enough, so sleep 0.2 sec
				 */
				current->timeout = jiffies + HZ / 5;
				current->state = TASK_INTERRUPTIBLE;
				schedule();
				current->timeout = 0;
			}
		}

		result = smb_trans2_request(server, command,
					    0, NULL, 12 + mask_len + 1, param,
					    &resp_data_len, &resp_data,
					    &resp_param_len, &resp_param);

		if (result < 0)
		{
			if (smb_retry(server))
			{
#ifdef SMBFS_PARANOIA
printk("smb_proc_readdir_long: error=%d, retrying\n", result);
#endif
				goto retry;
			}
#ifdef SMBFS_PARANOIA
printk("smb_proc_readdir_long: error=%d, breaking\n", result);
#endif
			entries = result;
			break;
		}
		if (server->rcls != 0)
		{ 
#ifdef SMBFS_PARANOIA
printk("smb_proc_readdir_long: rcls=%d, err=%d, breaking\n",
server->rcls, server->err);
#endif
			entries = -smb_errno(server->rcls, server->err);
			break;
		}
#ifdef SMBFS_PARANOIA
if (resp_data + resp_data_len > server->packet + server->packet_size)
printk("s_p_r_l: data past packet end! data=%p, len=%d, packet=%p\n",
resp_data + resp_data_len, resp_data_len, server->packet + server->packet_size);
#endif

		/* parse out some important return info */
		if (first != 0)
		{
			ff_dir_handle = WVAL(resp_param, 0);
			ff_searchcount = WVAL(resp_param, 2);
			ff_eos = WVAL(resp_param, 4);
			ff_lastname = WVAL(resp_param, 8);
		} else
		{
			ff_searchcount = WVAL(resp_param, 0);
			ff_eos = WVAL(resp_param, 2);
			ff_lastname = WVAL(resp_param, 6);
		}

		if (ff_searchcount == 0)
		{
			break;
		}

		/* we might need the lastname for continuations */
		mask_len = 0;
		if (ff_lastname > 0)
		{
			lastname = resp_data + ff_lastname;
			switch (info_level)
			{
			case 259:
 				if (ff_lastname < resp_data_len)
					mask_len = resp_data_len - ff_lastname;
				break;
			case 1:
				/* Win NT 4.0 doesn't set the length byte */
				lastname++;
 				if (ff_lastname + 2 < resp_data_len)
					mask_len = strlen(lastname);
				break;
			}
			/*
			 * Update the mask string for the next message.
			 */
			if (mask_len > 255)
				mask_len = 255;
			if (mask_len)
				strncpy(mask, lastname, mask_len);
			ff_resume_key = 0;
		}
		mask[mask_len] = 0;
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_proc_readdir_long: new mask, len=%d@%d, mask=%s\n",
mask_len, ff_lastname, mask);
#endif
		/* Now we are ready to parse smb directory entries. */

		/* point to the data bytes */
		p = resp_data;
		for (i = 0; i < ff_searchcount; i++)
		{
			struct cache_dirent this_ent, *entry = &this_ent;

			p = smb_decode_long_dirent(server, p, entry,
							info_level);

			pr_debug("smb_readdir_long: got %s\n", entry->name);

			/* ignore . and .. from the server */
			if (entries_seen == 2 && entry->name[0] == '.')
			{
				if (entry->len == 1)
					continue;
				if (entry->name[1] == '.' && entry->len == 2)
					continue;
			}
			if (entries_seen >= fpos)
			{
				smb_add_to_cache(cachep, entry, entries_seen);
				entries += 1;
			}
 			entries_seen++;
		}

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_proc_readdir_long: received %d entries, eos=%d, resume=%d\n",
ff_searchcount, ff_eos, ff_resume_key);
#endif
		first = 0;
	}

	smb_unlock_server(server);
	return entries;
}

int
smb_proc_readdir(struct dentry *dir, int fpos, void *cachep)
{
	struct smb_sb_info *server;

	server = server_from_dentry(dir);
	if (server->opt.protocol >= SMB_PROTOCOL_LANMAN2)
		return smb_proc_readdir_long(server, dir, fpos, cachep);
	else
		return smb_proc_readdir_short(server, dir, fpos, cachep);
}

static int
smb_proc_getattr_core(struct smb_sb_info *server, struct dentry *dir,
			struct qstr *name, struct smb_fattr *attr)
{
	int result;
	char *p;

	smb_lock_server(server);

      retry:
	p = smb_setup_header(server, SMBgetatr, 0, 0);
	*p++ = 4;
	p = smb_encode_path(server, p, dir, name);
	smb_setup_bcc(server, p);

	if ((result = smb_request_ok(server, SMBgetatr, 10, 0)) < 0)
	{
		if (smb_retry(server))
			goto retry;
		goto out;
	}
	attr->attr   = WVAL(server->packet, smb_vwv0);
	attr->f_ctime = attr->f_atime = attr->f_mtime = 
	     local2utc(DVAL(server->packet, smb_vwv1));
	attr->f_size = DVAL(server->packet, smb_vwv3);
	result = 0;

out:
	smb_unlock_server(server);
	return result;
}

static int
smb_proc_getattr_trans2(struct smb_sb_info *server, struct dentry *dir,
			struct qstr *name, struct smb_fattr *attr)
{
	char *p;
	int result;

	unsigned char *resp_data = NULL;
	unsigned char *resp_param = NULL;
	int resp_data_len = 0;
	int resp_param_len = 0;
	char param[SMB_MAXPATHLEN + 20]; /* too big for the stack! */

	smb_lock_server(server);

      retry:
	WSET(param, 0, 1);	/* Info level SMB_INFO_STANDARD */
	DSET(param, 2, 0);
	p = smb_encode_path(server, param + 6, dir, name);

	result = smb_trans2_request(server, TRANSACT2_QPATHINFO,
				    0, NULL, p - param, param,
				    &resp_data_len, &resp_data,
				    &resp_param_len, &resp_param);
	if (result < 0)
	{
		if (smb_retry(server))
			goto retry;
		goto out;
	}
	if (server->rcls != 0)
	{
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_proc_getattr_trans2: for %s: result=%d, rcls=%d, err=%d\n",
&param[6], result, server->rcls, server->err);
#endif
		result = -smb_errno(server->rcls, server->err);
		goto out;
	}
	result = -ENOENT;
	if (resp_data_len < 22)
	{
#ifdef SMBFS_PARANOIA
printk("smb_proc_getattr_trans2: not enough data for %s, len=%d\n",
&param[6], resp_data_len);
#endif
		goto out;
	}

	attr->f_ctime = date_dos2unix(WVAL(resp_data, 2),
				      WVAL(resp_data, 0));
	attr->f_atime = date_dos2unix(WVAL(resp_data, 6),
				      WVAL(resp_data, 4));
	attr->f_mtime = date_dos2unix(WVAL(resp_data, 10),
				      WVAL(resp_data, 8));
	attr->f_size = DVAL(resp_data, 12);
	attr->attr = WVAL(resp_data, 20);
	result = 0;

out:
	smb_unlock_server(server);
	return result;
}

int
smb_proc_getattr(struct dentry *dir, struct qstr *name,
		     struct smb_fattr *fattr)
{
	struct smb_sb_info *server;
	int result;

	server = server_from_dentry(dir);
	smb_init_dirent(server, fattr);

	/*
	 * Win 95 is painfully slow at returning trans2 getattr info ...
 	 */
	if (server->opt.protocol >= SMB_PROTOCOL_LANMAN2 &&
	    !(server->mnt->version & 1))
		result = smb_proc_getattr_trans2(server, dir, name, fattr);
	else
		result = smb_proc_getattr_core(server, dir, name, fattr);

	smb_finish_dirent(server, fattr);

	return result;
}

/* In core protocol, there is only 1 time to be set, we use
   entry->f_mtime, to make touch work. */
static int
smb_proc_setattr_core(struct smb_sb_info *server,
		      struct dentry *dir, struct smb_fattr *fattr)
{
	char *p;
	char *buf;
	int result;

	smb_lock_server(server);

      retry:
	buf = server->packet;
	p = smb_setup_header(server, SMBsetatr, 8, 0);
	WSET(buf, smb_vwv0, fattr->attr);
	DSET(buf, smb_vwv1, utc2local(fattr->f_mtime));
	*p++ = 4;
	p = smb_encode_path(server, p, dir, NULL);
	*p++ = 4;
	*p++ = 0;

	smb_setup_bcc(server, p);
	result = smb_request_ok(server, SMBsetatr, 0, 0);
	if (result < 0)
	{
		if (smb_retry(server))
			goto retry;
		goto out;
	}
	result = 0;
out:
	smb_unlock_server(server);
	return result;
}

static int
smb_proc_setattr_trans2(struct smb_sb_info *server,
			struct dentry *dir, struct smb_fattr *fattr)
{
	char *p;
	int result;

	unsigned char *resp_data = NULL;
	unsigned char *resp_param = NULL;
	int resp_data_len = 0;
	int resp_param_len = 0;
	char param[SMB_MAXPATHLEN + 20]; /* too long for the stack! */
	char data[26];

	smb_lock_server(server);

      retry:
	WSET(param, 0, 1);	/* Info level SMB_INFO_STANDARD */
	DSET(param, 2, 0);
	p = smb_encode_path(server, param + 6, dir, NULL);

	date_unix2dos(fattr->f_ctime, &(data[0]), &(data[2]));
	date_unix2dos(fattr->f_atime, &(data[4]), &(data[6]));
	date_unix2dos(fattr->f_mtime, &(data[8]), &(data[10]));
	DSET(data, 12, fattr->f_size);
	DSET(data, 16, fattr->f_blksize);
	WSET(data, 20, fattr->attr);
	WSET(data, 22, 0);

	result = smb_trans2_request(server, TRANSACT2_SETPATHINFO,
				    26, data, p - param, param,
				    &resp_data_len, &resp_data,
				    &resp_param_len, &resp_param);
	if (result < 0)
	{
		if (smb_retry(server))
			goto retry;
		goto out;
	}
	result = 0;
	if (server->rcls != 0)
		result = -smb_errno(server->rcls, server->err);

out:
	smb_unlock_server(server);
	return result;
}

int
smb_proc_setattr(struct smb_sb_info *server, struct dentry *dir,
		 struct smb_fattr *fattr)
{
	int result;

	if (server->opt.protocol >= SMB_PROTOCOL_LANMAN2)
		result = smb_proc_setattr_trans2(server, dir, fattr);
	else
		result = smb_proc_setattr_core(server, dir, fattr);

	return result;
}

int
smb_proc_dskattr(struct super_block *sb, struct statfs *attr)
{
	struct smb_sb_info *server = &(sb->u.smbfs_sb);
	int error;
	char *p;

	smb_lock_server(server);

      retry:
	smb_setup_header(server, SMBdskattr, 0, 0);

	if ((error = smb_request_ok(server, SMBdskattr, 5, 0)) < 0)
	{
		if (smb_retry(server))
			goto retry;
		goto out;
	}
	p = SMB_VWV(server->packet);
	attr->f_bsize = WVAL(p, 2) * WVAL(p, 4);
	attr->f_blocks = WVAL(p, 0);
	attr->f_bavail = attr->f_bfree = WVAL(p, 6);
	error = 0;

out:
	smb_unlock_server(server);
	return error;
}

int
smb_proc_disconnect(struct smb_sb_info *server)
{
	int result;
	smb_lock_server(server);
	smb_setup_header(server, SMBtdis, 0, 0);
	result = smb_request_ok(server, SMBtdis, 0, 0);
	smb_unlock_server(server);
	return result;
}
