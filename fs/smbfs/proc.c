/*
 *  proc.c
 *
 *  Copyright (C) 1995 by Paal-Kr. Engstad and Volker Lendecke
 *
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
#include <asm/segment.h>
#include <asm/string.h>

#define ARCH i386
#define SMB_VWV(packet)  ((packet) + SMB_HEADER_LEN)
#define SMB_CMD(packet)  ((packet)[8])
#define SMB_WCT(packet)  ((packet)[SMB_HEADER_LEN - 1])
#define SMB_BCC(packet)  smb_bcc(packet)
#define SMB_BUF(packet)  ((packet) + SMB_HEADER_LEN + SMB_WCT(packet) * 2 + 2)

#define SMB_DIRINFO_SIZE 43
#define SMB_STATUS_SIZE  21

#define HI_WORD(l) ((word)(l >> 16))
#define LO_WORD(l) ((word)(l % 0xFFFF))

void smb_printerr(int class, int num);
static int smb_request_ok(struct smb_server *s, int command, int wct, int bcc);

static inline int min(int a, int b)
{
	return a<b ? a : b;
}

/*****************************************************************************/
/*                                                                           */
/*  Encoding/Decoding section                                                */
/*                                                                           */
/*****************************************************************************/

static byte *
smb_encode_word(byte *p, word data)
{
#if (ARCH == i386)
        *((word *)p) = data;
#else
	p[0] = data & 0x00ffU;
	p[1] = (data & 0xff00U) >> 8;
#error "Non-Intel"
#endif
	return &p[2];
}

static byte *
smb_decode_word(byte *p, word *data)
{
#if (ARCH == i386)
	*data = *(word *)p;
#else
	*data = (word) p[0] | p[1] << 8;
#endif 
	return &p[2];
}

byte *
smb_encode_smb_length(byte *p, dword len)
{
	p[0] = p[1] = 0;
	p[2] = (len & 0xFF00) >> 8;
	p[3] = (len & 0xFF);
	if (len > 0xFFFF)
		p[1] |= 0x01;
	return &p[4];
}

static byte *
smb_encode_dialect(byte *p, const byte *name, int len)
{
	*p ++ = 2;
	strcpy(p, name);
	return p + len + 1;
}

static byte *
smb_encode_ascii(byte *p, const byte *name, int len)
{
	*p ++ = 4;
	strcpy(p, name);
	return p + len + 1;
}

static byte *
smb_encode_vblock(byte *p, const byte *data, word len, int fs)
{
	*p ++ = 5;
	p = smb_encode_word(p, len);
	if (fs)
		memcpy_fromfs(p, data, len);
	else
		memcpy(p, data, len);
	return p + len;
}

static byte *
smb_decode_data(byte *p, byte *data, word *data_len, int fs)
{
        word len;

	if (!(*p == 1 || *p == 5)) {
                printk("smb_decode_data: Warning! Data block not starting "
                       "with 1 or 5\n");
        }

        len = WVAL(p, 1);
        p += 3;

        if (fs)
                memcpy_tofs(data, p, len);
        else
                memcpy(data, p, len);

        *data_len = len;

        return p + len;
}

static byte *
smb_name_mangle(byte *p, const byte *name)
{
	int len, pad = 0;

	len = strlen(name);

	if (len < 16)
		pad = 16 - len;

	*p ++ = 2 * (len + pad);

	while (*name) {
		*p ++ = (*name >> 4) + 'A';
		*p ++ = (*name & 0x0F) + 'A';
		name ++;
	}
	while (pad --) {
		*p ++ = 'C';
		*p ++ = 'A';
	}
	*p++ = '\0';
	
	return p;
}

/* The following are taken directly from msdos-fs */

/* Linear day numbers of the respective 1sts in non-leap years. */

static int day_n[] = { 0,31,59,90,120,151,181,212,243,273,304,334,0,0,0,0 };
		  /* JanFebMarApr May Jun Jul Aug Sep Oct Nov Dec */


extern struct timezone sys_tz;

static int
utc2local(int time)
{
        return time - sys_tz.tz_minuteswest*60;
}

static int
local2utc(int time)
{
        return time + sys_tz.tz_minuteswest*60;
}

/* Convert a MS-DOS time/date pair to a UNIX date (seconds since 1 1 70). */

static int
date_dos2unix(unsigned short time,unsigned short date)
{
	int month,year,secs;

	month = ((date >> 5) & 15)-1;
	year = date >> 9;
	secs = (time & 31)*2+60*((time >> 5) & 63)+(time >> 11)*3600+86400*
	    ((date & 31)-1+day_n[month]+(year/4)+year*365-((year & 3) == 0 &&
	    month < 2 ? 1 : 0)+3653);
			/* days since 1.1.70 plus 80's leap day */
	return local2utc(secs);
}


/* Convert linear UNIX date to a MS-DOS time/date pair. */

static void
date_unix2dos(int unix_date,unsigned short *time, unsigned short *date)
{
	int day,year,nl_day,month;

	unix_date = utc2local(unix_date);
	*time = (unix_date % 60)/2+(((unix_date/60) % 60) << 5)+
	    (((unix_date/3600) % 24) << 11);
	day = unix_date/86400-3652;
	year = day/365;
	if ((year+3)/4+365*year > day) year--;
	day -= (year+3)/4+365*year;
	if (day == 59 && !(year & 3)) {
		nl_day = day;
		month = 2;
	}
	else {
		nl_day = (year & 3) || day <= 59 ? day : day-1;
		for (month = 0; month < 12; month++)
			if (day_n[month] > nl_day) break;
	}
	*date = nl_day-day_n[month-1]+1+(month << 5)+(year << 9);
}



/*****************************************************************************/
/*                                                                           */
/*  Support section.                                                         */
/*                                                                           */
/*****************************************************************************/

dword
smb_len(byte *packet) 
{
	return ((packet[1] & 0x1) << 16L) | (packet[2] << 8L) | (packet[3]);
}

static word
smb_bcc(byte *packet)
{
	int pos = SMB_HEADER_LEN + SMB_WCT(packet) * sizeof(word);
#if (ARCH == i386)
	return *((word *)((byte *)packet + pos));
#else
	return packet[pos] | packet[pos+1] << 8;
#endif
}

/* smb_valid_packet: We check if packet fulfills the basic
   requirements of a smb packet */

static int
smb_valid_packet(byte *packet)
{
        DDPRINTK("len: %ld, wct: %d, bcc: %d\n",
                 smb_len(packet), SMB_WCT(packet), SMB_BCC(packet));
	return (   packet[4] == 0xff
                && packet[5] == 'S'
                && packet[6] == 'M'
                && packet[7] == 'B'
                && (smb_len(packet) + 4 == SMB_HEADER_LEN
                    + SMB_WCT(packet) * 2 + SMB_BCC(packet)));
}

/* smb_verify: We check if we got the answer we expected, and if we
   got enough data. If bcc == -1, we don't care. */

static int
smb_verify(byte *packet, int command, int wct, int bcc)
{
	return (SMB_CMD(packet) == command &&
		SMB_WCT(packet) >= wct &&
		(bcc == -1 || SMB_BCC(packet) >= bcc)) ? 0 : -EIO;
}

static int
smb_errno(int errcls, int error)
{

#if DEBUG_SMB > 1
	if (errcls) {
		printk("smb_errno: ");
		smb_printerr(errcls, error);
		printk("\n");
	}
#endif

	if (errcls == ERRDOS) 
		switch (error) {
			case ERRbadfunc:    return EINVAL;
			case ERRbadfile:    return ENOENT;
			case ERRbadpath:    return ENOENT;
			case ERRnofids:     return EMFILE;
			case ERRnoaccess:   return EACCES;
			case ERRbadfid:     return EBADF;
			case ERRbadmcb:     return EREMOTEIO;
			case ERRnomem:      return ENOMEM;
			case ERRbadmem:     return EFAULT;
			case ERRbadenv:     return EREMOTEIO;
			case ERRbadformat:  return EREMOTEIO;
			case ERRbadaccess:  return EACCES;
			case ERRbaddata:    return E2BIG;
			case ERRbaddrive:   return ENXIO;
			case ERRremcd:      return EREMOTEIO;
			case ERRdiffdevice: return EXDEV;
			case ERRnofiles:    return 0;
			case ERRbadshare:   return ETXTBSY;
			case ERRlock:       return EDEADLK;
			case ERRfilexists:  return EEXIST;
			case 87:            return 0; /* Unknown error!! */
			/* This next error seems to occur on an mv when
			 * the destination exists */
			case 183:	    return EEXIST;
			default:            return EIO;
		}
	else if (errcls == ERRSRV) 
		switch (error) {
			case ERRerror: return ENFILE;
			case ERRbadpw: return EINVAL;
			case ERRbadtype: return EIO;
			case ERRaccess: return EACCES;
			default: return EIO;
		}
	else if (errcls == ERRHRD) 
		switch (error) {
			case ERRnowrite: return EROFS; 
			case ERRbadunit: return ENODEV;
			case ERRnotready: return EUCLEAN;
			case ERRbadcmd: return EIO;
			case ERRdata: return EIO;
			case ERRbadreq: return ERANGE;
			case ERRbadshare: return ETXTBSY;
			case ERRlock: return EDEADLK;
			default: return EIO;
		}
	else if (errcls == ERRCMD) 
	        return EIO;
	return 0;
}

#if DEBUG_SMB > 0
static char
print_char(char c)
{
	if ((c < ' ') || (c > '~'))
		return '.';
	return c;
}

static void
smb_dump_packet(byte *packet) {
	int i, j, len;
        int errcls, error;

        errcls = (int)packet[9];
        error  = (int)(int)(packet[11]|packet[12]<<8);

	printk("smb_len = %d  valid = %d    \n", 
	       len = smb_len(packet), smb_valid_packet(packet));
	printk("smb_cmd = %d  smb_wct = %d  smb_bcc = %d\n", 
	       packet[8], SMB_WCT(packet), SMB_BCC(packet)); 
	printk("smb_rcls = %d smb_err = %d\n", errcls, error);

	if (errcls) {
		smb_printerr(errcls, error);
		printk("\n");
	}

        if (len > 100)
                len = 100;
	
	for (i = 0; i < len; i += 10) {
		printk("%03d:", i);
		for (j = i; j < i+10; j++)
			if (j < len)
				printk("%02x ", packet[j]);
			else
				printk("   ");
		printk(": ");
	        for (j = i; j < i+10; j++)
			if (j < len)
				printk("%c", print_char(packet[j]));
		printk("\n");
	}
}
#endif

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
        if (server->lock != 1) {
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
        s->err  = 0;

        if (smb_request(s) < 0) {
                DPRINTK("smb_request failed\n");
                result = -EIO;
        }
        else if (smb_valid_packet(s->packet) != 0) {
                DPRINTK("not a valid packet!\n");
                result = -EIO;
        }
        else if (s->rcls != 0) {
                result =  -smb_errno(s->rcls, s->err);
        }
        else if (smb_verify(s->packet, command, wct, bcc) != 0) {
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
        if (server->state != CONN_INVALID) {
                return 0;
        }
        
        if (smb_release(server) < 0) {
                DPRINTK("smb_retry: smb_release failed\n");
                server->state = CONN_RETRIED;
                return 0;
        }
        if(smb_proc_reconnect(server) < 0) {
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

static byte *
smb_setup_header(struct smb_server *server, byte command, word wct, word bcc)
{
	dword xmit_len = SMB_HEADER_LEN + wct * sizeof(word) + bcc + 2;
	byte *p = server->packet;
        byte *buf = server->packet;

	p = smb_encode_smb_length(p, xmit_len);

        BSET(p,0,0xff);
        BSET(p,1,'S');
        BSET(p,2,'M');
        BSET(p,3,'B');
        BSET(p,4,command);

        p += 5;
	memset(p, '\0', 19);
	p += 19;
        p += 8;

        WSET(buf, smb_tid, server->tid);
        WSET(buf, smb_pid, server->pid);
        WSET(buf, smb_uid, server->server_uid);
        WSET(buf, smb_mid, server->mid);

        if (server->protocol > PROTOCOL_CORE) {
                BSET(buf, smb_flg, 0x8);
                WSET(buf, smb_flg2, 0x3);
        }
        
	*p++ = wct;		/* wct */
        p += 2*wct;
        WSET(p, 0, bcc);
        return p+2;
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


/*****************************************************************************/
/*                                                                           */
/*  File operation section.                                                  */
/*                                                                           */
/*****************************************************************************/

int
smb_proc_open(struct smb_server *server, const char *pathname, int len,
              struct smb_dirent *entry)
{
	int error;
	char* p;
        char* buf = server->packet;
	const word o_attr = aSYSTEM | aHIDDEN | aDIR;

        DPRINTK("smb_proc_open: path=%s\n", pathname);

        smb_lock_server(server);

 retry:
        p = smb_setup_header(server, SMBopen, 2, 2 + len);
        WSET(buf, smb_vwv0, 0x42); /* read/write */
        WSET(buf, smb_vwv1, o_attr);
        smb_encode_ascii(p, pathname, len);

        if ((error = smb_request_ok(server, SMBopen, 7, 0)) != 0) {

                if (smb_retry(server)) {
                        goto retry;
                }
                
                if (error != -EACCES) {
                        smb_unlock_server(server);
                        return error;
                }

                p = smb_setup_header(server, SMBopen, 2, 2 + len);
                WSET(buf, smb_vwv0, 0x40); /* read only */
                WSET(buf, smb_vwv1, o_attr);
                smb_encode_ascii(p, pathname, len);

                if ((error = smb_request_ok(server, SMBopen, 7, 0)) != 0) {
                        if (smb_retry(server)) {
                                goto retry;
                        }
                        smb_unlock_server(server);
                        return error;
                }
        }

	/* We should now have data in vwv[0..6]. */

        entry->fileid = WVAL(buf, smb_vwv0);
        entry->attr   = WVAL(buf, smb_vwv1);
        entry->ctime = entry->atime =
                entry->mtime = local2utc(DVAL(buf, smb_vwv2));
        entry->size   = DVAL(buf, smb_vwv4);
        entry->access = WVAL(buf, smb_vwv6);

        smb_unlock_server(server);

	entry->access &= 3;
        DPRINTK("smb_proc_open: entry->access = %d\n", entry->access);
	return 0;
}

/* smb_proc_close: in finfo->mtime we can send a modification time to
   the server */
int
smb_proc_close(struct smb_server *server, struct smb_dirent *finfo)
{
        char *buf = server->packet;

	smb_setup_header_exclusive(server, SMBclose, 3, 0);
        WSET(buf, smb_vwv0, finfo->fileid);
        DSET(buf, smb_vwv1, utc2local(finfo->mtime));

        return smb_request_ok_unlock(server, SMBclose, 0, 0);
}

/* In smb_proc_read and smb_proc_write we do not retry, because the
   file-id would not be valid after a reconnection. */

/* smb_proc_read: fs indicates if it should be copied with
   memcpy_tofs. */

int
smb_proc_read(struct smb_server *server, struct smb_dirent *finfo, 
              off_t offset, long count, char *data, int fs)
{
	word returned_count, data_len;
        char *buf = server->packet;
        int error;

	smb_setup_header_exclusive(server, SMBread, 5, 0);

        WSET(buf, smb_vwv0, finfo->fileid);
        WSET(buf, smb_vwv1, count);
        DSET(buf, smb_vwv2, offset);
        WSET(buf, smb_vwv4, 0);
	
	if ((error = smb_request_ok(server, SMBread, 5, -1)) < 0) {
                smb_unlock_server(server);
		return error;
        }

	returned_count = WVAL(buf, smb_vwv0);
	
	smb_decode_data(SMB_BUF(server->packet), data, &data_len, fs);

        smb_unlock_server(server);

	if (returned_count != data_len) {
		printk("smb_proc_read: Warning, returned_count != data_len\n");
                printk("smb_proc_read: ret_c=%d, data_len=%d\n",
                       returned_count, data_len);
        }

        return data_len;
}

/* count must be <= 65535. No error number is returned.  A result of 0
   indicates an error, which has to be investigated by a normal read
   call. */
int
smb_proc_read_raw(struct smb_server *server, struct smb_dirent *finfo, 
                  off_t offset, long count, char *data)
{
        char *buf = server->packet;
        int result;

        if ((count <= 0) || (count > 65535)) {
                return -EINVAL;
        }

	smb_setup_header_exclusive(server, SMBreadbraw, 8, 0);

        WSET(buf, smb_vwv0, finfo->fileid);
        DSET(buf, smb_vwv1, offset);
        WSET(buf, smb_vwv3, count);
        WSET(buf, smb_vwv4, 0);
        DSET(buf, smb_vwv5, 0);

        result = smb_request_read_raw(server, data, count);
        smb_unlock_server(server);
        return result;
}

int
smb_proc_write(struct smb_server *server, struct smb_dirent *finfo,
               off_t offset, int count, const char *data)
{
        int res = 0;
        char *buf = server->packet;
        byte *p;

	p = smb_setup_header_exclusive(server, SMBwrite, 5, count + 3);
        WSET(buf, smb_vwv0, finfo->fileid);
        WSET(buf, smb_vwv1, count);
        DSET(buf, smb_vwv2, offset);
        WSET(buf, smb_vwv4, 0);

        *p++ = 1;
        WSET(p, 0, count);
        memcpy_fromfs(p+2, data, count);

	if ((res = smb_request_ok(server, SMBwrite, 1, 0)) >= 0) {
                res = WVAL(buf, smb_vwv0);
        }

        smb_unlock_server(server);

	return res;
}

/* count must be <= 65535 */
int
smb_proc_write_raw(struct smb_server *server, struct smb_dirent *finfo, 
                   off_t offset, long count, const char *data)
{
        char *buf = server->packet;
        int result;

        if ((count <= 0) || (count > 65535)) {
                return -EINVAL;
        }

	smb_setup_header_exclusive(server, SMBwritebraw, 11, 0);

        WSET(buf, smb_vwv0, finfo->fileid);
        WSET(buf, smb_vwv1, count);
        WSET(buf, smb_vwv2, 0); /* reserved */
        DSET(buf, smb_vwv3, offset);
        DSET(buf, smb_vwv5, 0); /* timeout */
        WSET(buf, smb_vwv7, 1); /* send final result response */
        DSET(buf, smb_vwv8, 0); /* reserved */
        WSET(buf, smb_vwv10, 0); /* no data in this buf */
        WSET(buf, smb_vwv11, 0); /* no data in this buf */

        result = smb_request_ok(server, SMBwritebraw, 1, 0);

        DPRINTK("smb_proc_write_raw: first request returned %d\n", result);
        
        if (result < 0) {
                smb_unlock_server(server);
                return result;
        }
        
        result = smb_request_write_raw(server, data, count);

        DPRINTK("smb_proc_write_raw: raw request returned %d\n", result);
        
        if (result > 0) {
                /* We have to do the checks of smb_request_ok here as well */
                if (smb_valid_packet(server->packet) != 0) {
                        DPRINTK("not a valid packet!\n");
                        result = -EIO;
                } else if (server->rcls != 0) {
                        result = -smb_errno(server->rcls, server->err);
                } else if (smb_verify(server->packet, SMBwritec,1,0) != 0) {
                        DPRINTK("smb_verify failed\n");
                        result = -EIO;
                }
        }

        smb_unlock_server(server);
        return result;
}


/* smb_proc_do_create: We expect entry->attry & entry->ctime to be set. */

static int
smb_proc_do_create(struct smb_server *server, const char *path, int len, 
                   struct smb_dirent *entry, word command)
{
	int error;
	char *p;
        char *buf = server->packet;

	smb_lock_server(server);
 retry:
	p = smb_setup_header(server, command, 3, len + 2);
        WSET(buf, smb_vwv0, entry->attr);
        DSET(buf, smb_vwv1, utc2local(entry->ctime));
	smb_encode_ascii(p, path, len);

	if ((error = smb_request_ok(server, command, 1, 0)) < 0) {
                if (smb_retry(server)) {
                        goto retry;
                }
                smb_unlock_server(server);
		return error;
        }

        entry->opened = 1;
        entry->fileid = WVAL(buf, smb_vwv0);
        smb_unlock_server(server);

        smb_proc_close(server, entry);

	return 0;
}
	
int
smb_proc_create(struct smb_server *server, const char *path, int len,
                struct smb_dirent *entry)
{
	return smb_proc_do_create(server, path, len, entry, SMBcreate);
}

int
smb_proc_mknew(struct smb_server *server, const char *path, int len,
               struct smb_dirent *entry)
{
	return smb_proc_do_create(server, path, len, entry, SMBmknew);
}

int
smb_proc_mv(struct smb_server *server, 
            const char *opath, const int olen,
            const char *npath, const int nlen)
{
	char *p;
        char *buf = server->packet;
        int result;

        smb_lock_server(server);

 retry:
	p = smb_setup_header(server, SMBmv, 1, olen + nlen + 4);
        WSET(buf, smb_vwv0, 0);
	p = smb_encode_ascii(p, opath, olen);
	smb_encode_ascii(p, npath, olen);

        if ((result = smb_request_ok(server, SMBmv, 0, 0)) < 0) {
                if (smb_retry(server)) {
                        goto retry;
                }
        }
        smb_unlock_server(server);
        return result;
}

int
smb_proc_mkdir(struct smb_server *server, const char *path, const int len)
{
	char *p;
        int result;

        smb_lock_server(server);

 retry:
	p = smb_setup_header(server, SMBmkdir, 0, 2 + len);
	smb_encode_ascii(p, path, len);

        if ((result = smb_request_ok(server, SMBmkdir, 0, 0)) < 0) {
                if (smb_retry(server)) {
                        goto retry;
                }
        }
        smb_unlock_server(server);
        return result;
}

int
smb_proc_rmdir(struct smb_server *server, const char *path, const int len)
{
	char *p;
        int result;

        smb_lock_server(server);

 retry:
	p = smb_setup_header(server, SMBrmdir, 0, 2 + len);
	smb_encode_ascii(p, path, len);

        if ((result = smb_request_ok(server, SMBrmdir, 0, 0)) < 0) {
                if (smb_retry(server)) {
                        goto retry;
                }
        }
        smb_unlock_server(server);
        return result;
}

int
smb_proc_unlink(struct smb_server *server, const char *path, const int len)
{
	char *p;
        char *buf = server->packet;
        int result;

        smb_lock_server(server);

 retry:
	p = smb_setup_header(server, SMBunlink, 1, 2 + len);
        WSET(buf, smb_vwv0, 0);
	smb_encode_ascii(p, path, len);

        if ((result = smb_request_ok(server, SMBunlink, 0, 0)) < 0) {
                if (smb_retry(server)) {
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
        char *buf = server->packet;
        int result;

        smb_lock_server(server);

 retry:
        p = smb_setup_header(server, SMBwrite, 5, 3);
        WSET(buf, smb_vwv0, fid);
        WSET(buf, smb_vwv1, 0);
        DSET(buf, smb_vwv2, length);
        WSET(buf, smb_vwv4, 0);
	smb_encode_ascii(p, "", 0);
	
        if ((result = smb_request_ok(server, SMBwrite, 1, 0)) < 0) {
                if (smb_retry(server)) {
                        goto retry;
                }
        }
        smb_unlock_server(server);
        return result;
}

static char *
smb_decode_dirent(char *p, struct smb_dirent *entry)
{
	p += SMB_STATUS_SIZE;                  /* reserved (search_status) */
        entry->attr = BVAL(p, 0);
        entry->mtime = entry->atime = entry->ctime =
                date_dos2unix(WVAL(p, 1), WVAL(p, 3));
        entry->size = DVAL(p, 5);
        memcpy(entry->path, p+9, 13);
	DDPRINTK("smb_decode_dirent: path = %s\n", entry->path);
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
	int dirlen = strlen(SMB_FINFO(dir)->path);
	char mask[dirlen + 5];

	strcpy(mask, SMB_FINFO(dir)->path);
	strcat(mask, "\\*.*");

 	DPRINTK("SMB call  readdir %d @ %d\n", cache_size, fpos);        
	DPRINTK("          mask = %s\n", mask);

        buf = server->packet;

        smb_lock_server(server);

 retry:
	first = 1;
        total_count = 0;
        current_entry = entry;
	
	while (1) {
		if (first == 1) {
			p = smb_setup_header(server, SMBsearch, 2,
                                             5 + strlen(mask));
                        WSET(buf, smb_vwv0, entries_asked);
                        WSET(buf, smb_vwv1, aDIR);
			p = smb_encode_ascii(p, mask, strlen(mask));
			*p ++ = 5;
		        p = smb_encode_word(p, 0);
		} else {
			p = smb_setup_header(server, SMBsearch, 2,
                                             5 + SMB_STATUS_SIZE);
                        WSET(buf, smb_vwv0, entries_asked);
                        WSET(buf, smb_vwv1, aDIR);
			p = smb_encode_ascii(p, "", 0);
			p = smb_encode_vblock(p, status, SMB_STATUS_SIZE, 0);
		}
		
		if ((error = smb_request_ok(server, SMBsearch, 1, -1)) < 0) {
                        if (   (server->rcls == ERRDOS)
                            && (server->err  == ERRnofiles)) {
                                result = total_count - fpos;
                                goto unlock_return;
                        }
                        else
                        {
                                if (smb_retry(server)) {
                                        goto retry;
                                }
                                result = error;
                                goto unlock_return;
                        }
                }

		p = SMB_VWV(server->packet);
		p = smb_decode_word(p, &count); /* vwv[0] = count-returned */
		p = smb_decode_word(p, &bcc);           
		
		first = 0;
		
		if (count <= 0) {
			result = total_count - fpos;
                        goto unlock_return;
                }
		if (bcc != count * SMB_DIRINFO_SIZE + 3) {
			result = -EIO;
                        goto unlock_return;
                }

		p += 3; /* Skipping VBLOCK header (5, length lo, length hi). */

		/* Read the last entry into the status field. */
		memcpy(status,
                       SMB_BUF(server->packet) + 3 +
                       (count - 1) * SMB_DIRINFO_SIZE, 
                       SMB_STATUS_SIZE);

		/* Now we are ready to parse smb directory entries. */
		
		for (i = 0; i < count; i ++) {
			if (total_count < fpos) {
				p += SMB_DIRINFO_SIZE;
				DDPRINTK("smb_proc_readdir: skipped entry.\n");
				DDPRINTK("                  total_count = %d\n"
                                         "                i = %d, fpos = %d\n",
                                         total_count, i, fpos);
                        }
                        else if (total_count >= fpos + cache_size) {
                                result = total_count - fpos;
                                goto unlock_return;
			}
			else {
				p = smb_decode_dirent(p, current_entry);
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
smb_decode_long_dirent(char *p, struct smb_dirent *finfo, int level)
{
        char *result;

        if (finfo) {
                /* I have to set times to 0 here, because I do not
                   have specs about this for all info levels. */
                finfo->ctime = finfo->mtime = finfo->atime = 0;
        }

        switch (level)
        {
        case 1:                 /* OS/2 understands this */
                if (finfo)
                {
                        DPRINTK("received entry\n");
                        strcpy(finfo->path,p+27);
                        finfo->len  = strlen(finfo->path);
                        finfo->size = DVAL(p,16);
                        finfo->attr = BVAL(p,24);

                        finfo->ctime = date_dos2unix(WVAL(p, 6), WVAL(p, 4));
                        finfo->atime = date_dos2unix(WVAL(p, 10), WVAL(p, 8));
                        finfo->mtime = date_dos2unix(WVAL(p, 14), WVAL(p, 12));
                }
                result = p + 28 + BVAL(p,26);
                break;

        case 2:                 /* this is what OS/2 uses */
                if (finfo)
                {
                        strcpy(finfo->path,p+31);
                        finfo->len  = strlen(finfo->path);
                        finfo->size = DVAL(p,16);
                        finfo->attr = BVAL(p,24);
#if 0
                        finfo->atime = make_unix_date2(p+8);
                        finfo->mtime = make_unix_date2(p+12);
#endif
                }
                result = p + 32 + BVAL(p,30);
                break;

        case 260:               /* NT uses this, but also accepts 2 */
                result = p + WVAL(p,0);
                if (finfo)
                {
                        int namelen;
                        p += 4; /* next entry offset */
                        p += 4; /* fileindex */
                        /* finfo->ctime = interpret_filetime(p);*/
                        p += 8;
                        /* finfo->atime = interpret_filetime(p);*/
                        p += 8;
                        p += 8; /* write time */
                        /* finfo->mtime = interpret_filetime(p);*/
                        p += 8;
                        finfo->size = DVAL(p,0);
                        p += 8;
                        p += 8; /* alloc size */
                        finfo->attr = BVAL(p,0);
                        p += 4;
                        namelen = min(DVAL(p,0), SMB_MAXNAMELEN);
                        p += 4;
                        p += 4; /* EA size */
                        p += 2; /* short name len? */
                        p += 24; /* short name? */	  
                        strncpy(finfo->path,p,namelen);
                        finfo->len = namelen;
                }
                break;

        default:
                DPRINTK("Unknown long filename format %d\n",level);
                result = p + WVAL(p,0);
        }
        return result;
}

int
smb_proc_readdir_long(struct smb_server *server, struct inode *dir, int fpos,
                      int cache_size, struct smb_dirent *entry)
{
        int max_matches = 64; /* this should actually be based on the 
				 maxxmit */
  
        /* NT uses 260, OS/2 uses 2. Both accept 1. */
        int info_level = 1;

	char *p;
	int i;
	int first, total_count;
        struct smb_dirent *current_entry;

        char *resp_data;
        char *resp_param;
        int resp_data_len = 0;
        int resp_param_len=0;

        int attribute = aSYSTEM | aHIDDEN | aDIR;
        int result;

        int ff_resume_key = 0;
        int ff_searchcount=0;
        int ff_eos=0;
        int ff_lastname=0;
        int ff_dir_handle=0;
        int loop_count = 0;

	int dirlen = strlen(SMB_FINFO(dir)->path);
	char mask[dirlen + 5];

	strcpy(mask, SMB_FINFO(dir)->path);
	strcat(mask, "\\*");

 	DPRINTK("SMB call lreaddir %d @ %d\n", cache_size, fpos);        
	DPRINTK("          mask = %s\n", mask);

        resp_param = NULL;
        resp_data  = NULL;

        smb_lock_server(server);

 retry:

	first = 1;
        total_count = 0;
        current_entry = entry;
	
        while (ff_eos == 0)
        {
                int masklen = strlen(mask);
                unsigned char *outbuf = server->packet;
                
                loop_count += 1;
                if (loop_count > 200)
                {
                        printk("smb_proc_readdir_long: "
                               "Looping in FIND_NEXT??\n");
                        break;
                }

                smb_setup_header(server, SMBtrans2, 15,
                                 5 + 12 + masklen + 1);

                WSET(outbuf,smb_tpscnt,12 + masklen +1);
                WSET(outbuf,smb_tdscnt,0);
                WSET(outbuf,smb_mprcnt,10); 
                WSET(outbuf,smb_mdrcnt,TRANS2_MAX_TRANSFER);
                WSET(outbuf,smb_msrcnt,0);
                WSET(outbuf,smb_flags,0); 
                DSET(outbuf,smb_timeout,0);
                WSET(outbuf,smb_pscnt,WVAL(outbuf,smb_tpscnt));
                WSET(outbuf,smb_psoff,((SMB_BUF(outbuf)+3) - outbuf)-4);
                WSET(outbuf,smb_dscnt,0);
                WSET(outbuf,smb_dsoff,0);
                WSET(outbuf,smb_suwcnt,1);
                WSET(outbuf,smb_setup0,
                     first == 1 ? TRANSACT2_FINDFIRST : TRANSACT2_FINDNEXT);

                p = SMB_BUF(outbuf);
                *p++=0;         /* put in a null smb_name */
                *p++='D'; *p++ = ' '; /* this was added because OS/2 does it */

                if (first != 0)
                {
                        WSET(p,0,attribute); /* attribute */
                        WSET(p,2,max_matches); /* max count */
                        WSET(p,4,8+4+2); /* resume required + close on end +
                                            continue */
                        WSET(p,6,info_level); 
                        DSET(p,8,0);
                        p += 12;
                        strncpy(p, mask, masklen);
                        p += masklen;
                        *p++ = 0; *p++ = 0;
                }
                else
                {
                        DPRINTK("hand=0x%X resume=%d ff_lastname=%d mask=%s\n",
                                ff_dir_handle,ff_resume_key,ff_lastname,mask);
                        WSET(p,0,ff_dir_handle);
                        WSET(p,2,max_matches); /* max count */
                        WSET(p,4,info_level); 
                        DSET(p,6,ff_resume_key); /* ff_resume_key */
                        WSET(p,10,8+4+2); /* resume required + close on end +
                                             continue */
                        p += 12;
                        strncpy(p, mask, masklen);
                        p += masklen;
                        *p++ = 0; *p++ = 0;
                }

                result = smb_trans2_request(server,
                                            &resp_data_len,&resp_param_len,
                                            &resp_data,&resp_param);

                if (result < 0) {
                        if (smb_retry(server)) {
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
                p = resp_param;
                if (first != 0)
                {
                        ff_dir_handle = WVAL(p,0);
                        ff_searchcount = WVAL(p,2);
                        ff_eos = WVAL(p,4);
                        ff_lastname = WVAL(p,8);
                }
                else
                {
                        ff_searchcount = WVAL(p,0);
                        ff_eos = WVAL(p,2);
                        ff_lastname = WVAL(p,6);
                }

                if (ff_searchcount == 0) 
                        break;

                /* point to the data bytes */
                p = resp_data;

                /* we might need the lastname for continuations */
                if (ff_lastname > 0)
                {
                        switch(info_level)
                        {
                        case 260:
                                ff_resume_key =0;
                                strcpy(mask,p+ff_lastname+94);
                                break;
                        case 1:
                                strcpy(mask,p + ff_lastname + 1);
                                ff_resume_key = 0;
                                break;
                        }
                }
                else
                        strcpy(mask,"");
  
		/* Now we are ready to parse smb directory entries. */
		
		for (i = 0; i < ff_searchcount; i ++) {
			if (total_count < fpos) {
				p = smb_decode_long_dirent(p, NULL,
                                                           info_level);
				DPRINTK("smb_proc_readdir: skipped entry.\n");
				DDPRINTK("                  total_count = %d\n"
                                         "                i = %d, fpos = %d\n",
                                         total_count, i, fpos);
                        }
                        else if (total_count >= fpos + cache_size) {
                                goto finished;
			}
			else {
				p = smb_decode_long_dirent(p, current_entry,
                                                           info_level);
				current_entry->f_pos = total_count;
				DDPRINTK("smb_proc_readdir: entry->f_pos = "
                                         "%lu\n", entry->f_pos);	
				current_entry += 1;
			}
			total_count += 1;
		}

                if (resp_data != NULL) {
                        smb_kfree_s(resp_data,  0);
                        resp_data = NULL;
                }
                if (resp_param != NULL) {
                        smb_kfree_s(resp_param, 0);
                        resp_param = NULL;
                }

                DPRINTK("received %d entries (eos=%d resume=%d)\n",
                        ff_searchcount,ff_eos,ff_resume_key);

                first = 0;
        }

 finished:
        if (resp_data != NULL) {
                smb_kfree_s(resp_data,  0);
                resp_data = NULL;
        }
        if (resp_param != NULL) {
                smb_kfree_s(resp_param, 0);
                resp_param = NULL;
        }

        smb_unlock_server(server);

        return total_count - fpos;
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
smb_proc_getattr_core(struct smb_server *server, const char *path, int len, 
                      struct smb_dirent *entry)
{
	int result;
	char *p;
        char *buf = server->packet;

        smb_lock_server(server);

        DDPRINTK("smb_proc_getattr: %s\n", path);

 retry:
	p = smb_setup_header(server, SMBgetatr, 0, 2 + len);
	smb_encode_ascii(p, path, len);
	
	if ((result = smb_request_ok(server, SMBgetatr, 10, 0)) < 0) {
                if (smb_retry(server)) {
                        goto retry;
                }
                smb_unlock_server(server);
		return result;
        }

        entry->attr         = WVAL(buf, smb_vwv0);
        entry->ctime = entry->atime = /* The server only tells us 1 time */
                entry->mtime = local2utc(DVAL(buf, smb_vwv1));

        entry->size         = DVAL(buf, smb_vwv3);
        smb_unlock_server(server);
	return 0;
}

/* smb_proc_getattrE: entry->fid must be valid */

static int
smb_proc_getattrE(struct smb_server *server, struct smb_dirent *entry)
{
        char* buf = server->packet;
        int result;

        smb_setup_header_exclusive(server, SMBgetattrE, 1, 0);
        WSET(buf, smb_vwv0, entry->fileid);

        if ((result = smb_request_ok(server, SMBgetattrE, 11, 0)) != 0) {
                smb_unlock_server(server);
                return result;
        }

        entry->ctime = date_dos2unix(WVAL(buf, smb_vwv1), WVAL(buf, smb_vwv0));
        entry->atime = date_dos2unix(WVAL(buf, smb_vwv3), WVAL(buf, smb_vwv2));
        entry->mtime = date_dos2unix(WVAL(buf, smb_vwv5), WVAL(buf, smb_vwv4));
        entry->size  = DVAL(buf, smb_vwv6);
        entry->attr  = WVAL(buf, smb_vwv10);

        smb_unlock_server(server);
        return 0;
}

int
smb_proc_getattr(struct smb_server *server, const char *path, int len, 
                 struct smb_dirent *entry)
{
        if (server->protocol >= PROTOCOL_LANMAN1) {

                int result = 0;
                struct smb_dirent temp_entry;

                if ((result=smb_proc_open(server,path,len,
                                          &temp_entry)) < 0) {
                        /* We cannot open directories, so we try to use the
                           core variant */
                        return smb_proc_getattr_core(server,path,len,entry);
                }

                if ((result=smb_proc_getattrE(server, &temp_entry)) >= 0) {
                        entry->attr  = temp_entry.attr;
                        entry->atime = temp_entry.atime;
                        entry->mtime = temp_entry.mtime;
                        entry->ctime = temp_entry.ctime;
                        entry->size  = temp_entry.size;
                }
                
                smb_proc_close(server, &temp_entry);
                return result;

        } else {
                return smb_proc_getattr_core(server, path, len, entry);
        }
}


/* In core protocol, there is only 1 time to be set, we use
   entry->mtime, to make touch work. */
static int
smb_proc_setattr_core(struct smb_server *server,
                      const char *path, int len,
                      struct smb_dirent *new_finfo)
{
        char *p;
        char *buf = server->packet;
        int result;

        smb_lock_server(server);

 retry:
        p = smb_setup_header(server, SMBsetatr, 8, 4 + len);
        WSET(buf, smb_vwv0, new_finfo->attr);
        DSET(buf, smb_vwv1, utc2local(new_finfo->mtime));
        p = smb_encode_ascii(p, path, len);
        p = smb_encode_ascii(p, "", 0);

        if ((result = smb_request_ok(server, SMBsetatr, 0, 0)) < 0) {
                if (smb_retry(server)) {
                        goto retry;
                }
        }
        smb_unlock_server(server);
        return result;
}

/* smb_proc_setattrE: we do not retry here, because we rely on fid,
   which would not be valid after a retry. */
static int
smb_proc_setattrE(struct smb_server *server, word fid,
                  struct smb_dirent *new_entry)
{
        char *buf = server->packet;
        word date, time;

        smb_setup_header_exclusive(server, SMBsetattrE, 7, 0);

        WSET(buf, smb_vwv0, fid);

        date_unix2dos(new_entry->ctime, &time, &date);
        WSET(buf, smb_vwv1, date);
        WSET(buf, smb_vwv2, time);
        
        date_unix2dos(new_entry->atime, &time, &date);
        WSET(buf, smb_vwv3, date);
        WSET(buf, smb_vwv4, time);
        
        date_unix2dos(new_entry->mtime, &time, &date);
        WSET(buf, smb_vwv5, date);
        WSET(buf, smb_vwv6, time);

        return smb_request_ok_unlock(server, SMBsetattrE, 0, 0);
}

/* smb_proc_setattr: for protocol >= LANMAN1 we expect the file to be
   opened for writing. */
int
smb_proc_setattr(struct smb_server *server, struct inode *inode,
                 struct smb_dirent *new_finfo)
{
        struct smb_dirent *finfo = SMB_FINFO(inode);
        int result;

        if (server->protocol >= PROTOCOL_LANMAN1) {
                if ((result = smb_make_open(inode, O_RDWR)) < 0)
                        return result;
                return smb_proc_setattrE(server, finfo->fileid, new_finfo);
        } else {
                return smb_proc_setattr_core(server, finfo->path, finfo->len,
                                             new_finfo);
        }
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
	
	if ((error = smb_request_ok(server, SMBdskattr, 5, 0)) < 0) {
                if (smb_retry(server)) {
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

struct smb_prots {
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
        { { PROTOCOL_CORE, "PC NETWORK PROGRAM 1.0"},
          { PROTOCOL_COREPLUS,"MICROSOFT NETWORKS 1.03"},
#ifdef LANMAN1
          { PROTOCOL_LANMAN1,"MICROSOFT NETWORKS 3.0"},
          { PROTOCOL_LANMAN1,"LANMAN1.0"},
#endif
#ifdef CONFIG_SMB_LONG
#ifdef LANMAN2
          { PROTOCOL_LANMAN2,"LM1.2X002"},
#endif
#ifdef NT1
	  { PROTOCOL_NT1,"NT LM 0.12"},
	  { PROTOCOL_NT1,"NT LANMAN 1.0"},
#endif
#endif
          {-1, NULL} };
	char dev[] = "A:";
	int i, plength;
	int max_xmit = 1024;	/* Space needed for first request. */
        int given_max_xmit = server->m.max_xmit;
	int result;
	byte *p;

        if ((result = smb_connect(server)) < 0) {
                DPRINTK("smb_proc_reconnect: could not smb_connect\n");
                goto fail;
        }

        /* Here we assume that the connection is valid */
        server->state = CONN_VALID;

        if (server->packet != NULL) {
                smb_kfree_s(server->packet, server->max_xmit);
        }
        
	server->packet = smb_kmalloc(max_xmit, GFP_KERNEL);

	if (server->packet == NULL) {
		printk("smb_proc_connect: No memory! Bailing out.\n");
                result = -ENOMEM;
                goto fail;
	}

        server->max_xmit = max_xmit;

	/*
	 * Start with an RFC1002 session request packet.
	 */
	p = server->packet + 4;

	p = smb_name_mangle(p, server->m.server_name);
	p = smb_name_mangle(p, server->m.client_name);
	
	smb_encode_smb_length(server->packet,
                              (void *)p - (void *)(server->packet));
	
	server->packet[0] = 0x81; /* SESSION REQUEST */

        if (smb_catch_keepalive(server) < 0) {
                printk("smb_proc_connect: could not catch_keepalives\n");
        }
        
	if ((result = smb_request(server)) < 0) {
		printk("smb_proc_connect: Failed to send SESSION REQUEST.\n");
                smb_dont_catch_keepalive(server);
                goto fail;
	}
	
	if (server->packet[0] != 0x82) {
		printk("smb_proc_connect: Did not receive positive response "
                       "(err = %x)\n", 
		       server->packet[0]);
                smb_dont_catch_keepalive(server);
#if DEBUG_SMB > 0
                smb_dump_packet(server->packet);
#endif
                result = -EIO;
                goto fail;
	}

        DPRINTK("smb_proc_connect: Passed SESSION REQUEST.\n");
	
	/* Now we are ready to send a SMB Negotiate Protocol packet. */
	memset(server->packet, 0, SMB_HEADER_LEN);

	plength = 0;
	for (i = 0; prots[i].name != NULL; i++) {
		plength += strlen(prots[i].name) + 2;
        }

	smb_setup_header(server, SMBnegprot, 0, plength);

	p = SMB_BUF(server->packet);
	
	for (i = 0; prots[i].name != NULL; i++) {
		p = smb_encode_dialect(p,prots[i].name, strlen(prots[i].name));
        }
	
	if ((result = smb_request_ok(server, SMBnegprot, 1, -1)) < 0) {
         	printk("smb_proc_connect: Failure requesting SMBnegprot\n");
                smb_dont_catch_keepalive(server);
                goto fail;
	} else {
                DDPRINTK("smb_proc_connect: Request SMBnegprot..");
        }

        DDPRINTK("Verified!\n");

        p = SMB_VWV(server->packet);
	p = smb_decode_word(p, (word *)&i);
        server->protocol = prots[i].prot;

	DPRINTK("smb_proc_connect: Server wants %s protocol.\n",
                prots[i].name);

        if (server->protocol > PROTOCOL_LANMAN1) {

                word passlen = strlen(server->m.password);
                word userlen = strlen(server->m.username);
                
                DPRINTK("smb_proc_connect: password = %s\n",
                        server->m.password);
                DPRINTK("smb_proc_connect: usernam = %s\n",
                        server->m.username);
                DPRINTK("smb_proc_connect: blkmode = %d\n",
                        WVAL(server->packet, smb_vwv5));

		if (server->protocol >= PROTOCOL_NT1) {
			server->maxxmt = DVAL(server->packet,smb_vwv3+1);
			server->maxmux = WVAL(server->packet, smb_vwv1+1);
			server->maxvcs = WVAL(server->packet, smb_vwv2+1);
			server->blkmode= DVAL(server->packet, smb_vwv9+1);
			server->sesskey= DVAL(server->packet, smb_vwv7+1);
		} else {
			server->maxxmt = WVAL(server->packet, smb_vwv2);
			server->maxmux = WVAL(server->packet, smb_vwv3);
			server->maxvcs = WVAL(server->packet, smb_vwv4);
			server->blkmode= WVAL(server->packet, smb_vwv5);
			server->sesskey= DVAL(server->packet, smb_vwv6);
		}


		if (server->protocol >= PROTOCOL_NT1) {
			char *workgroup = "WORKGROUP";
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
		} else {
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

                if ((result = smb_request_ok(server,SMBsesssetupX,3,0)) < 0) {
                        DPRINTK("smb_proc_connect: SMBsessetupX failed\n");
                        smb_dont_catch_keepalive(server);
                        goto fail;
                }
                smb_decode_word(server->packet+32, &(server->server_uid));
        }
        else

        {
                server->maxxmt = 0;
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
	p = smb_encode_ascii(p,server->m.password, strlen(server->m.password));
	p = smb_encode_ascii(p, dev, strlen(dev));

	if ((result = smb_request_ok(server, SMBtcon, 2, 0)) < 0) {
		DPRINTK("smb_proc_connect: SMBtcon not verified.\n");
                smb_dont_catch_keepalive(server);
                goto fail;
	}

        DDPRINTK("OK! Managed to set up SMBtcon!\n");
   
	p = SMB_VWV(server->packet);
	p = smb_decode_word(p, &server->max_xmit);

        if (server->max_xmit > given_max_xmit)
                server->max_xmit = given_max_xmit;
        
	p = smb_decode_word(p, &server->tid);

	/* Ok, everything is fine. max_xmit does not include */
	/* the TCP-SMB header of 4 bytes. */
	server->max_xmit += 4;

	DPRINTK("max_xmit = %d, tid = %d\n", server->max_xmit, server->tid);

	/* Now make a new packet with the correct size. */
	smb_kfree_s(server->packet, max_xmit); 

	server->packet = smb_kmalloc(server->max_xmit, GFP_KERNEL);
	if (server->packet == NULL) {
		printk("smb_proc_connect: No memory left in end of "
                       "connection phase :-(\n");
                smb_dont_catch_keepalive(server);
                goto fail;
	}

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
        if ((result < 0) && (server->packet != NULL)) {
                smb_kfree_s(server->packet, server->max_xmit);
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

/* error code stuff - put together by Merik Karman
   merik@blackadder.dsh.oz.au */

#if DEBUG_SMB > 0

typedef struct {
	char *name;
	int code;
	char *message;
} err_code_struct;

/* Dos Error Messages */
err_code_struct dos_msgs[] = {
  { "ERRbadfunc",1,"Invalid function."},
  { "ERRbadfile",2,"File not found."},
  { "ERRbadpath",3,"Directory invalid."},
  { "ERRnofids",4,"No file descriptors available"},
  { "ERRnoaccess",5,"Access denied."},
  { "ERRbadfid",6,"Invalid file handle."},
  { "ERRbadmcb",7,"Memory control blocks destroyed."},
  { "ERRnomem",8,"Insufficient server memory to perform the requested function."},
  { "ERRbadmem",9,"Invalid memory block address."},
  { "ERRbadenv",10,"Invalid environment."},
  { "ERRbadformat",11,"Invalid format."},
  { "ERRbadaccess",12,"Invalid open mode."},
  { "ERRbaddata",13,"Invalid data."},
  { "ERR",14,"reserved."},
  { "ERRbaddrive",15,"Invalid drive specified."},
  { "ERRremcd",16,"A Delete Directory request attempted  to  remove  the  server's  current directory."},
  { "ERRdiffdevice",17,"Not same device."},
  { "ERRnofiles",18,"A File Search command can find no more files matching the specified criteria."},
  { "ERRbadshare",32,"The sharing mode specified for an Open conflicts with existing  FIDs  on the file."},
  { "ERRlock",33,"A Lock request conflicted with an existing lock or specified an  invalid mode,  or an Unlock requested attempted to remove a lock held by another process."},
  { "ERRfilexists",80,"The file named in a Create Directory, Make  New  File  or  Link  request already exists."},
  { "ERRbadpipe",230,"Pipe invalid."},
  { "ERRpipebusy",231,"All instances of the requested pipe are busy."},
  { "ERRpipeclosing",232,"Pipe close in progress."},
  { "ERRnotconnected",233,"No process on other end of pipe."},
  { "ERRmoredata",234,"There is more data to be returned."},
  { NULL,-1,NULL}};

/* Server Error Messages */
err_code_struct server_msgs[] = { 
  { "ERRerror",1,"Non-specific error code."},
  { "ERRbadpw",2,"Bad password - name/password pair in a Tree Connect or Session Setup are invalid."},
  { "ERRbadtype",3,"reserved."},
  { "ERRaccess",4,"The requester does not have  the  necessary  access  rights  within  the specified  context for the requested function. The context is defined by the TID or the UID."},
  { "ERRinvnid",5,"The tree ID (TID) specified in a command was invalid."},
  { "ERRinvnetname",6,"Invalid network name in tree connect."},
  { "ERRinvdevice",7,"Invalid device - printer request made to non-printer connection or  non-printer request made to printer connection."},
  { "ERRqfull",49,"Print queue full (files) -- returned by open print file."},
  { "ERRqtoobig",50,"Print queue full -- no space."},
  { "ERRqeof",51,"EOF on print queue dump."},
  { "ERRinvpfid",52,"Invalid print file FID."},
  { "ERRsmbcmd",64,"The server did not recognize the command received."},
  { "ERRsrverror",65,"The server encountered an internal error, e.g., system file unavailable."},
  { "ERRfilespecs",67,"The file handle (FID) and pathname parameters contained an invalid  combination of values."},
  { "ERRreserved",68,"reserved."},
  { "ERRbadpermits",69,"The access permissions specified for a file or directory are not a valid combination.  The server cannot set the requested attribute."},
  { "ERRreserved",70,"reserved."},
  { "ERRsetattrmode",71,"The attribute mode in the Set File Attribute request is invalid."},
  { "ERRpaused",81,"Server is paused."},
  { "ERRmsgoff",82,"Not receiving messages."},
  { "ERRnoroom",83,"No room to buffer message."},
  { "ERRrmuns",87,"Too many remote user names."},
  { "ERRtimeout",88,"Operation timed out."},
  { "ERRnoresource",89,"No resources currently available for request."},
  { "ERRtoomanyuids",90,"Too many UIDs active on this session."},
  { "ERRbaduid",91,"The UID is not known as a valid ID on this session."},
  { "ERRusempx",250,"Temp unable to support Raw, use MPX mode."},
  { "ERRusestd",251,"Temp unable to support Raw, use standard read/write."},
  { "ERRcontmpx",252,"Continue in MPX mode."},
  { "ERRreserved",253,"reserved."},
  { "ERRreserved",254,"reserved."},
  { "ERRnosupport",0xFFFF,"Function not supported."},
  { NULL,-1,NULL}};

/* Hard Error Messages */
err_code_struct hard_msgs[] = { 
  { "ERRnowrite",19,"Attempt to write on write-protected diskette."},
  { "ERRbadunit",20,"Unknown unit."},
  { "ERRnotready",21,"Drive not ready."},
  { "ERRbadcmd",22,"Unknown command."},
  { "ERRdata",23,"Data error (CRC)."},
  { "ERRbadreq",24,"Bad request structure length."},
  { "ERRseek",25 ,"Seek error."},
  { "ERRbadmedia",26,"Unknown media type."},
  { "ERRbadsector",27,"Sector not found."},
  { "ERRnopaper",28,"Printer out of paper."},
  { "ERRwrite",29,"Write fault."},
  { "ERRread",30,"Read fault."},
  { "ERRgeneral",31,"General failure."},
  { "ERRbadshare",32,"A open conflicts with an existing open."},
  { "ERRlock",33,"A Lock request conflicted with an existing lock or specified an invalid mode, or an Unlock requested attempted to remove a lock held by another process."},
  { "ERRwrongdisk",34,"The wrong disk was found in a drive."},
  { "ERRFCBUnavail",35,"No FCBs are available to process request."},
  { "ERRsharebufexc",36,"A sharing buffer has been exceeded."},
  { NULL,-1,NULL}
};


struct { 
	int code;
	char *class;
	err_code_struct *err_msgs;
} err_classes[] = {  
  { 0,"SUCCESS",NULL},
  { 0x01,"ERRDOS",dos_msgs},
  { 0x02,"ERRSRV",server_msgs},
  { 0x03,"ERRHRD",hard_msgs},
  { 0x04,"ERRXOS",NULL},
  { 0xE1,"ERRRMX1",NULL},
  { 0xE2,"ERRRMX2",NULL},
  { 0xE3,"ERRRMX3",NULL},
  { 0xFF,"ERRCMD",NULL},
  { -1,NULL,NULL}
};

void
smb_printerr(int class, int num)
{
	int i,j;
	err_code_struct *err;

	for (i=0; err_classes[i].class; i++) {
		if (err_classes[i].code != class)
			continue;
		if (!err_classes[i].err_msgs) {
			printk("%s - %d", err_classes[i].class, num);
			return;
		}

		err = err_classes[i].err_msgs;
		for (j=0; err[j].name; j++) {
			if (num != err[j].code)
				continue;
			printk("%s - %s (%s)",
			       err_classes[i].class, err[j].name,
                               err[j].message);
			return;
		}
	}
	
	printk("Unknown error - (%d,%d)", class, num);
	return;
}

#endif /* DEBUG_SMB > 0 */
