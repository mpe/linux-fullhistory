/*
 *   fs/cifs/inode.c
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
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <linux/fs.h>
#include <linux/stat.h>
#include <asm/div64.h>
#include "cifsfs.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"

int
cifs_get_inode_info_unix(struct inode **pinode,
			 const unsigned char *search_path,
			 struct super_block *sb)
{
	int xid;
	int rc = 0;
	FILE_UNIX_BASIC_INFO findData;
	struct cifsTconInfo *pTcon;
	struct inode *inode;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	char *tmp_path;

	xid = GetXid();

	pTcon = cifs_sb->tcon;
	cFYI(1, ("\nGetting info on %s ", search_path));
	/* we could have done a find first instead but this returns more info */
	rc = CIFSSMBUnixQPathInfo(xid, pTcon, search_path, &findData,
				  cifs_sb->local_nls);
	/* dump_mem("\nUnixQPathInfo return data", &findData, sizeof(findData)); */
	if (rc) {
		if (rc == -EREMOTE) {
/* rc = *//* CIFSGetDFSRefer(xid, pTcon->ses, search_path,
   &referrals,
   &num_referrals,
   cifs_sb->local_nls); */
			tmp_path =
			    kmalloc(strnlen
				    (pTcon->treeName,
				     MAX_TREE_SIZE + 1) +
				    strnlen(search_path, MAX_PATHCONF) + 1,
				    GFP_KERNEL);
			if (tmp_path == NULL) {
				FreeXid(xid);
				return -ENOMEM;
			}
        /* have to skip first of the double backslash of UNC name */
			strncpy(tmp_path, pTcon->treeName, MAX_TREE_SIZE);	
			strncat(tmp_path, search_path, MAX_PATHCONF);
			rc = connect_to_dfs_path(xid, pTcon->ses,
						 /* treename + */ tmp_path,
						 cifs_sb->local_nls);
			kfree(tmp_path);

			/* BB fix up inode etc. */
		} else if (rc) {
			FreeXid(xid);
			return rc;
		}

	} else {
		struct cifsInodeInfo *cifsInfo;

		/* get new inode */
		if (*pinode == NULL) {
			*pinode = new_inode(sb);
			cFYI(1, ("\nAlloc new inode %p ", *pinode));
		}
		inode = *pinode;
/*        new_inode = iget(parent_dir_inode->i_sb, findData.IndexNumber); */
        /* index number not reliable in response data */

		cifsInfo = CIFS_I(inode);

		cFYI(1, ("\nOld time %ld ", cifsInfo->time));
		cifsInfo->time = jiffies;
		cFYI(1, (" New time %ld ", cifsInfo->time));
		atomic_inc(&cifsInfo->inUse);	/* inc on every refresh of inode */

		inode->i_atime =
		    le64_to_cpu(cifs_NTtimeToUnix(findData.LastAccessTime));
		inode->i_mtime =
		    le64_to_cpu(cifs_NTtimeToUnix
				(findData.LastModificationTime));
		inode->i_ctime =
		    le64_to_cpu(cifs_NTtimeToUnix(findData.LastStatusChange));
		inode->i_mode = le64_to_cpu(findData.Permissions);
		findData.Type = le32_to_cpu(findData.Type);
		if (findData.Type == UNIX_FILE) {
			inode->i_mode |= S_IFREG;
		} else if (findData.Type == UNIX_SYMLINK) {
			inode->i_mode |= S_IFLNK;
		} else if (findData.Type == UNIX_DIR) {
			inode->i_mode |= S_IFDIR;
		} else if (findData.Type == UNIX_CHARDEV) {
			inode->i_mode |= S_IFCHR;
		} else if (findData.Type == UNIX_BLOCKDEV) {
			inode->i_mode |= S_IFBLK;
		} else if (findData.Type == UNIX_FIFO) {
			inode->i_mode |= S_IFIFO;
		} else if (findData.Type == UNIX_SOCKET) {
			inode->i_mode |= S_IFSOCK;
		}
		inode->i_uid = le64_to_cpu(findData.Uid);
		inode->i_gid = le64_to_cpu(findData.Gid);
		inode->i_nlink = le64_to_cpu(findData.Nlinks);
		findData.NumOfBytes = le64_to_cpu(findData.NumOfBytes);
		findData.EndOfFile = le64_to_cpu(findData.EndOfFile);
		inode->i_size = findData.EndOfFile;
		inode->i_blksize =
		    (pTcon->ses->maxBuf - MAX_CIFS_HDR_SIZE) & 0xFFFFFE00;
		inode->i_blocks = do_div(findData.NumOfBytes, inode->i_blksize);
		cFYI(1,
		     ("\nFinddata alloc size (from smb) %lld",
		      findData.NumOfBytes));
		if (findData.NumOfBytes < findData.EndOfFile)
			cFYI(1, ("\nServer inconsistency Error: it says allocation size less than end of file "));
		cFYI(1,
		     ("\nCIFS FFIRST: Size %ld and blocks %ld ",
		      (unsigned long) inode->i_size, inode->i_blocks));
		if (S_ISREG(inode->i_mode)) {
			cFYI(1, (" File inode "));
			inode->i_op = &cifs_file_inode_ops;
			inode->i_fop = &cifs_file_ops;
		} else if (S_ISDIR(inode->i_mode)) {
			cFYI(1, (" Directory inode"));
			inode->i_op = &cifs_dir_inode_ops;
			inode->i_fop = &cifs_dir_ops;
		} else if (S_ISLNK(inode->i_mode)) {
			cFYI(1, (" Symbolic Link inode "));
			inode->i_op = &cifs_symlink_inode_ops;
/* tmp_inode->i_fop = *//* do not need to set to anything */
		} else {
			cFYI(1, ("\nInit special inode "));
			init_special_inode(inode, inode->i_mode,
					   kdev_t_to_nr(inode->i_rdev));
		}
	}
	FreeXid(xid);
	return rc;
}

int
cifs_get_inode_info(struct inode **pinode,
		    const unsigned char *search_path, struct super_block *sb)
{
	int xid;
	int rc = 0;
	FILE_ALL_INFO findData;
	struct cifsTconInfo *pTcon;
	struct inode *inode;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	char *tmp_path;

	xid = GetXid();

	pTcon = cifs_sb->tcon;
	cFYI(1, ("\nGetting info on %s ", search_path));
	/* we could have done a find first instead but this returns more info */
	rc = CIFSSMBQPathInfo(xid, pTcon, search_path, &findData,
			      cifs_sb->local_nls);
	/* dump_mem("\nQPathInfo return data",&findData, sizeof(findData)); */
	if (rc) {
		if (rc == -EREMOTE) {
			/* BB add call to new func rc = GetDFSReferral(); */
/* rc = *//* CIFSGetDFSRefer(xid, pTcon->ses, search_path,
   &referrals,
   &num_referrals,
   cifs_sb->local_nls); */
			tmp_path =
			    kmalloc(strnlen
				    (pTcon->treeName,
				     MAX_TREE_SIZE + 1) +
				    strnlen(search_path, MAX_PATHCONF) + 1,
				    GFP_KERNEL);
			if (tmp_path == NULL) {
				FreeXid(xid);
				return -ENOMEM;
			}

			strncpy(tmp_path, pTcon->treeName, MAX_TREE_SIZE);
			strncat(tmp_path, search_path, MAX_PATHCONF);
			rc = connect_to_dfs_path(xid, pTcon->ses,
						 /* treename + */ tmp_path,
						 cifs_sb->local_nls);
			kfree(tmp_path);
			/* BB fix up inode etc. */
		} else if (rc) {
			FreeXid(xid);
			return rc;
		}
	} else {
		struct cifsInodeInfo *cifsInfo;

		/* get new inode */
		if (*pinode == NULL) {
			*pinode = new_inode(sb);
			cFYI(1, ("\nAlloc new inode %p ", *pinode));
		}

		inode = *pinode;
		cifsInfo = CIFS_I(inode);
		findData.Attributes = le32_to_cpu(findData.Attributes);
		cifsInfo->cifsAttrs = findData.Attributes;
		cFYI(1, ("\nOld time %ld ", cifsInfo->time));
		cifsInfo->time = jiffies;
		cFYI(1, (" New time %ld ", cifsInfo->time));
		atomic_inc(&cifsInfo->inUse);	/* inc on every refresh of inode */

		inode->i_blksize =
		    (pTcon->ses->maxBuf - MAX_CIFS_HDR_SIZE) & 0xFFFFFE00;
		/* Linux can not store file creation time unfortunately so we ignore it */
		inode->i_atime =
		    cifs_NTtimeToUnix(le64_to_cpu(findData.LastAccessTime));
		inode->i_mtime =
		    cifs_NTtimeToUnix(le64_to_cpu(findData.LastWriteTime));
		inode->i_ctime =
		    cifs_NTtimeToUnix(le64_to_cpu(findData.ChangeTime));
/* inode->i_mode = S_IRWXUGO;  *//* 777 perms */
		/* should we treat the dos attribute of read-only as read-only mode bit e.g. 555 */
		inode->i_mode = S_IALLUGO & ~(S_ISUID | S_IXGRP);	/* 2767 perms indicate mandatory locking - will override for dirs later */
		cFYI(0,
		     ("\nAttributes came in as 0x%x\n", findData.Attributes));
		if (findData.Attributes & ATTR_REPARSE) {	
   /* Can IFLNK be set as it basically is on windows with IFREG or IFDIR? */
			inode->i_mode |= S_IFLNK;
		} else if (findData.Attributes & ATTR_DIRECTORY) {
   /* override default perms since we do not do byte range locking on dirs */
			inode->i_mode = S_IRWXUGO;	
            inode->i_mode |= S_IFDIR;
		} else {
			inode->i_mode |= S_IFREG;
   /* BB add code here - validate if device or weird share or device type? */
		}
		inode->i_size = le64_to_cpu(findData.EndOfFile);
		findData.AllocationSize = le64_to_cpu(findData.AllocationSize);
		inode->i_blocks =
		    do_div(findData.AllocationSize, inode->i_blksize);
		cFYI(1,
		     ("\n Size %ld and blocks %ld ",
		      (unsigned long) inode->i_size, inode->i_blocks));
		inode->i_nlink = le32_to_cpu(findData.NumberOfLinks);

		/* BB fill in uid and gid here? with help from winbind? */

		if (S_ISREG(inode->i_mode)) {
			cFYI(1, (" File inode "));
			inode->i_op = &cifs_file_inode_ops;
			inode->i_fop = &cifs_file_ops;
		} else if (S_ISDIR(inode->i_mode)) {
			cFYI(1, (" Directory inode "));
			inode->i_op = &cifs_dir_inode_ops;
			inode->i_fop = &cifs_dir_ops;
		} else if (S_ISLNK(inode->i_mode)) {
			cFYI(1, (" Symbolic Link inode "));
			inode->i_op = &cifs_symlink_inode_ops;
		} else {
			init_special_inode(inode, inode->i_mode,
					   kdev_t_to_nr(inode->i_rdev));
		}
	}
	FreeXid(xid);
	return rc;
}

void
cifs_read_inode(struct inode *inode)
{				/* gets root inode */

	struct cifs_sb_info *cifs_sb;

	cifs_sb = CIFS_SB(inode->i_sb);

	if (cifs_sb->tcon->ses->capabilities & CAP_UNIX)
		cifs_get_inode_info_unix(&inode, "", inode->i_sb);
	else
		cifs_get_inode_info(&inode, "", inode->i_sb);
}

int
cifs_unlink(struct inode *inode, struct dentry *direntry)
{
	int rc = 0;
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;
	struct cifsInodeInfo *cifsInode;

	cFYI(1, ("\n cifs_unlink, inode = 0x%p with ", inode));

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	/* BB Should we close the file if it is already open from our client? */

	full_path = build_path_from_dentry(direntry);

	rc = CIFSSMBDelFile(xid, pTcon, full_path, cifs_sb->local_nls);

	if (!rc) {
		direntry->d_inode->i_nlink--;
	}
	cifsInode = CIFS_I(direntry->d_inode);
	cifsInode->time = 0;	/* will force revalidate to get info when needed */
	direntry->d_inode->i_ctime = inode->i_ctime = inode->i_mtime =
	    CURRENT_TIME;
	cifsInode = CIFS_I(inode);
	cifsInode->time = 0;	/* force revalidate of dir as well */

	if (full_path)
		kfree(full_path);
	FreeXid(xid);
	return rc;
}

int
cifs_mkdir(struct inode *inode, struct dentry *direntry, int mode)
{
	int rc = 0;
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;
	struct inode *newinode = NULL;

	cFYI(1, ("In cifs_mkdir, mode = 0x%x inode = 0x%p\n", mode, inode));

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	full_path = build_path_from_dentry(direntry);
	/* BB add setting the equivalent of mode via CreateX w/ACLs */
	rc = CIFSSMBMkDir(xid, pTcon, full_path, cifs_sb->local_nls);
	if (rc) {
		cFYI(1, ("\ncifs_mkdir returned 0x%x ", rc));
	} else {
		inode->i_nlink++;
		if (pTcon->ses->capabilities & CAP_UNIX)
			rc = cifs_get_inode_info_unix(&newinode, full_path,
						      inode->i_sb);
		else
			rc = cifs_get_inode_info(&newinode, full_path,
						 inode->i_sb);

		direntry->d_op = &cifs_dentry_ops;
		d_instantiate(direntry, newinode);
		direntry->d_inode->i_nlink = 2;
        if (cifs_sb->tcon->ses->capabilities & CAP_UNIX)                
            CIFSSMBUnixSetPerms(xid, pTcon, full_path, mode,
                        0xFFFFFFFFFFFFFFFF,  
                        0xFFFFFFFFFFFFFFFF,
                        cifs_sb->local_nls);
        else { /* BB to be implemented via Windows secrty descriptors*/
        /* eg CIFSSMBWinSetPerms(xid,pTcon,full_path,mode,-1,-1,local_nls);*/
        }

	}
	if (full_path)
		kfree(full_path);
	FreeXid(xid);

	return rc;
}

int
cifs_rmdir(struct inode *inode, struct dentry *direntry)
{
	int rc = 0;
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;
	struct cifsInodeInfo *cifsInode;

	cFYI(1, ("\nn cifs_rmdir, inode = 0x%p with ", inode));

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	full_path = build_path_from_dentry(direntry);

	rc = CIFSSMBRmDir(xid, pTcon, full_path, cifs_sb->local_nls);

	if (!rc) {
		inode->i_nlink--;
		direntry->d_inode->i_size = 0;
		direntry->d_inode->i_nlink = 0;
	}

	cifsInode = CIFS_I(direntry->d_inode);
	cifsInode->time = 0;	/* force revalidate to go get info when needed */
	direntry->d_inode->i_ctime = inode->i_ctime = inode->i_mtime =
	    CURRENT_TIME;

	if (full_path)
		kfree(full_path);
	FreeXid(xid);
	return rc;
}

int
cifs_rename(struct inode *source_inode, struct dentry *source_direntry,
	    struct inode *target_inode, struct dentry *target_direntry)
{
	char *fromName;
	char *toName;
	struct cifs_sb_info *cifs_sb_source;
	struct cifs_sb_info *cifs_sb_target;
	struct cifsTconInfo *pTcon;
	int xid;
	int rc = 0;

	xid = GetXid();

	cifs_sb_target = CIFS_SB(target_inode->i_sb);
	cifs_sb_source = CIFS_SB(source_inode->i_sb);
	pTcon = cifs_sb_source->tcon;

	if (pTcon != cifs_sb_target->tcon)
		return -EXDEV;	/* BB actually could be allowed if same server, but
                     different share. Might eventually add support for this */

	fromName = build_path_from_dentry(source_direntry);
	toName = build_path_from_dentry(target_direntry);

	rc = CIFSSMBRename(xid, pTcon, fromName, toName,
			   cifs_sb_source->local_nls);
	if (fromName)
		kfree(fromName);
	if (toName)
		kfree(toName);

	return rc;
}

int
cifs_revalidate(struct dentry *direntry)
{
	int xid;
	int rc = 0;
	char *full_path;
	struct cifs_sb_info *cifs_sb;
	struct cifsInodeInfo *cifsInode;

	xid = GetXid();

	cifs_sb = CIFS_SB(direntry->d_sb);

	full_path = build_path_from_dentry(direntry);
	cFYI(1,
	     (" full path: %s for inode 0x%p with count %d dentry: 0x%p d_time %ld at time %ld \n",
	      full_path, direntry->d_inode,
	      direntry->d_inode->i_count.counter, direntry,
	      direntry->d_time, jiffies));

	cifsInode = CIFS_I(direntry->d_inode);

	if ((time_before(jiffies, cifsInode->time + HZ))
	    && (direntry->d_inode->i_nlink == 1)) {
		cFYI(1, (" Do not need to revalidate "));
		if (full_path)
			kfree(full_path);
		FreeXid(xid);
		return rc;
	}

	if (cifs_sb->tcon->ses->capabilities & CAP_UNIX)
		cifs_get_inode_info_unix(&direntry->d_inode, full_path,
					 direntry->d_sb);
	else
		cifs_get_inode_info(&direntry->d_inode, full_path,
				    direntry->d_sb);

	/* BB if not oplocked, invalidate inode pages if mtime has changed */

	if (full_path)
		kfree(full_path);
	FreeXid(xid);

	return rc;
}

void
cifs_truncate_file(struct inode *inode)
{				/* BB remove - may not need this function after all BB */
	int xid;
	int rc = 0;
    struct cifsFileInfo *open_file = NULL;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	struct cifsInodeInfo *cifsInode;
	struct dentry *dirent;
	char *full_path = NULL;   

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	if (list_empty(&inode->i_dentry)) {
		cERROR(1,
		       ("Can not get pathname from empty dentry in inode 0x%p ",
			inode));
		FreeXid(xid);
		return;
	}
	dirent = list_entry(inode->i_dentry.next, struct dentry, d_alias);
	if (dirent) {
		full_path = build_path_from_dentry(dirent);
		rc = CIFSSMBSetEOF(xid, pTcon, full_path, inode->i_size,FALSE,
				   cifs_sb->local_nls);
        cFYI(1,("\nSetEOF (truncate) rc = %d",rc));
        if(rc == -ETXTBSY) {        
            cifsInode = CIFS_I(inode);
            if(!list_empty(&(cifsInode->openFileList))) {            
	            open_file = list_entry(cifsInode->openFileList.next,
                                       struct cifsFileInfo, flist);           
                /* We could check if file is open for writing first and 
                   also we could also override the smb pid with the pid 
                   of the file opener when sending the CIFS request */
                rc = CIFSSMBSetFileSize(xid, pTcon, inode->i_size,
                                        open_file->netfid,open_file->pid,FALSE);
            } else {
                cFYI(1,("\nNo open files to get file handle from"));
            }
        }
		if (!rc)
			CIFSSMBSetEOF(xid,pTcon,full_path,inode->i_size,TRUE,cifs_sb->local_nls);
           /* allocation size setting seems optional so ignore return code */
	}
	if (full_path)
		kfree(full_path);
	FreeXid(xid);
	return;
}

int
cifs_setattr(struct dentry *direntry, struct iattr *attrs)
{
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;
	int rc = -EACCES;
    struct cifsFileInfo *open_file = NULL;
	FILE_BASIC_INFO time_buf;
	int set_time = FALSE;
	__u64 mode = 0xFFFFFFFFFFFFFFFF;
	__u64 uid = 0xFFFFFFFFFFFFFFFF;
	__u64 gid = 0xFFFFFFFFFFFFFFFF;
	struct cifsInodeInfo *cifsInode;

	xid = GetXid();

	cFYI(1,
	     ("\nIn cifs_setattr, name = %s attrs->iavalid 0x%x\n",
	      direntry->d_name.name, attrs->ia_valid));
	cifs_sb = CIFS_SB(direntry->d_inode->i_sb);
	pTcon = cifs_sb->tcon;

	full_path = build_path_from_dentry(direntry);
	cifsInode = CIFS_I(direntry->d_inode);

	/* BB check if we need to refresh inode from server now ? BB */

	cFYI(1, ("\nChanging attributes 0x%x", attrs->ia_valid));

	if (attrs->ia_valid & ATTR_SIZE) {
		rc = CIFSSMBSetEOF(xid, pTcon, full_path, attrs->ia_size,FALSE,
				   cifs_sb->local_nls);
        cFYI(1,("\nSetEOF (setattrs) rc = %d",rc));

        if(rc == -ETXTBSY) {
            if(!list_empty(&(cifsInode->openFileList))) {            
                open_file = list_entry(cifsInode->openFileList.next, 
                               struct cifsFileInfo, flist);           
    /* We could check if file is open for writing first */
                rc = CIFSSMBSetFileSize(xid, pTcon, attrs->ia_size,
                                        open_file->netfid,open_file->pid,FALSE);           
            } else {
                cFYI(1,("\nNo open files to get file handle from"));
            }
        }
        /*  Set Allocation Size of file - might not even need to call the
            following but might as well and it does not hurt if it fails */
		CIFSSMBSetEOF(xid, pTcon, full_path, attrs->ia_size, TRUE, cifs_sb->local_nls);
		if (rc == 0)
			vmtruncate(direntry->d_inode, attrs->ia_size);
		/* BB add special case to handle sharing violation (due to Samba bug)
		   by calling SetFileInfo to set the sizes */
	}
	if (attrs->ia_valid & ATTR_UID) {
		cFYI(1, ("\nCIFS - UID changed to %d", attrs->ia_uid));
		uid = attrs->ia_uid;
		/*        entry->uid = cpu_to_le16(attr->ia_uid); */
	}
	if (attrs->ia_valid & ATTR_GID) {
		cFYI(1, ("\nCIFS - GID changed to %d", attrs->ia_gid));
		gid = attrs->ia_gid;
		/*      entry->gid = cpu_to_le16(attr->ia_gid); */
	}
	if (attrs->ia_valid & ATTR_MODE) {
		cFYI(1, ("\nCIFS - Mode changed to 0x%x", attrs->ia_mode));
		mode = attrs->ia_mode;
		/*      entry->mode = cpu_to_le16(attr->ia_mode); */
	}

	if ((cifs_sb->tcon->ses->capabilities & CAP_UNIX)
	    && (attrs->ia_valid & (ATTR_MODE | ATTR_GID | ATTR_UID)))
		CIFSSMBUnixSetPerms(xid, pTcon, full_path, mode, uid, gid,
				    cifs_sb->local_nls);
	else {			/* BB to be implemented - via Windows security descriptors */
		/* CIFSSMBWinSetPerms(xid,pTcon,full_path,mode,uid,gid,cifs_sb->local_nls);*/
	}

	if (attrs->ia_valid & ATTR_ATIME) {
		set_time = TRUE;
		time_buf.LastAccessTime =
		    cpu_to_le64(cifs_UnixTimeToNT(attrs->ia_atime));
	} else
		time_buf.LastAccessTime = 0;

	if (attrs->ia_valid & ATTR_MTIME) {
		set_time = TRUE;
		time_buf.LastWriteTime =
		    cpu_to_le64(cifs_UnixTimeToNT(attrs->ia_mtime));
	} else
		time_buf.LastWriteTime = 0;

	if (attrs->ia_valid & ATTR_CTIME) {
		set_time = TRUE;
		cFYI(1, ("\nCIFS - CTIME changed ")); /* BB probably do not need */
		time_buf.ChangeTime =
		    cpu_to_le64(cifs_UnixTimeToNT(attrs->ia_ctime));
	} else
		time_buf.ChangeTime = 0;

	if (set_time) {		
        /* BB handle errors better if one attribute not set 
            (such as size) but time setting works */
		time_buf.CreationTime = 0;	/* do not change */
		time_buf.Attributes = 0;	/* BB is this ignored by server?  
                        or do I have to query and reset anyway BB */
		rc = CIFSSMBSetTimes(xid, pTcon, full_path, &time_buf,
				     cifs_sb->local_nls);
	}
	cifsInode->time = 0; /* force revalidate to get attributes when needed */

	if (full_path)
		kfree(full_path);
	FreeXid(xid);
	return rc;
}

void
cifs_delete_inode(struct inode *inode)
{
	/* Note: called without the big kernel filelock - remember spinlocks! */
	cFYI(1, ("In cifs_delete_inode, inode = 0x%p\n", inode));
	/* may have to add back in when safe distributed caching of
             directories via e.g. FindNotify added */

}
