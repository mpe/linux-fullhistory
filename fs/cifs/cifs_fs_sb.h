/*
 *   fs/cifs/cifs_fs_sb.h
 *
 *   Copyright (c) International Business Machines  Corp., 2002
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
 */
#ifndef _CIFS_FS_SB_H
#define _CIFS_FS_SB_H

struct cifs_sb_info {
	struct cifsTconInfo *tcon;	/* primary mount */
	/* list of implicit mounts beneath this mount point - needed in dfs case */
	struct list_head nested_tcon_q;
	struct nls_table *local_nls;
};
#endif				/* _CIFS_FS_SB_H */
