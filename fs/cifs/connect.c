/*
 *   fs/cifs/connect.c
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2003
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 */
#include <linux/fs.h>
#include <linux/net.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/version.h>
#include <linux/ipv6.h>
#include <linux/pagemap.h>
#include <asm/uaccess.h>
#include <asm/processor.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"
#include "ntlmssp.h"
#include "nterr.h"

#define CIFS_PORT 445
#define RFC1001_PORT 139

extern void SMBencrypt(unsigned char *passwd, unsigned char *c8,
		       unsigned char *p24);
extern void SMBNTencrypt(unsigned char *passwd, unsigned char *c8,
			 unsigned char *p24);
extern int inet_addr(char *);

struct smb_vol {
	char *username;
	char *password;
	char *domainname;
	char *UNC;
	char *UNCip;
	char *iocharset;  /* local code page for mapping to and from Unicode */
	char *source_rfc1001_name; /* netbios name of client */
	uid_t linux_uid;
	gid_t linux_gid;
	mode_t file_mode;
	mode_t dir_mode;
	int rw;
	unsigned int rsize;
	unsigned int wsize;
	unsigned int sockopt;
	unsigned short int port;
};

int ipv4_connect(struct sockaddr_in *psin_server, struct socket **csocket);


	/* 
	 * cifs tcp session reconnection
	 * 
	 * mark tcp session as reconnecting so temporarily locked
	 * mark all smb sessions as reconnecting for tcp session  (TBD BB)
	 * reconnect tcp session
	 * wake up waiters on reconnection? - (not needed currently)
	 */

int
cifs_reconnect(struct TCP_Server_Info *server)
{
	int rc = 0;
	struct list_head *tmp;
	struct cifsSesInfo *ses;
	struct cifsTconInfo *tcon;
	
	if(server->tcpStatus == CifsExiting)
		return rc;
	server->tcpStatus = CifsNeedReconnect;
	server->maxBuf = 0;

	cFYI(1, ("Reconnecting tcp session "));

	/* before reconnecting the tcp session, mark the smb session (uid)
		and the tid bad so they are not used until reconnected */
	read_lock(&GlobalSMBSeslock);
	list_for_each(tmp, &GlobalSMBSessionList) {
		ses = list_entry(tmp, struct cifsSesInfo, cifsSessionList);
		if (ses->server) {
			if (ses->server == server) {
				ses->status = CifsNeedReconnect;
				ses->ipc_tid = 0;
			}
		}
		/* else tcp and smb sessions need reconnection */
	}
	list_for_each(tmp, &GlobalTreeConnectionList) {
		tcon = list_entry(tmp, struct cifsTconInfo, cifsConnectionList);
		if((tcon) && (tcon->ses) && (tcon->ses->server == server)) {
			tcon->tidStatus = CifsNeedReconnect;
		}
	}
	read_unlock(&GlobalSMBSeslock);

	if(server->ssocket) {
		cFYI(1,("State: 0x%x Flags: 0x%lx", server->ssocket->state,
			server->ssocket->flags));
		server->ssocket->ops->shutdown(server->ssocket,SEND_SHUTDOWN);;
		cFYI(1,("Post shutdown state: 0x%x Flags: 0x%lx", server->ssocket->state,
			server->ssocket->flags));
		sock_release(server->ssocket);
		server->ssocket = NULL;
	}

	while ((server->tcpStatus != CifsExiting) && (server->tcpStatus != CifsGood))
	{
		rc = ipv4_connect(&server->sockAddr, &server->ssocket);
		if(rc) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(3 * HZ);
		} else {
			server->tcpStatus = CifsGood;
			wake_up(&server->response_q);
		}
	}

	return rc;
}

int
cifs_demultiplex_thread(struct TCP_Server_Info *server)
{
	int length;
	unsigned int pdu_length, total_read;
	struct smb_hdr *smb_buffer = NULL;
	struct msghdr smb_msg;
	mm_segment_t temp_fs;
	struct iovec iov;
	struct socket *csocket = server->ssocket;
	struct list_head *tmp;
	struct cifsSesInfo *ses;
	struct task_struct *task_to_wake = NULL;
	struct mid_q_entry *mid_entry;
	char *temp;

	daemonize("cifsd");
	allow_signal(SIGKILL);

	server->tsk = current;	/* save process info to wake at shutdown */
	cFYI(1, ("Demultiplex PID: %d", current->pid));

	temp_fs = get_fs();	/* we must turn off socket api parm checking */
	set_fs(get_ds());

	while (server->tcpStatus != CifsExiting) {
		if (smb_buffer == NULL)
			smb_buffer = buf_get();
		else
			memset(smb_buffer, 0, sizeof (struct smb_hdr));

		if (smb_buffer == NULL) {
			cERROR(1,
			       ("Can not get mem for SMB response buffer "));
			return -ENOMEM;
		}
		iov.iov_base = smb_buffer;
		iov.iov_len = sizeof (struct smb_hdr) - 1;	
        /* 1 byte less above since wct is not always returned in error cases */
		smb_msg.msg_iov = &iov;
		smb_msg.msg_iovlen = 1;
		smb_msg.msg_control = NULL;
		smb_msg.msg_controllen = 0;

		length =
		    sock_recvmsg(csocket, &smb_msg,
				 sizeof (struct smb_hdr) -
				 1 /* RFC1001 header and SMB header */ ,
				 MSG_PEEK /* flags see socket.h */ );

		if(server->tcpStatus == CifsExiting) {
			break;
		} else if (server->tcpStatus == CifsNeedReconnect) {
			cFYI(1,("Reconnecting after server stopped responding"));
			cifs_reconnect(server);
			csocket = server->ssocket;
			continue;
		} else if ((length == -ERESTARTSYS) || (length == -EAGAIN)) {
			schedule_timeout(1); /* minimum sleep to prevent looping
				allowing socket to clear and app threads to set
				tcpStatus CifsNeedReconnect if server hung */
			continue;
		} else if (length <= 0) {
			cFYI(1,("Reconnecting after unexpected rcvmsg error "));
			cifs_reconnect(server);
			csocket = server->ssocket;
			continue;
		}

		pdu_length = 4 + ntohl(smb_buffer->smb_buf_length);
		cFYI(1, ("Peek length rcvd: %d with smb length: %d", length, pdu_length));

		temp = (char *) smb_buffer;
		if (length > 3) {
			if (temp[0] == (char) 0x85) {
				iov.iov_base = smb_buffer;
				iov.iov_len = 4;
				length = sock_recvmsg(csocket, &smb_msg, 4, 0);
				cFYI(0,
				     ("Received 4 byte keep alive packet "));
			} else if ((temp[0] == (char) 0x83)
				   && (length == 5)) {
				/* we get this from Windows 98 instead of error on SMB negprot response */
				cERROR(1,
				       ("Negative RFC 1002 Session response. Error = 0x%x",
					temp[4]));
				break;

			} else if (temp[0] != (char) 0) {
				cERROR(1,
				       ("Unknown RFC 1001 frame received not 0x00 nor 0x85"));
				cifs_dump_mem(" Received Data is: ", temp, length);
				cifs_reconnect(server);
				csocket = server->ssocket;
				continue;
			} else {
				if ((length != sizeof (struct smb_hdr) - 1)
				    || (pdu_length >
					CIFS_MAX_MSGSIZE + MAX_CIFS_HDR_SIZE)
				    || (pdu_length <
					sizeof (struct smb_hdr) - 1)
				    ||
				    (checkSMBhdr
				     (smb_buffer, smb_buffer->Mid))) {
					cERROR(1,
					    ("Invalid size or format for SMB found with length %d and pdu_lenght %d",
						length, pdu_length));
					cifs_dump_mem("Received Data is: ",temp,sizeof(struct smb_hdr));
					/* could we fix this network corruption by finding next 
						smb header (instead of killing the session) and
						restart reading from next valid SMB found? */
					cifs_reconnect(server);
					csocket = server->ssocket;
					continue;
				} else {	/* length ok */

					length = 0;
					iov.iov_base = smb_buffer;
					iov.iov_len = pdu_length;
					for (total_read = 0; total_read < pdu_length; total_read += length) {	
             /* Should improve check for buffer overflow with bad pdu_length */
						length = sock_recvmsg(csocket, &smb_msg, 
							pdu_length - total_read, 0);
						if (length == 0) {
							cERROR(1,
							       ("Zero length receive when expecting %d ",
								pdu_length - total_read));
							cifs_reconnect(server);
							csocket = server->ssocket;
							continue;
						}
					}
				}

				dump_smb(smb_buffer, length);
				if (checkSMB
				    (smb_buffer, smb_buffer->Mid, total_read)) {
					cERROR(1, ("Bad SMB Received "));
					continue;
				}

				task_to_wake = NULL;
				spin_lock(&GlobalMid_Lock);
				list_for_each(tmp, &server->pending_mid_q) {
					mid_entry = list_entry(tmp, struct
							       mid_q_entry,
							       qhead);

					if (mid_entry->mid == smb_buffer->Mid) {
						cFYI(1,
						     (" Mid 0x%x matched - waking up ",mid_entry->mid));
						task_to_wake = mid_entry->tsk;
						mid_entry->resp_buf =
						    smb_buffer;
						mid_entry->midState =
						    MID_RESPONSE_RECEIVED;
					}
				}
				spin_unlock(&GlobalMid_Lock);
				if (task_to_wake) {
					smb_buffer = NULL;	/* will be freed by users thread after he is done */
					wake_up_process(task_to_wake);
				} else if (is_valid_oplock_break(smb_buffer) == FALSE) {                          
					cERROR(1, ("No task to wake, unknown frame rcvd!"));
				}
			}
		} else {
			cFYI(0,
			     ("Frame less than four bytes received  %d bytes long.",
			      length));
			if (length > 0) {
				length = sock_recvmsg(csocket, &smb_msg, length, 0);	/* throw away junk frame */
				cFYI(1,
				     (" with junk  0x%x in it ",
				      *(__u32 *) smb_buffer));
			}
		}
	}
	/* BB add code to lock SMB sessions while releasing */
	if(server->ssocket) {
		sock_release(csocket);
	    server->ssocket = NULL;
	}
	set_fs(temp_fs);
	if (smb_buffer) /* buffer usually freed in free_mid - need to free it on error or exit */
		buf_release(smb_buffer);

	read_lock(&GlobalSMBSeslock);
	if (list_empty(&server->pending_mid_q)) {
		/* loop through server session structures attached to this and mark them dead */
		list_for_each(tmp, &GlobalSMBSessionList) {
			ses =
			    list_entry(tmp, struct cifsSesInfo,
				       cifsSessionList);
			if (ses->server == server) {
				ses->status = CifsExiting;
				ses->server = NULL;
			}
		}
		kfree(server);
	} else	/* BB need to more gracefully handle the rare negative session 
			   response case because response will be still outstanding */
		cERROR(1, ("Active MIDs in queue while exiting - can not delete mid_q_entries or TCP_Server_Info structure due to pending requests MEMORY LEAK!!"));
	/* BB wake up waitors, and/or wait and/or free stale mids and try again? BB */
	/* BB Need to fix bug in error path above - perhaps wait until smb requests
   time out and then free the tcp per server struct BB */
	read_unlock(&GlobalSMBSeslock);

	cFYI(1, ("About to exit from demultiplex thread"));
	return 0;
}

int
parse_mount_options(char *options, const char *devname, struct smb_vol *vol)
{
	char *value;
	char *data;
	int  temp_len;

	memset(vol,0,sizeof(struct smb_vol));
	vol->linux_uid = current->uid;	/* current->euid instead? */
	vol->linux_gid = current->gid;
	vol->dir_mode = S_IRWXUGO;
	/* 2767 perms indicate mandatory locking support */
	vol->file_mode = S_IALLUGO & ~(S_ISUID | S_IXGRP);

	vol->rw = TRUE;

	if (!options)
		return 1;

	while ((data = strsep(&options, ",")) != NULL) {
		if (!*data)
			continue;
		if ((value = strchr(data, '=')) != NULL)
			*value++ = '\0';
		if (strnicmp(data, "user", 4) == 0) {
			if (!value || !*value) {
				printk(KERN_WARNING
				       "CIFS: invalid or missing username\n");
				return 1;	/* needs_arg; */
			}
			if (strnlen(value, 200) < 200) {
				vol->username = value;
			} else {
				printk(KERN_WARNING "CIFS: username too long\n");
				return 1;
			}
		} else if (strnicmp(data, "pass", 4) == 0) {
			if (!value || !*value) {
				vol->password = NULL;
			} else if (strnlen(value, 17) < 17) {
				vol->password = value;
			} else {
				printk(KERN_WARNING "CIFS: password too long\n");
				return 1;
			}
		} else if (strnicmp(data, "ip", 2) == 0) {
			if (!value || !*value) {
				vol->UNCip = NULL;
			} else if (strnlen(value, 35) < 35) {
				vol->UNCip = value;
			} else {
				printk(KERN_WARNING "CIFS: ip address too long\n");
				return 1;
			}
		} else if ((strnicmp(data, "unc", 3) == 0)
			   || (strnicmp(data, "target", 6) == 0)
			   || (strnicmp(data, "path", 4) == 0)) {
			if (!value || !*value) {
				printk(KERN_WARNING
				       "CIFS: invalid path to network resource\n");
				return 1;	/* needs_arg; */
			}
			if ((temp_len = strnlen(value, 300)) < 300) {
				vol->UNC = kmalloc(temp_len+1,GFP_KERNEL);
				strcpy(vol->UNC,value);
				if (strncmp(vol->UNC, "//", 2) == 0) {
					vol->UNC[0] = '\\';
					vol->UNC[1] = '\\';
				} else if (strncmp(vol->UNC, "\\\\", 2) != 0) {	                   
					printk(KERN_WARNING
					       "CIFS: UNC Path does not begin with // or \\\\ \n");
					return 1;
				}
			} else {
				printk(KERN_WARNING "CIFS: UNC name too long\n");
				return 1;
			}
		} else if ((strnicmp(data, "domain", 3) == 0)
			   || (strnicmp(data, "workgroup", 5) == 0)) {
			if (!value || !*value) {
				printk(KERN_WARNING "CIFS: invalid domain name\n");
				return 1;	/* needs_arg; */
			}
			if (strnlen(value, 65) < 65) {
				vol->domainname = value;
				cFYI(1, ("Domain name set"));
			} else {
				printk(KERN_WARNING "CIFS: domain name too long\n");
				return 1;
			}
		} else if (strnicmp(data, "iocharset", 9) == 0) {
			if (!value || !*value) {
				printk(KERN_WARNING "CIFS: invalid iocharset specified\n");
				return 1;	/* needs_arg; */
			}
			if (strnlen(value, 65) < 65) {
				if(strnicmp(value,"default",7))
					vol->iocharset = value;
				/* if iocharset not set load_nls_default used by caller */
				cFYI(1, ("iocharset set to %s",value));
			} else {
				printk(KERN_WARNING "CIFS: iocharset name too long.\n");
				return 1;
			}
		} else if (strnicmp(data, "uid", 3) == 0) {
			if (value && *value) {
				vol->linux_uid =
					simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "gid", 3) == 0) {
			if (value && *value) {
				vol->linux_gid =
					simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "file_mode", 4) == 0) {
			if (value && *value) {
				vol->file_mode =
					simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "dir_mode", 3) == 0) {
			if (value && *value) {
				vol->dir_mode =
					simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "port", 4) == 0) {
			if (value && *value) {
				vol->port =
					simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "rsize", 5) == 0) {
			if (value && *value) {
				vol->rsize =
					simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "wsize", 5) == 0) {
			if (value && *value) {
				vol->wsize =
					simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "sockopt", 5) == 0) {
			if (value && *value) {
				vol->sockopt =
					simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "netbiosname", 4) == 0) {
			if (!value || !*value) {
				vol->source_rfc1001_name = NULL;
			} else if (strnlen(value, 17) < 17) {
				vol->source_rfc1001_name = value;
			} else {
				printk(KERN_WARNING "CIFS: netbiosname too long (more than 15)\n");
			}
		} else if (strnicmp(data, "version", 3) == 0) {
			/* ignore */
		} else if (strnicmp(data, "rw", 2) == 0) {
			vol->rw = TRUE;
		} else if (strnicmp(data, "ro", 2) == 0) {
			vol->rw = FALSE;
		} else
			printk(KERN_WARNING "CIFS: Unknown mount option %s\n",data);
	}
	if (vol->UNC == NULL) {
		if(devname == NULL) {
			printk(KERN_WARNING "CIFS: Missing UNC name for mount target\n");
			return 1;
		}
		if ((temp_len = strnlen(devname, 300)) < 300) {
			vol->UNC = kmalloc(temp_len+1,GFP_KERNEL);
			strcpy(vol->UNC,devname);
			if (strncmp(vol->UNC, "//", 2) == 0) {
				vol->UNC[0] = '\\';
				vol->UNC[1] = '\\';
			} else if (strncmp(vol->UNC, "\\\\", 2) != 0) {
				printk(KERN_WARNING "CIFS: UNC Path does not begin with // or \\\\ \n");
				return 1;
			}
		} else {
			printk(KERN_WARNING "CIFS: UNC name too long\n");
			return 1;
		}
	}
	if(vol->UNCip == 0)
		vol->UNCip = &vol->UNC[2];

	return 0;
}

struct cifsSesInfo *
find_tcp_session(__u32 new_target_ip_addr,
		 char *userName, struct TCP_Server_Info **psrvTcp)
{
	struct list_head *tmp;
	struct cifsSesInfo *ses;

	*psrvTcp = NULL;
	read_lock(&GlobalSMBSeslock);
	list_for_each(tmp, &GlobalSMBSessionList) {
		ses = list_entry(tmp, struct cifsSesInfo, cifsSessionList);
		if (ses->server) {
			if (ses->server->sockAddr.sin_addr.s_addr ==
			    new_target_ip_addr) {
				/* BB lock server and tcp session and increment use count here?? */
				*psrvTcp = ses->server;	/* found a match on the TCP session */
				/* BB check if reconnection needed */
				if (strncmp
				    (ses->userName, userName,
				     MAX_USERNAME_SIZE) == 0){
					read_unlock(&GlobalSMBSeslock);
					return ses;	/* found exact match on both tcp and SMB sessions */
				}
			}
		}
		/* else tcp and smb sessions need reconnection */
	}
	read_unlock(&GlobalSMBSeslock);
	return NULL;
}

struct cifsTconInfo *
find_unc(__u32 new_target_ip_addr, char *uncName, char *userName)
{
	struct list_head *tmp;
	struct cifsTconInfo *tcon;

	read_lock(&GlobalSMBSeslock);
	list_for_each(tmp, &GlobalTreeConnectionList) {
		cFYI(1, ("Next tcon - "));
		tcon = list_entry(tmp, struct cifsTconInfo, cifsConnectionList);
		if (tcon->ses) {
			if (tcon->ses->server) {
				cFYI(1,
				     (" old ip addr: %x == new ip %x ?",
				      tcon->ses->server->sockAddr.sin_addr.
				      s_addr, new_target_ip_addr));
				if (tcon->ses->server->sockAddr.sin_addr.
				    s_addr == new_target_ip_addr) {
	/* BB lock tcon and server and tcp session and increment use count here? */
					/* found a match on the TCP session */
					/* BB check if reconnection needed */
					cFYI(1,("Matched ip, old UNC: %s == new: %s ?",
					      tcon->treeName, uncName));
					if (strncmp
					    (tcon->treeName, uncName,
					     MAX_TREE_SIZE) == 0) {
						cFYI(1,
						     ("Matched UNC, old user: %s == new: %s ?",
						      tcon->treeName, uncName));
						if (strncmp
						    (tcon->ses->userName,
						     userName,
						     MAX_USERNAME_SIZE) == 0) {
							read_unlock(&GlobalSMBSeslock);
							return tcon;/* also matched user (smb session)*/
						}
					}
				}
			}
		}
	}
	read_unlock(&GlobalSMBSeslock);
	return NULL;
}

int
connect_to_dfs_path(int xid, struct cifsSesInfo *pSesInfo,
		    const char *old_path, const struct nls_table *nls_codepage)
{
	unsigned char *referrals = NULL;
	unsigned int num_referrals;
	int rc = 0;

	rc = get_dfs_path(xid, pSesInfo,old_path, nls_codepage, 
			&num_referrals, &referrals);

	/* BB Add in code to: if valid refrl, if not ip address contact
		the helper that resolves tcp names, mount to it, try to 
		tcon to it unmount it if fail */

	/* BB free memory for referrals string BB */

	return rc;
}

int
get_dfs_path(int xid, struct cifsSesInfo *pSesInfo,
			const char *old_path, const struct nls_table *nls_codepage, 
			unsigned int *pnum_referrals, unsigned char ** preferrals)
{
	char *temp_unc;
	int rc = 0;

	*pnum_referrals = 0;

	if (pSesInfo->ipc_tid == 0) {
		temp_unc = kmalloc(2 /* for slashes */ +
			strnlen(pSesInfo->serverName,SERVER_NAME_LEN_WITH_NULL * 2)
				 + 1 + 4 /* slash IPC$ */  + 2,
				GFP_KERNEL);
		if (temp_unc == NULL)
			return -ENOMEM;
		temp_unc[0] = '\\';
		temp_unc[1] = '\\';
		strcpy(temp_unc + 2, pSesInfo->serverName);
		strcpy(temp_unc + 2 + strlen(pSesInfo->serverName), "\\IPC$");
		rc = CIFSTCon(xid, pSesInfo, temp_unc, NULL, nls_codepage);
		cFYI(1,
		     ("CIFS Tcon rc = %d ipc_tid = %d", rc,pSesInfo->ipc_tid));
		kfree(temp_unc);
	}
	if (rc == 0)
		rc = CIFSGetDFSRefer(xid, pSesInfo, old_path, preferrals,
				     pnum_referrals, nls_codepage);

	return rc;
}

int setup_session(unsigned int xid, struct cifsSesInfo *pSesInfo, struct nls_table * nls_info)
{
	int rc = 0;
	char ntlm_session_key[CIFS_SESSION_KEY_SIZE];
	int ntlmv2_flag = FALSE;

    /* what if server changes its buffer size after dropping the session? */
	if(pSesInfo->server->maxBuf == 0) /* no need to send on reconnect */
		rc = CIFSSMBNegotiate(xid, pSesInfo);
	pSesInfo->capabilities = pSesInfo->server->capabilities;
	pSesInfo->sequence_number = 0;
	if (!rc) {
		cFYI(1,("Security Mode: 0x%x Capabilities: 0x%x Time Zone: %d",
			pSesInfo->server->secMode,
			pSesInfo->server->capabilities,
			pSesInfo->server->timeZone));
		if (extended_security
				&& (pSesInfo->capabilities & CAP_EXTENDED_SECURITY)
				&& (pSesInfo->server->secType == NTLMSSP)) {
			cFYI(1, ("New style sesssetup "));
			rc = CIFSSpnegoSessSetup(xid, pSesInfo,
				NULL /* security blob */, 
				0 /* blob length */,
				nls_info);
		} else if (extended_security
			   && (pSesInfo->capabilities & CAP_EXTENDED_SECURITY)
			   && (pSesInfo->server->secType == RawNTLMSSP)) {
			cFYI(1, ("NTLMSSP sesssetup "));
			rc = CIFSNTLMSSPNegotiateSessSetup(xid,
						pSesInfo,
						&ntlmv2_flag,
						nls_info);
			if (!rc) {
				if(ntlmv2_flag) {
					char * v2_response;
					cFYI(1,("Can use more secure NTLM version 2 password hash"));
					CalcNTLMv2_partial_mac_key(pSesInfo, 
						nls_info);
					v2_response = kmalloc(16 + 64 /* blob */, GFP_KERNEL);
					if(v2_response) {
						CalcNTLMv2_response(pSesInfo,v2_response);
/*						cifs_calculate_ntlmv2_mac_key(pSesInfo->mac_signing_key, response, ntlm_session_key, */
						kfree(v2_response);
					/* BB Put dummy sig in SessSetup PDU? */
					} else
						rc = -ENOMEM;

				} else {
					SMBNTencrypt(pSesInfo->password_with_pad,
						pSesInfo->server->cryptKey,
						ntlm_session_key);

					cifs_calculate_mac_key(pSesInfo->mac_signing_key,
						ntlm_session_key,
						pSesInfo->password_with_pad);
				}
			/* for better security the weaker lanman hash not sent
			   in AuthSessSetup so we no longer calculate it */

				rc = CIFSNTLMSSPAuthSessSetup(xid,
					pSesInfo,
					ntlm_session_key,
					ntlmv2_flag,
					nls_info);
			}
		} else { /* old style NTLM 0.12 session setup */
			SMBNTencrypt(pSesInfo->password_with_pad,
				pSesInfo->server->cryptKey,
				ntlm_session_key);

			cifs_calculate_mac_key(pSesInfo->mac_signing_key, 
				ntlm_session_key, pSesInfo->password_with_pad);
			rc = CIFSSessSetup(xid, pSesInfo,
				ntlm_session_key, nls_info);
		}
		if (rc) {
			cERROR(1,("Send error in SessSetup = %d",rc));
		} else {
			cFYI(1,("CIFS Session Established successfully"));
			pSesInfo->status = CifsGood;
		}
	}
	return rc;
}

int
ipv4_connect(struct sockaddr_in *psin_server, struct socket **csocket)
{
	int rc = 0;

	if(*csocket == NULL) {
		rc = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, csocket);
		if (rc < 0) {
			cERROR(1, ("Error %d creating socket",rc));
			*csocket = NULL;
			return rc;
		} else {
		/* BB other socket options to set KEEPALIVE, NODELAY? */
			cFYI(1,("Socket created"));
		}
	}

	psin_server->sin_family = AF_INET;
	if(psin_server->sin_port) { /* user overrode default port */
		rc = (*csocket)->ops->connect(*csocket,
				(struct sockaddr *) psin_server,
				sizeof (struct sockaddr_in),0);
		if (rc >= 0) {
			return rc;
		}
	}

	/* do not retry on the same port we just failed on */
	if(psin_server->sin_port != htons(CIFS_PORT)) {
		psin_server->sin_port = htons(CIFS_PORT);

		rc = (*csocket)->ops->connect(*csocket,
					(struct sockaddr *) psin_server,
					sizeof (struct sockaddr_in),0);
	}
	if (rc < 0) {
		psin_server->sin_port = htons(RFC1001_PORT);
		rc = (*csocket)->ops->connect(*csocket, (struct sockaddr *)
					      psin_server, sizeof (struct sockaddr_in),0);
		if (rc < 0) {
			cFYI(1, ("Error connecting to socket. %d", rc));
			sock_release(*csocket);
			*csocket = NULL;
			return rc;
		}
	}

	/* Eventually check for other socket options to change from 
		the default. sock_setsockopt not used because it expects 
		user space buffer */
	(*csocket)->sk->sk_rcvtimeo = 8 * HZ;
		
	return rc;
}

int
ipv6_connect(struct sockaddr_in6 *psin_server, struct socket **csocket)
{
	int rc = 0;

	rc = sock_create(PF_INET6, SOCK_STREAM,
			 IPPROTO_TCP /* IPPROTO_IPV6 ? */ , csocket);
	if (rc < 0) {
		cERROR(1, ("Error creating socket. Aborting operation"));
		return rc;
	}

	psin_server->sin6_family = AF_INET6;
	if(psin_server->sin6_port) { /* user overrode default port */
		rc = (*csocket)->ops->connect(*csocket,
				(struct sockaddr *) psin_server,
				sizeof (struct sockaddr_in6),0);
		if (rc >= 0) {
                /* BB other socket options to set KEEPALIVE, timeouts? NODELAY? */
			return rc;
		}
	}

	/* do not retry on the same port we just failed on */
	if(psin_server->sin6_port != htons(CIFS_PORT)) {
		psin_server->sin6_port = htons(CIFS_PORT);

		rc = (*csocket)->ops->connect(*csocket,
				(struct sockaddr *) psin_server,
				sizeof (struct sockaddr_in6), 0);
/* BB fix the timeout to be shorter above - and check flags */
	}
	if (rc < 0) {
		psin_server->sin6_port = htons(RFC1001_PORT);
		rc = (*csocket)->ops->connect(*csocket, (struct sockaddr *)
					      psin_server,
					      sizeof (struct sockaddr_in6), 0);
		if (rc < 0) {
			cFYI(1,
			     ("Error connecting to socket (via ipv6). %d",
			      rc));
			sock_release(*csocket);
			*csocket = NULL;
			return rc;
		}
	}

	return rc;
}

int
cifs_mount(struct super_block *sb, struct cifs_sb_info *cifs_sb,
	   char *mount_data, const char *devname)
{
	int rc = 0;
	int xid;
	struct socket *csocket = NULL;
	struct sockaddr_in sin_server;
/*	struct sockaddr_in6 sin_server6; */
	struct smb_vol volume_info;
	struct cifsSesInfo *pSesInfo = NULL;
	struct cifsSesInfo *existingCifsSes = NULL;
	struct cifsTconInfo *tcon = NULL;
	struct TCP_Server_Info *srvTcp = NULL;

	xid = GetXid();
	cFYI(1, ("Entering cifs_mount. Xid: %d with: %s", xid, mount_data));

	if (parse_mount_options(mount_data, devname, &volume_info)) {
		if(volume_info.UNC)
			kfree(volume_info.UNC);
		FreeXid(xid);
		return -EINVAL;
	}

	if (volume_info.username) {
		cFYI(1, ("Username: %s ", volume_info.username));

	} else {
		cifserror("No username specified ");
        /* In userspace mount helper we can get user name from alternate
           locations such as env variables and files on disk */
		FreeXid(xid);
		return -EINVAL;
	}

	if (volume_info.UNC) {
		sin_server.sin_addr.s_addr = inet_addr(volume_info.UNCip);
		cFYI(1, ("UNC: %s  ", volume_info.UNC));
	} else {
		/* BB we could connect to the DFS root? but which server do we ask? */
		cERROR(1,
		       ("CIFS mount error: No UNC path (e.g. -o unc=//192.168.1.100/public) specified  "));
		FreeXid(xid);
		return -EINVAL;
	}

	/* this is needed for ASCII cp to Unicode converts */
	if(volume_info.iocharset == NULL) {
		cifs_sb->local_nls = load_nls_default();
	/* load_nls_default can not return null */
	} else {
		cifs_sb->local_nls = load_nls(volume_info.iocharset);
		if(cifs_sb->local_nls == NULL) {
			cERROR(1,("CIFS mount error: iocharset %s not found",volume_info.iocharset));
			FreeXid(xid);
			return -ELIBACC;
		}
	}

	existingCifsSes =
	    find_tcp_session(sin_server.sin_addr.s_addr,
			     volume_info.username, &srvTcp);
	if (srvTcp) {
		cFYI(1, ("Existing tcp session with server found "));                
	} else {	/* create socket */
		if(volume_info.port)
			sin_server.sin_port = htons(volume_info.port);
		rc = ipv4_connect(&sin_server, &csocket);
		if (rc < 0) {
			cERROR(1,
			       ("Error connecting to IPv4 socket. Aborting operation"));
			if(csocket != NULL)
				sock_release(csocket);
			if(volume_info.UNC)
				kfree(volume_info.UNC);
			FreeXid(xid);
			return rc;
		}

		srvTcp = kmalloc(sizeof (struct TCP_Server_Info), GFP_KERNEL);
		if (srvTcp == NULL) {
			rc = -ENOMEM;
			sock_release(csocket);
			if(volume_info.UNC)
				kfree(volume_info.UNC);
			FreeXid(xid);
			return rc;
		} else {
			memset(srvTcp, 0, sizeof (struct TCP_Server_Info));
			memcpy(&srvTcp->sockAddr, &sin_server, sizeof (struct sockaddr_in));	
            /* BB Add code for ipv6 case too */
			srvTcp->ssocket = csocket;
			init_waitqueue_head(&srvTcp->response_q);
			INIT_LIST_HEAD(&srvTcp->pending_mid_q);
			srvTcp->tcpStatus = CifsGood;
			kernel_thread((void *)(void *)cifs_demultiplex_thread, srvTcp,
				      CLONE_FS | CLONE_FILES | CLONE_VM);
		}
	}

	if (existingCifsSes) {
		pSesInfo = existingCifsSes;
		cFYI(1, ("Existing smb sess found "));
	} else if (!rc) {
		cFYI(1, ("Existing smb sess not found "));
		pSesInfo = sesInfoAlloc();
		if (pSesInfo == NULL)
			rc = -ENOMEM;
		else {
			pSesInfo->server = srvTcp;
			sprintf(pSesInfo->serverName, "%u.%u.%u.%u",
				NIPQUAD(sin_server.sin_addr.s_addr));
		}

		if (!rc){   
			if (volume_info.password)
				strncpy(pSesInfo->password_with_pad,
					volume_info.password,CIFS_ENCPWD_SIZE);
			if (volume_info.username)
				strncpy(pSesInfo->userName,
					volume_info.username,MAX_USERNAME_SIZE);
			if (volume_info.domainname)
				strncpy(pSesInfo->domainName,
					volume_info.domainname,MAX_USERNAME_SIZE);
			pSesInfo->linux_uid = volume_info.linux_uid;

			rc = setup_session(xid,pSesInfo, cifs_sb->local_nls);
			if(!rc)
				atomic_inc(&srvTcp->socketUseCount);
		}
	}
    
	/* search for existing tcon to this server share */
	if (!rc) {
		if((volume_info.rsize) && (volume_info.rsize + MAX_CIFS_HDR_SIZE < srvTcp->maxBuf))
			cifs_sb->rsize = volume_info.rsize;
		else
			cifs_sb->rsize = srvTcp->maxBuf - MAX_CIFS_HDR_SIZE; /* default */
		if((volume_info.wsize) && (volume_info.wsize + MAX_CIFS_HDR_SIZE < srvTcp->maxBuf))
			cifs_sb->wsize = volume_info.wsize;
		else
			cifs_sb->wsize = srvTcp->maxBuf - MAX_CIFS_HDR_SIZE; /* default */
		if(cifs_sb->rsize < PAGE_CACHE_SIZE) {
			cifs_sb->rsize = PAGE_CACHE_SIZE;
			cERROR(1,("Attempt to set readsize for mount to less than one page (4096)"));
		}
		cifs_sb->mnt_uid = volume_info.linux_uid;
		cifs_sb->mnt_gid = volume_info.linux_gid;
		cifs_sb->mnt_file_mode = volume_info.file_mode;
		cifs_sb->mnt_dir_mode = volume_info.dir_mode;
		cFYI(1,("file mode: 0x%x  dir mode: 0x%x",cifs_sb->mnt_file_mode,cifs_sb->mnt_dir_mode));
		tcon =
		    find_unc(sin_server.sin_addr.s_addr, volume_info.UNC,
			     volume_info.username);
		if (tcon) {
			cFYI(1, ("Found match on UNC path "));
		} else {
			tcon = tconInfoAlloc();
			if (tcon == NULL)
				rc = -ENOMEM;
			else {
				/* check for null share name ie connect to dfs root */

				/* BB check if this works for exactly length three strings */
				if ((strchr(volume_info.UNC + 3, '\\') == NULL)
				    && (strchr(volume_info.UNC + 3, '/') ==
					NULL)) {
					rc = connect_to_dfs_path(xid,
								 pSesInfo,
								 "",
								 cifs_sb->
								 local_nls);
					if(volume_info.UNC)
						kfree(volume_info.UNC);
					FreeXid(xid);
					return -ENODEV;
				} else {
					rc = CIFSTCon(xid, pSesInfo, 
						volume_info.UNC,
						tcon, cifs_sb->local_nls);
					cFYI(1, ("CIFS Tcon rc = %d", rc));
				}
				if (!rc)
					atomic_inc(&pSesInfo->inUse);
			}
		}
	}
	if (pSesInfo->capabilities & CAP_LARGE_FILES) {
		cFYI(0, ("Large files supported "));
		sb->s_maxbytes = (u64) 1 << 63;
	} else
		sb->s_maxbytes = (u64) 1 << 31;	/* 2 GB */

/* on error free sesinfo and tcon struct if needed */
	if (rc) {
		if(atomic_read(&srvTcp->socketUseCount) == 0)
                	srvTcp->tcpStatus = CifsExiting;
		           /* If find_unc succeeded then rc == 0 so we can not end */
		if (tcon)  /* up here accidently freeing someone elses tcon struct */
			tconInfoFree(tcon);
		if (existingCifsSes == 0) {
			if (pSesInfo) {
				if (pSesInfo->server) {
					if (pSesInfo->Suid)
						CIFSSMBLogoff(xid, pSesInfo);
					if(pSesInfo->server->tsk)
						send_sig(SIGKILL,pSesInfo->server->tsk,1);
					else
						cFYI(1,("Can not wake captive thread on cleanup of failed mount"));
					set_current_state(TASK_INTERRUPTIBLE);
					schedule_timeout(HZ / 4);	/* give captive thread time to exit */
				} else
					cFYI(1, ("No session or bad tcon"));
				sesInfoFree(pSesInfo);
				/* pSesInfo = NULL; */
			}
		}
	} else {
		atomic_inc(&tcon->useCount);
		cifs_sb->tcon = tcon;
		tcon->ses = pSesInfo;

		/* do not care if following two calls succeed - informational only */
		CIFSSMBQFSDeviceInfo(xid, tcon, cifs_sb->local_nls);
		CIFSSMBQFSAttributeInfo(xid, tcon, cifs_sb->local_nls);
		if (tcon->ses->capabilities & CAP_UNIX)
			CIFSSMBQFSUnixInfo(xid, tcon, cifs_sb->local_nls);
	}
	if(volume_info.UNC)
		kfree(volume_info.UNC);
	FreeXid(xid);
	return rc;
}

int
CIFSSessSetup(unsigned int xid, struct cifsSesInfo *ses,
	      char session_key[CIFS_SESSION_KEY_SIZE],
	      const struct nls_table *nls_codepage)
{
	struct smb_hdr *smb_buffer;
	struct smb_hdr *smb_buffer_response;
	SESSION_SETUP_ANDX *pSMB;
	SESSION_SETUP_ANDX *pSMBr;
	char *bcc_ptr;
	char *user = ses->userName;
	char *domain = ses->domainName;
	int rc = 0;
	int remaining_words = 0;
	int bytes_returned = 0;
	int len;

	cFYI(1, ("In sesssetup "));

	smb_buffer = buf_get();
	if (smb_buffer == 0) {
		return -ENOMEM;
	}
	smb_buffer_response = smb_buffer;
	pSMBr = pSMB = (SESSION_SETUP_ANDX *) smb_buffer;

	/* send SMBsessionSetup here */
	header_assemble(smb_buffer, SMB_COM_SESSION_SETUP_ANDX,
			0 /* no tCon exists yet */ , 13 /* wct */ );

	pSMB->req_no_secext.AndXCommand = 0xFF;
	pSMB->req_no_secext.MaxBufferSize = cpu_to_le16(ses->server->maxBuf);
	pSMB->req_no_secext.MaxMpxCount = cpu_to_le16(ses->server->maxReq);

	if(ses->server->secMode & (SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
		smb_buffer->Flags2 |= SMBFLG2_SECURITY_SIGNATURE;

	pSMB->req_no_secext.Capabilities =
	    CAP_LARGE_FILES | CAP_NT_SMBS | CAP_LEVEL_II_OPLOCKS;
	if (ses->capabilities & CAP_UNICODE) {
		smb_buffer->Flags2 |= SMBFLG2_UNICODE;
		pSMB->req_no_secext.Capabilities |= CAP_UNICODE;
	}
	if (ses->capabilities & CAP_STATUS32) {
		smb_buffer->Flags2 |= SMBFLG2_ERR_STATUS;
		pSMB->req_no_secext.Capabilities |= CAP_STATUS32;
	}
	if (ses->capabilities & CAP_DFS) {
		smb_buffer->Flags2 |= SMBFLG2_DFS;
		pSMB->req_no_secext.Capabilities |= CAP_DFS;
	}
	pSMB->req_no_secext.Capabilities =
	    cpu_to_le32(pSMB->req_no_secext.Capabilities);
	/* pSMB->req_no_secext.CaseInsensitivePasswordLength =
	   CIFS_SESSION_KEY_SIZE; */
	pSMB->req_no_secext.CaseInsensitivePasswordLength = 0;
	pSMB->req_no_secext.CaseSensitivePasswordLength =
	    cpu_to_le16(CIFS_SESSION_KEY_SIZE);
	bcc_ptr = pByteArea(smb_buffer);
	/* memcpy(bcc_ptr, (char *) lm_session_key, CIFS_SESSION_KEY_SIZE);
	   bcc_ptr += CIFS_SESSION_KEY_SIZE; */
	memcpy(bcc_ptr, (char *) session_key, CIFS_SESSION_KEY_SIZE);
	bcc_ptr += CIFS_SESSION_KEY_SIZE;

	if (ses->capabilities & CAP_UNICODE) {
		if ((long) bcc_ptr % 2) {	/* must be word aligned for Unicode */
			*bcc_ptr = 0;
			bcc_ptr++;
		}
		if(user == NULL)
			bytes_returned = 0; /* skill null user */
        else
		bytes_returned =
		        cifs_strtoUCS((wchar_t *) bcc_ptr, user, 100, nls_codepage);
		bcc_ptr += 2 * bytes_returned;	/* convert num 16 bit words to bytes */
		bcc_ptr += 2;	/* trailing null */
		if (domain == NULL)
			bytes_returned =
			    cifs_strtoUCS((wchar_t *) bcc_ptr,
					  "CIFS_LINUX_DOM", 32, nls_codepage);
		else
			bytes_returned =
			    cifs_strtoUCS((wchar_t *) bcc_ptr, domain, 64,
					  nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;
		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, "Linux version ",
				  32, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, UTS_RELEASE, 32,
				  nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;
		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, CIFS_NETWORK_OPSYS,
				  64, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;
	} else {
		if(user != NULL) {                
		    strncpy(bcc_ptr, user, 200);
		    bcc_ptr += strnlen(user, 200);
		}
		*bcc_ptr = 0;
		bcc_ptr++;
		if (domain == NULL) {
			strcpy(bcc_ptr, "CIFS_LINUX_DOM");
			bcc_ptr += strlen("CIFS_LINUX_DOM") + 1;
		} else {
			strncpy(bcc_ptr, domain, 64);
			bcc_ptr += strnlen(domain, 64);
			*bcc_ptr = 0;
			bcc_ptr++;
		}
		strcpy(bcc_ptr, "Linux version ");
		bcc_ptr += strlen("Linux version ");
		strcpy(bcc_ptr, UTS_RELEASE);
		bcc_ptr += strlen(UTS_RELEASE) + 1;
		strcpy(bcc_ptr, CIFS_NETWORK_OPSYS);
		bcc_ptr += strlen(CIFS_NETWORK_OPSYS) + 1;
	}
	BCC(smb_buffer) = (long) bcc_ptr - (long) pByteArea(smb_buffer);
	smb_buffer->smb_buf_length += BCC(smb_buffer);
	BCC(smb_buffer) = cpu_to_le16(BCC(smb_buffer));

	rc = SendReceive(xid, ses, smb_buffer, smb_buffer_response,
			 &bytes_returned, 1);
	if (rc) {
/* rc = map_smb_to_linux_error(smb_buffer_response); now done in SendReceive */
	} else if ((smb_buffer_response->WordCount == 3)
		   || (smb_buffer_response->WordCount == 4)) {
		pSMBr->resp.Action = le16_to_cpu(pSMBr->resp.Action);
		if (pSMBr->resp.Action & GUEST_LOGIN)
			cFYI(1, (" Guest login"));	/* do we want to mark SesInfo struct ? */
		if (ses) {
			ses->Suid = smb_buffer_response->Uid;	/* UID left in wire format (le) */
			cFYI(1, ("UID = %d ", ses->Suid));
         /* response can have either 3 or 4 word count - Samba sends 3 */
			bcc_ptr = pByteArea(smb_buffer_response);	
			if ((pSMBr->resp.hdr.WordCount == 3)
			    || ((pSMBr->resp.hdr.WordCount == 4)
				&& (pSMBr->resp.SecurityBlobLength <
				    pSMBr->resp.ByteCount))) {
				if (pSMBr->resp.hdr.WordCount == 4)
					bcc_ptr +=
					    pSMBr->resp.SecurityBlobLength;

				if (smb_buffer->Flags2 &= SMBFLG2_UNICODE) {
					if ((long) (bcc_ptr) % 2) {
						remaining_words =
						    (BCC(smb_buffer_response)
						     - 1) / 2;
						bcc_ptr++;	/* Unicode strings must be word aligned */
					} else {
						remaining_words =
						    BCC
						    (smb_buffer_response) / 2;
					}
					len =
					    UniStrnlen((wchar_t *) bcc_ptr,
						       remaining_words - 1);
/* We look for obvious messed up bcc or strings in response so we do not go off
   the end since (at least) WIN2K and Windows XP have a major bug in not null
   terminating last Unicode string in response  */
					ses->serverOS = kcalloc(2 * (len + 1), GFP_KERNEL);
					cifs_strfromUCS_le(ses->serverOS,
							   (wchar_t *)bcc_ptr, len,nls_codepage);
					bcc_ptr += 2 * (len + 1);
					remaining_words -= len + 1;
					ses->serverOS[2 * len] = 0;
					ses->serverOS[1 + (2 * len)] = 0;
					if (remaining_words > 0) {
						len = UniStrnlen((wchar_t *)bcc_ptr,
								 remaining_words
								 - 1);
						ses->serverNOS =kcalloc(2 * (len + 1),GFP_KERNEL);
						cifs_strfromUCS_le(ses->serverNOS,
								   (wchar_t *)bcc_ptr,len,nls_codepage);
						bcc_ptr += 2 * (len + 1);
						ses->serverNOS[2 * len] = 0;
						ses->serverNOS[1 + (2 * len)] = 0;
						remaining_words -= len + 1;
						if (remaining_words > 0) {
							len = UniStrnlen((wchar_t *) bcc_ptr, remaining_words);	
          /* last string is not always null terminated (for e.g. for Windows XP & 2000) */
							ses->serverDomain =
							    kcalloc(2*(len+1),GFP_KERNEL);
							cifs_strfromUCS_le(ses->serverDomain,
							     (wchar_t *)bcc_ptr,len,nls_codepage);
							bcc_ptr += 2 * (len + 1);
							ses->serverDomain[2*len] = 0;
							ses->serverDomain[1+(2*len)] = 0;
						} /* else no more room so create dummy domain string */
						else
							ses->serverDomain =
							    kcalloc(2,
								    GFP_KERNEL);
					} else {	/* no room so create dummy domain and NOS string */
						ses->serverDomain =
						    kcalloc(2, GFP_KERNEL);
						ses->serverNOS =
						    kcalloc(2, GFP_KERNEL);
					}
				} else {	/* ASCII */
					len = strnlen(bcc_ptr, 1024);
					if (((long) bcc_ptr + len) - (long)
					    pByteArea(smb_buffer_response)
					    <= BCC(smb_buffer_response)) {
						ses->serverOS = kcalloc(len + 1,GFP_KERNEL);
						strncpy(ses->serverOS,bcc_ptr, len);

						bcc_ptr += len;
						bcc_ptr[0] = 0;	/* null terminate the string */
						bcc_ptr++;

						len = strnlen(bcc_ptr, 1024);
						ses->serverNOS = kcalloc(len + 1,GFP_KERNEL);
						strncpy(ses->serverNOS, bcc_ptr, len);
						bcc_ptr += len;
						bcc_ptr[0] = 0;
						bcc_ptr++;

						len = strnlen(bcc_ptr, 1024);
						ses->serverDomain = kcalloc(len + 1,GFP_KERNEL);
						strncpy(ses->serverDomain, bcc_ptr, len);
						bcc_ptr += len;
						bcc_ptr[0] = 0;
						bcc_ptr++;
					} else
						cFYI(1,
						     ("Variable field of length %d extends beyond end of smb ",
						      len));
				}
			} else {
				cERROR(1,
				       (" Security Blob Length extends beyond end of SMB"));
			}
		} else {
			cERROR(1, ("No session structure passed in."));
		}
	} else {
		cERROR(1,
		       (" Invalid Word count %d: ",
			smb_buffer_response->WordCount));
		rc = -EIO;
	}
	
	if (smb_buffer)
		buf_release(smb_buffer);

	return rc;
}

int
CIFSSpnegoSessSetup(unsigned int xid, struct cifsSesInfo *ses,
		char *SecurityBlob,int SecurityBlobLength,
		const struct nls_table *nls_codepage)
{
	struct smb_hdr *smb_buffer;
	struct smb_hdr *smb_buffer_response;
	SESSION_SETUP_ANDX *pSMB;
	SESSION_SETUP_ANDX *pSMBr;
	char *bcc_ptr;
	char *user = ses->userName;
	char *domain = ses->domainName;
	int rc = 0;
	int remaining_words = 0;
	int bytes_returned = 0;
	int len;

	cFYI(1, ("In spnego sesssetup "));

	smb_buffer = buf_get();
	if (smb_buffer == 0) {
		return -ENOMEM;
	}
	smb_buffer_response = smb_buffer;
	pSMBr = pSMB = (SESSION_SETUP_ANDX *) smb_buffer;

	/* send SMBsessionSetup here */
	header_assemble(smb_buffer, SMB_COM_SESSION_SETUP_ANDX,
			0 /* no tCon exists yet */ , 12 /* wct */ );
	pSMB->req.hdr.Flags2 |= SMBFLG2_EXT_SEC;
	pSMB->req.AndXCommand = 0xFF;
	pSMB->req.MaxBufferSize = cpu_to_le16(ses->server->maxBuf);
	pSMB->req.MaxMpxCount = cpu_to_le16(ses->server->maxReq);

	if(ses->server->secMode & (SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
		smb_buffer->Flags2 |= SMBFLG2_SECURITY_SIGNATURE;

	pSMB->req.Capabilities =
	    CAP_LARGE_FILES | CAP_NT_SMBS | CAP_LEVEL_II_OPLOCKS |
	    CAP_EXTENDED_SECURITY;
	if (ses->capabilities & CAP_UNICODE) {
		smb_buffer->Flags2 |= SMBFLG2_UNICODE;
		pSMB->req.Capabilities |= CAP_UNICODE;
	}
	if (ses->capabilities & CAP_STATUS32) {
		smb_buffer->Flags2 |= SMBFLG2_ERR_STATUS;
		pSMB->req.Capabilities |= CAP_STATUS32;
	}
	if (ses->capabilities & CAP_DFS) {
		smb_buffer->Flags2 |= SMBFLG2_DFS;
		pSMB->req.Capabilities |= CAP_DFS;
	}
	pSMB->req.Capabilities = cpu_to_le32(pSMB->req.Capabilities);

	pSMB->req.SecurityBlobLength = cpu_to_le16(SecurityBlobLength);
	bcc_ptr = pByteArea(smb_buffer);
	memcpy(bcc_ptr, SecurityBlob, SecurityBlobLength);
	bcc_ptr += SecurityBlobLength;

	if (ses->capabilities & CAP_UNICODE) {
		if ((long) bcc_ptr % 2) {	/* must be word aligned for Unicode strings */
			*bcc_ptr = 0;
			bcc_ptr++;
		}
		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, user, 100, nls_codepage);
		bcc_ptr += 2 * bytes_returned;	/* convert num of 16 bit words to bytes */
		bcc_ptr += 2;	/* trailing null */
		if (domain == NULL)
			bytes_returned =
			    cifs_strtoUCS((wchar_t *) bcc_ptr,
					  "CIFS_LINUX_DOM", 32, nls_codepage);
		else
			bytes_returned =
			    cifs_strtoUCS((wchar_t *) bcc_ptr, domain, 64,
					  nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;
		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, "Linux version ",
				  32, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, UTS_RELEASE, 32,
				  nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;
		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, CIFS_NETWORK_OPSYS,
				  64, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;
	} else {
		strncpy(bcc_ptr, user, 200);
		bcc_ptr += strnlen(user, 200);
		*bcc_ptr = 0;
		bcc_ptr++;
		if (domain == NULL) {
			strcpy(bcc_ptr, "CIFS_LINUX_DOM");
			bcc_ptr += strlen("CIFS_LINUX_DOM") + 1;
		} else {
			strncpy(bcc_ptr, domain, 64);
			bcc_ptr += strnlen(domain, 64);
			*bcc_ptr = 0;
			bcc_ptr++;
		}
		strcpy(bcc_ptr, "Linux version ");
		bcc_ptr += strlen("Linux version ");
		strcpy(bcc_ptr, UTS_RELEASE);
		bcc_ptr += strlen(UTS_RELEASE) + 1;
		strcpy(bcc_ptr, CIFS_NETWORK_OPSYS);
		bcc_ptr += strlen(CIFS_NETWORK_OPSYS) + 1;
	}
	BCC(smb_buffer) = (long) bcc_ptr - (long) pByteArea(smb_buffer);
	smb_buffer->smb_buf_length += BCC(smb_buffer);
	BCC(smb_buffer) = cpu_to_le16(BCC(smb_buffer));

	rc = SendReceive(xid, ses, smb_buffer, smb_buffer_response,
			 &bytes_returned, 1);
	if (rc) {
/*    rc = map_smb_to_linux_error(smb_buffer_response);  *//* done in SendReceive now */
	} else if ((smb_buffer_response->WordCount == 3)
		   || (smb_buffer_response->WordCount == 4)) {
		pSMBr->resp.Action = le16_to_cpu(pSMBr->resp.Action);
		pSMBr->resp.SecurityBlobLength =
		    le16_to_cpu(pSMBr->resp.SecurityBlobLength);
		if (pSMBr->resp.Action & GUEST_LOGIN)
			cFYI(1, (" Guest login"));	/* BB do we want to set anything in SesInfo struct ? */
		if (ses) {
			ses->Suid = smb_buffer_response->Uid;	/* UID left in wire format (le) */
			cFYI(1, ("UID = %d ", ses->Suid));
			bcc_ptr = pByteArea(smb_buffer_response);	/* response can have either 3 or 4 word count - Samba sends 3 */

			/* BB Fix below to make endian neutral !! */

			if ((pSMBr->resp.hdr.WordCount == 3)
			    || ((pSMBr->resp.hdr.WordCount == 4)
				&& (pSMBr->resp.SecurityBlobLength <
				    pSMBr->resp.ByteCount))) {
				if (pSMBr->resp.hdr.WordCount == 4) {
					bcc_ptr +=
					    pSMBr->resp.SecurityBlobLength;
					cFYI(1,
					     ("Security Blob Length %d ",
					      pSMBr->resp.SecurityBlobLength));
				}

				if (smb_buffer->Flags2 &= SMBFLG2_UNICODE) {
					if ((long) (bcc_ptr) % 2) {
						remaining_words =
						    (BCC(smb_buffer_response)
						     - 1) / 2;
						bcc_ptr++;	/* Unicode strings must be word aligned */
					} else {
						remaining_words =
						    BCC
						    (smb_buffer_response) / 2;
					}
					len =
					    UniStrnlen((wchar_t *) bcc_ptr,
						       remaining_words - 1);
/* We look for obvious messed up bcc or strings in response so we do not go off
   the end since (at least) WIN2K and Windows XP have a major bug in not null
   terminating last Unicode string in response  */
					ses->serverOS =
					    kcalloc(2 * (len + 1), GFP_KERNEL);
					cifs_strfromUCS_le(ses->serverOS,
							   (wchar_t *)
							   bcc_ptr, len,
							   nls_codepage);
					bcc_ptr += 2 * (len + 1);
					remaining_words -= len + 1;
					ses->serverOS[2 * len] = 0;
					ses->serverOS[1 + (2 * len)] = 0;
					if (remaining_words > 0) {
						len = UniStrnlen((wchar_t *)bcc_ptr,
								 remaining_words
								 - 1);
						ses->serverNOS =
						    kcalloc(2 * (len + 1),
							    GFP_KERNEL);
						cifs_strfromUCS_le(ses->serverNOS,
								   (wchar_t *)bcc_ptr,
								   len,
								   nls_codepage);
						bcc_ptr += 2 * (len + 1);
						ses->serverNOS[2 * len] = 0;
						ses->serverNOS[1 + (2 * len)] = 0;
						remaining_words -= len + 1;
						if (remaining_words > 0) {
							len = UniStrnlen((wchar_t *) bcc_ptr, remaining_words);	
                            /* last string is not always null terminated (for e.g. for Windows XP & 2000) */
							ses->serverDomain = kcalloc(2*(len+1),GFP_KERNEL);
							cifs_strfromUCS_le(ses->serverDomain,
							     (wchar_t *)bcc_ptr, 
                                 len,
							     nls_codepage);
							bcc_ptr += 2*(len+1);
							ses->serverDomain[2*len] = 0;
							ses->serverDomain[1+(2*len)] = 0;
						} /* else no more room so create dummy domain string */
						else
							ses->serverDomain =
							    kcalloc(2,GFP_KERNEL);
					} else {	/* no room so create dummy domain and NOS string */
						ses->serverDomain = kcalloc(2, GFP_KERNEL);
						ses->serverNOS = kcalloc(2, GFP_KERNEL);
					}
				} else {	/* ASCII */

					len = strnlen(bcc_ptr, 1024);
					if (((long) bcc_ptr + len) - (long)
					    pByteArea(smb_buffer_response)
					    <= BCC(smb_buffer_response)) {
						ses->serverOS = kcalloc(len + 1, GFP_KERNEL);
						strncpy(ses->serverOS, bcc_ptr, len);

						bcc_ptr += len;
						bcc_ptr[0] = 0;	/* null terminate the string */
						bcc_ptr++;

						len = strnlen(bcc_ptr, 1024);
						ses->serverNOS = kcalloc(len + 1,GFP_KERNEL);
						strncpy(ses->serverNOS, bcc_ptr, len);
						bcc_ptr += len;
						bcc_ptr[0] = 0;
						bcc_ptr++;

						len = strnlen(bcc_ptr, 1024);
						ses->serverDomain = kcalloc(len + 1, GFP_KERNEL);
						strncpy(ses->serverDomain, bcc_ptr, len);
						bcc_ptr += len;
						bcc_ptr[0] = 0;
						bcc_ptr++;
					} else
						cFYI(1,
						     ("Variable field of length %d extends beyond end of smb ",
						      len));
				}
			} else {
				cERROR(1,
				       (" Security Blob Length extends beyond end of SMB"));
			}
		} else {
			cERROR(1, ("No session structure passed in."));
		}
	} else {
		cERROR(1,
		       (" Invalid Word count %d: ",
			smb_buffer_response->WordCount));
		rc = -EIO;
	}

	if (smb_buffer)
		buf_release(smb_buffer);

	return rc;
}

int
CIFSNTLMSSPNegotiateSessSetup(unsigned int xid,
			      struct cifsSesInfo *ses, int * pNTLMv2_flag,
			      const struct nls_table *nls_codepage)
{
	struct smb_hdr *smb_buffer;
	struct smb_hdr *smb_buffer_response;
	SESSION_SETUP_ANDX *pSMB;
	SESSION_SETUP_ANDX *pSMBr;
	char *bcc_ptr;
	char *domain = ses->domainName;
	int rc = 0;
	int remaining_words = 0;
	int bytes_returned = 0;
	int len;
	int SecurityBlobLength = sizeof (NEGOTIATE_MESSAGE);
	PNEGOTIATE_MESSAGE SecurityBlob;
	PCHALLENGE_MESSAGE SecurityBlob2;

	cFYI(1, ("In NTLMSSP sesssetup (negotiate) "));
	*pNTLMv2_flag = FALSE;
	smb_buffer = buf_get();
	if (smb_buffer == 0) {
		return -ENOMEM;
	}
	smb_buffer_response = smb_buffer;
	pSMB = (SESSION_SETUP_ANDX *) smb_buffer;
	pSMBr = (SESSION_SETUP_ANDX *) smb_buffer_response;

	/* send SMBsessionSetup here */
	header_assemble(smb_buffer, SMB_COM_SESSION_SETUP_ANDX,
			0 /* no tCon exists yet */ , 12 /* wct */ );
	pSMB->req.hdr.Flags2 |= SMBFLG2_EXT_SEC;
	pSMB->req.hdr.Flags |= (SMBFLG_CASELESS | SMBFLG_CANONICAL_PATH_FORMAT);

	pSMB->req.AndXCommand = 0xFF;
	pSMB->req.MaxBufferSize = cpu_to_le16(ses->server->maxBuf);
	pSMB->req.MaxMpxCount = cpu_to_le16(ses->server->maxReq);

	if(ses->server->secMode & (SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
		smb_buffer->Flags2 |= SMBFLG2_SECURITY_SIGNATURE;

	pSMB->req.Capabilities =
	    CAP_LARGE_FILES | CAP_NT_SMBS | CAP_LEVEL_II_OPLOCKS |
	    CAP_EXTENDED_SECURITY;
	if (ses->capabilities & CAP_UNICODE) {
		smb_buffer->Flags2 |= SMBFLG2_UNICODE;
		pSMB->req.Capabilities |= CAP_UNICODE;
	}
	if (ses->capabilities & CAP_STATUS32) {
		smb_buffer->Flags2 |= SMBFLG2_ERR_STATUS;
		pSMB->req.Capabilities |= CAP_STATUS32;
	}
	if (ses->capabilities & CAP_DFS) {
		smb_buffer->Flags2 |= SMBFLG2_DFS;
		pSMB->req.Capabilities |= CAP_DFS;
	}
	pSMB->req.Capabilities = cpu_to_le32(pSMB->req.Capabilities);

	bcc_ptr = (char *) &pSMB->req.SecurityBlob;
	SecurityBlob = (PNEGOTIATE_MESSAGE) bcc_ptr;
	strncpy(SecurityBlob->Signature, NTLMSSP_SIGNATURE, 8);
	SecurityBlob->MessageType = NtLmNegotiate;
	SecurityBlob->NegotiateFlags =
	    NTLMSSP_NEGOTIATE_UNICODE | NTLMSSP_NEGOTIATE_OEM |
	    NTLMSSP_REQUEST_TARGET | NTLMSSP_NEGOTIATE_NTLM | 0x80000000 |
	    /* NTLMSSP_NEGOTIATE_ALWAYS_SIGN | */ NTLMSSP_NEGOTIATE_128;
	if(sign_CIFS_PDUs)
		SecurityBlob->NegotiateFlags |= NTLMSSP_NEGOTIATE_SIGN;
	if(ntlmv2_support)
		SecurityBlob->NegotiateFlags |= NTLMSSP_NEGOTIATE_NTLMV2;
	/* setup pointers to domain name and workstation name */
	bcc_ptr += SecurityBlobLength;

	SecurityBlob->WorkstationName.Buffer = 0;
	SecurityBlob->WorkstationName.Length = 0;
	SecurityBlob->WorkstationName.MaximumLength = 0;

	if (domain == NULL) {
		SecurityBlob->DomainName.Buffer = 0;
		SecurityBlob->DomainName.Length = 0;
		SecurityBlob->DomainName.MaximumLength = 0;
	} else {
		SecurityBlob->NegotiateFlags |=
		    NTLMSSP_NEGOTIATE_DOMAIN_SUPPLIED;
		strncpy(bcc_ptr, domain, 63);
		SecurityBlob->DomainName.Length = strnlen(domain, 64);
		SecurityBlob->DomainName.MaximumLength =
		    cpu_to_le16(SecurityBlob->DomainName.Length);
		SecurityBlob->DomainName.Buffer =
		    cpu_to_le32((long) &SecurityBlob->
				DomainString -
				(long) &SecurityBlob->Signature);
		bcc_ptr += SecurityBlob->DomainName.Length;
		SecurityBlobLength += SecurityBlob->DomainName.Length;
		SecurityBlob->DomainName.Length =
		    cpu_to_le16(SecurityBlob->DomainName.Length);
	}
	if (ses->capabilities & CAP_UNICODE) {
		if ((long) bcc_ptr % 2) {
			*bcc_ptr = 0;
			bcc_ptr++;
		}

		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, "Linux version ",
				  32, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, UTS_RELEASE, 32,
				  nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;	/* null terminate Linux version */
		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, CIFS_NETWORK_OPSYS,
				  64, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		*(bcc_ptr + 1) = 0;
		*(bcc_ptr + 2) = 0;
		bcc_ptr += 2;	/* null terminate network opsys string */
		*(bcc_ptr + 1) = 0;
		*(bcc_ptr + 2) = 0;
		bcc_ptr += 2;	/* null domain */
	} else {		/* ASCII */
		strcpy(bcc_ptr, "Linux version ");
		bcc_ptr += strlen("Linux version ");
		strcpy(bcc_ptr, UTS_RELEASE);
		bcc_ptr += strlen(UTS_RELEASE) + 1;
		strcpy(bcc_ptr, CIFS_NETWORK_OPSYS);
		bcc_ptr += strlen(CIFS_NETWORK_OPSYS) + 1;
		bcc_ptr++;	/* empty domain field */
		*bcc_ptr = 0;
	}
	SecurityBlob->NegotiateFlags =
	    cpu_to_le32(SecurityBlob->NegotiateFlags);
	pSMB->req.SecurityBlobLength = cpu_to_le16(SecurityBlobLength);
	BCC(smb_buffer) = (long) bcc_ptr - (long) pByteArea(smb_buffer);
	smb_buffer->smb_buf_length += BCC(smb_buffer);
	BCC(smb_buffer) = cpu_to_le16(BCC(smb_buffer));

	rc = SendReceive(xid, ses, smb_buffer, smb_buffer_response,
			 &bytes_returned, 1);

	if (smb_buffer_response->Status.CifsError ==
	    (NT_STATUS_MORE_PROCESSING_REQUIRED))
		rc = 0;

	if (rc) {
/*    rc = map_smb_to_linux_error(smb_buffer_response);  *//* done in SendReceive now */
	} else if ((smb_buffer_response->WordCount == 3)
		   || (smb_buffer_response->WordCount == 4)) {
		pSMBr->resp.Action = le16_to_cpu(pSMBr->resp.Action);
		pSMBr->resp.SecurityBlobLength =
		    le16_to_cpu(pSMBr->resp.SecurityBlobLength);
		if (pSMBr->resp.Action & GUEST_LOGIN)
			cFYI(1, (" Guest login"));	
        /* Do we want to set anything in SesInfo struct when guest login? */

		bcc_ptr = pByteArea(smb_buffer_response);	
        /* response can have either 3 or 4 word count - Samba sends 3 */

		SecurityBlob2 = (PCHALLENGE_MESSAGE) bcc_ptr;
		if (SecurityBlob2->MessageType != NtLmChallenge) {
			cFYI(1,
			     ("Unexpected NTLMSSP message type received %d",
			      SecurityBlob2->MessageType));
		} else if (ses) {
			ses->Suid = smb_buffer_response->Uid; /* UID left in le format */ 
			cFYI(1, ("UID = %d ", ses->Suid));
			if ((pSMBr->resp.hdr.WordCount == 3)
			    || ((pSMBr->resp.hdr.WordCount == 4)
				&& (pSMBr->resp.SecurityBlobLength <
				    pSMBr->resp.ByteCount))) {
				if (pSMBr->resp.hdr.WordCount == 4) {
					bcc_ptr +=
					    pSMBr->resp.SecurityBlobLength;
					cFYI(1,
					     ("Security Blob Length %d ",
					      pSMBr->resp.SecurityBlobLength));
				}

				cFYI(1, ("NTLMSSP Challenge rcvd "));

				memcpy(ses->server->cryptKey,
				       SecurityBlob2->Challenge,
				       CIFS_CRYPTO_KEY_SIZE);
				if(SecurityBlob2->NegotiateFlags & NTLMSSP_NEGOTIATE_NTLMV2)
					*pNTLMv2_flag = TRUE;

				if((SecurityBlob2->NegotiateFlags & 
					NTLMSSP_NEGOTIATE_ALWAYS_SIGN) 
					|| (sign_CIFS_PDUs > 1))
						ses->server->secMode |= 
							SECMODE_SIGN_REQUIRED;	
				if ((SecurityBlob2->NegotiateFlags & 
					NTLMSSP_NEGOTIATE_SIGN) && (sign_CIFS_PDUs))
						ses->server->secMode |= 
							SECMODE_SIGN_ENABLED;

				if (smb_buffer->Flags2 &= SMBFLG2_UNICODE) {
					if ((long) (bcc_ptr) % 2) {
						remaining_words =
						    (BCC(smb_buffer_response)
						     - 1) / 2;
						bcc_ptr++;	/* Unicode strings must be word aligned */
					} else {
						remaining_words =
						    BCC
						    (smb_buffer_response) / 2;
					}
					len =
					    UniStrnlen((wchar_t *) bcc_ptr,
						       remaining_words - 1);
/* We look for obvious messed up bcc or strings in response so we do not go off
   the end since (at least) WIN2K and Windows XP have a major bug in not null
   terminating last Unicode string in response  */
					ses->serverOS =
					    kcalloc(2 * (len + 1), GFP_KERNEL);
					cifs_strfromUCS_le(ses->serverOS,
							   (wchar_t *)
							   bcc_ptr, len,
							   nls_codepage);
					bcc_ptr += 2 * (len + 1);
					remaining_words -= len + 1;
					ses->serverOS[2 * len] = 0;
					ses->serverOS[1 + (2 * len)] = 0;
					if (remaining_words > 0) {
						len = UniStrnlen((wchar_t *)
								 bcc_ptr,
								 remaining_words
								 - 1);
						ses->serverNOS =
						    kcalloc(2 * (len + 1),
							    GFP_KERNEL);
						cifs_strfromUCS_le(ses->
								   serverNOS,
								   (wchar_t *)
								   bcc_ptr,
								   len,
								   nls_codepage);
						bcc_ptr += 2 * (len + 1);
						ses->serverNOS[2 * len] = 0;
						ses->serverNOS[1 +
							       (2 * len)] = 0;
						remaining_words -= len + 1;
						if (remaining_words > 0) {
							len = UniStrnlen((wchar_t *) bcc_ptr, remaining_words);	
           /* last string is not always null terminated (for e.g. for Windows XP & 2000) */
							ses->serverDomain =
							    kcalloc(2 *
								    (len +
								     1),
								    GFP_KERNEL);
							cifs_strfromUCS_le
							    (ses->
							     serverDomain,
							     (wchar_t *)
							     bcc_ptr, len,
							     nls_codepage);
							bcc_ptr +=
							    2 * (len + 1);
							ses->
							    serverDomain[2
									 * len]
							    = 0;
							ses->
							    serverDomain[1
									 +
									 (2
									  *
									  len)]
							    = 0;
						} /* else no more room so create dummy domain string */
						else
							ses->serverDomain =
							    kcalloc(2,
								    GFP_KERNEL);
					} else {	/* no room so create dummy domain and NOS string */
						ses->serverDomain =
						    kcalloc(2, GFP_KERNEL);
						ses->serverNOS =
						    kcalloc(2, GFP_KERNEL);
					}
				} else {	/* ASCII */
					len = strnlen(bcc_ptr, 1024);
					if (((long) bcc_ptr + len) - (long)
					    pByteArea(smb_buffer_response)
					    <= BCC(smb_buffer_response)) {
						ses->serverOS =
						    kcalloc(len + 1,
							    GFP_KERNEL);
						strncpy(ses->serverOS,
							bcc_ptr, len);

						bcc_ptr += len;
						bcc_ptr[0] = 0;	/* null terminate string */
						bcc_ptr++;

						len = strnlen(bcc_ptr, 1024);
						ses->serverNOS =
						    kcalloc(len + 1,
							    GFP_KERNEL);
						strncpy(ses->serverNOS, bcc_ptr, len);
						bcc_ptr += len;
						bcc_ptr[0] = 0;
						bcc_ptr++;

						len = strnlen(bcc_ptr, 1024);
						ses->serverDomain =
						    kcalloc(len + 1,
							    GFP_KERNEL);
						strncpy(ses->serverDomain, bcc_ptr, len);	
						bcc_ptr += len;
						bcc_ptr[0] = 0;
						bcc_ptr++;
					} else
						cFYI(1,
						     ("Variable field of length %d extends beyond end of smb ",
						      len));
				}
			} else {
				cERROR(1,
				       (" Security Blob Length extends beyond end of SMB"));
			}
		} else {
			cERROR(1, ("No session structure passed in."));
		}
	} else {
		cERROR(1,
		       (" Invalid Word count %d: ",
			smb_buffer_response->WordCount));
		rc = -EIO;
	}

	if (smb_buffer)
		buf_release(smb_buffer);

	return rc;
}

int
CIFSNTLMSSPAuthSessSetup(unsigned int xid, struct cifsSesInfo *ses,
		char *ntlm_session_key, int ntlmv2_flag,
		const struct nls_table *nls_codepage)
{
	struct smb_hdr *smb_buffer;
	struct smb_hdr *smb_buffer_response;
	SESSION_SETUP_ANDX *pSMB;
	SESSION_SETUP_ANDX *pSMBr;
	char *bcc_ptr;
	char *user = ses->userName;
	char *domain = ses->domainName;
	int rc = 0;
	int remaining_words = 0;
	int bytes_returned = 0;
	int len;
	int SecurityBlobLength = sizeof (AUTHENTICATE_MESSAGE);
	PAUTHENTICATE_MESSAGE SecurityBlob;

	cFYI(1, ("In NTLMSSPSessSetup (Authenticate)"));

	smb_buffer = buf_get();
	if (smb_buffer == 0) {
		return -ENOMEM;
	}
	smb_buffer_response = smb_buffer;
	pSMB = (SESSION_SETUP_ANDX *) smb_buffer;
	pSMBr = (SESSION_SETUP_ANDX *) smb_buffer_response;

	/* send SMBsessionSetup here */
	header_assemble(smb_buffer, SMB_COM_SESSION_SETUP_ANDX,
			0 /* no tCon exists yet */ , 12 /* wct */ );
	pSMB->req.hdr.Flags |= (SMBFLG_CASELESS | SMBFLG_CANONICAL_PATH_FORMAT);
	pSMB->req.hdr.Flags2 |= SMBFLG2_EXT_SEC;
	pSMB->req.AndXCommand = 0xFF;
	pSMB->req.MaxBufferSize = cpu_to_le16(ses->server->maxBuf);
	pSMB->req.MaxMpxCount = cpu_to_le16(ses->server->maxReq);

	pSMB->req.hdr.Uid = ses->Suid;

	if(ses->server->secMode & (SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
		smb_buffer->Flags2 |= SMBFLG2_SECURITY_SIGNATURE;

	pSMB->req.Capabilities =
	    CAP_LARGE_FILES | CAP_NT_SMBS | CAP_LEVEL_II_OPLOCKS |
	    CAP_EXTENDED_SECURITY;
	if (ses->capabilities & CAP_UNICODE) {
		smb_buffer->Flags2 |= SMBFLG2_UNICODE;
		pSMB->req.Capabilities |= CAP_UNICODE;
	}
	if (ses->capabilities & CAP_STATUS32) {
		smb_buffer->Flags2 |= SMBFLG2_ERR_STATUS;
		pSMB->req.Capabilities |= CAP_STATUS32;
	}
	if (ses->capabilities & CAP_DFS) {
		smb_buffer->Flags2 |= SMBFLG2_DFS;
		pSMB->req.Capabilities |= CAP_DFS;
	}
	pSMB->req.Capabilities = cpu_to_le32(pSMB->req.Capabilities);

	bcc_ptr = (char *) &pSMB->req.SecurityBlob;
	SecurityBlob = (PAUTHENTICATE_MESSAGE) bcc_ptr;
	strncpy(SecurityBlob->Signature, NTLMSSP_SIGNATURE, 8);
	SecurityBlob->MessageType = NtLmAuthenticate;
	bcc_ptr += SecurityBlobLength;
	SecurityBlob->NegotiateFlags =
	    NTLMSSP_NEGOTIATE_UNICODE | NTLMSSP_REQUEST_TARGET |
	    NTLMSSP_NEGOTIATE_NTLM | NTLMSSP_NEGOTIATE_TARGET_INFO |
	    0x80000000 | NTLMSSP_NEGOTIATE_128;
	if(sign_CIFS_PDUs)
		SecurityBlob->NegotiateFlags |= /* NTLMSSP_NEGOTIATE_ALWAYS_SIGN |*/ NTLMSSP_NEGOTIATE_SIGN;
	if(ntlmv2_flag)
		SecurityBlob->NegotiateFlags |= NTLMSSP_NEGOTIATE_NTLMV2;

/* setup pointers to domain name and workstation name */

	SecurityBlob->WorkstationName.Buffer = 0;
	SecurityBlob->WorkstationName.Length = 0;
	SecurityBlob->WorkstationName.MaximumLength = 0;
	SecurityBlob->SessionKey.Length = 0;
	SecurityBlob->SessionKey.MaximumLength = 0;
	SecurityBlob->SessionKey.Buffer = 0;

	SecurityBlob->LmChallengeResponse.Length = 0;
	SecurityBlob->LmChallengeResponse.MaximumLength = 0;
	SecurityBlob->LmChallengeResponse.Buffer = 0;

	SecurityBlob->NtChallengeResponse.Length =
	    cpu_to_le16(CIFS_SESSION_KEY_SIZE);
	SecurityBlob->NtChallengeResponse.MaximumLength =
	    cpu_to_le16(CIFS_SESSION_KEY_SIZE);
	memcpy(bcc_ptr, ntlm_session_key, CIFS_SESSION_KEY_SIZE);
	SecurityBlob->NtChallengeResponse.Buffer =
	    cpu_to_le32(SecurityBlobLength);
	SecurityBlobLength += CIFS_SESSION_KEY_SIZE;
	bcc_ptr += CIFS_SESSION_KEY_SIZE;

	if (ses->capabilities & CAP_UNICODE) {
		if (domain == NULL) {
			SecurityBlob->DomainName.Buffer = 0;
			SecurityBlob->DomainName.Length = 0;
			SecurityBlob->DomainName.MaximumLength = 0;
		} else {
			SecurityBlob->DomainName.Length =
			    cifs_strtoUCS((wchar_t *) bcc_ptr, domain, 64,
					  nls_codepage);
			SecurityBlob->DomainName.Length *= 2;
			SecurityBlob->DomainName.MaximumLength =
			    cpu_to_le16(SecurityBlob->DomainName.Length);
			SecurityBlob->DomainName.Buffer =
			    cpu_to_le32(SecurityBlobLength);
			bcc_ptr += SecurityBlob->DomainName.Length;
			SecurityBlobLength += SecurityBlob->DomainName.Length;
			SecurityBlob->DomainName.Length =
			    cpu_to_le16(SecurityBlob->DomainName.Length);
		}
		if (user == NULL) {
			SecurityBlob->UserName.Buffer = 0;
			SecurityBlob->UserName.Length = 0;
			SecurityBlob->UserName.MaximumLength = 0;
		} else {
			SecurityBlob->UserName.Length =
			    cifs_strtoUCS((wchar_t *) bcc_ptr, user, 64,
					  nls_codepage);
			SecurityBlob->UserName.Length *= 2;
			SecurityBlob->UserName.MaximumLength =
			    cpu_to_le16(SecurityBlob->UserName.Length);
			SecurityBlob->UserName.Buffer =
			    cpu_to_le32(SecurityBlobLength);
			bcc_ptr += SecurityBlob->UserName.Length;
			SecurityBlobLength += SecurityBlob->UserName.Length;
			SecurityBlob->UserName.Length =
			    cpu_to_le16(SecurityBlob->UserName.Length);
		}

		/* SecurityBlob->WorkstationName.Length = cifs_strtoUCS((wchar_t *) bcc_ptr, "AMACHINE",64, nls_codepage);
		   SecurityBlob->WorkstationName.Length *= 2;
		   SecurityBlob->WorkstationName.MaximumLength = cpu_to_le16(SecurityBlob->WorkstationName.Length);
		   SecurityBlob->WorkstationName.Buffer = cpu_to_le32(SecurityBlobLength);
		   bcc_ptr += SecurityBlob->WorkstationName.Length;
		   SecurityBlobLength += SecurityBlob->WorkstationName.Length;
		   SecurityBlob->WorkstationName.Length = cpu_to_le16(SecurityBlob->WorkstationName.Length);  */

		if ((long) bcc_ptr % 2) {
			*bcc_ptr = 0;
			bcc_ptr++;
		}
		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, "Linux version ",
				  32, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, UTS_RELEASE, 32,
				  nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;	/* null term version string */
		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, CIFS_NETWORK_OPSYS,
				  64, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		*(bcc_ptr + 1) = 0;
		*(bcc_ptr + 2) = 0;
		bcc_ptr += 2;	/* null terminate network opsys string */
		*(bcc_ptr + 1) = 0;
		*(bcc_ptr + 2) = 0;
		bcc_ptr += 2;	/* null domain */
	} else {		/* ASCII */
		if (domain == NULL) {
			SecurityBlob->DomainName.Buffer = 0;
			SecurityBlob->DomainName.Length = 0;
			SecurityBlob->DomainName.MaximumLength = 0;
		} else {
			SecurityBlob->NegotiateFlags |=
			    NTLMSSP_NEGOTIATE_DOMAIN_SUPPLIED;
			strncpy(bcc_ptr, domain, 63);
			SecurityBlob->DomainName.Length = strnlen(domain, 64);
			SecurityBlob->DomainName.MaximumLength =
			    cpu_to_le16(SecurityBlob->DomainName.Length);
			SecurityBlob->DomainName.Buffer =
			    cpu_to_le32(SecurityBlobLength);
			bcc_ptr += SecurityBlob->DomainName.Length;
			SecurityBlobLength += SecurityBlob->DomainName.Length;
			SecurityBlob->DomainName.Length =
			    cpu_to_le16(SecurityBlob->DomainName.Length);
		}
		if (user == NULL) {
			SecurityBlob->UserName.Buffer = 0;
			SecurityBlob->UserName.Length = 0;
			SecurityBlob->UserName.MaximumLength = 0;
		} else {
			strncpy(bcc_ptr, user, 63);
			SecurityBlob->UserName.Length = strnlen(user, 64);
			SecurityBlob->UserName.MaximumLength =
			    cpu_to_le16(SecurityBlob->UserName.Length);
			SecurityBlob->UserName.Buffer =
			    cpu_to_le32(SecurityBlobLength);
			bcc_ptr += SecurityBlob->UserName.Length;
			SecurityBlobLength += SecurityBlob->UserName.Length;
			SecurityBlob->UserName.Length =
			    cpu_to_le16(SecurityBlob->UserName.Length);
		}
		/* BB fill in our workstation name if known BB */

		strcpy(bcc_ptr, "Linux version ");
		bcc_ptr += strlen("Linux version ");
		strcpy(bcc_ptr, UTS_RELEASE);
		bcc_ptr += strlen(UTS_RELEASE) + 1;
		strcpy(bcc_ptr, CIFS_NETWORK_OPSYS);
		bcc_ptr += strlen(CIFS_NETWORK_OPSYS) + 1;
		bcc_ptr++;	/* null domain */
		*bcc_ptr = 0;
	}
	SecurityBlob->NegotiateFlags =
	    cpu_to_le32(SecurityBlob->NegotiateFlags);
	pSMB->req.SecurityBlobLength = cpu_to_le16(SecurityBlobLength);
	BCC(smb_buffer) = (long) bcc_ptr - (long) pByteArea(smb_buffer);
	smb_buffer->smb_buf_length += BCC(smb_buffer);
	BCC(smb_buffer) = cpu_to_le16(BCC(smb_buffer));

	rc = SendReceive(xid, ses, smb_buffer, smb_buffer_response,
			 &bytes_returned, 1);
	if (rc) {
/*    rc = map_smb_to_linux_error(smb_buffer_response);  *//* done in SendReceive now */
	} else if ((smb_buffer_response->WordCount == 3)
		   || (smb_buffer_response->WordCount == 4)) {
		pSMBr->resp.Action = le16_to_cpu(pSMBr->resp.Action);
		pSMBr->resp.SecurityBlobLength =
		    le16_to_cpu(pSMBr->resp.SecurityBlobLength);
		if (pSMBr->resp.Action & GUEST_LOGIN)
			cFYI(1, (" Guest login"));	/* BB do we want to set anything in SesInfo struct ? */
/*        if(SecurityBlob2->MessageType != NtLm??){                               
                 cFYI("Unexpected message type on auth response is %d ")); 
        } */
		if (ses) {
			cFYI(1,
			     ("Does UID on challenge %d match auth response UID %d ",
			      ses->Suid, smb_buffer_response->Uid));
			ses->Suid = smb_buffer_response->Uid; /* UID left in wire format */
			bcc_ptr = pByteArea(smb_buffer_response);	
            /* response can have either 3 or 4 word count - Samba sends 3 */
			if ((pSMBr->resp.hdr.WordCount == 3)
			    || ((pSMBr->resp.hdr.WordCount == 4)
				&& (pSMBr->resp.SecurityBlobLength <
				    pSMBr->resp.ByteCount))) {
				if (pSMBr->resp.hdr.WordCount == 4) {
					bcc_ptr +=
					    pSMBr->resp.SecurityBlobLength;
					cFYI(1,
					     ("Security Blob Length %d ",
					      pSMBr->resp.SecurityBlobLength));
				}

				cFYI(1,
				     ("NTLMSSP response to Authenticate "));

				if (smb_buffer->Flags2 &= SMBFLG2_UNICODE) {
					if ((long) (bcc_ptr) % 2) {
						remaining_words =
						    (BCC(smb_buffer_response)
						     - 1) / 2;
						bcc_ptr++;	/* Unicode strings must be word aligned */
					} else {
						remaining_words = BCC(smb_buffer_response) / 2;
					}
					len =
					    UniStrnlen((wchar_t *) bcc_ptr,remaining_words - 1);
/* We look for obvious messed up bcc or strings in response so we do not go off
  the end since (at least) WIN2K and Windows XP have a major bug in not null
  terminating last Unicode string in response  */
					ses->serverOS =
					    kcalloc(2 * (len + 1), GFP_KERNEL);
					cifs_strfromUCS_le(ses->serverOS,
							   (wchar_t *)
							   bcc_ptr, len,
							   nls_codepage);
					bcc_ptr += 2 * (len + 1);
					remaining_words -= len + 1;
					ses->serverOS[2 * len] = 0;
					ses->serverOS[1 + (2 * len)] = 0;
					if (remaining_words > 0) {
						len = UniStrnlen((wchar_t *)
								 bcc_ptr,
								 remaining_words
								 - 1);
						ses->serverNOS =
						    kcalloc(2 * (len + 1),
							    GFP_KERNEL);
						cifs_strfromUCS_le(ses->
								   serverNOS,
								   (wchar_t *)
								   bcc_ptr,
								   len,
								   nls_codepage);
						bcc_ptr += 2 * (len + 1);
						ses->serverNOS[2 * len] = 0;
						ses->serverNOS[1+(2*len)] = 0;
						remaining_words -= len + 1;
						if (remaining_words > 0) {
							len = UniStrnlen((wchar_t *) bcc_ptr, remaining_words);	
     /* last string not always null terminated (e.g. for Windows XP & 2000) */
							ses->serverDomain =
							    kcalloc(2 *
								    (len +
								     1),
								    GFP_KERNEL);
							cifs_strfromUCS_le
							    (ses->
							     serverDomain,
							     (wchar_t *)
							     bcc_ptr, len,
							     nls_codepage);
							bcc_ptr +=
							    2 * (len + 1);
							ses->
							    serverDomain[2
									 * len]
							    = 0;
							ses->
							    serverDomain[1
									 +
									 (2
									  *
									  len)]
							    = 0;
						} /* else no more room so create dummy domain string */
						else
							ses->serverDomain = kcalloc(2,GFP_KERNEL);
					} else {  /* no room so create dummy domain and NOS string */
						ses->serverDomain = kcalloc(2, GFP_KERNEL);
						ses->serverNOS = kcalloc(2, GFP_KERNEL);
					}
				} else {	/* ASCII */
					len = strnlen(bcc_ptr, 1024);
					if (((long) bcc_ptr + len) - 
                        (long) pByteArea(smb_buffer_response) 
                            <= BCC(smb_buffer_response)) {
						ses->serverOS = kcalloc(len + 1,GFP_KERNEL);
						strncpy(ses->serverOS,bcc_ptr, len);

						bcc_ptr += len;
						bcc_ptr[0] = 0;	/* null terminate the string */
						bcc_ptr++;

						len = strnlen(bcc_ptr, 1024);
						ses->serverNOS = kcalloc(len+1,GFP_KERNEL);
						strncpy(ses->serverNOS, bcc_ptr, len);	
						bcc_ptr += len;
						bcc_ptr[0] = 0;
						bcc_ptr++;

						len = strnlen(bcc_ptr, 1024);
						ses->serverDomain = kcalloc(len+1,GFP_KERNEL);
						strncpy(ses->serverDomain, bcc_ptr, len);
						bcc_ptr += len;
						bcc_ptr[0] = 0;
						bcc_ptr++;
					} else
						cFYI(1,
						     ("Variable field of length %d extends beyond end of smb ",
						      len));
				}
			} else {
				cERROR(1,
				       (" Security Blob Length extends beyond end of SMB"));
			}
		} else {
			cERROR(1, ("No session structure passed in."));
		}
	} else {
		cERROR(1,
		       (" Invalid Word count %d: ",
			smb_buffer_response->WordCount));
		rc = -EIO;
	}

	if (smb_buffer)
		buf_release(smb_buffer);

	return rc;
}

int
CIFSTCon(unsigned int xid, struct cifsSesInfo *ses,
	 const char *tree, struct cifsTconInfo *tcon,
	 const struct nls_table *nls_codepage)
{
	struct smb_hdr *smb_buffer;
	struct smb_hdr *smb_buffer_response;
	TCONX_REQ *pSMB;
	TCONX_RSP *pSMBr;
	char *bcc_ptr;
	int rc = 0;
	int length;

	if (ses == NULL)
		return -EIO;

	smb_buffer = buf_get();
	if (smb_buffer == 0) {
		return -ENOMEM;
	}
	smb_buffer_response = smb_buffer;

	header_assemble(smb_buffer, SMB_COM_TREE_CONNECT_ANDX,
			0 /*no tid */ , 4 /*wct */ );
	smb_buffer->Uid = ses->Suid;
	pSMB = (TCONX_REQ *) smb_buffer;
	pSMBr = (TCONX_RSP *) smb_buffer_response;

	pSMB->AndXCommand = 0xFF;
	pSMB->Flags = cpu_to_le16(TCON_EXTENDED_SECINFO);
	pSMB->PasswordLength = cpu_to_le16(1);	/* minimum */
	bcc_ptr = &(pSMB->Password[0]);
	bcc_ptr++;		/* skip password */

	if(ses->server->secMode & (SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
		smb_buffer->Flags2 |= SMBFLG2_SECURITY_SIGNATURE;

	if (ses->capabilities & CAP_STATUS32) {
		smb_buffer->Flags2 |= SMBFLG2_ERR_STATUS;
	}
	if (ses->capabilities & CAP_DFS) {
		smb_buffer->Flags2 |= SMBFLG2_DFS;
	}
	if (ses->capabilities & CAP_UNICODE) {
		smb_buffer->Flags2 |= SMBFLG2_UNICODE;
		length =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, tree, 100, nls_codepage);
		bcc_ptr += 2 * length;	/* convert num of 16 bit words to bytes */
		bcc_ptr += 2;	/* skip trailing null */
	} else {		/* ASCII */

		strcpy(bcc_ptr, tree);
		bcc_ptr += strlen(tree) + 1;
	}
	strcpy(bcc_ptr, "?????");
	bcc_ptr += strlen("?????");
	bcc_ptr += 1;
	BCC(smb_buffer) = (long) bcc_ptr - (long) pByteArea(smb_buffer);
	smb_buffer->smb_buf_length += BCC(smb_buffer);
	BCC(smb_buffer) = cpu_to_le16(BCC(smb_buffer));

	rc = SendReceive(xid, ses, smb_buffer, smb_buffer_response, &length, 0);

	/* if (rc) rc = map_smb_to_linux_error(smb_buffer_response); */
	/* above now done in SendReceive */
	if ((rc == 0) && (tcon != NULL)) {
        tcon->tidStatus = CifsGood;
		tcon->tid = smb_buffer_response->Tid;
		bcc_ptr = pByteArea(smb_buffer_response);
		length = strnlen(bcc_ptr, BCC(smb_buffer_response) - 2);
        /* skip service field (NB: this field is always ASCII) */
		bcc_ptr += length + 1;	
		strncpy(tcon->treeName, tree, MAX_TREE_SIZE);
		if (smb_buffer->Flags2 &= SMBFLG2_UNICODE) {
			length = UniStrnlen((wchar_t *) bcc_ptr, 512);
			if (((long) bcc_ptr + (2 * length)) -
			    (long) pByteArea(smb_buffer_response) <=
			    BCC(smb_buffer_response)) {
				tcon->nativeFileSystem =
				    kcalloc(length + 2, GFP_KERNEL);
				cifs_strfromUCS_le(tcon->nativeFileSystem,
						   (wchar_t *) bcc_ptr,
						   length, nls_codepage);
				bcc_ptr += 2 * length;
				bcc_ptr[0] = 0;	/* null terminate the string */
				bcc_ptr[1] = 0;
				bcc_ptr += 2;
			}
			/* else do not bother copying these informational fields */
		} else {
			length = strnlen(bcc_ptr, 1024);
			if (((long) bcc_ptr + length) -
			    (long) pByteArea(smb_buffer_response) <=
			    BCC(smb_buffer_response)) {
				tcon->nativeFileSystem =
				    kcalloc(length + 1, GFP_KERNEL);
				strncpy(tcon->nativeFileSystem, bcc_ptr,
					length);
			}
			/* else do not bother copying these informational fields */
		}
		tcon->Flags = le16_to_cpu(pSMBr->OptionalSupport);
		cFYI(1, ("Tcon flags: 0x%x ", tcon->Flags));
	} else if ((rc == 0) && tcon == NULL) {
        /* all we need to save for IPC$ connection */
		ses->ipc_tid = smb_buffer_response->Tid;
	}

	if (smb_buffer)
		buf_release(smb_buffer);
	return rc;
}

int
cifs_umount(struct super_block *sb, struct cifs_sb_info *cifs_sb)
{
	int rc = 0;
	int xid;
	struct cifsSesInfo *ses = NULL;

	xid = GetXid();

	if (cifs_sb->tcon) {
		ses = cifs_sb->tcon->ses; /* save ptr to ses before delete tcon!*/
		rc = CIFSSMBTDis(xid, cifs_sb->tcon);
		if (rc == -EBUSY) {
			FreeXid(xid);
			return 0;
		}
		tconInfoFree(cifs_sb->tcon);
		if ((ses) && (ses->server)) {
			cFYI(1, ("About to do SMBLogoff "));
			rc = CIFSSMBLogoff(xid, ses);
			if (rc == -EBUSY) {
				FreeXid(xid);
				return 0;
			}
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(HZ / 4);	/* give captive thread time to exit */
			if((ses->server) && (ses->server->ssocket)) {            
				cFYI(1,("Waking up socket by sending it signal "));
				send_sig(SIGKILL,ses->server->tsk,1);
			}
		} else
			cFYI(1, ("No session or bad tcon"));
	}
	/* BB future check active count of tcon and then free if needed BB */
	cifs_sb->tcon = NULL;
	if (ses) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ / 2);
	}
	if (ses)
		sesInfoFree(ses);

	FreeXid(xid);
	return rc;		/* BB check if we should always return zero here */
} 
