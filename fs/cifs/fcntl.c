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

int cifs_directory_notify(unsigned long arg, struct file * file)
{
	int xid;
	int rc = -EINVAL;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;

	xid = GetXid();
	cifs_sb = CIFS_SB(file->f_dentry->d_sb);
	pTcon = cifs_sb->tcon;

	down(&file->f_dentry->d_sb->s_vfs_rename_sem);
	full_path = build_path_from_dentry(file->f_dentry);
	up(&file->f_dentry->d_sb->s_vfs_rename_sem);

	if(full_path == NULL) {
		rc = -ENOMEM;
	} else {
		cFYI(1,("cifs dir notify on file %s",full_path));
		/* CIFSSMBNotify(xid, pTcon, full_path, cifs_sb->local_nls);*/
	}
	
	FreeXid(xid);
	return rc;
}


long cifs_fcntl(int file_desc, unsigned int command, unsigned long arg,
				struct file * file)
{
	/* Few few file control functions need to be specially mapped. So far
	only:
		F_NOTIFY (for directory change notification)
	And eventually:
		F_GETLEASE
		F_SETLEASE 
	need to be mapped here. The others either already are mapped downstream
	or do not need to go to the server (client only sideeffects):
		F_DUPFD:
		F_GETFD:
		F_SETFD:
		F_GETFL:
		F_SETFL:
		F_GETLK:
		F_SETLK:
		F_SETLKW:
		F_GETOWN:
		F_SETOWN:
		F_GETSIG:
		F_SETSIG:
	*/
	long rc = 0;

	cFYI(1,("cifs_fcntl: command %d with arg %lx",command,arg)); /* BB removeme BB */

	switch (command) {
	case F_NOTIFY:
		/* let the local call have a chance to fail first */
		rc = generic_file_fcntl(file_desc,command,arg,file);
		if(rc)
			return rc;
		else {
			/* local call succeeded try to do remote notify to
			pick up changes from other clients to server file */
			cifs_directory_notify(arg, file);
			/* BB add case to long and return rc from above */
			return rc;
		}
		break;
	default:
		break;
	}
	return generic_file_fcntl(file_desc,command,arg,file);
}
                
