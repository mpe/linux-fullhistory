/*
 *  proc.c
 *
 *  Copyright (C) 1995, 1996 by Paal-Kr. Engstad and Volker Lendecke
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
#define SMB_CMD(packet)  (BVAL(packet,8))
#define SMB_WCT(packet)  (BVAL(packet, SMB_HEADER_LEN - 1))
#define SMB_BCC(packet)  smb_bcc(packet)
#define SMB_BUF(packet)  ((packet) + SMB_HEADER_LEN + SMB_WCT(packet) * 2 + 2)

#define SMB_DIRINFO_SIZE 43
#define SMB_STATUS_SIZE  21

static int smb_request_ok(struct smb_server *s, int command, int wct, int bcc);

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

static inline byte *
smb_decode_word(byte * p, word * data)
{
	*data = WVAL(p, 0);
	return p + 2;
}

byte *
smb_encode_smb_length(byte * p, dword len)
{
	BSET(p, 0, 0);
	BSET(p, 1, 0);
	BSET(p, 2, (len & 0xFF00) >> 8);
	BSET(p, 3, (len & 0xFF));
	if (len > 0xFFFF)
	{
		BSET(p, 1, 1);
	}
	return p + 4;
}

static byte *
smb_encode_ascii(byte * p, const byte * name, int len)
{
	*p++ = 4;
	strcpy(p, name);
	return p + len + 1;
}

static byte *
smb_encode_this_name(byte * p, const char *name, const int len)
{
	*p++ = '\\';
	strncpy(p, name, len);
	return p + len;
}

/* I put smb_encode_parents into a separate function so that the
   recursion only takes 16 bytes on the stack per path component on a
   386. */

static byte *
smb_encode_parents(byte * p, struct smb_inode_info *ino)
{
	byte *q;

	if (ino->dir == NULL)
	{
		return p;
	}
	q = smb_encode_parents(p, ino->dir);
	if (q - p + 1 + ino->finfo.len > SMB_MAXPATHLEN)
	{
		return p;
	}
	return smb_encode_this_name(q, ino->finfo.name, ino->finfo.len);
}

static byte *
smb_encode_path(struct smb_server *server,
		byte * p, struct smb_inode_info *dir,
		const char *name, const int len)
{
	byte *start = p;
	p = smb_encode_parents(p, dir);
	p = smb_encode_this_name(p, name, len);
	*p++ = 0;
	if (server->protocol <= PROTOCOL_COREPLUS)
	{
		str_upper(start);
	}
	return p;
}

static byte *
smb_decode_data(byte * p, byte * data, word * data_len, int fs)
{
	word len;

	if (!(*p == 1 || *p == 5))
	{
		printk("smb_decode_data: Warning! Data block not starting "
		       "with 1 or 5\n");
	}
	len = WVAL(p, 1);
	p += 3;

	if (fs)
		copy_to_user(data, p, len);
	else
		memcpy(data, p, len);

	*data_len = len;

	return p + len;
}

static byte *
smb_name_mangle(byte * p, const byte * name)
{
	int len, pad = 0;

	len = strlen(name);

	if (len < 16)
		pad = 16 - len;

	*p++ = 2 * (len + pad);

	while (*name)
	{
		*p++ = (*name >> 4) + 'A';
		*p++ = (*name & 0x0F) + 'A';
		name++;
	}
	while (pad--)
	{
		*p++ = 'C';
		*p++ = 'A';
	}
	*p++ = '\0';

	return p;
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
date_unix2dos(int unix_date, byte * date, byte * time)
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

dword
smb_len(byte * p)
{
	return ((BVAL(p, 1) & 0x1) << 16L) | (BVAL(p, 2) << 8L) | (BVAL(p, 3));
}

static word
smb_bcc(byte * packet)
{
	int pos = SMB_HEADER_LEN + SMB_WCT(packet) * sizeof(word);
	return WVAL(packet, pos);
}

/* smb_valid_packet: We check if packet fulfills the basic
   requirements of a smb packet */

static int
smb_valid_packet(byte * packet)
{
	DDPRINTK("len: %d, wct: %d, bcc: %d\n",
		 smb_len(packet), SMB_WCT(packet), SMB_BCC(packet));
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
smb_verify(byte * packet, int command, int wct, int bcc)
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

static void
smb_lock_server(struct smb_server *server)
{
	while (server->lock)
		sleep_on(&server->wait);
	server->lock = 1;
}

static void
smb_unlock_server(struct smb_server *server)
{
	if (server->lock != 1)
	{
		printk("smb_unlock_server: was not locked!\n");
	}
	server->lock = 0;
	wake_up(&server->wait);
}

/* smb_request_ok: We expect the server to be locked. Then we do the
   request and check the answer completely. When smb_request_ok
   returns 0, you can be quite sure that everything went well. When
   the answer is <=0, the returned number is a valid unix errno. */

static int
smb_request_ok(struct smb_server *s, int command, int wct, int bcc)
{
	int result = 0;
	s->rcls = 0;
	s->err = 0;

	if (smb_request(s) < 0)
	{
		DPRINTK("smb_request failed\n");
		result = -EIO;
	} else if (smb_valid_packet(s->packet) != 0)
	{
		DPRINTK("not a valid packet!\n");
		result = -EIO;
	} else if (s->rcls != 0)
	{
		result = -smb_errno(s->rcls, s->err);
	} else if (smb_verify(s->packet, command, wct, bcc) != 0)
	{
		DPRINTK("smb_verify failed\n");
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
smb_retry(struct smb_server *server)
{
	if (server->state != CONN_INVALID)
	{
		return 0;
	}
	if (smb_release(server) < 0)
	{
		DPRINTK("smb_retry: smb_release failed\n");
		server->state = CONN_RETRIED;
		return 0;
	}
	if (smb_proc_reconnect(server) < 0)
	{
		DPRINTK("smb_proc_reconnect failed\n");
		server->state = CONN_RETRIED;
		return 0;
	}
	server->state = CONN_VALID;
	return 1;
}

static int
smb_request_ok_unlock(struct smb_server *s, int command, int wct, int bcc)
{
	int result = smb_request_ok(s, command, wct, bcc);

	smb_unlock_server(s);

	return result;
}

/* smb_setup_header: We completely set up the packet. You only have to
   insert the command-specific fields */

__u8 *
smb_setup_header(struct smb_server * server, byte command, word wct, word bcc)
{
	dword xmit_len = SMB_HEADER_LEN + wct * sizeof(word) + bcc + 2;
	byte *p = server->packet;
	byte *buf = server->packet;

	p = smb_encode_smb_length(p, xmit_len - 4);

	BSET(p, 0, 0xff);
	BSET(p, 1, 'S');
	BSET(p, 2, 'M');
	BSET(p, 3, 'B');
	BSET(p, 4, command);

	p += 5;
	memset(p, '\0', 19);
	p += 19;
	p += 8;

	WSET(buf, smb_tid, server->tid);
	WSET(buf, smb_pid, server->pid);
	WSET(buf, smb_uid, server->server_uid);
	WSET(buf, smb_mid, server->mid);

	if (server->protocol > PROTOCOL_CORE)
	{
		BSET(buf, smb_flg, 0x8);
		WSET(buf, smb_flg2, 0x3);
	}
	*p++ = wct;		/* wct */
	p += 2 * wct;
	WSET(p, 0, bcc);
	return p + 2;
}

/* smb_setup_header_exclusive waits on server->lock and locks the
   server, when it's free. You have to unlock it manually when you're
   finished with server->packet! */

static byte *
smb_setup_header_exclusive(struct smb_server *server,
			   byte command, word wct, word bcc)
{
	smb_lock_server(server);
	return smb_setup_header(server, command, wct, bcc);
}

static void
smb_setup_bcc(struct smb_server *server, byte * p)
{
	__u8 *packet = server->packet;
	__u8 *pbcc = packet + SMB_HEADER_LEN + 2 * SMB_WCT(packet);
	__u16 bcc = p - (pbcc + 2);

	WSET(pbcc, 0, bcc);
	smb_encode_smb_length(packet,
			      SMB_HEADER_LEN + 2 * SMB_WCT(packet) - 2 + bcc);
}


/*****************************************************************************/
/*                                                                           */
/*  File operation section.                                                  */
/*                                                                           */
/*****************************************************************************/

int
smb_proc_open(struct smb_server *server,
	      struct smb_inode_info *dir, const char *name, int len,
	      struct smb_dirent *entry)
{
	int error;
	char *p;
	char *buf;
	const word o_attr = aSYSTEM | aHIDDEN | aDIR;

	DPRINTK("smb_proc_open: name=%s\n", name);

	smb_lock_server(server);
	buf = server->packet;

	if (entry->opened != 0)
	{
		/* Somebody else opened the file while we slept */
		smb_unlock_server(server);
		return 0;
	}
      retry:
	p = smb_setup_header(server, SMBopen, 2, 0);
	WSET(buf, smb_vwv0, 0x42);	/* read/write */
	WSET(buf, smb_vwv1, o_attr);
	*p++ = 4;
	p = smb_encode_path(server, p, dir, name, len);
	smb_setup_bcc(server, p);

	if ((error = smb_request_ok(server, SMBopen, 7, 0)) != 0)
	{

		if (smb_retry(server))
		{
			goto retry;
		}
		if ((error != -EACCES) && (error != -ETXTBSY)
		    && (error != -EROFS))
		{
			smb_unlock_server(server);
			return error;
		}
		p = smb_setup_header(server, SMBopen, 2, 0);
		WSET(buf, smb_vwv0, 0x40);	/* read only */
		WSET(buf, smb_vwv1, o_attr);
		*p++ = 4;
		p = smb_encode_path(server, p, dir, name, len);
		smb_setup_bcc(server, p);

		if ((error = smb_request_ok(server, SMBopen, 7, 0)) != 0)
		{
			if (smb_retry(server))
			{
				goto retry;
			}
			smb_unlock_server(server);
			return error;
		}
	}
	/* We should now have data in vwv[0..6]. */

	entry->fileid = WVAL(buf, smb_vwv0);
	entry->attr = WVAL(buf, smb_vwv1);
	entry->f_ctime = entry->f_atime =
	    entry->f_mtime = local2utc(DVAL(buf, smb_vwv2));
	entry->f_size = DVAL(buf, smb_vwv4);
	entry->access = WVAL(buf, smb_vwv6);

	entry->opened = 1;
	entry->access &= 3;

	smb_unlock_server(server);

	DPRINTK("smb_proc_open: entry->access = %d\n", entry->access);
	return 0;
}

int
smb_proc_close(struct smb_server *server,
	       __u16 fileid, __u32 mtime)
{
	char *buf;

	smb_setup_header_exclusive(server, SMBclose, 3, 0);
	buf = server->packet;
	WSET(buf, smb_vwv0, fileid);
	DSET(buf, smb_vwv1, utc2local(mtime));

	return smb_request_ok_unlock(server, SMBclose, 0, 0);
}

/* In smb_proc_read and smb_proc_write we do not retry, because the
   file-id would not be valid after a reconnection. */

/* smb_proc_read: fs indicates if it should be copied with
   copy_to_user. */

int
smb_proc_read(struct smb_server *server, struct smb_dirent *finfo,
	      off_t offset, long count, char *data, int fs)
{
	word returned_count, data_len;
	char *buf;
	int error;

	smb_setup_header_exclusive(server, SMBread, 5, 0);
	buf = server->packet;

	WSET(buf, smb_vwv0, finfo->fileid);
	WSET(buf, smb_vwv1, count);
	DSET(buf, smb_vwv2, offset);
	WSET(buf, smb_vwv4, 0);

	if ((error = smb_request_ok(server, SMBread, 5, -1)) < 0)
	{
		smb_unlock_server(server);
		return error;
	}
	returned_count = WVAL(buf, smb_vwv0);

	smb_decode_data(SMB_BUF(server->packet), data, &data_len, fs);

	smb_unlock_server(server);

	if (returned_count != data_len)
	{
		printk("smb_proc_read: Warning, returned_count != data_len\n");
		printk("smb_proc_read: ret_c=%d, data_len=%d\n",
		       returned_count, data_len);
	}
	return data_len;
}

int
smb_proc_write(struct smb_server *server, struct smb_dirent *finfo,
	       off_t offset, int count, const char *data)
{
	int res = 0;
	char *buf;
	byte *p;

	p = smb_setup_header_exclusive(server, SMBwrite, 5, count + 3);
	buf = server->packet;
	WSET(buf, smb_vwv0, finfo->fileid);
	WSET(buf, smb_vwv1, count);
	DSET(buf, smb_vwv2, offset);
	WSET(buf, smb_vwv4, 0);

	*p++ = 1;
	WSET(p, 0, count);
	copy_from_user(p + 2, data, count);

	if ((res = smb_request_ok(server, SMBwrite, 1, 0)) >= 0)
	{
		res = WVAL(buf, smb_vwv0);
	}
	smb_unlock_server(server);

	return res;
}

int
smb_proc_create(struct inode *dir, const char *name, int len,
		word attr, time_t ctime)
{
	int error;
	char *p;
	struct smb_server *server = SMB_SERVER(dir);
	char *buf;
	__u16 fileid;

	smb_lock_server(server);
	buf = server->packet;
      retry:
	p = smb_setup_header(server, SMBcreate, 3, 0);
	WSET(buf, smb_vwv0, attr);
	DSET(buf, smb_vwv1, utc2local(ctime));
	*p++ = 4;
	p = smb_encode_path(server, p, SMB_INOP(dir), name, len);
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
	fileid = WVAL(buf, smb_vwv0);
	smb_unlock_server(server);

	smb_proc_close(server, fileid, CURRENT_TIME);

	return 0;
}

int
smb_proc_mv(struct inode *odir, const char *oname, const int olen,
	    struct inode *ndir, const char *nname, const int nlen)
{
	char *p;
	struct smb_server *server = SMB_SERVER(odir);
	char *buf;
	int result;

	smb_lock_server(server);
	buf = server->packet;

      retry:
	p = smb_setup_header(server, SMBmv, 1, 0);
	WSET(buf, smb_vwv0, aSYSTEM | aHIDDEN);
	*p++ = 4;
	p = smb_encode_path(server, p, SMB_INOP(odir), oname, olen);
	*p++ = 4;
	p = smb_encode_path(server, p, SMB_INOP(ndir), nname, nlen);
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
smb_proc_mkdir(struct inode *dir, const char *name, const int len)
{
	char *p;
	int result;
	struct smb_server *server = SMB_SERVER(dir);

	smb_lock_server(server);

      retry:
	p = smb_setup_header(server, SMBmkdir, 0, 0);
	*p++ = 4;
	p = smb_encode_path(server, p, SMB_INOP(dir), name, len);
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
smb_proc_rmdir(struct inode *dir, const char *name, const int len)
{
	char *p;
	int result;
	struct smb_server *server = SMB_SERVER(dir);

	smb_lock_server(server);


      retry:
	p = smb_setup_header(server, SMBrmdir, 0, 0);
	*p++ = 4;
	p = smb_encode_path(server, p, SMB_INOP(dir), name, len);
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
smb_proc_unlink(struct inode *dir, const char *name, const int len)
{
	char *p;
	struct smb_server *server = SMB_SERVER(dir);
	char *buf;
	int result;

	smb_lock_server(server);
	buf = server->packet;

      retry:
	p = smb_setup_header(server, SMBunlink, 1, 0);
	WSET(buf, smb_vwv0, aSYSTEM | aHIDDEN);
	*p++ = 4;
	p = smb_encode_path(server, p, SMB_INOP(dir), name, len);
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
smb_proc_trunc(struct smb_server *server, word fid, dword length)
{
	char *p;
	char *buf;
	int result;

	smb_lock_server(server);
	buf = server->packet;

      retry:
	p = smb_setup_header(server, SMBwrite, 5, 0);
	WSET(buf, smb_vwv0, fid);
	WSET(buf, smb_vwv1, 0);
	DSET(buf, smb_vwv2, length);
	WSET(buf, smb_vwv4, 0);
	p = smb_encode_ascii(p, "", 0);
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
smb_init_dirent(struct smb_server *server, struct smb_dirent *entry)
{
	memset(entry, 0, sizeof(struct smb_dirent));

	entry->f_nlink = 1;
	entry->f_uid = server->m.uid;
	entry->f_gid = server->m.gid;
	entry->f_blksize = 512;
}

static void
smb_finish_dirent(struct smb_server *server, struct smb_dirent *entry)
{
	if ((entry->attr & aDIR) != 0)
	{
		entry->f_mode = server->m.dir_mode;
		entry->f_size = 512;
	} else
	{
		entry->f_mode = server->m.file_mode;
	}

	if ((entry->f_blksize != 0) && (entry->f_size != 0))
	{
		entry->f_blocks =
		    (entry->f_size - 1) / entry->f_blksize + 1;
	} else
	{
		entry->f_blocks = 0;
	}
	return;
}

void
smb_init_root_dirent(struct smb_server *server, struct smb_dirent *entry)
{
	smb_init_dirent(server, entry);
	entry->attr = aDIR;
	entry->f_ino = 1;
	smb_finish_dirent(server, entry);
}


static char *
smb_decode_dirent(struct smb_server *server, char *p, struct smb_dirent *entry)
{
	smb_init_dirent(server, entry);

	p += SMB_STATUS_SIZE;	/* reserved (search_status) */
	entry->attr = BVAL(p, 0);
	entry->f_mtime = entry->f_atime = entry->f_ctime =
	    date_dos2unix(WVAL(p, 1), WVAL(p, 3));
	entry->f_size = DVAL(p, 5);
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
	switch (server->case_handling)
	{
	case CASE_UPPER:
		str_upper(entry->name);
		break;
	case CASE_LOWER:
		str_lower(entry->name);
		break;
	default:
		break;
	}
	DPRINTK("smb_decode_dirent: name = %s\n", entry->name);
	smb_finish_dirent(server, entry);
	return p + 22;
}

/* This routine is used to read in directory entries from the network.
   Note that it is for short directory name seeks, i.e.: protocol <
   PROTOCOL_LANMAN2 */

static int
smb_proc_readdir_short(struct smb_server *server, struct inode *dir, int fpos,
		       int cache_size, struct smb_dirent *entry)
{
	char *p;
	char *buf;
	int error;
	int result;
	int i;
	int first, total_count;
	struct smb_dirent *current_entry;
	word bcc;
	word count;
	char status[SMB_STATUS_SIZE];
	int entries_asked = (server->max_xmit - 100) / SMB_DIRINFO_SIZE;

	DPRINTK("SMB call  readdir %d @ %d\n", cache_size, fpos);

	smb_lock_server(server);
	buf = server->packet;

      retry:
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
			p = smb_encode_path(server, p, SMB_INOP(dir), "*.*", 3);
			*p++ = 5;
			WSET(p, 0, 0);
			p += 2;
		} else
		{
			p = smb_setup_header(server, SMBsearch, 2, 0);
			WSET(buf, smb_vwv0, entries_asked);
			WSET(buf, smb_vwv1, aDIR);
			p = smb_encode_ascii(p, "", 0);
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
		p = smb_decode_word(p, &count);
		p = smb_decode_word(p, &bcc);

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
		p += 3;		/* Skipping VBLOCK header
				   (5, length lo, length hi). */

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
				DDPRINTK("smb_proc_readdir: skipped entry.\n");
				DDPRINTK("                  total_count = %d\n"
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
				DDPRINTK("smb_proc_readdir: entry->f_pos = "
					 "%lu\n", entry->f_pos);
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
smb_decode_long_dirent(struct smb_server *server, char *p,
		       struct smb_dirent *entry, int level)
{
	char *result;

	smb_init_dirent(server, entry);

	switch (level)
	{
		/* We might add more levels later... */
	case 1:
		entry->len = BVAL(p, 26);
		strncpy(entry->name, p + 27, entry->len);
		entry->name[entry->len] = '\0';
		entry->f_size = DVAL(p, 16);
		entry->attr = BVAL(p, 24);

		entry->f_ctime = date_dos2unix(WVAL(p, 6), WVAL(p, 4));
		entry->f_atime = date_dos2unix(WVAL(p, 10), WVAL(p, 8));
		entry->f_mtime = date_dos2unix(WVAL(p, 14), WVAL(p, 12));
		result = p + 28 + BVAL(p, 26);
		break;

	default:
		DPRINTK("Unknown long filename format %d\n", level);
		result = p + WVAL(p, 0);
	}

	switch (server->case_handling)
	{
	case CASE_UPPER:
		str_upper(entry->name);
		break;
	case CASE_LOWER:
		str_lower(entry->name);
		break;
	default:
		break;
	}

	smb_finish_dirent(server, entry);
	return result;
}

int
smb_proc_readdir_long(struct smb_server *server, struct inode *dir, int fpos,
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
	unsigned char *mask = &(param[12]);

	mask_len = smb_encode_path(server, mask,
				   SMB_INOP(dir), "*", 1) - mask;

	mask[mask_len] = 0;
	mask[mask_len + 1] = 0;

	DPRINTK("smb_readdir_long cache=%d, fpos=%d, mask=%s\n",
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
			printk("smb_proc_readdir_long: "
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
			DPRINTK("hand=0x%X resume=%d ff_lastname=%d mask=%s\n",
			     ff_dir_handle, ff_resume_key, ff_lastname, mask);
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
			DPRINTK("smb_proc_readdir_long: "
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
				lastname_len = BVAL(p, ff_lastname);
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

			DDPRINTK("smb_readdir_long: got %s\n", entry->name);

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

		DPRINTK("received %d entries (eos=%d resume=%d)\n",
			ff_searchcount, ff_eos, ff_resume_key);

		first = 0;
	}

      finished:
	smb_unlock_server(server);
	return entries;
}

int
smb_proc_readdir(struct smb_server *server, struct inode *dir, int fpos,
		 int cache_size, struct smb_dirent *entry)
{
	if (server->protocol >= PROTOCOL_LANMAN2)
		return smb_proc_readdir_long(server, dir, fpos, cache_size,
					     entry);
	else
		return smb_proc_readdir_short(server, dir, fpos, cache_size,
					      entry);
}

static int
smb_proc_getattr_core(struct inode *dir, const char *name, int len,
		      struct smb_dirent *entry)
{
	int result;
	char *p;
	struct smb_server *server = SMB_SERVER(dir);
	char *buf;

	smb_lock_server(server);
	buf = server->packet;

	DDPRINTK("smb_proc_getattr: %s\n", name);

      retry:
	p = smb_setup_header(server, SMBgetatr, 0, 0);
	*p++ = 4;
	p = smb_encode_path(server, p, SMB_INOP(dir), name, len);
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
	entry->attr = WVAL(buf, smb_vwv0);
	entry->f_ctime = entry->f_atime =
	    entry->f_mtime = local2utc(DVAL(buf, smb_vwv1));

	entry->f_size = DVAL(buf, smb_vwv3);
	smb_unlock_server(server);
	return 0;
}

static int
smb_proc_getattr_trans2(struct inode *dir, const char *name, int len,
			struct smb_dirent *entry)
{
	struct smb_server *server = SMB_SERVER(dir);
	char param[SMB_MAXPATHLEN + 20];
	char *p;
	int result;

	unsigned char *resp_data = NULL;
	unsigned char *resp_param = NULL;
	int resp_data_len = 0;
	int resp_param_len = 0;

	WSET(param, 0, 1);	/* Info level SMB_INFO_STANDARD */
	DSET(param, 2, 0);
	p = smb_encode_path(server, param + 6, SMB_INOP(dir), name, len);

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
	entry->f_ctime = date_dos2unix(WVAL(resp_data, 2),
				       WVAL(resp_data, 0));
	entry->f_atime = date_dos2unix(WVAL(resp_data, 6),
				       WVAL(resp_data, 4));
	entry->f_mtime = date_dos2unix(WVAL(resp_data, 10),
				       WVAL(resp_data, 8));
	entry->f_size = DVAL(resp_data, 12);
	entry->attr = WVAL(resp_data, 20);
	smb_unlock_server(server);

	return 0;
}

int
smb_proc_getattr(struct inode *dir, const char *name, int len,
		 struct smb_dirent *entry)
{
	struct smb_server *server = SMB_SERVER(dir);
	int result = 0;

	smb_init_dirent(server, entry);

	if (server->protocol >= PROTOCOL_LANMAN2)
	{
		result = smb_proc_getattr_trans2(dir, name, len, entry);
	}
	if ((server->protocol < PROTOCOL_LANMAN2) || (result < 0))
	{
		result = smb_proc_getattr_core(dir, name, len, entry);
	}
	smb_finish_dirent(server, entry);

	entry->len = len;
	memcpy(entry->name, name, len);
	/* entry->name is null terminated from smb_init_dirent */

	return result;
}


/* In core protocol, there is only 1 time to be set, we use
   entry->f_mtime, to make touch work. */
static int
smb_proc_setattr_core(struct smb_server *server,
		      struct inode *i, struct smb_dirent *new_finfo)
{
	char *p;
	char *buf;
	int result;

	smb_lock_server(server);
	buf = server->packet;

      retry:
	p = smb_setup_header(server, SMBsetatr, 8, 0);
	WSET(buf, smb_vwv0, new_finfo->attr);
	DSET(buf, smb_vwv1, utc2local(new_finfo->f_mtime));
	*p++ = 4;
	p = smb_encode_path(server, p,
			    SMB_INOP(i)->dir, SMB_INOP(i)->finfo.name,
			    SMB_INOP(i)->finfo.len);
	p = smb_encode_ascii(p, "", 0);

	smb_setup_bcc(server, p);
	if ((result = smb_request_ok(server, SMBsetatr, 0, 0)) < 0)
	{
		if (smb_retry(server))
		{
			goto retry;
		}
	}
	smb_unlock_server(server);
	return result;
}

static int
smb_proc_setattr_trans2(struct smb_server *server,
			struct inode *i, struct smb_dirent *new_finfo)
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
	p = smb_encode_path(server, param + 6,
			    SMB_INOP(i)->dir, SMB_INOP(i)->finfo.name,
			    SMB_INOP(i)->finfo.len);

	date_unix2dos(new_finfo->f_ctime, &(data[0]), &(data[2]));
	date_unix2dos(new_finfo->f_atime, &(data[4]), &(data[6]));
	date_unix2dos(new_finfo->f_mtime, &(data[8]), &(data[10]));
	DSET(data, 12, new_finfo->f_size);
	DSET(data, 16, new_finfo->f_blksize);
	WSET(data, 20, new_finfo->attr);
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
	{
		if (smb_retry(server))
		{
			goto retry;
		}
	}
	smb_unlock_server(server);
	return 0;
}

int
smb_proc_setattr(struct smb_server *server, struct inode *inode,
		 struct smb_dirent *new_finfo)
{
	int result;

	if (server->protocol >= PROTOCOL_LANMAN2)
	{
		result = smb_proc_setattr_trans2(server, inode, new_finfo);
	}
	if ((server->protocol < PROTOCOL_LANMAN2) || (result < 0))
	{
		result = smb_proc_setattr_core(server, inode, new_finfo);
	}
	return result;
}

int
smb_proc_dskattr(struct super_block *super, struct smb_dskattr *attr)
{
	int error;
	char *p;
	struct smb_server *server = &(SMB_SBP(super)->s_server);

	smb_lock_server(server);

      retry:
	smb_setup_header(server, SMBdskattr, 0, 0);

	if ((error = smb_request_ok(server, SMBdskattr, 5, 0)) < 0)
	{
		if (smb_retry(server))
		{
			goto retry;
		}
		smb_unlock_server(server);
		return error;
	}
	p = SMB_VWV(server->packet);
	p = smb_decode_word(p, &attr->total);
	p = smb_decode_word(p, &attr->allocblocks);
	p = smb_decode_word(p, &attr->blocksize);
	p = smb_decode_word(p, &attr->free);
	smb_unlock_server(server);
	return 0;
}

/*****************************************************************************/
/*                                                                           */
/*  Mount/umount operations.                                                 */
/*                                                                           */
/*****************************************************************************/

struct smb_prots
{
	enum smb_protocol prot;
	const char *name;
};

/* smb_proc_reconnect: We expect the server to be locked, so that you
   can call the routine from within smb_retry. The socket must be
   created, like after a user-level socket()-call. It may not be
   connected. */

int
smb_proc_reconnect(struct smb_server *server)
{
	struct smb_prots prots[] =
	{
		{PROTOCOL_CORE, "PC NETWORK PROGRAM 1.0"},
		{PROTOCOL_COREPLUS, "MICROSOFT NETWORKS 1.03"},
#ifdef LANMAN1
		{PROTOCOL_LANMAN1, "MICROSOFT NETWORKS 3.0"},
		{PROTOCOL_LANMAN1, "LANMAN1.0"},
#endif
#ifdef LANMAN2
		{PROTOCOL_LANMAN2, "LM1.2X002"},
#endif
#ifdef NT1
		{PROTOCOL_NT1, "NT LM 0.12"},
		{PROTOCOL_NT1, "NT LANMAN 1.0"},
#endif
		{-1, NULL}};
	char dev[] = "A:";
	int i, plength;
	int max_xmit = 1024;	/* Space needed for first request. */
	int given_max_xmit = server->m.max_xmit;
	int result;
	byte *p;

	if ((result = smb_connect(server)) < 0)
	{
		DPRINTK("smb_proc_reconnect: could not smb_connect\n");
		goto fail;
	}
	/* Here we assume that the connection is valid */
	server->state = CONN_VALID;

	if (server->packet != NULL)
	{
		smb_vfree(server->packet);
		server->packet_size = 0;
	}
	server->packet = smb_vmalloc(max_xmit);

	if (server->packet == NULL)
	{
		printk("smb_proc_connect: No memory! Bailing out.\n");
		result = -ENOMEM;
		goto fail;
	}
	server->packet_size = server->max_xmit = max_xmit;

	/*
	 * Start with an RFC1002 session request packet.
	 */
	p = server->packet + 4;

	p = smb_name_mangle(p, server->m.server_name);
	p = smb_name_mangle(p, server->m.client_name);

	smb_encode_smb_length(server->packet,
			      (void *) p - (void *) (server->packet));

	server->packet[0] = 0x81;	/* SESSION REQUEST */

	if (smb_catch_keepalive(server) < 0)
	{
		printk("smb_proc_connect: could not catch_keepalives\n");
	}
	if ((result = smb_request(server)) < 0)
	{
		DPRINTK("smb_proc_connect: Failed to send SESSION REQUEST.\n");
		smb_dont_catch_keepalive(server);
		goto fail;
	}
	if (server->packet[0] != 0x82)
	{
		printk("smb_proc_connect: Did not receive positive response "
		       "(err = %x)\n",
		       server->packet[0]);
		smb_dont_catch_keepalive(server);
		result = -EIO;
		goto fail;
	}
	DPRINTK("smb_proc_connect: Passed SESSION REQUEST.\n");

	/* Now we are ready to send a SMB Negotiate Protocol packet. */
	memset(server->packet, 0, SMB_HEADER_LEN);

	plength = 0;
	for (i = 0; prots[i].name != NULL; i++)
	{
		plength += strlen(prots[i].name) + 2;
	}

	smb_setup_header(server, SMBnegprot, 0, plength);

	p = SMB_BUF(server->packet);

	for (i = 0; prots[i].name != NULL; i++)
	{
		*p++ = 2;
		strcpy(p, prots[i].name);
		p += strlen(prots[i].name) + 1;
	}

	if ((result = smb_request_ok(server, SMBnegprot, 1, -1)) < 0)
	{
		DPRINTK("smb_proc_connect: Failure requesting SMBnegprot\n");
		smb_dont_catch_keepalive(server);
		goto fail;
	} else
	{
		DDPRINTK("smb_proc_connect: Request SMBnegprot..");
	}

	DDPRINTK("Verified!\n");

	p = SMB_VWV(server->packet);
	p = smb_decode_word(p, (word *) & i);
	server->protocol = prots[i].prot;

	DPRINTK("smb_proc_connect: Server wants %s protocol.\n",
		prots[i].name);

	if (server->protocol >= PROTOCOL_LANMAN1)
	{

		word passlen = strlen(server->m.password);
		word userlen = strlen(server->m.username);

		DPRINTK("smb_proc_connect: password = %s\n",
			server->m.password);
		DPRINTK("smb_proc_connect: usernam = %s\n",
			server->m.username);
		DPRINTK("smb_proc_connect: blkmode = %d\n",
			WVAL(server->packet, smb_vwv5));

		if (server->protocol >= PROTOCOL_NT1)
		{
			server->max_xmit = DVAL(server->packet, smb_vwv3 + 1);
			server->maxmux = WVAL(server->packet, smb_vwv1 + 1);
			server->maxvcs = WVAL(server->packet, smb_vwv2 + 1);
			server->blkmode = DVAL(server->packet, smb_vwv9 + 1);
			server->sesskey = DVAL(server->packet, smb_vwv7 + 1);
		} else
		{
			server->max_xmit = WVAL(server->packet, smb_vwv2);
			server->maxmux = WVAL(server->packet, smb_vwv3);
			server->maxvcs = WVAL(server->packet, smb_vwv4);
			server->blkmode = WVAL(server->packet, smb_vwv5);
			server->sesskey = DVAL(server->packet, smb_vwv6);
		}

		if (server->max_xmit < given_max_xmit)
		{
			/* We do not distinguish between the client
			   requests and the server response. */
			given_max_xmit = server->max_xmit;
		}
		if (server->protocol >= PROTOCOL_NT1)
		{
			char *workgroup = server->m.domain;
			char *OS_id = "Unix";
			char *client_id = "ksmbfs";

			smb_setup_header(server, SMBsesssetupX, 13,
					 5 + userlen + passlen +
					 strlen(workgroup) + strlen(OS_id) +
					 strlen(client_id));

			WSET(server->packet, smb_vwv0, 0x00ff);
			WSET(server->packet, smb_vwv1, 0);
			WSET(server->packet, smb_vwv2, given_max_xmit);
			WSET(server->packet, smb_vwv3, 2);
			WSET(server->packet, smb_vwv4, server->pid);
			DSET(server->packet, smb_vwv5, server->sesskey);
			WSET(server->packet, smb_vwv7, passlen + 1);
			WSET(server->packet, smb_vwv8, 0);
			WSET(server->packet, smb_vwv9, 0);

			p = SMB_BUF(server->packet);
			strcpy(p, server->m.password);
			p += passlen + 1;
			strcpy(p, server->m.username);
			p += userlen + 1;
			strcpy(p, workgroup);
			p += strlen(p) + 1;
			strcpy(p, OS_id);
			p += strlen(p) + 1;
			strcpy(p, client_id);
		} else
		{
			smb_setup_header(server, SMBsesssetupX, 10,
					 2 + userlen + passlen);

			WSET(server->packet, smb_vwv0, 0x00ff);
			WSET(server->packet, smb_vwv1, 0);
			WSET(server->packet, smb_vwv2, given_max_xmit);
			WSET(server->packet, smb_vwv3, 2);
			WSET(server->packet, smb_vwv4, server->pid);
			DSET(server->packet, smb_vwv5, server->sesskey);
			WSET(server->packet, smb_vwv7, passlen + 1);
			WSET(server->packet, smb_vwv8, 0);
			WSET(server->packet, smb_vwv9, 0);

			p = SMB_BUF(server->packet);
			strcpy(p, server->m.password);
			p += passlen + 1;
			strcpy(p, server->m.username);
		}

		if ((result = smb_request_ok(server, SMBsesssetupX, 3, 0)) < 0)
		{
			DPRINTK("smb_proc_connect: SMBsessetupX failed\n");
			smb_dont_catch_keepalive(server);
			goto fail;
		}
		smb_decode_word(server->packet + 32, &(server->server_uid));
	} else
	{
		server->max_xmit = 0;
		server->maxmux = 0;
		server->maxvcs = 0;
		server->blkmode = 0;
		server->sesskey = 0;
	}

	/* Fine! We have a connection, send a tcon message. */

	smb_setup_header(server, SMBtcon, 0,
			 6 + strlen(server->m.service) +
			 strlen(server->m.password) + strlen(dev));

	p = SMB_BUF(server->packet);
	p = smb_encode_ascii(p, server->m.service, strlen(server->m.service));
	p = smb_encode_ascii(p, server->m.password, strlen(server->m.password));
	p = smb_encode_ascii(p, dev, strlen(dev));

	if ((result = smb_request_ok(server, SMBtcon, 2, 0)) < 0)
	{
		DPRINTK("smb_proc_connect: SMBtcon not verified.\n");
		smb_dont_catch_keepalive(server);
		goto fail;
	}
	DDPRINTK("OK! Managed to set up SMBtcon!\n");

	p = SMB_VWV(server->packet);

	if (server->protocol <= PROTOCOL_COREPLUS)
	{
		word max_xmit;

		p = smb_decode_word(p, &max_xmit);
		server->max_xmit = max_xmit;

		if (server->max_xmit > given_max_xmit)
		{
			server->max_xmit = given_max_xmit;
		}
	} else
	{
		p += 2;
	}

	p = smb_decode_word(p, &server->tid);

	/* Ok, everything is fine. max_xmit does not include */
	/* the TCP-SMB header of 4 bytes. */
	server->max_xmit += 4;

	DPRINTK("max_xmit = %d, tid = %d\n", server->max_xmit, server->tid);

	/* Now make a new packet with the correct size. */
	smb_vfree(server->packet);

	server->packet = smb_vmalloc(server->max_xmit);
	if (server->packet == NULL)
	{
		printk("smb_proc_connect: No memory left in end of "
		       "connection phase :-(\n");
		smb_dont_catch_keepalive(server);
		goto fail;
	}
	server->packet_size = server->max_xmit;

	DPRINTK("smb_proc_connect: Normal exit\n");
	return 0;

      fail:
	server->state = CONN_INVALID;
	return result;
}

/* smb_proc_reconnect: server->packet is allocated with
   server->max_xmit bytes if and only if we return >= 0 */
int
smb_proc_connect(struct smb_server *server)
{
	int result;
	smb_lock_server(server);

	result = smb_proc_reconnect(server);

	if ((result < 0) && (server->packet != NULL))
	{
		smb_vfree(server->packet);
		server->packet = NULL;
	}
	smb_unlock_server(server);
	return result;
}

int
smb_proc_disconnect(struct smb_server *server)
{
	smb_setup_header_exclusive(server, SMBtdis, 0, 0);
	return smb_request_ok_unlock(server, SMBtdis, 0, 0);
}
