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

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/dcache.h>
#include <linux/dirent.h>

#include <linux/smb_fs.h>
#include <linux/smbno.h>
#include <linux/smb_mount.h>

#include <asm/string.h>

#define SMBFS_PARANOIA 1
/* #define SMBFS_DEBUG_TIMESTAMP 1 */
/* #define SMBFS_DEBUG_VERBOSE 1 */
/* #define pr_debug printk */

#define SMB_VWV(packet)  ((packet) + SMB_HEADER_LEN)
#define SMB_CMD(packet)  (*(packet+8))
#define SMB_WCT(packet)  (*(packet+SMB_HEADER_LEN - 1))
#define SMB_BCC(packet)  smb_bcc(packet)
#define SMB_BUF(packet)  ((packet) + SMB_HEADER_LEN + SMB_WCT(packet) * 2 + 2)

#define SMB_DIRINFO_SIZE 43
#define SMB_STATUS_SIZE  21

static int smb_proc_setattr_ext(struct smb_sb_info *, struct inode *,
				struct smb_fattr *);
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

static time_t
utc2local(time_t time)
{
	return time - sys_tz.tz_minuteswest * 60 - (sys_tz.tz_dsttime ? 3600 :0);
}

static time_t
local2utc(time_t time)
{
	return time + sys_tz.tz_minuteswest * 60 + (sys_tz.tz_dsttime ? 3600 : 0);
}

/* Convert a MS-DOS time/date pair to a UNIX date (seconds since 1 1 70). */

static time_t
date_dos2unix(__u16 date, __u16 time)
{
	int month, year;
	time_t secs;

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
date_unix2dos(int unix_date, __u16 *date, __u16 *time)
{
	int day, year, nl_day, month;

	unix_date = utc2local(unix_date);
	*time = (unix_date % 60) / 2 +
		(((unix_date / 60) % 60) << 5) +
		(((unix_date / 3600) % 24) << 11);

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
	*date = nl_day - day_n[month - 1] + 1 + (month << 5) + (year << 9);
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
	if (SMB_CMD(packet) != command)
		goto bad_command;
	if (SMB_WCT(packet) < wct)
		goto bad_wct;
	if (bcc != -1 && SMB_BCC(packet) < bcc)
		goto bad_bcc;
	return 0;

bad_command:
	printk("smb_verify: command=%x, SMB_CMD=%x??\n",
		command, SMB_CMD(packet));
	goto fail;
bad_wct:
	printk("smb_verify: command=%x, wct=%d, SMB_WCT=%d??\n",
		command, wct, SMB_WCT(packet));
	goto fail;
bad_bcc:
	printk("smb_verify: command=%x, bcc=%d, SMB_BCC=%d??\n",
		command, bcc, SMB_BCC(packet));
fail:
	return -EIO;
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

int
smb_errno(struct smb_sb_info *server)
{
	int errcls = server->rcls;
	int error  = server->err;
	char *class = "Unknown";

	if (errcls == ERRDOS)
		switch (error)
		{
		case ERRbadfunc:
			return EINVAL;
		case ERRbadfile:
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
		case ERRnofiles:	/* Why is this mapped to 0?? */
			return 0;
		case ERRbadshare:
			return ETXTBSY;
		case ERRlock:
			return EDEADLK;
		case ERRfilexists:
			return EEXIST;
		case 87:		/* should this map to 0?? */
			return 0;	/* Unknown error!! */
		case 123:		/* Invalid name?? e.g. .tmp* */
			return ENOENT;
		case 145:		/* Win NT 4.0: non-empty directory? */
			return ENOTEMPTY;
			/* This next error seems to occur on an mv when
			 * the destination exists */
		case 183:
			return EEXIST;
		default:
			class = "ERRDOS";
			goto err_unknown;
	} else if (errcls == ERRSRV)
		switch (error)
		{
		/* N.B. This is wrong ... EIO ? */
		case ERRerror:
			return ENFILE;
		case ERRbadpw:
			return EINVAL;
		case ERRbadtype:
			return EIO;
		case ERRaccess:
			return EACCES;
		/*
		 * This is a fatal error, as it means the "tree ID"
		 * for this connection is no longer valid. We map
		 * to a special error code and get a new connection.
		 */
		case ERRinvnid:
			return EBADSLT;
		default:
			class = "ERRSRV";
			goto err_unknown;
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
		case ERRdata:
			return EIO;
		case ERRbadreq:
			return ERANGE;
		case ERRbadshare:
			return ETXTBSY;
		case ERRlock:
			return EDEADLK;
		default:
			class = "ERRHRD";
			goto err_unknown;
	} else if (errcls == ERRCMD)
		class = "ERRCMD";

err_unknown:
	printk("smb_errno: class %s, code %d from command %x\n",
		class, error, SMB_CMD(server->packet));
	return EIO;
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
	pid_t pid = server->conn_pid;
	int error, result = 0;

	if (server->state != CONN_INVALID)
		goto out;

	smb_close_socket(server);

	if (pid == 0)
	{
		printk("smb_retry: no connection process\n");
		server->state = CONN_RETRIED;
		goto out;
	}

	/*
	 * Clear the pid to enable the ioctl.
	 */
	server->conn_pid = 0;

	/*
	 * Note: use the "priv" flag, as a user process may need to reconnect.
	 */
	error = kill_proc(pid, SIGUSR1, 1);
	if (error)
	{
		printk("smb_retry: signal failed, error=%d\n", error);
		goto out_restore;
	}
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_retry: signalled pid %d, waiting for new connection\n",
server->conn_pid);
#endif
	/*
	 * Wait for the new connection.
	 */
	interruptible_sleep_on_timeout(&server->wait,  5*HZ);
	if (signal_pending(current))
		printk("smb_retry: caught signal\n");

	/*
	 * Check for a valid connection.
	 */
	if (server->state == CONN_VALID)
	{
#ifdef SMBFS_PARANOIA
printk("smb_retry: new pid=%d, generation=%d\n",
server->conn_pid, server->generation);
#endif
		result = 1;
	}

	/*
	 * Restore the original pid if we didn't get a new one.
	 */
out_restore:
	if (!server->conn_pid)
		server->conn_pid = pid;

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
	int result = -EIO;

	s->rcls = 0;
	s->err = 0;

	/* Make sure we have a connection */
	if (s->state != CONN_VALID)
	{
 		if (!smb_retry(s))
			goto out;
	}

	if (smb_request(s) < 0)
	{
		pr_debug("smb_request failed\n");
		goto out;
	}
	if (smb_valid_packet(s->packet) != 0)
	{
#ifdef SMBFS_PARANOIA
printk("smb_request_ok: invalid packet!\n");
#endif
		goto out;
	}

	/*
	 * Check for server errors.  The current smb_errno() routine
	 * is squashing some error codes, but I don't think this is
	 * correct: after a server error the packet won't be valid.
	 */
	if (s->rcls != 0)
	{
		result = -smb_errno(s);
		if (!result)
			printk("smb_request_ok: rcls=%d, err=%d mapped to 0\n",
				s->rcls, s->err);
		/*
		 * Exit now even if the error was squashed ...
		 * packet verify will fail anyway.
		 */
		goto out;
	}
	result = smb_verify(s->packet, command, wct, bcc);

out:
	return result;
}

/*
 * This implements the NEWCONN ioctl. It installs the server pid,
 * sets server->state to CONN_VALID, and wakes up the waiting process.
 *
 * Note that this must be called with the server locked, except for
 * the first call made after mounting the volume. The server pid
 * will be set to zero to indicate that smbfs is awaiting a connection.
 */
int
smb_newconn(struct smb_sb_info *server, struct smb_conn_opt *opt)
{
	struct file *filp;
	int error;

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_newconn: fd=%d, pid=%d\n", opt->fd, current->pid);
#endif
	/*
	 * Make sure we don't already have a pid ...
	 */
	error = -EINVAL;
	if (server->conn_pid)
		goto out;

	error = -EACCES;
	if (current->uid != server->mnt->mounted_uid && 
	    !capable(CAP_SYS_ADMIN))
		goto out;

	error = -EBADF;
	filp = fget(opt->fd);
	if (!filp)
		goto out;
	if (!smb_valid_socket(filp->f_dentry->d_inode))
		goto out_putf;

	server->sock_file = filp;
	server->conn_pid = current->pid;
	smb_catch_keepalive(server);
	server->opt = *opt;
	server->generation += 1;
	server->state = CONN_VALID;
	error = 0;
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_newconn: protocol=%d, max_xmit=%d, pid=%d\n",
server->opt.protocol, server->opt.max_xmit, server->conn_pid);
#endif

out:
	wake_up_interruptible(&server->wait);
	return error;

out_putf:
	fput(filp);
	goto out;
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
 */
static int
smb_proc_open(struct smb_sb_info *server, struct dentry *dentry, int wish)
{
	struct inode *ino = dentry->d_inode;
	int mode, read_write = 0x42, read_only = 0x40;
	int error;
	char *p;

	/*
	 * Attempt to open r/w, unless there are no write privileges.
	 */
	mode = read_write;
	if (!(ino->i_mode & (S_IWUSR | S_IWGRP | S_IWOTH)))
		mode = read_only;
#if 0
	if (!(wish & (O_WRONLY | O_RDWR)))
		mode = read_only;
#endif

      retry:
	p = smb_setup_header(server, SMBopen, 2, 0);
	WSET(server->packet, smb_vwv0, mode);
	WSET(server->packet, smb_vwv1, aSYSTEM | aHIDDEN | aDIR);
	*p++ = 4;
	p = smb_encode_path(server, p, dentry, NULL);
	smb_setup_bcc(server, p);

	error = smb_request_ok(server, SMBopen, 7, 0);
	if (error != 0)
	{
		if (smb_retry(server))
			goto retry;

		if (mode == read_write &&
		    (error == -EACCES || error == -ETXTBSY || error == -EROFS))
		{
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_proc_open: %s/%s R/W failed, error=%d, retrying R/O\n",
dentry->d_parent->d_name.name, dentry->d_name.name, error);
#endif
			mode = read_only;
			goto retry;
		}
		goto out;
	}
	/* We should now have data in vwv[0..6]. */

	ino->u.smbfs_i.fileid = WVAL(server->packet, smb_vwv0);
	ino->u.smbfs_i.attr   = WVAL(server->packet, smb_vwv1);
	/* smb_vwv2 has mtime */
	/* smb_vwv4 has size  */
	ino->u.smbfs_i.access = (WVAL(server->packet, smb_vwv6) & SMB_ACCMASK);
	ino->u.smbfs_i.open = server->generation;

out:
	return error;
}

/*
 * Make sure the file is open, and check that the access
 * is compatible with the desired access.
 */
int
smb_open(struct dentry *dentry, int wish)
{
	struct inode *inode = dentry->d_inode;
	int result;

	result = -ENOENT;
	if (!inode)
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
	if (!smb_is_open(inode))
	{
		struct smb_sb_info *server = SMB_SERVER(inode);
		smb_lock_server(server);
		result = 0;
		if (!smb_is_open(inode))
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

	/*
	 * Check whether the access is compatible with the desired mode.
	 */
	result = 0;
	if (inode->u.smbfs_i.access != wish && 
	    inode->u.smbfs_i.access != SMB_O_RDWR)
	{
#ifdef SMBFS_PARANOIA
printk("smb_open: %s/%s access denied, access=%x, wish=%x\n",
dentry->d_parent->d_name.name, dentry->d_name.name,
inode->u.smbfs_i.access, wish);
#endif
		result = -EACCES;
	}
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
 * Called with the server locked.
 *
 * Win NT 4.0 has an apparent bug in that it fails to update the
 * modify time when writing to a file. As a workaround, we update
 * both modify and access time locally, and post the times to the
 * server when closing the file.
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

		/*
		 * Kludge alert: SMB timestamps are accurate only to
		 * two seconds ... round the times to avoid needless
		 * cache invalidations!
		 */
		if (ino->i_mtime & 1)
			ino->i_mtime--;
		if (ino->i_atime & 1)
			ino->i_atime--;
		/*
		 * If the file is open with write permissions,
		 * update the time stamps to sync mtime and atime.
		 */
		if ((server->opt.protocol >= SMB_PROTOCOL_LANMAN2) &&
		    !(ino->u.smbfs_i.access == SMB_O_RDONLY))
		{
			struct smb_fattr fattr;
			smb_get_inode_attr(ino, &fattr);
			smb_proc_setattr_ext(server, ino, &fattr);
		}

		result = smb_proc_close(server, ino->u.smbfs_i.fileid,
						ino->i_mtime);
		ino->u.smbfs_i.cache_valid &= ~SMB_F_LOCALWRITE;
		/*
		 * Force a revalidation after closing ... some servers
		 * don't post the size until the file has been closed.
		 */
		if (server->opt.protocol < SMB_PROTOCOL_NT1)
			ino->u.smbfs_i.oldmtime = 0;
		ino->u.smbfs_i.closed = jiffies;
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

/*
 * This is used to close a file following a failed instantiate.
 * Since we don't have an inode, we can't use any of the above.
 */
int
smb_close_fileid(struct dentry *dentry, __u16 fileid)
{
	struct smb_sb_info *server = server_from_dentry(dentry);
	int result;

	smb_lock_server(server);
	result = smb_proc_close(server, fileid, CURRENT_TIME);
	smb_unlock_server(server);
	return result;
}

/* In smb_proc_read and smb_proc_write we do not retry, because the
   file-id would not be valid after a reconnection. */

int
smb_proc_read(struct dentry *dentry, off_t offset, int count, char *data)
{
	struct smb_sb_info *server = server_from_dentry(dentry);
	__u16 returned_count, data_len;
	char *buf;
	int result;

	smb_lock_server(server);
	smb_setup_header(server, SMBread, 5, 0);
	buf = server->packet;
	WSET(buf, smb_vwv0, dentry->d_inode->u.smbfs_i.fileid);
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
dentry->d_parent->d_name.name, dentry->d_name.name, count, result);
#endif
	smb_unlock_server(server);
	return result;
}

int
smb_proc_write(struct dentry *dentry, off_t offset, int count, const char *data)
{
	struct smb_sb_info *server = server_from_dentry(dentry);
	int result;
	__u8 *p;

#if SMBFS_DEBUG_VERBOSE
printk("smb_proc_write: file %s/%s, count=%d@%ld, packet_size=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, 
count, offset, server->packet_size);
#endif
	smb_lock_server(server);
	p = smb_setup_header(server, SMBwrite, 5, count + 3);
	WSET(server->packet, smb_vwv0, dentry->d_inode->u.smbfs_i.fileid);
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
smb_proc_create(struct dentry *dentry, __u16 attr, time_t ctime, __u16 *fileid)
{
	struct smb_sb_info *server = server_from_dentry(dentry);
	char *p;
	int error;

	smb_lock_server(server);

      retry:
	p = smb_setup_header(server, SMBcreate, 3, 0);
	WSET(server->packet, smb_vwv0, attr);
	DSET(server->packet, smb_vwv1, utc2local(ctime));
	*p++ = 4;
	p = smb_encode_path(server, p, dentry, NULL);
	smb_setup_bcc(server, p);

	error = smb_request_ok(server, SMBcreate, 1, 0);
	if (error < 0)
	{
		if (smb_retry(server))
			goto retry;
		goto out;
	}
	*fileid = WVAL(server->packet, smb_vwv0);
	error = 0;

out:
	smb_unlock_server(server);
	return error;
}

int
smb_proc_mv(struct dentry *old_dentry, struct dentry *new_dentry)
{
	struct smb_sb_info *server = server_from_dentry(old_dentry);
	char *p;
	int result;

	smb_lock_server(server);

      retry:
	p = smb_setup_header(server, SMBmv, 1, 0);
	WSET(server->packet, smb_vwv0, aSYSTEM | aHIDDEN);
	*p++ = 4;
	p = smb_encode_path(server, p, old_dentry, NULL);
	*p++ = 4;
	p = smb_encode_path(server, p, new_dentry, NULL);
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

/*
 * Code common to mkdir and rmdir.
 */
static int
smb_proc_generic_command(struct dentry *dentry, __u8 command)
{
	struct smb_sb_info *server = server_from_dentry(dentry);
	char *p;
	int result;

	smb_lock_server(server);

      retry:
	p = smb_setup_header(server, command, 0, 0);
	*p++ = 4;
	p = smb_encode_path(server, p, dentry, NULL);
	smb_setup_bcc(server, p);

	result = smb_request_ok(server, command, 0, 0);
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

int
smb_proc_mkdir(struct dentry *dentry)
{
	return smb_proc_generic_command(dentry, SMBmkdir);
}

int
smb_proc_rmdir(struct dentry *dentry)
{
	return smb_proc_generic_command(dentry, SMBrmdir);
}

int
smb_proc_unlink(struct dentry *dentry)
{
	struct smb_sb_info *server = server_from_dentry(dentry);
	char *p;
	int result;

	smb_lock_server(server);

      retry:
	p = smb_setup_header(server, SMBunlink, 1, 0);
	WSET(server->packet, smb_vwv0, aSYSTEM | aHIDDEN);
	*p++ = 4;
	p = smb_encode_path(server, p, dentry, NULL);
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
	/* Check the read-only flag */
	if (fattr->attr & aRONLY)
		fattr->f_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);

	fattr->f_blocks = 0;
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
	fattr->f_ino = 2; /* traditional root inode number */
	fattr->f_mtime = CURRENT_TIME;
	smb_finish_dirent(server, fattr);
}

/*
 * Note that we are now returning the name as a reference to avoid
 * an extra copy, and that the upper/lower casing is done in place.
 *
 * Bugs Noted:
 * (1) Pathworks servers may pad the name with extra spaces.
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
 *
 * We return a reference to the name string to avoid copying, and perform
 * any needed upper/lower casing in place.  Note!! Level 259 entries may
 * not have any space beyond the name, so don't try to write a null byte!
 *
 * Bugs Noted:
 * (1) Win NT 4.0 appends a null byte to names and counts it in the length!
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

/*
 * Bugs Noted:
 * (1) When using Info Level 1 Win NT 4.0 truncates directory listings 
 * for certain patterns of names and/or lengths. The breakage pattern
 * is completely reproducible and can be toggled by the creation of a
 * single file. (E.g. echo hi >foo breaks, rm -f foo works.)
 */
static int
smb_proc_readdir_long(struct smb_sb_info *server, struct dentry *dir, int fpos,
		      void *cachep)
{
	char *p, *mask, *lastname, *param = server->temp_buf;
	__u16 command;
	int first, entries, entries_seen;

	/* Both NT and OS/2 accept info level 1 (but see note below). */
	int info_level = 1;
	const int max_matches = 512;

	unsigned char *resp_data = NULL;
	unsigned char *resp_param = NULL;
	int resp_data_len = 0;
	int resp_param_len = 0;
	int ff_resume_key = 0; /* this isn't being used */
	int ff_searchcount = 0;
	int ff_eos = 0;
	int ff_lastname = 0;
	int ff_dir_handle = 0;
	int loop_count = 0;
	int mask_len, i, result;
	static struct qstr star = { "*", 1, 0 };

	/*
	 * Check whether to change the info level.  There appears to be
	 * a bug in Win NT 4.0's handling of info level 1, whereby it
	 * truncates the directory scan for certain patterns of files.
	 * Hence we use level 259 for NT.
	 */
	if (server->opt.protocol >= SMB_PROTOCOL_NT1 &&
	    !(server->mnt->version & SMB_FIX_WIN95))
		info_level = 259;

	smb_lock_server(server);

      retry:
	/*
	 * Encode the initial path
	 */
	mask = param + 12;
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
			if (server->mnt->version & SMB_FIX_WIN95)
			{
				/* Windows 95 is not able to deliver answers
				 * to FIND_NEXT fast enough, so sleep 0.2 sec
				 */
				current->state = TASK_INTERRUPTIBLE;
				schedule_timeout(HZ/5);
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
printk("smb_proc_readdir_long: name=%s, entries=%d, rcls=%d, err=%d\n",
mask, entries, server->rcls, server->err);
#endif
			entries = -smb_errno(server);
			break;
		}

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

/*
 * This version uses the trans2 TRANSACT2_FINDFIRST message 
 * to get the attribute data.
 * Note: called with the server locked.
 *
 * Bugs Noted:
 */
static int
smb_proc_getattr_ff(struct smb_sb_info *server, struct dentry *dentry,
			struct smb_fattr *fattr)
{
	char *param = server->temp_buf, *mask = param + 12;
	__u16 date, time;
	unsigned char *resp_data = NULL;
	unsigned char *resp_param = NULL;
	int resp_data_len = 0;
	int resp_param_len = 0;
	int mask_len, result;

retry:
	mask_len = smb_encode_path(server, mask, dentry, NULL) - mask;
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_proc_getattr_ff: name=%s, len=%d\n", mask, mask_len);
#endif
	WSET(param, 0, aSYSTEM | aHIDDEN | aDIR);
	WSET(param, 2, 1);	/* max count */
	WSET(param, 4, 1);	/* close after this call */
	WSET(param, 6, 1);	/* info_level */
	DSET(param, 8, 0);

	result = smb_trans2_request(server, TRANSACT2_FINDFIRST,
				    0, NULL, 12 + mask_len + 1, param,
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
		result = -smb_errno(server);
#ifdef SMBFS_PARANOIA
if (result != -ENOENT)
printk("smb_proc_getattr_ff: error for %s, rcls=%d, err=%d\n",
mask, server->rcls, server->err);
#endif
		goto out;
	}
	/* Make sure we got enough data ... */
	result = -EINVAL;
	if (resp_data_len < 22 || WVAL(resp_param, 2) != 1)
	{
#ifdef SMBFS_PARANOIA
printk("smb_proc_getattr_ff: bad result for %s, len=%d, count=%d\n",
mask, resp_data_len, WVAL(resp_param, 2));
#endif
		goto out;
	}

	/*
	 * Decode the response into the fattr ...
	 */
	date = WVAL(resp_data, 0);
	time = WVAL(resp_data, 2);
	fattr->f_ctime = date_dos2unix(date, time);

	date = WVAL(resp_data, 4);
	time = WVAL(resp_data, 6);
	fattr->f_atime = date_dos2unix(date, time);

	date = WVAL(resp_data, 8);
	time = WVAL(resp_data, 10);
	fattr->f_mtime = date_dos2unix(date, time);
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_proc_getattr_ff: name=%s, date=%x, time=%x, mtime=%ld\n",
mask, date, time, fattr->f_mtime);
#endif
	fattr->f_size = DVAL(resp_data, 12);
	/* ULONG allocation size */
	fattr->attr = WVAL(resp_data, 20);
	result = 0;

out:
	return result;
}

/*
 * Note: called with the server locked.
 */
static int
smb_proc_getattr_core(struct smb_sb_info *server, struct dentry *dir,
			struct smb_fattr *fattr)
{
	int result;
	char *p;

      retry:
	p = smb_setup_header(server, SMBgetatr, 0, 0);
	*p++ = 4;
	p = smb_encode_path(server, p, dir, NULL);
	smb_setup_bcc(server, p);

	if ((result = smb_request_ok(server, SMBgetatr, 10, 0)) < 0)
	{
		if (smb_retry(server))
			goto retry;
		goto out;
	}
	fattr->attr    = WVAL(server->packet, smb_vwv0);
	fattr->f_mtime = local2utc(DVAL(server->packet, smb_vwv1));
	fattr->f_size  = DVAL(server->packet, smb_vwv3);
	fattr->f_ctime = fattr->f_mtime; 
	fattr->f_atime = fattr->f_mtime; 
#ifdef SMBFS_DEBUG_TIMESTAMP
printk("getattr_core: %s/%s, mtime=%ld\n",
dir->d_name.name, name->name, fattr->f_mtime);
#endif
	result = 0;

out:
	return result;
}

/*
 * Note: called with the server locked.
 *
 * Bugs Noted:
 * (1) Win 95 swaps the date and time fields in the standard info level.
 */
static int
smb_proc_getattr_trans2(struct smb_sb_info *server, struct dentry *dir,
			struct smb_fattr *attr)
{
	char *p, *param = server->temp_buf;
	__u16 date, time;
	int off_date = 0, off_time = 2;
	unsigned char *resp_data = NULL;
	unsigned char *resp_param = NULL;
	int resp_data_len = 0;
	int resp_param_len = 0;
	int result;

      retry:
	WSET(param, 0, 1);	/* Info level SMB_INFO_STANDARD */
	DSET(param, 2, 0);
	p = smb_encode_path(server, param + 6, dir, NULL);

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
		result = -smb_errno(server);
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

	/*
	 * Kludge alert: Win 95 swaps the date and time field,
	 * contrary to the CIFS docs and Win NT practice.
	 */
	if (server->mnt->version & SMB_FIX_WIN95) {
		off_date = 2;
		off_time = 0;
	}
	date = WVAL(resp_data, off_date);
	time = WVAL(resp_data, off_time);
	attr->f_ctime = date_dos2unix(date, time);

	date = WVAL(resp_data, 4 + off_date);
	time = WVAL(resp_data, 4 + off_time);
	attr->f_atime = date_dos2unix(date, time);

	date = WVAL(resp_data, 8 + off_date);
	time = WVAL(resp_data, 8 + off_time);
	attr->f_mtime = date_dos2unix(date, time);
#ifdef SMBFS_DEBUG_TIMESTAMP
printk("getattr_trans2: %s/%s, date=%x, time=%x, mtime=%ld\n",
dir->d_name.name, name->name, date, time, attr->f_mtime);
#endif
	attr->f_size = DVAL(resp_data, 12);
	attr->attr = WVAL(resp_data, 20);
	result = 0;

out:
	return result;
}

int
smb_proc_getattr(struct dentry *dir, struct smb_fattr *fattr)
{
	struct smb_sb_info *server = server_from_dentry(dir);
	int result;

	smb_lock_server(server);
	smb_init_dirent(server, fattr);

	/*
	 * Select whether to use core or trans2 getattr.
 	 */
	if (server->opt.protocol >= SMB_PROTOCOL_LANMAN2) {
		/*
		 * Win 95 appears to break with the trans2 getattr.
 	 	 */
		if (server->mnt->version & (SMB_FIX_OLDATTR|SMB_FIX_WIN95))
			goto core_attr;
		if (server->mnt->version & SMB_FIX_DIRATTR)
			result = smb_proc_getattr_ff(server, dir, fattr);
		else
			result = smb_proc_getattr_trans2(server, dir, fattr);
	} else {
	core_attr:
		result = smb_proc_getattr_core(server, dir, fattr);
	}

	smb_finish_dirent(server, fattr);

	smb_unlock_server(server);
	return result;
}

/*
 * Called with the server locked. Because of bugs in the
 * core protocol, we use this only to set attributes. See
 * smb_proc_settime() below for timestamp handling.
 *
 * Bugs Noted:
 * (1) If mtime is non-zero, both Win 3.1 and Win 95 fail
 * with an undocumented error (ERRDOS code 50). Setting
 * mtime to 0 allows the attributes to be set.
 * (2) The extra parameters following the name string aren't
 * in the CIFS docs, but seem to be necessary for operation.
 */
static int
smb_proc_setattr_core(struct smb_sb_info *server, struct dentry *dentry,
			__u16 attr)
{
	char *p;
	int result;

      retry:
	p = smb_setup_header(server, SMBsetatr, 8, 0);
	WSET(server->packet, smb_vwv0, attr);
	DSET(server->packet, smb_vwv1, 0); /* mtime */
	WSET(server->packet, smb_vwv3, 0); /* reserved values */
	WSET(server->packet, smb_vwv4, 0);
	WSET(server->packet, smb_vwv5, 0);
	WSET(server->packet, smb_vwv6, 0);
	WSET(server->packet, smb_vwv7, 0);
	*p++ = 4;
	/*
	 * Samba uses three leading '\', so we'll do it too.
	 */
	*p++ = '\\';
	*p++ = '\\';
	p = smb_encode_path(server, p, dentry, NULL);
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
	return result;
}

/*
 * Because of bugs in the trans2 setattr messages, we must set
 * attributes and timestamps separately. The core SMBsetatr
 * message seems to be the only reliable way to set attributes.
 */
int
smb_proc_setattr(struct dentry *dir, struct smb_fattr *fattr)
{
	struct smb_sb_info *server = server_from_dentry(dir);
	int result;

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_proc_setattr: setting %s/%s, open=%d\n", 
dir->d_parent->d_name.name, dir->d_name.name, smb_is_open(dir->d_inode));
#endif
	smb_lock_server(server);
	result = smb_proc_setattr_core(server, dir, fattr->attr);
	smb_unlock_server(server);
	return result;
}

/*
 * Called with the server locked. Sets the timestamps for an
 * file open with write permissions.
 */
static int
smb_proc_setattr_ext(struct smb_sb_info *server,
		      struct inode *inode, struct smb_fattr *fattr)
{
	__u16 date, time;
	int result;

      retry:
	smb_setup_header(server, SMBsetattrE, 7, 0);
	WSET(server->packet, smb_vwv0, inode->u.smbfs_i.fileid);
	/* We don't change the creation time */
	WSET(server->packet, smb_vwv1, 0);
	WSET(server->packet, smb_vwv2, 0);
	date_unix2dos(fattr->f_atime, &date, &time);
	WSET(server->packet, smb_vwv3, date);
	WSET(server->packet, smb_vwv4, time);
	date_unix2dos(fattr->f_mtime, &date, &time);
	WSET(server->packet, smb_vwv5, date);
	WSET(server->packet, smb_vwv6, time);
#ifdef SMBFS_DEBUG_TIMESTAMP
printk("smb_proc_setattr_ext: date=%d, time=%d, mtime=%ld\n", 
date, time, fattr->f_mtime);
#endif

	result = smb_request_ok(server, SMBsetattrE, 0, 0);
	if (result < 0)
	{
		if (smb_retry(server))
			goto retry;
		goto out;
	}
	result = 0;
out:
	return result;
}

/*
 * Note: called with the server locked.
 *
 * Bugs Noted:
 * (1) The TRANSACT2_SETPATHINFO message under Win NT 4.0 doesn't
 * set the file's attribute flags.
 */
static int
smb_proc_setattr_trans2(struct smb_sb_info *server,
			struct dentry *dir, struct smb_fattr *fattr)
{
	__u16 date, time;
	char *p, *param = server->temp_buf;
	unsigned char *resp_data = NULL;
	unsigned char *resp_param = NULL;
	int resp_data_len = 0;
	int resp_param_len = 0;
	int result;
	char data[26];

      retry:
	WSET(param, 0, 1);	/* Info level SMB_INFO_STANDARD */
	DSET(param, 2, 0);
	p = smb_encode_path(server, param + 6, dir, NULL);

	WSET(data, 0, 0); /* creation time */
	WSET(data, 2, 0);
	date_unix2dos(fattr->f_atime, &date, &time);
	WSET(data, 4, date);
	WSET(data, 6, time);
	date_unix2dos(fattr->f_mtime, &date, &time);
	WSET(data, 8, date);
	WSET(data, 10, time);
#ifdef SMBFS_DEBUG_TIMESTAMP
printk("setattr_trans2: %s/%s, date=%x, time=%x, mtime=%ld\n", 
dir->d_parent->d_name.name, dir->d_name.name, date, time, fattr->f_mtime);
#endif
	DSET(data, 12, 0); /* size */
	DSET(data, 16, 0); /* blksize */
	WSET(data, 20, 0); /* attr */
	DSET(data, 22, 0); /* ULONG EA size */

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
		result = -smb_errno(server);

out:
	return result;
}

/*
 * Set the modify and access timestamps for a file.
 *
 * Incredibly enough, in all of SMB there is no message to allow
 * setting both attributes and timestamps at once. 
 *
 * Bugs Noted:
 * (1) Win 95 doesn't support the TRANSACT2_SETFILEINFO message 
 * with info level 1 (INFO_STANDARD).
 * (2) Win 95 seems not to support setting directory timestamps.
 * (3) Under the core protocol apparently the only way to set the
 * timestamp is to open and close the file.
 */
int
smb_proc_settime(struct dentry *dentry, struct smb_fattr *fattr)
{
	struct smb_sb_info *server = server_from_dentry(dentry);
	struct inode *inode = dentry->d_inode;
	int result;

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_proc_settime: setting %s/%s, open=%d\n", 
dentry->d_parent->d_name.name, dentry->d_name.name, smb_is_open(inode));
#endif
	smb_lock_server(server);
	if (server->opt.protocol >= SMB_PROTOCOL_LANMAN2)
	{
		if (smb_is_open(inode) &&
		    inode->u.smbfs_i.access != SMB_O_RDONLY)
			result = smb_proc_setattr_ext(server, inode, fattr);
		else
			result = smb_proc_setattr_trans2(server, dentry, fattr);
	} else
	{
		/*
		 * Fail silently on directories ... timestamp can't be set?
		 */
		result = 0;
		if (S_ISREG(inode->i_mode))
		{
			/*
			 * Set the mtime by opening and closing the file.
			 */
			result = -EACCES;
			if (!smb_is_open(inode))
				smb_proc_open(server, dentry, SMB_O_WRONLY);
			if (smb_is_open(inode) &&
			    inode->u.smbfs_i.access != SMB_O_RDONLY)
			{
				inode->i_mtime = fattr->f_mtime;
				result = smb_proc_close_inode(server, inode);
			}
		}
	}

	smb_unlock_server(server);
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
	attr->f_blocks = WVAL(p, 0);
	attr->f_bsize  = WVAL(p, 2) * WVAL(p, 4);
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
