/*
 *  smb_fs_sb.h
 *
 *  Copyright (C) 1995 by Paal-Kr. Engstad and Volker Lendecke
 *
 */

#ifndef _SMB_FS_SB
#define _SMB_FS_SB

#include <linux/smb.h>
#include <linux/smb_fs_i.h>
#include <linux/smb_mount.h>
#include <linux/types.h>

#ifdef __KERNEL__

struct smb_server {
	enum smb_protocol  protocol;       /* The protocol this
                                              connection accepts. */
	enum smb_case_hndl case_handling;
	struct file *      sock_file;	   /* The socket we transfer
                                              data on. */
	int                lock;       	   /* To prevent mismatch in
                                              protocols. */
	struct wait_queue *wait;

	word               max_xmit;
	char               hostname[256];
	word               pid;
	word               server_uid;
	word               mid;
	word               tid;

        struct smb_mount_data m; /* We store the complete information here
                                  * to be able to reconnect.
                                  */

        unsigned short     rcls; /* The error codes we received */
        unsigned short     err;
	unsigned char *    packet;

        enum smb_conn_state state;
        unsigned long reconnect_time; /* The time of the last attempt */

        /* The following are LANMAN 1.0 options transferred to us in
           SMBnegprot */
        word   secmode;
        word   maxxmt;
        word   maxmux;
        word   maxvcs;
        word   blkmode;
        dword  sesskey;

        /* We use our on data_ready callback, but need the original one */
        void *data_ready;

        /* We do not have unique numbers for files in the smb protocol
           like NFS-filehandles. (SMB was designed for DOS, not for
           UNIX!) So we have to create our own inode numbers. We keep
           a complete path of smb_inode_info's to each active
           inode. The inode number is then created by the address of
           this structure. */
        struct smb_inode_info root;
};

/*
 * This is the part of the super-block (in memory) for the SMB file system.
 */

struct smb_sb_info {
	struct smb_server  s_server;
	struct smb_dskattr s_attr;
};

#endif /* __KERNEL__ */

#endif
