/*
 *  proc.c
 *
 *  Copyright (C) 1995, 1996 by Paal-Kr. Engstad and Volker Lendecke
 *  Copyright (C) 1997 by Volker Lendecke
 *
 *  28/06/96 - Fixed long file name support (smb_proc_readdir_long) by Yuri Per
 */

#include <linux/config.h>
#include <linux/fs.h>
#include <linux/smbno.h>
#include <linux/smb_fs.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/stat.h>
#include <linux/fcntl.h>

#include <asm/uaccess.h>
#include <asm/string.h>

#define SMB_VWV(packet)  ((packet) + SMB_HEADER_LEN)
#define SMB_CMD(packet)  (*(packet+8))
#define SMB_WCT(packet)  (*(packet+SMB_HEADER_LEN - 1))
#define SMB_BCC(packet)  smb_bcc(packet)
#define SMB_BUF(packet)  ((packet) + SMB_HEADER_LEN + SMB_WCT(packet) * 2 + 2)

#define SMB_DIRINFO_SIZE 43
#define SMB_STATUS_SIZE  21

static int smb_request_ok(struct smb_sb_info *s, int command, int wct, int bcc);

static inline int
min(int a, int b)
{
	return a < b ? a : b;
}

static void
str_upper(char *name)
{
	while (*name)
	{
		if (*name >= 'a' && *name <= 'z')
			*name -= ('a' - 'A');
		name++;
	}
}

static void
str_lower(char *name)
{
	while (*name)
	{
		if (*name >= 'A' && *name <= 'Z')
			*name += ('a' - 'A');
		name++;
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

static int smb_d_path(struct dentry * entry, char * buf)
{
	if (IS_ROOT(entry)) {
		*buf = '\\';
		return 1;
	} else {
		int len = smb_d_path(entry->d_parent, buf);

		buf += len;
		if (len > 1) {
			*buf++ = '\\';
			len++;
		}
		memcpy(buf, entry->d_name.name, entry->d_name.len);
		return len + entry->d_name.len;
	}
}

static char *smb_encode_path(struct smb_sb_info *server, char *buf,
			     struct dentry *dir, struct qstr *name)
{
	char *start = buf;

	if (dir != NULL)
		buf += smb_d_path(dir, buf);

	if (name != NULL) {
		*buf++ = '\\';
		memcpy(buf, name->name, name->len);
		buf += name->len;
		*buf++ = 0;
	}

	if (server->opt.protocol <= SMB_PROTOCOL_COREPLUS)
		str_upper(start);

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
			/* This next error seems to occur on an mv when
			 * the destination exists */
		case 183:
			return EEXIST;
		default:
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
			return EIO;
	} else if (errcls == ERRCMD)
		return EIO;
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

	if (smb_request(s) < 0)
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

/* smb_retry: This function should be called when smb_request_ok has
   indicated an error. If the error was indicated because the
   connection was killed, we try to reconnect. If smb_retry returns 0,
   the error was indicated for another reason, so a retry would not be
   of any use. */

static int
smb_retry(struct smb_sb_info *server)
{
	if (server->state != CONN_INVALID)
	{
		return 0;
	}
	if (server->sock_file != NULL)
	{
		close_fp(server->sock_file);
		server->sock_file = NULL;
	}

	if (server->conn_pid == 0)
	{
		server->state = CONN_RETRIED;
		return 0;
	}

	kill_proc(server->conn_pid, SIGUSR1, 0);
	server->conn_pid = 0;

	smb_lock_server(server);

	if (server->sock_file != NULL)
	{
		server->state = CONN_VALID;
		return 1;
	}
	return 0;
}

int
smb_offerconn(struct smb_sb_info *server)
{
	if (!suser() && (current->uid != server->m.mounted_uid))
	{
		return -EACCES;
	}
	server->conn_pid = current->pid;
	return 0;
}

int
smb_newconn(struct smb_sb_info *server, struct smb_conn_opt *opt)
{
	struct file *filp;

	if (opt->fd >= NR_OPEN || !(filp = current->files->fd[opt->fd]))
	{
		return -EBADF;
	}
	if (!S_ISSOCK(filp->f_dentry->d_inode->i_mode))
	{
		return -EBADF;
	}
	if (!suser() && (current->uid != server->m.mounted_uid))
	{
		return -EACCES;
	}
	if (server->sock_file != NULL)
	{
		close_fp(server->sock_file);
		server->sock_file = NULL;
	}
	filp->f_count += 1;
	server->sock_file = filp;
	smb_catch_keepalive(server);
	server->opt = *opt;
	pr_debug("smb_newconn: protocol = %d\n", server->opt.protocol);
	server->conn_pid = 0;
	server->generation += 1;
	smb_unlock_server(server);
	return 0;
}

/* smb_setup_header: We completely set up the packet. You only have to
   insert the command-specific fields */

__u8 *
smb_setup_header(struct smb_sb_info * server, __u8 command, __u16 wct, __u16 bcc)
{
	__u32 xmit_len = SMB_HEADER_LEN + wct * sizeof(__u16) + bcc + 2;
	__u8 *p = server->packet;
	__u8 *buf = server->packet;

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
 * We're called with the server locked, and we leave it that way. We
 * try maximum permissions.
 */

static int
smb_proc_open(struct dentry *dir)
{
	struct inode *ino = dir->d_inode;
	struct smb_sb_info *server = SMB_SERVER(ino);
	int error;
	char *p;

      retry:
	p = smb_setup_header(server, SMBopen, 2, 0);
	WSET(server->packet, smb_vwv0, 0x42);	/* read/write */
	WSET(server->packet, smb_vwv1, aSYSTEM | aHIDDEN | aDIR);
	*p++ = 4;
	p = smb_encode_path(server, p, dir, NULL);
	smb_setup_bcc(server, p);

	if ((error = smb_request_ok(server, SMBopen, 7, 0)) != 0)
	{
		if (smb_retry(server))
			goto retry;

		if ((error != -EACCES) && (error != -ETXTBSY)
		    && (error != -EROFS))
			return error;

		p = smb_setup_header(server, SMBopen, 2, 0);
		WSET(server->packet, smb_vwv0, 0x40);	/* read only */
		WSET(server->packet, smb_vwv1, aSYSTEM | aHIDDEN | aDIR);
		*p++ = 4;
		p = smb_encode_path(server, p, dir, NULL);
		smb_setup_bcc(server, p);

		if ((error = smb_request_ok(server, SMBopen, 7, 0)) != 0)
		{
			if (smb_retry(server))
				goto retry;

			return error;
		}
	}
	/* We should now have data in vwv[0..6]. */

	ino->u.smbfs_i.fileid = WVAL(server->packet, smb_vwv0);
	ino->u.smbfs_i.attr = WVAL(server->packet, smb_vwv1);
	ino->u.smbfs_i.access = WVAL(server->packet, smb_vwv6);
	ino->u.smbfs_i.access &= 3;

	ino->u.smbfs_i.open = server->generation;

	pr_debug("smb_proc_open: entry->access = %d\n", ino->u.smbfs_i.access);
	return 0;
}

int
smb_open(struct dentry *dir, int wish)
{
	struct inode *i=dir->d_inode;
	struct smb_sb_info *server = SMB_SERVER(i);
	int result = -EACCES;

	smb_lock_server(server);

	if (!smb_is_open(i)) {
		int error = smb_proc_open(dir);
		if (error) {
			smb_unlock_server(server);
			return error;
		}
	}

	if (((wish == O_RDONLY) && ((i->u.smbfs_i.access == O_RDONLY)
				     || (i->u.smbfs_i.access == O_RDWR)))
	    || ((wish == O_WRONLY) && ((i->u.smbfs_i.access == O_WRONLY)
					|| (i->u.smbfs_i.access == O_RDWR)))
	    || ((wish == O_RDWR) && (i->u.smbfs_i.access == O_RDWR)))
		result = 0;

	smb_unlock_server(server);
	return result;
}

/* We're called with the server locked */

static int smb_proc_close(struct smb_sb_info *server,
			  __u16 fileid, __u32 mtime)
{
	smb_setup_header(server, SMBclose, 3, 0);
	WSET(server->packet, smb_vwv0, fileid);
	DSET(server->packet, smb_vwv1, mtime);
	return smb_request_ok(server, SMBclose, 0, 0);
}
	

int smb_close(struct dentry *dir)
{
	struct inode *ino = dir->d_inode;
	struct smb_sb_info *server = SMB_SERVER(ino);
	int result;

	smb_lock_server(server);

	if (!smb_is_open(ino)) {
		smb_unlock_server(server);
		return 0;
	}

	result = smb_proc_close(server, ino->u.smbfs_i.fileid, ino->i_mtime);
	ino->u.smbfs_i.open = 0;
	smb_unlock_server(server);
	return result;
}

/* In smb_proc_read and smb_proc_write we do not retry, because the
   file-id would not be valid after a reconnection. */

/* smb_proc_read: fs indicates if it should be copied with
   copy_to_user. */

int
smb_proc_read(struct inode *ino, off_t offset, long count, char *data)
{
	struct smb_sb_info *server = SMB_SERVER(ino);
	__u16 returned_count, data_len;
	char *buf;
	int error;

	smb_lock_server(server);
	smb_setup_header(server, SMBread, 5, 0);
	buf = server->packet;

	WSET(buf, smb_vwv0, ino->u.smbfs_i.fileid);
	WSET(buf, smb_vwv1, count);
	DSET(buf, smb_vwv2, offset);
	WSET(buf, smb_vwv4, 0);

	if ((error = smb_request_ok(server, SMBread, 5, -1)) < 0)
	{
		smb_unlock_server(server);
		return error;
	}
	returned_count = WVAL(buf, smb_vwv0);

	buf = SMB_BUF(server->packet);
	data_len = WVAL(buf, 1);

	memcpy(data, buf+3, data_len);

	smb_unlock_server(server);

	if (returned_count != data_len)
	{
		printk(KERN_NOTICE "smb_proc_read: returned != data_len\n");
		printk(KERN_NOTICE "smb_proc_read: ret_c=%d, data_len=%d\n",
		       returned_count, data_len);
	}
	return data_len;
}

int
smb_proc_write(struct inode *ino, off_t offset, int count, const char *data)
{
	struct smb_sb_info *server = SMB_SERVER(ino);
	int res = 0;
	__u8 *p;

	smb_lock_server(server);
	p = smb_setup_header(server, SMBwrite, 5, count + 3);
	WSET(server->packet, smb_vwv0, ino->u.smbfs_i.fileid);
	WSET(server->packet, smb_vwv1, count);
	DSET(server->packet, smb_vwv2, offset);
	WSET(server->packet, smb_vwv4, 0);

	*p++ = 1;
	WSET(p, 0, count);
	memcpy(p+2, data, count);

	if ((res = smb_request_ok(server, SMBwrite, 1, 0)) >= 0)
		res = WVAL(server->packet, smb_vwv0);

	smb_unlock_server(server);

	return res;
}

int
smb_proc_create(struct dentry *dir, struct qstr *name,
		__u16 attr, time_t ctime)
{
	int error;
	char *p;
	struct inode *i=dir->d_inode;
	struct smb_sb_info *server = SMB_SERVER(i);
	char *buf;

	smb_lock_server(server);
      retry:
	buf = server->packet;
	p = smb_setup_header(server, SMBcreate, 3, 0);
	WSET(buf, smb_vwv0, attr);
	DSET(buf, smb_vwv1, utc2local(ctime));
	*p++ = 4;
	p = smb_encode_path(server, p, dir, name);
	smb_setup_bcc(server, p);

	if ((error = smb_request_ok(server, SMBcreate, 1, 0)) < 0)
	{
		if (smb_retry(server))
		{
			goto retry;
		}
		smb_unlock_server(server);
		return error;
	}
	smb_proc_close(server, WVAL(buf, smb_vwv0), CURRENT_TIME);
	smb_unlock_server(server);

	return 0;
}

int
smb_proc_mv(struct dentry *odir, struct qstr *oname,
	    struct dentry *ndir, struct qstr *nname)
{
	char *p;
	struct smb_sb_info *server = SMB_SERVER(odir->d_inode);
	int result;

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
		{
			goto retry;
		}
	}
	smb_unlock_server(server);
	return result;
}

int
smb_proc_mkdir(struct dentry *dir, struct qstr *name)
{
	char *p;
	int result;
	struct smb_sb_info *server = SMB_SERVER(dir->d_inode);

	smb_lock_server(server);

      retry:
	p = smb_setup_header(server, SMBmkdir, 0, 0);
	*p++ = 4;
	p = smb_encode_path(server, p, dir, name);
	smb_setup_bcc(server, p);

	if ((result = smb_request_ok(server, SMBmkdir, 0, 0)) < 0)
	{
		if (smb_retry(server))
		{
			goto retry;
		}
	}
	smb_unlock_server(server);
	return result;
}

int
smb_proc_rmdir(struct dentry *dir, struct qstr *name)
{
	char *p;
	int result;
	struct smb_sb_info *server = SMB_SERVER(dir->d_inode);

	smb_lock_server(server);

      retry:
	p = smb_setup_header(server, SMBrmdir, 0, 0);
	*p++ = 4;
	p = smb_encode_path(server, p, dir, name);
	smb_setup_bcc(server, p);

	if ((result = smb_request_ok(server, SMBrmdir, 0, 0)) < 0)
	{
		if (smb_retry(server))
		{
			goto retry;
		}
	}
	smb_unlock_server(server);
	return result;
}

int
smb_proc_unlink(struct dentry *dir, struct qstr *name)
{
	char *p;
	struct smb_sb_info *server = SMB_SERVER(dir->d_inode);
	int result;

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
		{
			goto retry;
		}
	}
	smb_unlock_server(server);
	return result;
}

int
smb_proc_trunc(struct smb_sb_info *server, __u16 fid, __u32 length)
{
	char *p;
	char *buf;
	int result;

	smb_lock_server(server);

      retry:
	buf = server->packet;
	p = smb_setup_header(server, SMBwrite, 5, 0);
	WSET(buf, smb_vwv0, fid);
	WSET(buf, smb_vwv1, 0);
	DSET(buf, smb_vwv2, length);
	WSET(buf, smb_vwv4, 0);
	*p++ = 4;
	*p++ = 0;
	smb_setup_bcc(server, p);

	if ((result = smb_request_ok(server, SMBwrite, 1, 0)) < 0)
	{
		if (smb_retry(server))
		{
			goto retry;
		}
	}
	smb_unlock_server(server);
	return result;
}

static void
smb_init_dirent(struct smb_sb_info *server, struct smb_fattr *fattr)
{
	memset(fattr, 0, sizeof(*fattr));

	fattr->f_nlink = 1;
	fattr->f_uid = server->m.uid;
	fattr->f_gid = server->m.gid;
	fattr->f_blksize = 512;
}

static void
smb_finish_dirent(struct smb_sb_info *server, struct smb_fattr *fattr)
{
	if (fattr->attr & aDIR)
	{
		fattr->f_mode = server->m.dir_mode;
		fattr->f_size = 512;
	} else
	{
		fattr->f_mode = server->m.file_mode;
	}

	if ((fattr->f_blksize != 0) && (fattr->f_size != 0))
	{
		fattr->f_blocks =
		    (fattr->f_size - 1) / fattr->f_blksize + 1;
	} else
	{
		fattr->f_blocks = 0;
	}
	return;
}

void
smb_init_root_dirent(struct smb_sb_info *server, struct smb_fattr *fattr)
{
	smb_init_dirent(server, fattr);
	fattr->attr = aDIR;
	fattr->f_ino = 1;
	smb_finish_dirent(server, fattr);
}


static __u8 *
smb_decode_dirent(struct smb_sb_info *server, __u8 *p,
		  struct smb_dirent *entry)
{
	smb_init_dirent(server, &(entry->attr));

	p += SMB_STATUS_SIZE;	/* reserved (search_status) */
	entry->attr.attr = *p;
	entry->attr.f_mtime = entry->attr.f_atime = entry->attr.f_ctime =
	    date_dos2unix(WVAL(p, 1), WVAL(p, 3));
	entry->attr.f_size = DVAL(p, 5);
	entry->len = strlen(p + 9);
	if (entry->len > 12)
	{
		entry->len = 12;
	}
	memcpy(entry->name, p + 9, entry->len);
	entry->name[entry->len] = '\0';
	while (entry->len > 2)
	{
		/* Pathworks fills names with spaces */
		entry->len -= 1;
		if (entry->name[entry->len] == ' ')
		{
			entry->name[entry->len] = '\0';
		}
	}
	switch (server->opt.case_handling)
	{
	case SMB_CASE_UPPER:
		str_upper(entry->name);
		break;
	case SMB_CASE_LOWER:
		str_lower(entry->name);
		break;
	default:
		break;
	}
	pr_debug("smb_decode_dirent: name = %s\n", entry->name);
	smb_finish_dirent(server, &(entry->attr));
	return p + 22;
}

/* This routine is used to read in directory entries from the network.
   Note that it is for short directory name seeks, i.e.: protocol <
   SMB_PROTOCOL_LANMAN2 */

static int
smb_proc_readdir_short(struct smb_sb_info *server, struct dentry *dir, int fpos,
		       int cache_size, struct smb_dirent *entry)
{
	char *p;
	char *buf;
	int error;
	int result;
	int i;
	int first, total_count;
	struct smb_dirent *current_entry;
	__u16 bcc;
	__u16 count;
	char status[SMB_STATUS_SIZE];
	int entries_asked = (server->opt.max_xmit - 100) / SMB_DIRINFO_SIZE;

	static struct qstr mask = { "*.*", 3, 0 };

	pr_debug("SMB call  readdir %d @ %d\n", cache_size, fpos);

	smb_lock_server(server);

      retry:
	buf = server->packet;
	first = 1;
	total_count = 0;
	current_entry = entry;

	while (1)
	{
		if (first == 1)
		{
			p = smb_setup_header(server, SMBsearch, 2, 0);
			WSET(buf, smb_vwv0, entries_asked);
			WSET(buf, smb_vwv1, aDIR);
			*p++ = 4;
			p = smb_encode_path(server, p, dir, &mask);
			*p++ = 5;
			WSET(p, 0, 0);
			p += 2;
		} else
		{
			p = smb_setup_header(server, SMBsearch, 2, 0);
			WSET(buf, smb_vwv0, entries_asked);
			WSET(buf, smb_vwv1, aDIR);
			*p++ = 4;
			*p++ = 0;
			*p++ = 5;
			WSET(p, 0, SMB_STATUS_SIZE);
			p += 2;
			memcpy(p, status, SMB_STATUS_SIZE);
			p += SMB_STATUS_SIZE;
		}

		smb_setup_bcc(server, p);

		if ((error = smb_request_ok(server, SMBsearch, 1, -1)) < 0)
		{
			if ((server->rcls == ERRDOS)
			    && (server->err == ERRnofiles))
			{
				result = total_count - fpos;
				goto unlock_return;
			} else
			{
				if (smb_retry(server))
				{
					goto retry;
				}
				result = error;
				goto unlock_return;
			}
		}
		p = SMB_VWV(server->packet);
		count = WVAL(p, 0);
		bcc = WVAL(p, 2);

		first = 0;

		if (count <= 0)
		{
			result = total_count - fpos;
			goto unlock_return;
		}
		if (bcc != count * SMB_DIRINFO_SIZE + 3)
		{
			result = -EIO;
			goto unlock_return;
		}
		p += 7;

		/* Read the last entry into the status field. */
		memcpy(status,
		       SMB_BUF(server->packet) + 3 +
		       (count - 1) * SMB_DIRINFO_SIZE,
		       SMB_STATUS_SIZE);

		/* Now we are ready to parse smb directory entries. */

		for (i = 0; i < count; i++)
		{
			if (total_count < fpos)
			{
				p += SMB_DIRINFO_SIZE;
				pr_debug("smb_proc_readdir: skipped entry.\n");
				pr_debug("                  total_count = %d\n"
					 "                i = %d, fpos = %d\n",
					 total_count, i, fpos);
			} else if (total_count >= fpos + cache_size)
			{
				result = total_count - fpos;
				goto unlock_return;
			} else
			{
				p = smb_decode_dirent(server, p,
						      current_entry);
				current_entry->f_pos = total_count;
				pr_debug("smb_proc_readdir: entry->f_pos = "
					 "%u\n", entry->f_pos);
				current_entry += 1;
			}
			total_count += 1;
		}
	}
      unlock_return:
	smb_unlock_server(server);
	return result;
}

/* interpret a long filename structure - this is mostly guesses at the
   moment.  The length of the structure is returned.  The structure of
   a long filename depends on the info level. 260 is used by NT and 2
   is used by OS/2. */

static char *
smb_decode_long_dirent(struct smb_sb_info *server, char *p,
		       struct smb_dirent *entry, int level)
{
	char *result;

	smb_init_dirent(server, &(entry->attr));

	switch (level)
	{
		/* We might add more levels later... */
	case 1:
		entry->len = *(p+26);
		strncpy(entry->name, p + 27, entry->len);
		entry->name[entry->len] = '\0';
		entry->attr.f_size = DVAL(p, 16);
		entry->attr.attr = *(p+24);

		entry->attr.f_ctime = date_dos2unix(WVAL(p, 6), WVAL(p, 4));
		entry->attr.f_atime = date_dos2unix(WVAL(p, 10), WVAL(p, 8));
		entry->attr.f_mtime = date_dos2unix(WVAL(p, 14), WVAL(p, 12));
		result = p + 28 + *(p+26);
		break;

	default:
		pr_debug("Unknown long filename format %d\n", level);
		result = p + WVAL(p, 0);
	}

	switch (server->opt.case_handling)
	{
	case SMB_CASE_UPPER:
		str_upper(entry->name);
		break;
	case SMB_CASE_LOWER:
		str_lower(entry->name);
		break;
	default:
		break;
	}

	smb_finish_dirent(server, &(entry->attr));
	return result;
}

static int
smb_proc_readdir_long(struct smb_sb_info *server, struct dentry *dir, int fpos,
		      int cache_size, struct smb_dirent *cache)
{
	/* NT uses 260, OS/2 uses 2. Both accept 1. */
	const int info_level = 1;
	const int max_matches = 512;

	char *p;
	char *lastname;
	int lastname_len;
	int i;
	int first, entries, entries_seen;

	unsigned char *resp_data = NULL;
	unsigned char *resp_param = NULL;
	int resp_data_len = 0;
	int resp_param_len = 0;

	__u16 command;

	int result;

	int ff_resume_key = 0;
	int ff_searchcount = 0;
	int ff_eos = 0;
	int ff_lastname = 0;
	int ff_dir_handle = 0;
	int loop_count = 0;

	char param[SMB_MAXPATHLEN + 2 + 12];
	int mask_len;
	char *mask = &(param[12]);

	static struct qstr star = { "*", 1, 0 };

	mask_len = smb_encode_path(server, mask, dir, &star) - mask;

	mask[mask_len] = 0;
	mask[mask_len + 1] = 0;

	pr_debug("smb_readdir_long cache=%d, fpos=%d, mask=%s\n",
		 cache_size, fpos, mask);

	smb_lock_server(server);

      retry:

	first = 1;
	entries = 0;
	entries_seen = 2;

	while (ff_eos == 0)
	{
		loop_count += 1;
		if (loop_count > 200)
		{
			printk(KERN_WARNING "smb_proc_readdir_long: "
			       "Looping in FIND_NEXT??\n");
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
			pr_debug("hand=0x%X resume=%d ff_lastnm=%d mask=%s\n",
				 ff_dir_handle, ff_resume_key, ff_lastname,
				 mask);
			WSET(param, 0, ff_dir_handle);
			WSET(param, 2, max_matches);	/* max count */
			WSET(param, 4, info_level);
			DSET(param, 6, ff_resume_key);	/* ff_resume_key */
			WSET(param, 10, 8 + 4 + 2);	/* resume required +
							   close on end +
							   continue */
#ifdef CONFIG_SMB_WIN95
			/* Windows 95 is not able to deliver answers
			   to FIND_NEXT fast enough, so sleep 0.2 seconds */
			current->timeout = jiffies + HZ / 5;
			current->state = TASK_INTERRUPTIBLE;
			schedule();
			current->timeout = 0;
#endif
		}

		result = smb_trans2_request(server, command,
					    0, NULL, 12 + mask_len + 2, param,
					    &resp_data_len, &resp_data,
					    &resp_param_len, &resp_param);

		if (result < 0)
		{
			if (smb_retry(server))
			{
				goto retry;
			}
			pr_debug("smb_proc_readdir_long: "
				 "got error from trans2_request\n");
			break;
		}
		if (server->rcls != 0)
		{
			result = -EIO;
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
		/* point to the data bytes */
		p = resp_data;

		/* we might need the lastname for continuations */
		lastname = "";
		lastname_len = 0;
		if (ff_lastname > 0)
		{
			switch (info_level)
			{
			case 260:
				lastname = p + ff_lastname;
				lastname_len = resp_data_len - ff_lastname;
				ff_resume_key = 0;
				break;
			case 1:
				lastname = p + ff_lastname + 1;
				lastname_len = *(p+ff_lastname);
				ff_resume_key = 0;
				break;
			}
		}
		lastname_len = min(lastname_len, 256);
		strncpy(mask, lastname, lastname_len);
		mask[lastname_len] = '\0';

		/* Now we are ready to parse smb directory entries. */

		for (i = 0; i < ff_searchcount; i++)
		{
			struct smb_dirent *entry = &(cache[entries]);

			p = smb_decode_long_dirent(server, p,
						   entry, info_level);

			pr_debug("smb_readdir_long: got %s\n", entry->name);

			if ((entry->name[0] == '.')
			    && ((entry->name[1] == '\0')
				|| ((entry->name[1] == '.')
				    && (entry->name[2] == '\0'))))
			{
				/* ignore . and .. from the server */
				continue;
			}
			if (entries_seen >= fpos)
			{
				entry->f_pos = entries_seen;
				entries += 1;
			}
			if (entries >= cache_size)
			{
				goto finished;
			}
			entries_seen += 1;
		}

		pr_debug("received %d entries (eos=%d resume=%d)\n",
			 ff_searchcount, ff_eos, ff_resume_key);

		first = 0;
	}

      finished:
	smb_unlock_server(server);
	return entries;
}

int
smb_proc_readdir(struct dentry *dir, int fpos,
		 int cache_size, struct smb_dirent *entry)
{
	struct smb_sb_info *server = SMB_SERVER(dir->d_inode);

	if (server->opt.protocol >= SMB_PROTOCOL_LANMAN2)
		return smb_proc_readdir_long(server, dir, fpos, cache_size,
					     entry);
	else
		return smb_proc_readdir_short(server, dir, fpos, cache_size,
					      entry);
}

static int
smb_proc_getattr_core(struct dentry *dir, struct qstr *name,
		      struct smb_fattr *attr)
{
	int result;
	char *p;
	struct smb_sb_info *server = SMB_SERVER(dir->d_inode);
	char *buf;

	smb_lock_server(server);

      retry:
	buf = server->packet;
	p = smb_setup_header(server, SMBgetatr, 0, 0);
	*p++ = 4;
	p = smb_encode_path(server, p, dir, name);
	smb_setup_bcc(server, p);

	if ((result = smb_request_ok(server, SMBgetatr, 10, 0)) < 0)
	{
		if (smb_retry(server))
		{
			goto retry;
		}
		smb_unlock_server(server);
		return result;
	}
	attr->attr = WVAL(buf, smb_vwv0);
	attr->f_ctime = attr->f_atime =
	    attr->f_mtime = local2utc(DVAL(buf, smb_vwv1));

	attr->f_size = DVAL(buf, smb_vwv3);
	smb_unlock_server(server);
	return 0;
}

static int
smb_proc_getattr_trans2(struct dentry *dir, struct qstr *name,
			struct smb_fattr *attr)
{
	struct smb_sb_info *server = SMB_SERVER(dir->d_inode);
	char param[SMB_MAXPATHLEN + 20];
	char *p;
	int result;

	unsigned char *resp_data = NULL;
	unsigned char *resp_param = NULL;
	int resp_data_len = 0;
	int resp_param_len = 0;

	WSET(param, 0, 1);	/* Info level SMB_INFO_STANDARD */
	DSET(param, 2, 0);
	p = smb_encode_path(server, param + 6, dir, name);

	smb_lock_server(server);
      retry:
	result = smb_trans2_request(server, TRANSACT2_QPATHINFO,
				    0, NULL, p - param, param,
				    &resp_data_len, &resp_data,
				    &resp_param_len, &resp_param);

	if (server->rcls != 0)
	{
		smb_unlock_server(server);
		return -smb_errno(server->rcls, server->err);
	}
	if (result < 0)
	{
		if (smb_retry(server))
		{
			goto retry;
		}
		smb_unlock_server(server);
		return result;
	}
	if (resp_data_len < 22)
	{
		smb_unlock_server(server);
		return -ENOENT;
	}
	attr->f_ctime = date_dos2unix(WVAL(resp_data, 2),
				      WVAL(resp_data, 0));
	attr->f_atime = date_dos2unix(WVAL(resp_data, 6),
				      WVAL(resp_data, 4));
	attr->f_mtime = date_dos2unix(WVAL(resp_data, 10),
				      WVAL(resp_data, 8));
	attr->f_size = DVAL(resp_data, 12);
	attr->attr = WVAL(resp_data, 20);
	smb_unlock_server(server);

	return 0;
}

int smb_proc_getattr(struct dentry *dir, struct qstr *name,
		     struct smb_fattr *fattr)
{
	struct smb_sb_info *server = SMB_SERVER(dir->d_inode);
	int result = 0;

	smb_init_dirent(server, fattr);

	if (server->opt.protocol >= SMB_PROTOCOL_LANMAN2)
		result = smb_proc_getattr_trans2(dir, name, fattr);

	if ((server->opt.protocol < SMB_PROTOCOL_LANMAN2) || (result < 0))
		result = smb_proc_getattr_core(dir, name, fattr);

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
	if ((result = smb_request_ok(server, SMBsetatr, 0, 0)) < 0)
		if (smb_retry(server))
			goto retry;

	smb_unlock_server(server);
	return result;
}

static int
smb_proc_setattr_trans2(struct smb_sb_info *server,
			struct dentry *dir, struct smb_fattr *fattr)
{
	char param[SMB_MAXPATHLEN + 20];
	char data[26];
	char *p;
	int result;

	unsigned char *resp_data = NULL;
	unsigned char *resp_param = NULL;
	int resp_data_len = 0;
	int resp_param_len = 0;

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

	smb_lock_server(server);
      retry:
	result = smb_trans2_request(server, TRANSACT2_SETPATHINFO,
				    26, data, p - param, param,
				    &resp_data_len, &resp_data,
				    &resp_param_len, &resp_param);

	if (server->rcls != 0)
	{
		smb_unlock_server(server);
		return -smb_errno(server->rcls, server->err);
	}
	if (result < 0)
		if (smb_retry(server))
			goto retry;

	smb_unlock_server(server);
	return 0;
}

int
smb_proc_setattr(struct smb_sb_info *server, struct dentry *dir,
		 struct smb_fattr *fattr)
{
	int result;

	if (server->opt.protocol >= SMB_PROTOCOL_LANMAN2)
		result = smb_proc_setattr_trans2(server, dir, fattr);

	if ((server->opt.protocol < SMB_PROTOCOL_LANMAN2) || (result < 0))
		result = smb_proc_setattr_core(server, dir, fattr);

	return result;
}

int
smb_proc_dskattr(struct super_block *sb, struct statfs *attr)
{
	int error;
	char *p;
	struct smb_sb_info *server = &(sb->u.smbfs_sb);

	smb_lock_server(server);

      retry:
	smb_setup_header(server, SMBdskattr, 0, 0);

	if ((error = smb_request_ok(server, SMBdskattr, 5, 0)) < 0)
	{
		if (smb_retry(server))
			goto retry;

		smb_unlock_server(server);
		return error;
	}
	p = SMB_VWV(server->packet);
	attr->f_bsize = WVAL(p, 2) * WVAL(p, 4);
	attr->f_blocks = WVAL(p, 0);
	attr->f_bavail = attr->f_bfree = WVAL(p, 6);
	smb_unlock_server(server);
	return 0;
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
