/*
 *  dir.c
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *
 */

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/ncp_fs.h>
#include <asm/segment.h>
#include <linux/errno.h>
#include <linux/locks.h>
#include "ncplib_kernel.h"

struct ncp_dirent {
	struct nw_info_struct i;
	struct nw_search_sequence s; /* given back for i */
	unsigned long f_pos;
};

static int 
ncp_dir_read(struct inode *inode, struct file *filp, char *buf, int count);

static int 
ncp_readdir(struct inode *inode, struct file *filp,
            void *dirent, filldir_t filldir);

static int
ncp_read_volume_list(struct ncp_server *server, int start_with,
		     int cache_size);

static int
ncp_do_readdir(struct ncp_server *server, struct inode *dir, int fpos,
	       int cache_size, struct ncp_dirent *entry);

static struct inode *
ncp_iget(struct inode *dir, struct nw_file_info *finfo);

static struct ncp_inode_info *
ncp_find_dir_inode(struct inode *dir, const char *name);

static int
ncp_lookup(struct inode *dir, const char *__name,
           int len, struct inode **result);

static int 
ncp_create(struct inode *dir, const char *name, int len, int mode, 
           struct inode **result);

static int 
ncp_mkdir(struct inode *dir, const char *name, int len, int mode);

static int 
ncp_rmdir(struct inode *dir, const char *name, int len);

static int
ncp_unlink(struct inode *dir, const char *name, int len);

static int
ncp_rename(struct inode *old_dir, const char *old_name, int old_len, 
           struct inode *new_dir, const char *new_name, int new_len,
           int must_be_dir);

static inline void
str_upper(char *name)
{
	while (*name)
	{
		if (*name >= 'a' && *name <= 'z')
		{
			*name -= ('a' - 'A');
		}
		name++;
	}
}

static inline void
str_lower(char *name)
{
	while (*name)
	{
		if (*name >= 'A' && *name <= 'Z')
		{
			*name += ('a' - 'A');
		}
		name ++;
	}
}

static struct file_operations ncp_dir_operations = {
        NULL,			/* lseek - default */
	ncp_dir_read,		/* read - bad */
	NULL,			/* write - bad */
	ncp_readdir,		/* readdir */
	NULL,			/* select - default */
	ncp_ioctl,		/* ioctl */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* no special release code */
	NULL			/* fsync */
};

struct inode_operations ncp_dir_inode_operations = {
	&ncp_dir_operations,	/* default directory file ops */
	ncp_create,		/* create */
	ncp_lookup,    		/* lookup */
	NULL,			/* link */
	ncp_unlink,    		/* unlink */
	NULL,			/* symlink */
	ncp_mkdir,     		/* mkdir */
	ncp_rmdir,     		/* rmdir */
	NULL,			/* mknod */
	ncp_rename,    		/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL                    /* smap */
};


/* Here we encapsulate the inode number handling that depends upon the
 * mount mode: When we mount a complete server, the memory address of
 * the ncp_inode_info is used as the inode number. When only a single
 * volume is mounted, then the DosDirNum is used as the inode
 * number. As this is unique for the complete volume, this should
 * enable the NFS exportability of a ncpfs-mounted volume.
 */

static inline int
ncp_single_volume(struct ncp_server *server)
{
	return (server->m.mounted_vol[0] != '\0');
}

inline ino_t
ncp_info_ino(struct ncp_server *server, struct ncp_inode_info *info)
{
	return ncp_single_volume(server)
		? info->finfo.i.DosDirNum : (ino_t)info;
}

static inline int
ncp_is_server_root(struct inode *inode)
{
	struct ncp_server *s = NCP_SERVER(inode);

	return (   (!ncp_single_volume(s))
		&& (inode->i_ino == ncp_info_ino(s, &(s->root))));
}

struct ncp_inode_info *
ncp_find_inode(struct inode *inode)
{
	struct ncp_server *server = NCP_SERVER(inode);
        struct ncp_inode_info *root = &(server->root);
        struct ncp_inode_info *this = root;

	ino_t ino = inode->i_ino;

        do
	{
                if (ino == ncp_info_ino(server, this))
		{
			return this;
		}
		this = this->next;
        }
	while (this != root);

	return NULL;
}
	


static int 
ncp_dir_read(struct inode *inode, struct file *filp, char *buf, int count)
{
	return -EISDIR;
}

static kdev_t             c_dev = 0;
static unsigned long      c_ino = 0;
static int                c_size;
static int                c_seen_eof;
static int                c_last_returned_index;
static struct ncp_dirent* c_entry = NULL;
static int                c_lock = 0;
static struct wait_queue *c_wait = NULL;

static inline void
ncp_lock_dircache(void)
{
	while (c_lock)
		sleep_on(&c_wait);
	c_lock = 1;
}

static inline void
ncp_unlock_dircache(void)
{
	c_lock = 0;
	wake_up(&c_wait);
}

static int
ncp_readdir(struct inode *inode, struct file *filp,
            void *dirent, filldir_t filldir)
{
	int result = 0;
	int i = 0;
        int index = 0;
	struct ncp_dirent *entry = NULL;
        struct ncp_server *server = NCP_SERVER(inode);
	struct ncp_inode_info *dir = NCP_INOP(inode);

	DDPRINTK("ncp_readdir: filp->f_pos = %d\n", (int)filp->f_pos);
	DDPRINTK("ncp_readdir: inode->i_ino = %ld, c_ino = %ld\n",
		 inode->i_ino, c_ino);

	if (!inode || !S_ISDIR(inode->i_mode))
	{
		printk("ncp_readdir: inode is NULL or not a directory\n");
		return -EBADF;
	}

	if (!ncp_conn_valid(server))
	{
		return -EIO;
	}

	ncp_lock_dircache();

	if (c_entry == NULL) 
	{
		i = sizeof (struct ncp_dirent) * NCP_READDIR_CACHE_SIZE;
		c_entry = (struct ncp_dirent *) ncp_kmalloc(i, GFP_KERNEL);
		if (c_entry == NULL)
		{
			printk("ncp_readdir: no MEMORY for cache\n");
			result = -ENOMEM;
			goto finished;
		}
	}

        if (filp->f_pos == 0)
	{
                ncp_invalid_dir_cache(inode);
		if (filldir(dirent,".",1, filp->f_pos,
			    ncp_info_ino(server, dir)) < 0)
		{
			goto finished;
		}
		filp->f_pos += 1;
        }

	if (filp->f_pos == 1)
	{
		if (filldir(dirent,"..",2, filp->f_pos,
			    ncp_info_ino(server, dir->dir)) < 0)
		{
			goto finished;
		}
		filp->f_pos += 1;
	}

	if ((inode->i_dev == c_dev) && (inode->i_ino == c_ino))
	{
		for (i = 0; i < c_size; i++)
		{
			if (filp->f_pos == c_entry[i].f_pos)
			{
                                entry = &c_entry[i];
                                c_last_returned_index = i;
                                index = i;
                                break;
			}
		}
                if ((entry == NULL) && c_seen_eof)
		{
			goto finished;
		}
	}

	if (entry == NULL)
	{
		int entries;
		DDPRINTK("ncp_readdir: Not found in cache.\n");

		if (ncp_is_server_root(inode))
		{
			entries = ncp_read_volume_list(server, filp->f_pos,
						       NCP_READDIR_CACHE_SIZE);
			DPRINTK("ncp_read_volume_list returned %d\n", entries);

		}
		else
		{
			entries = ncp_do_readdir(server, inode, filp->f_pos,
						 NCP_READDIR_CACHE_SIZE,
						 c_entry);
			DPRINTK("ncp_readdir returned %d\n", entries);
		}

		if (entries < 0)
		{
			c_dev = 0;
			c_ino = 0;
			result = entries;
			goto finished;
		}

		if (entries > 0)
		{
                        c_seen_eof = (entries < NCP_READDIR_CACHE_SIZE);
			c_dev  = inode->i_dev;
			c_ino  = inode->i_ino;
			c_size = entries;
			entry = c_entry;
                        c_last_returned_index = 0;
                        index = 0;

			for (i = 0; i < c_size; i++)
			{
				str_lower(c_entry[i].i.entryName);
			}
		}
	}

        if (entry == NULL)
	{
                /* Nothing found, even from a ncp call */
		goto finished;
        }

        while (index < c_size)
	{
		ino_t ino;

		if (ncp_single_volume(server))
		{
			ino = (ino_t)(entry->i.DosDirNum);
		}
		else
		{
			/* For getwd() we have to return the correct
			 * inode in d_ino if the inode is currently in
			 * use. Otherwise the inode number does not
			 * matter. (You can argue a lot about this..) */
			struct ncp_inode_info *ino_info;
			ino_info = ncp_find_dir_inode(inode,
						      entry->i.entryName);

			/* Some programs seem to be confused about a
			 * zero inode number, so we set it to one.
			 * Thanks to Gordon Chaffee for this one. */
			if (ino_info == NULL)
			{
				ino_info = (struct ncp_inode_info *) 1;
			}
			ino = (ino_t)(ino_info);
		}

		DDPRINTK("ncp_readdir: entry->path= %s\n", entry->i.entryName);
		DDPRINTK("ncp_readdir: entry->f_pos = %ld\n", entry->f_pos);

                if (filldir(dirent, entry->i.entryName, entry->i.nameLen,
                            entry->f_pos, ino) < 0)
		{
			break;
                }

		if (   (inode->i_dev != c_dev)
		    || (inode->i_ino != c_ino)
		    || (entry->f_pos != filp->f_pos))
		{
			/* Someone has destroyed the cache while we slept
			   in filldir */
			break;
		}
                filp->f_pos += 1;
                index += 1;
                entry += 1;
	}
 finished:
	ncp_unlock_dircache();
	return result;
}

static int
ncp_read_volume_list(struct ncp_server *server, int fpos, int cache_size)
{
	struct ncp_dirent *entry = c_entry;

	int total_count = 2;
	int i;

#if 1
	if (fpos < 2)
	{
		printk("OOPS, we expect fpos >= 2");
		fpos = 2;
	}
#endif

	for (i=0; i<NCP_NUMBER_OF_VOLUMES; i++)
	{
		struct ncp_volume_info info;

		if (ncp_get_volume_info_with_number(server, i, &info) != 0)
		{
			return (total_count - fpos);
		}

		if (strlen(info.volume_name) > 0)
		{
			if (total_count < fpos)
			{
				DPRINTK("ncp_read_volumes: skipped vol: %s\n",
					info.volume_name);
			}
			else if (total_count >= fpos + cache_size)
			{
				return (total_count - fpos);
			}
			else
			{
				DPRINTK("ncp_read_volumes: found vol: %s\n",
					info.volume_name);

				if (ncp_lookup_volume(server,
						      info.volume_name,
						      &(entry->i)) != 0)
				{
					DPRINTK("ncpfs: could not lookup vol "
						"%s\n", info.volume_name);
					continue;
				}

				entry->f_pos = total_count;
				entry += 1;
			}
			total_count += 1;
		}
	}
	return (total_count - fpos);
}

static int
ncp_do_readdir(struct ncp_server *server, struct inode *dir, int fpos,
	       int cache_size, struct ncp_dirent *entry)
{
	static struct nw_search_sequence seq;
	static struct inode *last_dir;
	static int total_count;

#if 1
	if (fpos < 2)
	{
		printk("OOPS, we expect fpos >= 2");
		fpos = 2;
	}
#endif
	DPRINTK("ncp_do_readdir: fpos = %d\n", fpos);

	if (fpos == 2)
	{
		last_dir = NULL;
		total_count = 2;
	}

	if ((fpos != total_count) || (dir != last_dir))
	{
		total_count = 2;
		last_dir = dir;

		DPRINTK("ncp_do_readdir: re-used seq for %s\n",
			NCP_ISTRUCT(dir)->entryName);

		if (ncp_initialize_search(server, NCP_ISTRUCT(dir), &seq)!=0)
		{
			DPRINTK("ncp_init_search failed\n");
			return total_count - fpos;
		}
	}

	while (total_count < fpos + cache_size)
	{
		if (ncp_search_for_file_or_subdir(server, &seq,
						  &(entry->i)) != 0)
		{
			return total_count - fpos;
		}

		if (total_count < fpos)
		{
			DPRINTK("ncp_do_readdir: skipped file: %s\n",
				entry->i.entryName);
		}
		else
		{
			DDPRINTK("ncp_do_r: file: %s, f_pos=%d,total_count=%d",
				 entry->i.entryName, fpos, total_count);
			entry->s = seq;
			entry->f_pos = total_count;
			entry += 1;
		}
		total_count += 1;
	}
	return (total_count - fpos);
}

void
ncp_init_dir_cache(void)
{
	c_dev   = 0;
        c_ino   = 0;
        c_entry = NULL;
}

void
ncp_invalid_dir_cache(struct inode *ino)
{
	if ((ino->i_dev == c_dev) && (ino->i_ino == c_ino))
	{
		c_dev = 0;
                c_ino = 0;
                c_seen_eof = 0;
        }
}

void
ncp_free_dir_cache(void)
{
        DPRINTK("ncp_free_dir_cache: enter\n");
        
        if (c_entry == NULL)
	{
                return;
	}

        ncp_kfree_s(c_entry,
                    sizeof(struct ncp_dirent) * NCP_READDIR_CACHE_SIZE);
        c_entry = NULL;

        DPRINTK("ncp_free_dir_cache: exit\n");
}


static struct inode *
ncp_iget(struct inode *dir, struct nw_file_info *finfo)
{
	struct inode *inode;
        struct ncp_inode_info *new_inode_info;
        struct ncp_inode_info *root;

	if (dir == NULL)
	{
		printk("ncp_iget: dir is NULL\n");
		return NULL;
	}

	if (finfo == NULL)
	{
		printk("ncp_iget: finfo is NULL\n");
		return NULL;
	}

        new_inode_info = ncp_kmalloc(sizeof(struct ncp_inode_info),
                                     GFP_KERNEL);

        if (new_inode_info == NULL)
	{
                printk("ncp_iget: could not alloc mem for %s\n",
		       finfo->i.entryName);
                return NULL;
        }

        new_inode_info->state = NCP_INODE_LOOKED_UP;
        new_inode_info->nused = 0;
        new_inode_info->dir   = NCP_INOP(dir);
        new_inode_info->finfo = *finfo;

        NCP_INOP(dir)->nused += 1;

        /* We have to link the new inode_info into the doubly linked
           list of inode_infos to make a complete linear search
           possible. */

        root = &(NCP_SERVER(dir)->root);

        new_inode_info->prev = root;
        new_inode_info->next = root->next;
        root->next->prev = new_inode_info;
        root->next = new_inode_info;
        
	if (!(inode = iget(dir->i_sb, ncp_info_ino(NCP_SERVER(dir),
						   new_inode_info))))
	{
		printk("ncp_iget: iget failed!");
		return NULL;
	}

	return inode;
}

void
ncp_free_inode_info(struct ncp_inode_info *i)
{
        if (i == NULL)
	{
                printk("ncp_free_inode: i == NULL\n");
                return;
        }

        i->state = NCP_INODE_CACHED;
        while ((i->nused == 0) && (i->state == NCP_INODE_CACHED))
	{
                struct ncp_inode_info *dir = i->dir;

                i->next->prev = i->prev;
                i->prev->next = i->next;

		DDPRINTK("ncp_free_inode_info: freeing %s\n",
			 i->finfo.i.entryName);

                ncp_kfree_s(i, sizeof(struct ncp_inode_info));

                if (dir == i) return;

                (dir->nused)--;
                i = dir;
        }
}
        
void
ncp_init_root(struct ncp_server *server)
{
        struct ncp_inode_info *root = &(server->root);
	struct nw_info_struct *i = &(root->finfo.i);
	unsigned short dummy;

	DPRINTK("ncp_init_root: server %s\n", server->m.server_name);
	DPRINTK("ncp_init_root: i = %x\n", (int)i);

        root->finfo.opened = 0;
	i->attributes  = aDIR;
	i->dataStreamSize = 1024;
	i->DosDirNum = 0;
	i->volNumber = NCP_NUMBER_OF_VOLUMES+1;	/* illegal volnum */
	ncp_date_unix2dos(0, &(i->creationTime), &(i->creationDate));
	ncp_date_unix2dos(0, &(i->modifyTime), &(i->modifyDate));
	ncp_date_unix2dos(0, &dummy, &(i->lastAccessDate));
	i->nameLen = 0;
	i->entryName[0] = '\0';

        root->state = NCP_INODE_LOOKED_UP;
        root->nused = 1;
        root->dir   = root;
        root->next = root->prev = root;
        return;
}

int
ncp_conn_logged_in(struct ncp_server *server)
{
	if (server->m.mounted_vol[0] == '\0')
	{
		return 0;
	}

	str_upper(server->m.mounted_vol);
	if (ncp_lookup_volume(server, server->m.mounted_vol,
			      &(server->root.finfo.i)) != 0)
	{
		return -ENOENT;
	}
	str_lower(server->root.finfo.i.entryName);

	return 0;
}

void
ncp_free_all_inodes(struct ncp_server *server)
{
        /* Here nothing should be to do. I do not know whether it's
           better to leave some memory allocated or be stuck in an
           endless loop */
#if 1
        struct ncp_inode_info *root = &(server->root);

        if (root->next != root)
	{
                printk("ncp_free_all_inodes: INODES LEFT!!!\n");
        }

        while (root->next != root)
	{
                printk("ncp_free_all_inodes: freeing inode\n");
                ncp_free_inode_info(root->next);
                /* In case we have an endless loop.. */
                schedule();
        }
#endif        
        
        return;
}

/* We will search the inode that belongs to this name, currently by a
   complete linear search through the inodes belonging to this
   filesystem. This has to be fixed. */
static struct ncp_inode_info *
ncp_find_dir_inode(struct inode *dir, const char *name)
{
	struct ncp_server *server = NCP_SERVER(dir);
	struct nw_info_struct *dir_info = NCP_ISTRUCT(dir);
        struct ncp_inode_info *result = &(server->root);

        if (name == NULL)
	{
                return NULL;
	}

        do
	{
		if (   (result->dir->finfo.i.DosDirNum == dir_info->DosDirNum)
		    && (result->dir->finfo.i.volNumber == dir_info->volNumber)
		    && (strcmp(result->finfo.i.entryName, name) == 0)
		    /* The root dir is never looked up using this
		     * routine.  Without the following test a root
		     * directory 'sys' in a volume named 'sys' could
		     * never be looked up, because
		     * server->root->dir==server->root. */
		    && (result != &(server->root)))
		{
                        return result;
		}
                result = result->next;

        }
	while (result != &(server->root));

        return NULL;
}

static int 
ncp_lookup(struct inode *dir, const char *__name, int len,
           struct inode **result)
{
	struct nw_file_info finfo;
	struct ncp_server *server;
	struct ncp_inode_info *result_info;
	int found_in_cache;
	char name[len+1];

	*result = NULL;

	if (!dir || !S_ISDIR(dir->i_mode))
	{
		printk("ncp_lookup: inode is NULL or not a directory.\n");
		iput(dir);
		return -ENOENT;
	}

	server = NCP_SERVER(dir);

	if (!ncp_conn_valid(server))
	{
		iput(dir);
		return -EIO;
	}

	DPRINTK("ncp_lookup: %s, len %d\n", __name, len);

	/* Fast cheat for . */
	if (len == 0 || (len == 1 && __name[0] == '.'))
	{
		*result = dir;
		return 0;
	}

	/* ..and for .. */
	if (len == 2 && __name[0] == '.' && __name[1] == '.')
	{
		struct ncp_inode_info *parent = NCP_INOP(dir)->dir;

		if (parent->state == NCP_INODE_CACHED)
		{
			parent->state = NCP_INODE_LOOKED_UP;
		}

		*result = iget(dir->i_sb, ncp_info_ino(server, parent));
		iput(dir);
		if (*result == 0)
		{
			return -EACCES;
		}
		else
		{
			return 0;
		}
	}

	memcpy(name, __name, len);
	name[len] = 0;
	lock_super(dir->i_sb);
	result_info = ncp_find_dir_inode(dir, name);

        if (result_info != 0)
	{
                if (result_info->state == NCP_INODE_CACHED)
		{
                        result_info->state = NCP_INODE_LOOKED_UP;
		}

                /* Here we convert the inode_info address into an
                   inode number */

                *result = iget(dir->i_sb, ncp_info_ino(server, result_info));
		unlock_super(dir->i_sb);
                iput(dir);

                if (*result == NULL)
		{
                        return -EACCES;
                }

		return 0;
        }

        /* If the file is in the dir cache, we do not have to ask the
           server. */

        found_in_cache = 0;
	ncp_lock_dircache();

        if ((dir->i_dev == c_dev) && (dir->i_ino == c_ino))
	{
                int first = c_last_returned_index;
                int i;

                i = first;
                do
		{
                        DDPRINTK("ncp_lookup: trying index: %d, name: %s\n",
				 i, c_entry[i].i.entryName);

                        if (strcmp(c_entry[i].i.entryName, name) == 0)
			{
                                DPRINTK("ncp_lookup: found in cache!\n");
				finfo.i = c_entry[i].i;
				found_in_cache = 1;
				break;
                        }
                        i = (i + 1) % c_size;
                }
		while (i != first);
        }
	ncp_unlock_dircache();

        if (found_in_cache == 0)
	{
		int res;
		str_upper(name);

		DDPRINTK("ncp_lookup: do_lookup on %s/%s\n",
			 NCP_ISTRUCT(dir)->entryName, name);

		if (ncp_is_server_root(dir))
		{
			res = ncp_lookup_volume(server, name, &(finfo.i));
		}
		else
		{
			res = ncp_obtain_info(server,
					      NCP_ISTRUCT(dir)->volNumber,
					      NCP_ISTRUCT(dir)->DosDirNum,
					      name, &(finfo.i));
		}
		if (res != 0)
		{
			unlock_super(dir->i_sb);
                        iput(dir);
                        return -ENOENT;
                }
        }

	finfo.opened = 0;
	str_lower(finfo.i.entryName);

	if (!(*result = ncp_iget(dir, &finfo)))
	{
		unlock_super(dir->i_sb);
		iput(dir);
		return -EACCES;
	}

	unlock_super(dir->i_sb);
	iput(dir);
	return 0;
}

static int 
ncp_create(struct inode *dir, const char *name, int len, int mode,
           struct inode **result)
{
	struct nw_file_info finfo;
	__u8 _name[len+1];

	*result = NULL;

	if (!dir || !S_ISDIR(dir->i_mode))
	{
		printk("ncp_create: inode is NULL or not a directory\n");
		iput(dir);
		return -ENOENT;
	}
	if (!ncp_conn_valid(NCP_SERVER(dir)))
	{
		iput(dir);
		return -EIO;
	}

	strncpy(_name, name, len);
	_name[len] = '\0';
	str_upper(_name);

	lock_super(dir->i_sb);
	if (ncp_open_create_file_or_subdir(NCP_SERVER(dir),
					   NCP_ISTRUCT(dir), _name,
					   OC_MODE_CREATE|OC_MODE_OPEN|
					   OC_MODE_REPLACE,
					   0, AR_READ|AR_WRITE,
					   &finfo) != 0)
	{
		unlock_super(dir->i_sb);
		iput(dir);
		return -EACCES;
	}

	ncp_invalid_dir_cache(dir);

	str_lower(finfo.i.entryName);
	finfo.access = O_RDWR;

	if (!(*result = ncp_iget(dir, &finfo)) < 0)
	{
		ncp_close_file(NCP_SERVER(dir), finfo.file_handle);
		unlock_super(dir->i_sb);
		iput(dir);
		return -EINVAL;
	}

	unlock_super(dir->i_sb);
	iput(dir);
	return 0;	
}

static int
ncp_mkdir(struct inode *dir, const char *name, int len, int mode)
{
	int error;
	struct nw_file_info new_dir;
	__u8 _name[len+1];

	if (   (name[0] == '.')
	    && (   (len == 1)
		|| (   (len == 2)
		    && (name[1] == '.'))))
	{
		iput(dir);
		return -EEXIST;
	}

	strncpy(_name, name, len);
	_name[len] = '\0';
	str_upper(_name);

	if (!dir || !S_ISDIR(dir->i_mode))
	{
		printk("ncp_mkdir: inode is NULL or not a directory\n");
		iput(dir);
		return -ENOENT;
	}
	if (!ncp_conn_valid(NCP_SERVER(dir)))
	{
		iput(dir);
		return -EIO;
	}

	if (ncp_open_create_file_or_subdir(NCP_SERVER(dir),
					   NCP_ISTRUCT(dir), _name,
					   OC_MODE_CREATE, aDIR, 0xffff,
					   &new_dir) != 0)
	{
		error = -EACCES;
	}
	else
	{
		error = 0;
                ncp_invalid_dir_cache(dir);
        }

	iput(dir);
	return error;
}

static int
ncp_rmdir(struct inode *dir, const char *name, int len)
{
	int error;
	__u8 _name[len+1];

	if (!dir || !S_ISDIR(dir->i_mode))
	{
		printk("ncp_rmdir: inode is NULL or not a directory\n");
		iput(dir);
		return -ENOENT;
	}
	if (!ncp_conn_valid(NCP_SERVER(dir)))
	{
		iput(dir);
		return -EIO;
	}
        if (ncp_find_dir_inode(dir, name) != NULL)
	{
		iput(dir);
                error = -EBUSY;
        }
	else
	{

		strncpy(_name, name, len);
		_name[len] = '\0';
		str_upper(_name);

                if ((error = ncp_del_file_or_subdir(NCP_SERVER(dir),
						    NCP_ISTRUCT(dir),
						    _name)) == 0)
		{
                        ncp_invalid_dir_cache(dir);
		}
		else
		{
			error = -EACCES;
		}
        }
	iput(dir);
	return error;
}

static int
ncp_unlink(struct inode *dir, const char *name, int len)
{
	int error;
	__u8 _name[len+1];

	if (!dir || !S_ISDIR(dir->i_mode))
	{
		printk("ncp_unlink: inode is NULL or not a directory\n");
		iput(dir);
		return -ENOENT;
	}
	if (!ncp_conn_valid(NCP_SERVER(dir)))
	{
		iput(dir);
		return -EIO;
	}
        if (ncp_find_dir_inode(dir, name) != NULL)
	{
		iput(dir);
                error = -EBUSY;
        }
	else
	{
		strncpy(_name, name, len);
		_name[len] = '\0';
		str_upper(_name);

                if ((error = ncp_del_file_or_subdir(NCP_SERVER(dir),
						    NCP_ISTRUCT(dir),
						    _name)) == 0)
		{
                        ncp_invalid_dir_cache(dir);
		}
		else
		{
			error = -EACCES;
		}
        }
	iput(dir);
	return error;
}

static int
ncp_rename(struct inode *old_dir, const char *old_name, int old_len,
           struct inode *new_dir, const char *new_name, int new_len,
           int must_be_dir)
{
	int res;
	char _old_name[old_len+1];
	char _new_name[new_len+1];

	if (!old_dir || !S_ISDIR(old_dir->i_mode))
	{
		printk("ncp_rename: old inode is NULL or not a directory\n");
                res = -ENOENT;
                goto finished;
	}

	if (!ncp_conn_valid(NCP_SERVER(old_dir)))
	{
		res = -EIO;
		goto finished;
	}

	if (!new_dir || !S_ISDIR(new_dir->i_mode))
	{
		printk("ncp_rename: new inode is NULL or not a directory\n");
                res = -ENOENT;
                goto finished;
	}

        if (   (ncp_find_dir_inode(old_dir, old_name) != NULL)
            || (ncp_find_dir_inode(new_dir, new_name) != NULL))
	{
                res = -EBUSY;
                goto finished;
        }

	strncpy(_old_name, old_name, old_len);
	_old_name[old_len] = '\0';
	str_upper(_old_name);

	strncpy(_new_name, new_name, new_len);
	_new_name[new_len] = '\0';
	str_upper(_new_name);

	res = ncp_ren_or_mov_file_or_subdir(NCP_SERVER(old_dir),
					    NCP_ISTRUCT(old_dir), _old_name,
					    NCP_ISTRUCT(new_dir), _new_name);

        if (res == 0)
	{
                ncp_invalid_dir_cache(old_dir);
                ncp_invalid_dir_cache(new_dir);
        }
	else
	{
		res = -EACCES;
	}
	
 finished:
	iput(old_dir); 
	iput(new_dir);
	return res;
}

/* The following routines are taken directly from msdos-fs */

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

int
ncp_date_dos2unix(unsigned short time,unsigned short date)
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
void
ncp_date_unix2dos(int unix_date,unsigned short *time, unsigned short *date)
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
