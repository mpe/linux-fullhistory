/*
 *   fs/cifs/fcntl.c
 *
 *   vfs operations that deal with the file control API
 * 
 *   Copyright (C) International Business Machines  Corp., 2003,2004
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
#include <linux/stat.h>
#include <linux/fcntl.h>
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"
#include "cifsfs.h"

static __u32 convert_to_cifs_notify_flags(unsigned long fcntl_notify_flags)
{
	__u32 cifs_ntfy_flags = 0;

	/* No way on Linux VFS to ask to monitor xattr
	changes (and no stream support either */
	if(fcntl_notify_flags & DN_ACCESS) {
		cifs_ntfy_flags |= FILE_NOTIFY_CHANGE_LAST_ACCESS;
	}
	if(fcntl_notify_flags & DN_MODIFY) {
		/* What does this mean on directories? */
		cifs_ntfy_flags |= FILE_NOTIFY_CHANGE_LAST_WRITE |
			FILE_NOTIFY_CHANGE_SIZE;
	}
	if(fcntl_notify_flags & DN_CREATE) {
		cifs_ntfy_flags |= FILE_NOTIFY_CHANGE_CREATION | 
			FILE_NOTIFY_CHANGE_LAST_WRITE;
	}
	if(fcntl_notify_flags & DN_DELETE) {
		cifs_ntfy_flags |= FILE_NOTIFY_CHANGE_LAST_WRITE;
	}
	if(fcntl_notify_flags & DN_RENAME) {
		/* BB review this - checking various server behaviors */
		cifs_ntfy_flags |= FILE_NOTIFY_CHANGE_DIR_NAME | 
			FILE_NOTIFY_CHANGE_FILE_NAME;
	}
	if(fcntl_notify_flags & DN_ATTRIB) {
		cifs_ntfy_flags |= FILE_NOTIFY_CHANGE_SECURITY | 
			FILE_NOTIFY_CHANGE_ATTRIBUTES;
	}
/*	if(fcntl_notify_flags & DN_MULTISHOT) {
		cifs_ntfy_flags |= ;
	} */ /* BB fixme - not sure how to handle this with CIFS yet */


	return cifs_ntfy_flags;
}

int cifs_dir_notify(struct file * file, unsigned long arg)
{
	int xid;
	int rc = -EINVAL;
	int oplock = FALSE;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;
	__u32 filter = FILE_NOTIFY_CHANGE_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES;
	__u16 netfid;

	xid = GetXid();
	cifs_sb = CIFS_SB(file->f_dentry->d_sb);
	pTcon = cifs_sb->tcon;

	down(&file->f_dentry->d_sb->s_vfs_rename_sem);
	full_path = build_path_from_dentry(file->f_dentry);
	up(&file->f_dentry->d_sb->s_vfs_rename_sem);

	if(full_path == NULL) {
		rc = -ENOMEM;
	} else {
		cERROR(1,("cifs dir notify on file %s with arg 0x%lx",full_path,arg)); /* BB removeme BB */
		rc = CIFSSMBOpen(xid, pTcon, full_path, FILE_OPEN, 
			GENERIC_READ | SYNCHRONIZE, 0 /* create options */,
			&netfid, &oplock,NULL, cifs_sb->local_nls);
		/* BB fixme - add this handle to a notify handle list */
		if(rc) {
			cERROR(1,("Could not open directory for notify"));  /* BB remove BB */
		} else {
			filter = convert_to_cifs_notify_flags(arg);
			if(filter != 0) {
				rc = CIFSSMBNotify(xid, pTcon, 0 /* no subdirs */, netfid, 
					filter, cifs_sb->local_nls);
			} else {
				rc = -EINVAL;
			}
			/* BB add code to close file eventually (at unmount
			it would close automatically but may be a way
			to do it easily when inode freed or when
			notify info is cleared/changed */
            cERROR(1,("notify rc %d",rc));
		}
	}
	
	FreeXid(xid);
	return rc;
}
