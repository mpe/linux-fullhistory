/*
 *  dir.c
 *
 *  Copyright (C) 1995 by Paal-Kr. Engstad and Volker Lendecke
 *
 */

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/smb_fs.h>
#include <asm/segment.h>
#include <linux/errno.h>

#define NAME_OFFSET(de) ((int) ((de)->d_name - (char *) (de)))
#define ROUND_UP(x) (((x)+3) & ~3)

static int 
smb_dir_read(struct inode *inode, struct file *filp, char *buf, int count);

static int 
smb_readdir(struct inode *inode, struct file *filp,
            void *dirent, filldir_t filldir);

static int 
get_pname(struct inode *dir, const char *name, int len,
          char **res_path, int *res_len);

static int
get_pname_static(struct inode *dir, const char *name, int len,
                 char *path, int *res_len);

static struct inode *
smb_iget(struct inode *dir, char *path, struct smb_dirent *finfo);

static void 
put_pname(char *path);

static struct smb_inode_info *
smb_find_inode(struct smb_server *server, const char *path);

static int
smb_lookup(struct inode *dir, const char *__name,
           int len, struct inode **result);

static int 
smb_create(struct inode *dir, const char *name, int len, int mode, 
           struct inode **result);

static int 
smb_mkdir(struct inode *dir, const char *name, int len, int mode);

static int 
smb_rmdir(struct inode *dir, const char *name, int len);

static int
smb_unlink(struct inode *dir, const char *name, int len);

static int
smb_rename(struct inode *old_dir, const char *old_name, int old_len, 
           struct inode *new_dir, const char *new_name, int new_len,
           int must_be_dir);

static inline void str_upper(char *name)
{
	while (*name) {
		if (*name >= 'a' && *name <= 'z')
			*name -= ('a' - 'A');
		name++;
	}
}

static inline void str_lower(char *name)
{
	while (*name) {
		if (*name >= 'A' && *name <= 'Z')
			*name += ('a' - 'A');
		name ++;
	}
}

static struct file_operations smb_dir_operations = {
        NULL,			/* lseek - default */
	smb_dir_read,		/* read - bad */
	NULL,			/* write - bad */
	smb_readdir,		/* readdir */
	NULL,			/* select - default */
	smb_ioctl,		/* ioctl - default */
	NULL,                   /* mmap */
	NULL,			/* no special open code */
	NULL,			/* no special release code */
	NULL			/* fsync */
};

struct inode_operations smb_dir_inode_operations = 
{
	&smb_dir_operations,	/* default directory file ops */
	smb_create,		/* create */
	smb_lookup,    		/* lookup */
	NULL,			/* link */
	smb_unlink,    		/* unlink */
	NULL,			/* symlink */
	smb_mkdir,     		/* mkdir */
	smb_rmdir,     		/* rmdir */
	NULL,			/* mknod */
	smb_rename,    		/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL                    /* smap */
};


static int 
smb_dir_read(struct inode *inode, struct file *filp, char *buf, int count)
{
	return -EISDIR;
}

/*
 * Description:
 *  smb_readdir provides a listing in the form of filling the dirent structure.
 *  Note that dirent resides in the user space. This is to support reading of a
 *  directory "stream". 
 * Arguments:
 *  inode   ---  Pointer to to the directory.
 *  filp    ---  The directory stream. (filp->f_pos indicates
 *               position in the stream.)
 *  dirent  ---  Will hold count directory entries. (Is in user space.)
 *  count   ---  Number of entries to be read. Should indicate the total 
 *               buffer space available for filling with dirents.
 * Return values:
 *     < 0     ---  An error occurred (linux/errno.h).
 *     = 0     ---
 *     > 0     ---  Success, amount of bytes written to dirent.
 * Notes:
 *     Since we want to reduce directory lookups we revert into a
 *     dircache. It is taken rather directly out of the nfs_readdir.
 */

/* In smbfs, we have unique inodes across all mounted filesystems, for
   all inodes that are in memory. That's why it's enough to index the
   directory cache by the inode number. */

static unsigned long      c_ino = 0;
static int                c_size;
static int                c_seen_eof;
static int                c_last_returned_index;
static struct smb_dirent* c_entry = NULL;

static int
smb_readdir(struct inode *inode, struct file *filp,
            void *dirent, filldir_t filldir)
{
	int result, i = 0;
        int index = 0;
	struct smb_dirent *entry = NULL;
        struct smb_server *server = SMB_SERVER(inode);

	DDPRINTK("smb_readdir: filp->f_pos = %d\n", (int)filp->f_pos);
	DDPRINTK("smb_readdir: inode->i_ino = %ld, c_ino = %ld\n",
		 inode->i_ino, c_ino);

	if (!inode || !S_ISDIR(inode->i_mode)) {
		printk("smb_readdir: inode is NULL or not a directory\n");
		return -EBADF;
	}

	if (c_entry == NULL) 
	{
		i = sizeof (struct smb_dirent) * SMB_READDIR_CACHE_SIZE;
		c_entry = (struct smb_dirent *) smb_kmalloc(i, GFP_KERNEL);
		if (c_entry == NULL) {
			printk("smb_readdir: no MEMORY for cache\n");
			return -ENOMEM;
		}
		for (i = 0; i < SMB_READDIR_CACHE_SIZE; i++) {
			c_entry[i].path =
                                (char *) smb_kmalloc(SMB_MAXNAMELEN + 1,
                                                     GFP_KERNEL);
                        if (c_entry[i].path == NULL) {
                                DPRINTK("smb_readdir: could not alloc path\n");
				while (--i>=0)
					kfree(c_entry[i].path);
				kfree(c_entry);
				c_entry = NULL;
				return -ENOMEM;
                        }
                }
	}

        if (filp->f_pos == 0) {
                smb_invalid_dir_cache(inode->i_ino);
        }

	if (inode->i_ino == c_ino) {
		for (i = 0; i < c_size; i++) {
			if (filp->f_pos == c_entry[i].f_pos) {
                                entry = &c_entry[i];
                                c_last_returned_index = i;
                                index = i;
                                break;
			}
		}
                if ((entry == NULL) && c_seen_eof)
                        return 0;
	}

	if (entry == NULL) {
		DPRINTK("smb_readdir: Not found in cache.\n");
		result = smb_proc_readdir(server, inode,
                                          filp->f_pos, SMB_READDIR_CACHE_SIZE,
                                          c_entry);

		if (result < 0) {
			c_ino = 0;
			return result;
		}

		if (result > 0) {
                        c_seen_eof = (result < SMB_READDIR_CACHE_SIZE);
			c_ino  = inode->i_ino;
			c_size = result;
			entry = c_entry;
                        c_last_returned_index = 0;
                        index = 0;
                        for (i = 0; i < c_size; i++) {

                                switch (server->case_handling) 
                                {
                                case CASE_UPPER:
                                        str_upper(c_entry[i].path); break;
                                case CASE_LOWER:
                                        str_lower(c_entry[i].path); break;
                                case CASE_DEFAULT:
                                        break;
                                }
                        }
		}
	}

        if (entry == NULL) {
                /* Nothing found, even from a smb call */
                return 0;
        }

        while (index < c_size) {

                /* We found it.  For getwd(), we have to return the
                   correct inode in d_ino if the inode is currently in
                   use. Otherwise the inode number does not
                   matter. (You can argue a lot about this..) */ 

                int path_len;
                int len;
                struct smb_inode_info *ino_info;
                char complete_path[SMB_MAXPATHLEN];

		len = strlen(entry->path);
                if ((result = get_pname_static(inode, entry->path, len,
                                               complete_path,
                                               &path_len)) < 0)
                        return result;

                ino_info = smb_find_inode(server, complete_path);

                /* Some programs seem to be confused about a zero
                   inode number, so we set it to one.  Thanks to
                   Gordon Chaffee for this one. */
                if (ino_info == NULL) {
                        ino_info = (struct smb_inode_info *) 1;
                }

		DDPRINTK("smb_readdir: entry->path = %s\n", entry->path);
		DDPRINTK("smb_readdir: entry->f_pos = %ld\n", entry->f_pos);

                if (filldir(dirent, entry->path, len,
                            entry->f_pos, (ino_t)ino_info) < 0) {
                        break;
                }

                if (   (inode->i_ino != c_ino)
                    || (entry->f_pos != filp->f_pos)) {
                        /* Someone has destroyed the cache while we slept
                           in filldir */
                        break;
                }
                filp->f_pos += 1;
                index += 1;
                entry += 1;
	}
	return 0;
}

void
smb_init_dir_cache(void)
{
        c_ino   = 0;
        c_entry = NULL;
}

void
smb_invalid_dir_cache(unsigned long ino)
{
	if (ino == c_ino) {
                c_ino = 0;
                c_seen_eof = 0;
        }
}

void
smb_free_dir_cache(void)
{
        int i;

        DPRINTK("smb_free_dir_cache: enter\n");
        
        if (c_entry == NULL)
                return;

        for (i = 0; i < SMB_READDIR_CACHE_SIZE; i++) {
                smb_kfree_s(c_entry[i].path, NAME_MAX + 1);
        }

        smb_kfree_s(c_entry,
                    sizeof(struct smb_dirent) * SMB_READDIR_CACHE_SIZE);
        c_entry = NULL;

        DPRINTK("smb_free_dir_cache: exit\n");
}


/* get_pname_static: it expects the res_path to be a preallocated
   string of len SMB_MAXPATHLEN. */

static int
get_pname_static(struct inode *dir, const char *name, int len,
                 char *path, int *res_len)
{
        char *parentname = SMB_INOP(dir)->finfo.path;
        int   parentlen  = SMB_INOP(dir)->finfo.len;

#if 1
        if (parentlen != strlen(parentname)) {
                printk("get_pname: parent->finfo.len = %d instead of %d\n",
                       parentlen, strlen(parentname));
                parentlen = strlen(parentname);
        }
	
#endif
	DDPRINTK("get_pname_static: parentname = %s, len = %d\n",
                 parentname, parentlen);
	
        if (len > SMB_MAXNAMELEN) {
                return -ENAMETOOLONG;
        }

	/* Fast cheat for . */
	if (len == 0 || (len == 1 && name[0] == '.')) {

		memcpy(path, parentname, parentlen + 1);
		*res_len = parentlen;
		return 0;
	}
	
	/* Hmm, what about .. ? */
	if (len == 2 && name[0] == '.' && name[1] == '.') {

		char *pos = strrchr(parentname, '\\');

                if (   (pos == NULL)
                    && (parentlen == 0)) {

                        /* We're at the top */

                        path[0] = '\\';
                        path[1] = '\0';
                        *res_len  = 2;
                        return 0;
                }
                        
                
		if (pos == NULL) {
			printk("smb_make_name: Bad parent SMB-name: %s",
                               parentname);
			return -ENODATA;
		}
		
		len = pos - parentname;

	        memcpy(path, parentname, len);
		path[len] = '\0';
	}
	else
	{
		if (len + parentlen + 2 > SMB_MAXPATHLEN) 
			return -ENAMETOOLONG;
				
		memcpy(path, parentname, parentlen);
		path[parentlen] = '\\';
		memcpy(path + parentlen + 1, name, len);
		path[parentlen + 1 + len] = '\0';
		len = parentlen + len + 1;
	}

	switch (SMB_SERVER(dir)->case_handling) 
	{
        case CASE_UPPER: 
                str_upper(path); 
                break;
        case CASE_LOWER: 
                str_lower(path); 
                break;
        case CASE_DEFAULT: 
                break;
	}

	*res_len = len;

	DDPRINTK("get_pname: path = %s, *pathlen = %d\n",
                 path, *res_len);
	return 0;
}
        
static int 
get_pname(struct inode *dir, const char *name, int len,
          char **res_path, int *res_len)
{
        char result[SMB_MAXPATHLEN];
        int  result_len;
        int  res;

        if ((res = get_pname_static(dir,name,len,result,&result_len) != 0)) {
                return res;
        }

        if ((*res_path = smb_kmalloc(result_len+1, GFP_KERNEL)) == NULL) {
                printk("get_pname: Out of memory while allocating name.");
                return -ENOMEM;
        }

        strcpy(*res_path, result);
        *res_len = result_len;
        return 0;
}

static void
put_pname(char *path)
{
	smb_kfree_s(path, 0);
}

/* Insert a NEW smb_inode_info into the inode tree of our filesystem,
   under dir. The caller must assure that it's not already there. We
   assume that path is allocated for us. */

static struct inode *
smb_iget(struct inode *dir, char *path, struct smb_dirent *finfo)
{
	struct smb_dirent newent = { 0 };
	struct inode *inode;
	int error, len;
        struct smb_inode_info *new_inode_info;
        struct smb_inode_info *root;

	if (!dir) {
		printk("smb_iget: dir is NULL\n");
		return NULL;
	}

        if (!path) {
                printk("smb_iget: path is NULL\n");
                return NULL;
        }

        len = strlen(path);
        
	if (!finfo) {
		error = smb_proc_getattr(&(SMB_SBP(dir->i_sb)->s_server),
                                         path, len, &newent);
		if (error) {
			printk("smb_iget: getattr error = %d\n", -error);
			return NULL;
		}		
		finfo = &newent;
		DPRINTK("smb_iget: Read finfo:\n");
		DPRINTK("smb_iget: finfo->attr = 0x%X\n", finfo->attr);
	}

        new_inode_info = smb_kmalloc(sizeof(struct smb_inode_info),
                                     GFP_KERNEL);

        if (new_inode_info == NULL) {
                printk("smb_iget: could not alloc mem for %s\n", path);
                return NULL;
        }

        new_inode_info->state = SMB_INODE_LOOKED_UP;
        new_inode_info->nused = 0;
        new_inode_info->dir   = SMB_INOP(dir);

        new_inode_info->finfo        = *finfo;
        new_inode_info->finfo.opened = 0;
        new_inode_info->finfo.path   = path;
        new_inode_info->finfo.len    = len;

        SMB_INOP(dir)->nused += 1;

        /* We have to link the new inode_info into the doubly linked
           list of inode_infos to make a complete linear search
           possible. */

        root = &(SMB_SERVER(dir)->root);

        new_inode_info->prev = root;
        new_inode_info->next = root->next;
        root->next->prev = new_inode_info;
        root->next = new_inode_info;
        
	if (!(inode = iget(dir->i_sb, (int)new_inode_info))) {
		printk("smb_iget: iget failed!");
		return NULL;
	}

	return inode;
}

void
smb_free_inode_info(struct smb_inode_info *i)
{
        if (i == NULL) {
                printk("smb_free_inode: i == NULL\n");
                return;
        }

        i->state = SMB_INODE_CACHED;
        while ((i->nused == 0) && (i->state == SMB_INODE_CACHED)) {
                struct smb_inode_info *dir = i->dir;

                i->next->prev = i->prev;
                i->prev->next = i->next;

                smb_kfree_s(i->finfo.path, i->finfo.len+1);
                smb_kfree_s(i, sizeof(struct smb_inode_info));

                if (dir == NULL) return;

                (dir->nused)--;
                i = dir;
        }
}
        
void
smb_init_root(struct smb_server *server)
{
        struct smb_inode_info *root = &(server->root);

        root->finfo.path = server->m.root_path;
        root->finfo.len  = strlen(root->finfo.path);
        root->finfo.opened = 0;

        root->state = SMB_INODE_LOOKED_UP;
        root->nused = 1;
        root->dir   = NULL;
        root->next = root->prev = root;
        return;
}

int
smb_stat_root(struct smb_server *server)
{
        struct smb_inode_info *root = &(server->root);
        int result;

        if (root->finfo.len == 0) {
                result = smb_proc_getattr(server, "\\", 1, &(root->finfo));
        }
        else
        {
                result = smb_proc_getattr(server, 
                                          root->finfo.path, root->finfo.len,
                                          &(root->finfo));
        }
        return result;
}

void
smb_free_all_inodes(struct smb_server *server)
{
        /* Here nothing should be to do. I do not know whether it's
           better to leave some memory allocated or be stuck in an
           endless loop */
#if 1
        struct smb_inode_info *root = &(server->root);

        if (root->next != root) {
                printk("smb_free_all_inodes: INODES LEFT!!!\n");
        }

        while (root->next != root) {
                printk("smb_free_all_inodes: freeing inode\n");
                smb_free_inode_info(root->next);
                /* In case we have an endless loop.. */
                schedule();
        }
#endif        
        
        return;
}

/* This has to be called when a connection has gone down, so that all
   file-handles we got from the server are invalid */

void
smb_invalidate_all_inodes(struct smb_server *server)
{
        struct smb_inode_info *ino = &(server->root);

        do {
                ino->finfo.opened = 0;
                ino = ino->next;
        } while (ino != &(server->root));
        
        return;
}
        

/* We will search the inode that belongs to this name, currently by a
   complete linear search through the inodes belonging to this
   filesystem. This has to be fixed. */

static struct smb_inode_info *
smb_find_inode(struct smb_server *server, const char *path)
{
        struct smb_inode_info *result = &(server->root);

        if (path == NULL)
                return NULL;

        do {
                if (strcmp(result->finfo.path, path) == 0)
                        return result;
                result = result->next;

        } while (result != &(server->root));

        return NULL;
}


static int 
smb_lookup(struct inode *dir, const char *__name, int len,
           struct inode **result)
{
	char *name = NULL;
	struct smb_dirent finfo;
        struct smb_inode_info *result_info;
	int error;
        int found_in_cache;

	*result = NULL;

	if (!dir || !S_ISDIR(dir->i_mode)) {
		printk("smb_lookup: inode is NULL or not a directory.\n");
		iput(dir);
		return -ENOENT;
	}

        DDPRINTK("smb_lookup: %s\n", __name);

	/* Fast cheat for . */
	if (len == 0 || (len == 1 && __name[0] == '.')) {
		*result = dir;
		return 0;
	}

	/* Now we will have to build up an SMB filename. */
	if ((error = get_pname(dir, __name, len, &name, &len)) < 0) {
		iput(dir);
		return error;
	}

        result_info = smb_find_inode(SMB_SERVER(dir), name);

        if (result_info != 0) {

                if (result_info->state == SMB_INODE_CACHED)
                        result_info->state = SMB_INODE_LOOKED_UP;

                put_pname(name);

                /* Here we convert the inode_info address into an
                   inode number */

                *result = iget(dir->i_sb, (int)result_info);
                iput(dir);

                if (*result == NULL) {
                        return -EACCES;
                } else {
                        return 0;
                }
        }

	/* Ok, now we have made our name. We have to build a new
           smb_inode_info struct and insert it into the tree, if it is
           a name that exists on the server */

        /* If the file is in the dir cache, we do not have to ask the
           server. */

        found_in_cache = 0;
        
        if (dir->i_ino == c_ino) {
                int first = c_last_returned_index;
                int i;

                i = first;
                do {
                        DDPRINTK("smb_lookup: trying index: %d, name: %s\n",
                                i, c_entry[i].path);
                        if (strcmp(c_entry[i].path, __name) == 0) {
                                DPRINTK("smb_lookup: found in cache!\n");
                                finfo = c_entry[i];
                                finfo.path = NULL; /* It's not ours! */
                                found_in_cache = 1;
                                break;
                        }
                        i = (i + 1) % c_size;
                        DDPRINTK("smb_lookup: index %d, name %s failed\n",
                                 i, c_entry[i].path);
                } while (i != first);
        }

        if (found_in_cache == 0) {
                error = smb_proc_getattr(SMB_SERVER(dir), name, len, &finfo);
                if (error < 0) {
                        put_pname(name);
                        iput(dir);
                        return error;
                }
        }

	if (!(*result = smb_iget(dir, name, &finfo))) {
		put_pname(name);
		iput(dir);
		return -EACCES;
	}

        DDPRINTK("smb_lookup: %s => %lu\n", name, (unsigned long)result_info);
	iput(dir);
	return 0;
}

static int 
smb_create(struct inode *dir, const char *name, int len, int mode,
           struct inode **result)
{
	int error;
	char *path = NULL;
	struct smb_dirent entry;

	*result = NULL;

	if (!dir || !S_ISDIR(dir->i_mode)) {
		printk("smb_create: inode is NULL or not a directory\n");
		iput(dir);
		return -ENOENT;
	}

	/* Now we will have to build up an SMB filename. */
	if ((error = get_pname(dir, name, len, &path, &len)) < 0) {
		iput(dir);
		return error;
	}

        entry.attr  = 0;
        entry.ctime = CURRENT_TIME;
        entry.atime = CURRENT_TIME;
        entry.mtime = CURRENT_TIME;
        entry.size  = 0;

        error = smb_proc_create(SMB_SERVER(dir), path, len, &entry);
	if (error < 0) {
		put_pname(path);
		iput(dir);
		return error;
	}

        smb_invalid_dir_cache(dir->i_ino);

	if (!(*result = smb_iget(dir, path, &entry)) < 0) {
		put_pname(path);
		iput(dir);
		return error;
	}
	iput(dir);
	return 0;	
}

static int
smb_mkdir(struct inode *dir, const char *name, int len, int mode)
{
	int error;
	char path[SMB_MAXPATHLEN];

	if (!dir || !S_ISDIR(dir->i_mode)) {
		printk("smb_mkdir: inode is NULL or not a directory\n");
		iput(dir);
		return -ENOENT;
	}

	/* Now we will have to build up an SMB filename. */
	if ((error = get_pname_static(dir, name, len, path, &len)) < 0) {
		iput(dir);
		return error;
	}

	if ((error = smb_proc_mkdir(SMB_SERVER(dir), path, len)) == 0) {
                smb_invalid_dir_cache(dir->i_ino);
        }

	iput(dir);
	return error;
}

static int
smb_rmdir(struct inode *dir, const char *name, int len)
{
	int error;
        char path[SMB_MAXPATHLEN];

	if (!dir || !S_ISDIR(dir->i_mode)) {
		printk("smb_rmdir: inode is NULL or not a directory\n");
		iput(dir);
		return -ENOENT;
	}
	if ((error = get_pname_static(dir, name, len, path, &len)) < 0) {
		iput(dir);
		return error;
	}
        if (smb_find_inode(SMB_SERVER(dir), path) != NULL) {
                error = -EBUSY;
        } else {
                if ((error = smb_proc_rmdir(SMB_SERVER(dir), path, len)) == 0)
                        smb_invalid_dir_cache(dir->i_ino);
        }
	iput(dir);
	return error;
}

static int
smb_unlink(struct inode *dir, const char *name, int len)
{
	int error;
	char path[SMB_MAXPATHLEN];

	if (!dir || !S_ISDIR(dir->i_mode)) {
		printk("smb_unlink: inode is NULL or not a directory\n");
		iput(dir);
		return -ENOENT;
	}
	if ((error = get_pname_static(dir, name, len, path, &len)) < 0) {
		iput(dir);
		return error;
	}
        if (smb_find_inode(SMB_SERVER(dir), path) != NULL) {
                error = -EBUSY;
        } else {
                if ((error = smb_proc_unlink(SMB_SERVER(dir), path, len)) == 0)
                        smb_invalid_dir_cache(dir->i_ino);
        }
	iput(dir);
	return error;
}

static int
smb_rename(struct inode *old_dir, const char *old_name, int old_len,
           struct inode *new_dir, const char *new_name, int new_len,
           int must_be_dir)
{
	int res;
	char old_path[SMB_MAXPATHLEN], new_path[SMB_MAXPATHLEN];

	if (!old_dir || !S_ISDIR(old_dir->i_mode)) {
		printk("smb_rename: old inode is NULL or not a directory\n");
                res = -ENOENT;
                goto finished;
	}

	if (!new_dir || !S_ISDIR(new_dir->i_mode)) {
		printk("smb_rename: new inode is NULL or not a directory\n");
                res = -ENOENT;
                goto finished;
	}

        res = get_pname_static(old_dir, old_name, old_len, old_path, &old_len);
        if (res < 0) {
                goto finished;
	}

        res = get_pname_static(new_dir, new_name, new_len, new_path, &new_len);
	if (res < 0) {
                goto finished;
	}
	
        if (   (smb_find_inode(SMB_SERVER(old_dir), old_path) != NULL)
            || (smb_find_inode(SMB_SERVER(new_dir), new_path) != NULL)) {
                res = -EBUSY;
                goto finished;
        }

	res = smb_proc_mv(SMB_SERVER(old_dir), old_path, old_len,
                          new_path, new_len);

	if (res == -EEXIST) {
		int res1;
		res1 = smb_proc_unlink(SMB_SERVER(old_dir), new_path, new_len);
		if (res1 == 0) {
			res = smb_proc_mv(SMB_SERVER(old_dir), old_path,
					  old_len, new_path, new_len);
		}
	}

        if (res == 0) {
                smb_invalid_dir_cache(old_dir->i_ino);
                smb_invalid_dir_cache(new_dir->i_ino);
        }		
	
 finished:
	iput(old_dir); 
	iput(new_dir);
	return res;
}
