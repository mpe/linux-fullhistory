/*
 *  ncplib_kernel.h
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *  Modified for big endian by J.F. Chadima and David S. Miller
 *  Modified 1997 Peter Waltenberg, Bill Hawes, David Woodhouse for 2.1 dcache
 *  Modified 1998 Wolfram Pienkoss for NLS
 *  Modified 1999 Wolfram Pienkoss for directory caching
 *
 */

#ifndef _NCPLIB_H
#define _NCPLIB_H

#include <linux/config.h>

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/pagemap.h>

#include <asm/uaccess.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>
#include <asm/string.h>

#ifdef CONFIG_NCPFS_NLS
#include <linux/nls.h>
#endif

#include <linux/ncp_fs.h>

#define NCP_MIN_SYMLINK_SIZE	8
#define NCP_MAX_SYMLINK_SIZE	512

#define NCP_BLOCK_SIZE		512

int ncp_negotiate_buffersize(struct ncp_server *, int, int *);
int ncp_negotiate_size_and_options(struct ncp_server *server, int size,
  			  int options, int *ret_size, int *ret_options);
int ncp_get_volume_info_with_number(struct ncp_server *, int,
				struct ncp_volume_info *);
int ncp_close_file(struct ncp_server *, const char *);
static inline int ncp_read_bounce_size(__u32 size) {
	return sizeof(struct ncp_reply_header) + 2 + 2 + size + 8;
};
int ncp_read_bounce(struct ncp_server *, const char *, __u32, __u16, 
		char *, int *, void* bounce, __u32 bouncelen);
int ncp_read_kernel(struct ncp_server *, const char *, __u32, __u16, 
		char *, int *);
int ncp_write_kernel(struct ncp_server *, const char *, __u32, __u16,
		const char *, int *);

int ncp_obtain_info(struct ncp_server *server, struct inode *, char *,
		struct nw_info_struct *target);
int ncp_lookup_volume(struct ncp_server *, char *, struct nw_info_struct *);
int ncp_modify_file_or_subdir_dos_info(struct ncp_server *, struct inode *,
	 __u32, const struct nw_modify_dos_info *info);
int ncp_modify_file_or_subdir_dos_info_path(struct ncp_server *, struct inode *,
	 const char* path, __u32, const struct nw_modify_dos_info *info);

int ncp_del_file_or_subdir2(struct ncp_server *, struct dentry*);
int ncp_del_file_or_subdir(struct ncp_server *, struct inode *, char *);
int ncp_open_create_file_or_subdir(struct ncp_server *, struct inode *, char *,
				int, __u32, int, struct ncp_entry_info *);

int ncp_initialize_search(struct ncp_server *, struct inode *,
		      struct nw_search_sequence *target);
int ncp_search_for_file_or_subdir(struct ncp_server *server,
			      struct nw_search_sequence *seq,
			      struct nw_info_struct *target);

int ncp_ren_or_mov_file_or_subdir(struct ncp_server *server,
			      struct inode *, char *, struct inode *, char *);


int
ncp_LogPhysicalRecord(struct ncp_server *server,
		      const char *file_id, __u8 locktype,
		      __u32 offset, __u32 length, __u16 timeout);

#ifdef CONFIG_NCPFS_IOCTL_LOCKING
int
ncp_ClearPhysicalRecord(struct ncp_server *server,
			const char *file_id,
			__u32 offset, __u32 length);
#endif	/* CONFIG_NCPFS_IOCTL_LOCKING */

#ifdef CONFIG_NCPFS_MOUNT_SUBDIR
int
ncp_mount_subdir(struct ncp_server *, struct nw_info_struct *,
			__u8, __u8, __u32);
#endif	/* CONFIG_NCPFS_MOUNT_SUBDIR */

#ifdef CONFIG_NCPFS_NLS
/* This are the NLS conversion routines with inspirations and code parts
 * from the vfat file system and hints from Petr Vandrovec.
 */

/*
 * It should be replaced by charset specifc conversion. Gordon Chaffee
 * has prepared some things, but I don't know, what he thinks about it.
 * The conversion tables for the io charsets should be generatable by
 * Unicode table, shouldn't it? I have written so generation code for it.
 * The tables for the vendor specific codepages...? Hmm. The Samba sources
 * contains also any hints.
 */

#define toupperif(c, u) ((((u) != 0) && ((c) >= 'a') && ((c) <= 'z')) \
			? (c)-('a'-'A') : (c))
#define tolowerif(c, u) ((((u) != 0) && ((c) >= 'A') && ((c) <= 'Z')) \
			? (c)-('A'-'a') : (c))

static inline void
io2vol(struct ncp_server *server, char *name, int case_trans)
{
	unsigned char nc;
	unsigned char *np;
	unsigned char *up;
	struct nls_unicode uc;
	struct nls_table *nls_in;
	struct nls_table *nls_out;

	nls_in = server->nls_io;
	nls_out = server->nls_vol;
	np = name;

	while (*np)
	{
		nc = 0;
		uc = nls_in->charset2uni[toupperif(*np, case_trans)];
		up = nls_out->page_uni2charset[uc.uni2];
		if (up != NULL)	nc = up[uc.uni1];
		if (nc != 0) *np = nc;
		np++;
	}
}

static inline void
vol2io(struct ncp_server *server, char *name, int case_trans)
{
	unsigned char nc;
	unsigned char *np;
	unsigned char *up;
	struct nls_unicode uc;
	struct nls_table *nls_in;
	struct nls_table *nls_out;

	nls_in = server->nls_vol;
	nls_out = server->nls_io;
	np = name;

	while (*np)
	{
		nc = 0;
		uc = nls_in->charset2uni[*np];
		up = nls_out->page_uni2charset[uc.uni2];
		if (up != NULL)	nc = up[uc.uni1];
		if (nc == 0) nc = *np;
		*np = tolowerif(nc, case_trans);
		np++;
	}
}

#else

#define io2vol(S,N,U) if (U) str_upper(N)
#define vol2io(S,N,U) if (U) str_lower(N)

#endif /* CONFIG_NCPFS_NLS */

#define NCP_GET_AGE(dentry)	(jiffies - (dentry)->d_time)
#define NCP_MAX_AGE		(server->dentry_ttl)
#define NCP_TEST_AGE(server,dentry)	(NCP_GET_AGE(dentry) < NCP_MAX_AGE)

static inline void
ncp_age_dentry(struct ncp_server* server, struct dentry* dentry)
{
	dentry->d_time = jiffies - server->dentry_ttl;
}

static inline void
ncp_new_dentry(struct dentry* dentry)
{
	dentry->d_time = jiffies;
}

#define NCP_FPOS_EMPTY	0	/* init value for fpos variables. */

struct ncp_cache_control {
	struct nw_search_sequence seq;
	int		firstcache;
	int		currentpos;
	int		cachehead;
	int		cachetail;
	int		eof;
};

#define NCP_DIRCACHE_SIZE	(PAGE_CACHE_SIZE-sizeof(struct ncp_cache_control))
struct ncp_seq_cache {
	struct ncp_cache_control ctl;
	unsigned char cache[NCP_DIRCACHE_SIZE];
};

#endif /* _NCPLIB_H */
