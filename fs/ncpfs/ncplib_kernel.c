/*
 *  ncplib_kernel.c
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *  Modified for big endian by J.F. Chadima and David S. Miller
 *  Modified 1997 Peter Waltenberg, Bill Hawes, David Woodhouse for 2.1 dcache
 *
 */


#include <linux/config.h>

#include "ncplib_kernel.h"

static inline int min(int a, int b)
{
	return a < b ? a : b;
}

static void assert_server_locked(struct ncp_server *server)
{
	if (server->lock == 0) {
		DPRINTK(KERN_DEBUG "ncpfs: server not locked!\n");
	}
}

static void ncp_add_byte(struct ncp_server *server, __u8 x)
{
	assert_server_locked(server);
	*(__u8 *) (&(server->packet[server->current_size])) = x;
	server->current_size += 1;
	return;
}

static void ncp_add_word(struct ncp_server *server, __u16 x)
{
	assert_server_locked(server);
	put_unaligned(x, (__u16 *) (&(server->packet[server->current_size])));
	server->current_size += 2;
	return;
}

static void ncp_add_dword(struct ncp_server *server, __u32 x)
{
	assert_server_locked(server);
	put_unaligned(x, (__u32 *) (&(server->packet[server->current_size])));
	server->current_size += 4;
	return;
}

static void ncp_add_mem(struct ncp_server *server, const void *source, int size)
{
	assert_server_locked(server);
	memcpy(&(server->packet[server->current_size]), source, size);
	server->current_size += size;
	return;
}

static void ncp_add_mem_fromfs(struct ncp_server *server, const char *source, int size)
{
	assert_server_locked(server);
	copy_from_user(&(server->packet[server->current_size]), source, size);
	server->current_size += size;
	return;
}

static void ncp_add_pstring(struct ncp_server *server, const char *s)
{
	int len = strlen(s);
	assert_server_locked(server);
	if (len > 255) {
		DPRINTK(KERN_DEBUG "ncpfs: string too long: %s\n", s);
		len = 255;
	}
	ncp_add_byte(server, len);
	ncp_add_mem(server, s, len);
	return;
}

static void ncp_init_request(struct ncp_server *server)
{
	ncp_lock_server(server);

	server->current_size = sizeof(struct ncp_request_header);
	server->has_subfunction = 0;
}

static void ncp_init_request_s(struct ncp_server *server, int subfunction)
{
	ncp_init_request(server);
	ncp_add_word(server, 0);	/* preliminary size */

	ncp_add_byte(server, subfunction);

	server->has_subfunction = 1;
}

static char *
 ncp_reply_data(struct ncp_server *server, int offset)
{
	return &(server->packet[sizeof(struct ncp_reply_header) + offset]);
}

static __u8
 ncp_reply_byte(struct ncp_server *server, int offset)
{
	return get_unaligned((__u8 *) ncp_reply_data(server, offset));
}

static __u16
 ncp_reply_word(struct ncp_server *server, int offset)
{
	return get_unaligned((__u16 *) ncp_reply_data(server, offset));
}

static __u32
 ncp_reply_dword(struct ncp_server *server, int offset)
{
	return get_unaligned((__u32 *) ncp_reply_data(server, offset));
}

int
ncp_negotiate_buffersize(struct ncp_server *server, int size, int *target)
{
	int result;

	ncp_init_request(server);
	ncp_add_word(server, htons(size));

	if ((result = ncp_request(server, 33)) != 0) {
		ncp_unlock_server(server);
		return result;
	}
	*target = min(ntohs(ncp_reply_word(server, 0)), size);

	ncp_unlock_server(server);
	return 0;
}


/* options: 
 *	bit 0	ipx checksum
 *	bit 1	packet signing
 */
int
ncp_negotiate_size_and_options(struct ncp_server *server, 
	int size, int options, int *ret_size, int *ret_options) {
	int result;

	/* there is minimum */
	if (size < 512) size = 512;

	ncp_init_request(server);
	ncp_add_word(server, htons(size));
	ncp_add_byte(server, options);
	
	if ((result = ncp_request(server, 0x61)) != 0)
	{
		ncp_unlock_server(server);
		return result;
	}

	/* NCP over UDP returns 0 (!!!) */
	result = ntohs(ncp_reply_word(server, 0));
	if (result >= 512) size=min(result, size);
	*ret_size = size;
	*ret_options = ncp_reply_byte(server, 4);

	ncp_unlock_server(server);
	return 0;
}

int
ncp_get_volume_info_with_number(struct ncp_server *server, int n,
				    struct ncp_volume_info *target)
{
	int result;
	int len;

	ncp_init_request_s(server, 44);
	ncp_add_byte(server, n);

	if ((result = ncp_request(server, 22)) != 0) {
		goto out;
	}
	target->total_blocks = ncp_reply_dword(server, 0);
	target->free_blocks = ncp_reply_dword(server, 4);
	target->purgeable_blocks = ncp_reply_dword(server, 8);
	target->not_yet_purgeable_blocks = ncp_reply_dword(server, 12);
	target->total_dir_entries = ncp_reply_dword(server, 16);
	target->available_dir_entries = ncp_reply_dword(server, 20);
	target->sectors_per_block = ncp_reply_byte(server, 28);

	memset(&(target->volume_name), 0, sizeof(target->volume_name));

	result = -EIO;
	len = ncp_reply_byte(server, 29);
	if (len > NCP_VOLNAME_LEN) {
		DPRINTK(KERN_DEBUG "ncpfs: volume name too long: %d\n", len);
		goto out;
	}
	memcpy(&(target->volume_name), ncp_reply_data(server, 30), len);
	result = 0;
out:
	ncp_unlock_server(server);
	return result;
}

int
ncp_close_file(struct ncp_server *server, const char *file_id)
{
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 0);
	ncp_add_mem(server, file_id, 6);

	result = ncp_request(server, 66);
	ncp_unlock_server(server);
	return result;
}

/*
 * Called with the superblock locked.
 */
int
ncp_make_closed(struct inode *inode)
{
	int err;
	NCP_FINFO(inode)->opened = 0;
	err = ncp_close_file(NCP_SERVER(inode), NCP_FINFO(inode)->file_handle);
#ifdef NCPFS_PARANOIA
if (!err)
printk(KERN_DEBUG "ncp_make_closed: volnum=%d, dirent=%u, error=%d\n",
NCP_FINFO(inode)->volNumber, NCP_FINFO(inode)->dirEntNum, err);
#endif
	return err;
}

static void ncp_add_handle_path(struct ncp_server *server, __u8 vol_num,
				__u32 dir_base, int have_dir_base, char *path)
{
	ncp_add_byte(server, vol_num);
	ncp_add_dword(server, dir_base);
	if (have_dir_base != 0) {
		ncp_add_byte(server, 1);	/* dir_base */
	} else {
		ncp_add_byte(server, 0xff);	/* no handle */
	}
	if (path != NULL) {
		ncp_add_byte(server, 1);	/* 1 component */
		ncp_add_pstring(server, path);
	} else {
		ncp_add_byte(server, 0);
	}
}

static void ncp_extract_file_info(void *structure, struct nw_info_struct *target)
{
	__u8 *name_len;
	const int info_struct_size = sizeof(struct nw_info_struct) - 257;

	memcpy(target, structure, info_struct_size);
	name_len = structure + info_struct_size;
	target->nameLen = *name_len;
	strncpy(target->entryName, name_len + 1, *name_len);
	target->entryName[*name_len] = '\0';
	return;
}

/*
 * Returns information for a (one-component) name relative to
 * the specified directory.
 */
int ncp_obtain_info(struct ncp_server *server, struct inode *dir, char *path,
			struct nw_info_struct *target)
{
	__u8  volnum = NCP_FINFO(dir)->volNumber;
	__u32 dirent = NCP_FINFO(dir)->dirEntNum;
	int result;

	if (target == NULL) {
		printk(KERN_ERR "ncp_obtain_info: invalid call\n");
		return -EINVAL;
	}
	ncp_init_request(server);
	ncp_add_byte(server, 6);	/* subfunction */
	ncp_add_byte(server, server->name_space[volnum]);
	ncp_add_byte(server, server->name_space[volnum]); /* N.B. twice ?? */
	ncp_add_word(server, htons(0x0680));	/* get all */
	ncp_add_dword(server, RIM_ALL);
	ncp_add_handle_path(server, volnum, dirent, 1, path);

	if ((result = ncp_request(server, 87)) != 0)
		goto out;
	ncp_extract_file_info(ncp_reply_data(server, 0), target);

out:
	ncp_unlock_server(server);
	return result;
}

#ifdef CONFIG_NCPFS_NFS_NS
static int
ncp_obtain_DOS_dir_base(struct ncp_server *server,
		__u8 volnum, __u32 dirent,
		char *path, /* At most 1 component */
		__u32 *DOS_dir_base)
{
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 6); /* subfunction */
	ncp_add_byte(server, server->name_space[volnum]);
	ncp_add_byte(server, server->name_space[volnum]);
	ncp_add_word(server, htons(0x0680)); /* get all */
	ncp_add_dword(server, RIM_DIRECTORY);
	ncp_add_handle_path(server, volnum, dirent, 1, path);

	if ((result = ncp_request(server, 87)) == 0)
	{
	   	if (DOS_dir_base) *DOS_dir_base=ncp_reply_dword(server, 0x34);
	}
	ncp_unlock_server(server);
	return result;
}
#endif /* CONFIG_NCPFS_NFS_NS */

static inline int
ncp_get_known_namespace(struct ncp_server *server, __u8 volume)
{
#if defined(CONFIG_NCPFS_OS2_NS) || defined(CONFIG_NCPFS_NFS_NS)
	int result;
	__u8 *namespace;
	__u16 no_namespaces;

	ncp_init_request(server);
	ncp_add_byte(server, 24);	/* Subfunction: Get Name Spaces Loaded */
	ncp_add_word(server, 0);
	ncp_add_byte(server, volume);

	if ((result = ncp_request(server, 87)) != 0) {
		ncp_unlock_server(server);
		return NW_NS_DOS; /* not result ?? */
	}

	result = NW_NS_DOS;
	no_namespaces = ncp_reply_word(server, 0);
	namespace = ncp_reply_data(server, 2);

	while (no_namespaces > 0) {
		DPRINTK(KERN_DEBUG "get_namespaces: found %d on %d\n", *namespace, volume);

#ifdef CONFIG_NCPFS_NFS_NS
		if ((*namespace == NW_NS_NFS) && !(server->m.flags&NCP_MOUNT_NO_NFS)) 
		{
			result = NW_NS_NFS;
			break;
		}
#endif	/* CONFIG_NCPFS_NFS_NS */
#ifdef CONFIG_NCPFS_OS2_NS
		if ((*namespace == NW_NS_OS2) && !(server->m.flags&NCP_MOUNT_NO_OS2))
		{
			result = NW_NS_OS2;
		}
#endif	/* CONFIG_NCPFS_OS2_NS */
		namespace += 1;
		no_namespaces -= 1;
	}
	ncp_unlock_server(server);
	return result;
#else	/* neither OS2 nor NFS - only DOS */
	return NW_NS_DOS;
#endif	/* defined(CONFIG_NCPFS_OS2_NS) || defined(CONFIG_NCPFS_NFS_NS) */
}

static int
ncp_ObtainSpecificDirBase(struct ncp_server *server,
		__u8 nsSrc, __u8 nsDst, __u8 vol_num, __u32 dir_base,
		char *path, /* At most 1 component */
		__u32 *dirEntNum, __u32 *DosDirNum)
{
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 6); /* subfunction */
	ncp_add_byte(server, nsSrc);
	ncp_add_byte(server, nsDst);
	ncp_add_word(server, 0x8006); /* get all */
	ncp_add_dword(server, RIM_ALL);
	ncp_add_handle_path(server, vol_num, dir_base, 1, path);

	if ((result = ncp_request(server, 87)) != 0)
	{
		ncp_unlock_server(server);
		return result;
	}

	if (dirEntNum)
		*dirEntNum = ncp_reply_dword(server, 0x30);
	if (DosDirNum)
		*DosDirNum = ncp_reply_dword(server, 0x34);
	ncp_unlock_server(server);
	return 0;
}

int
ncp_mount_subdir(struct ncp_server *server,
		__u8 volNumber,
		__u8 srcNS, __u32 dirEntNum)
{
	int dstNS;
	int result;
	__u32 newDirEnt;
	__u32 newDosEnt;
	
	dstNS = ncp_get_known_namespace(server, volNumber);
	if ((result = ncp_ObtainSpecificDirBase(server, srcNS, dstNS, volNumber, 
				      dirEntNum, NULL, &newDirEnt, &newDosEnt)) != 0)
	{
		return result;
	}
	server->name_space[volNumber] = dstNS;
	server->root.finfo.i.volNumber = volNumber;
	server->root.finfo.i.dirEntNum = newDirEnt;
	server->root.finfo.i.DosDirNum = newDosEnt;
	server->m.mounted_vol[1] = 0;
	server->m.mounted_vol[0] = 'X';
	return 0;
}

int 
ncp_lookup_volume(struct ncp_server *server, char *volname,
		      struct nw_info_struct *target)
{
	int result;
	int volnum;

	DPRINTK(KERN_DEBUG "ncp_lookup_volume: looking up vol %s\n", volname);

	ncp_init_request(server);
	ncp_add_byte(server, 22);	/* Subfunction: Generate dir handle */
	ncp_add_byte(server, 0);	/* DOS namespace */
	ncp_add_byte(server, 0);	/* reserved */
	ncp_add_byte(server, 0);	/* reserved */
	ncp_add_byte(server, 0);	/* reserved */

	ncp_add_byte(server, 0);	/* faked volume number */
	ncp_add_dword(server, 0);	/* faked dir_base */
	ncp_add_byte(server, 0xff);	/* Don't have a dir_base */
	ncp_add_byte(server, 1);	/* 1 path component */
	ncp_add_pstring(server, volname);

	if ((result = ncp_request(server, 87)) != 0) {
		ncp_unlock_server(server);
		return result;
	}
	memset(target, 0, sizeof(*target));
	target->DosDirNum = target->dirEntNum = ncp_reply_dword(server, 4);
	target->volNumber = volnum = ncp_reply_byte(server, 8);
	ncp_unlock_server(server);

	server->name_space[volnum] = ncp_get_known_namespace(server, volnum);

	DPRINTK(KERN_DEBUG "lookup_vol: namespace[%d] = %d\n",
		volnum, server->name_space[volnum]);

	target->nameLen = strlen(volname);
	strcpy(target->entryName, volname);
	target->attributes = aDIR;
	return 0;
}

int ncp_modify_file_or_subdir_dos_info(struct ncp_server *server,
					struct inode *dir, __u32 info_mask,
				       struct nw_modify_dos_info *info)
{
	__u8  volnum = NCP_FINFO(dir)->volNumber;
	__u32 dirent = NCP_FINFO(dir)->dirEntNum;
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 7);	/* subfunction */
	ncp_add_byte(server, server->name_space[volnum]);
	ncp_add_byte(server, 0);	/* reserved */
	ncp_add_word(server, htons(0x0680));	/* search attribs: all */

	ncp_add_dword(server, info_mask);
	ncp_add_mem(server, info, sizeof(*info));
	ncp_add_handle_path(server, volnum, dirent, 1, NULL);

	result = ncp_request(server, 87);
	ncp_unlock_server(server);
	return result;
}

static int
ncp_DeleteNSEntry(struct ncp_server *server,
		  __u8 have_dir_base, __u8 volnum, __u32 dirent,
		  char* name, __u8 ns, int attr)
{
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 8);	/* subfunction */
	ncp_add_byte(server, ns);
	ncp_add_byte(server, 0);	/* reserved */
	ncp_add_word(server, attr);	/* search attribs: all */
	ncp_add_handle_path(server, volnum, dirent, have_dir_base, name);

	result = ncp_request(server, 87);
	ncp_unlock_server(server);
	return result;
}

int
ncp_del_file_or_subdir2(struct ncp_server *server,
			struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	__u8  volnum;
	__u32 dirent;

	if (!inode) {
#if CONFIG_NCPFS_DEBUGDENTRY
		printk(KERN_DEBUG "ncpfs: ncpdel2: dentry->d_inode == NULL\n");
#endif
		return 0xFF;	/* Any error */
	}
	volnum = NCP_FINFO(inode)->volNumber;
	dirent = NCP_FINFO(inode)->DosDirNum;
	return ncp_DeleteNSEntry(server, 1, volnum, dirent, NULL, NW_NS_DOS, htons(0x0680));
}

int
ncp_del_file_or_subdir(struct ncp_server *server,
		       struct inode *dir, char *name)
{
	__u8  volnum = NCP_FINFO(dir)->volNumber;
	__u32 dirent = NCP_FINFO(dir)->dirEntNum;

#ifdef CONFIG_NCPFS_NFS_NS
	if (server->name_space[volnum]==NW_NS_NFS)
 	{
 		int result;
 
 		result=ncp_obtain_DOS_dir_base(server, volnum, dirent, name, &dirent);
 		if (result) return result;
 		return ncp_DeleteNSEntry(server, 1, volnum, dirent, NULL, NW_NS_DOS, htons(0x0680));
 	}
 	else
#endif	/* CONFIG_NCPFS_NFS_NS */
 		return ncp_DeleteNSEntry(server, 1, volnum, dirent, name, server->name_space[volnum], htons(0x0680));
}

static inline void ConvertToNWfromDWORD(__u32 sfd, __u8 ret[6])
{
	__u16 *dest = (__u16 *) ret;
	memcpy(ret + 2, &sfd, 4);
	dest[0] = cpu_to_le16((le16_to_cpu(dest[1]) + le16_to_cpu(1)));
	return;
}

/* If both dir and name are NULL, then in target there's already a
   looked-up entry that wants to be opened. */
int ncp_open_create_file_or_subdir(struct ncp_server *server,
				   struct inode *dir, char *name,
				   int open_create_mode,
				   __u32 create_attributes,
				   int desired_acc_rights,
				   struct nw_file_info *target)
{
	__u16 search_attribs = ntohs(0x0600);
	__u8  volnum = target->i.volNumber;
	__u32 dirent = target->i.dirEntNum;
	int result;

	if (dir)
	{
		volnum = NCP_FINFO(dir)->volNumber;
		dirent = NCP_FINFO(dir)->dirEntNum;
	}

	if ((create_attributes & aDIR) != 0) {
		search_attribs |= ntohs(0x0080);
	}
	ncp_init_request(server);
	ncp_add_byte(server, 1);	/* subfunction */
	ncp_add_byte(server, server->name_space[volnum]);
	ncp_add_byte(server, open_create_mode);
	ncp_add_word(server, search_attribs);
	ncp_add_dword(server, RIM_ALL);
	ncp_add_dword(server, create_attributes);
	/* The desired acc rights seem to be the inherited rights mask
	   for directories */
	ncp_add_word(server, desired_acc_rights);
	ncp_add_handle_path(server, volnum, dirent, 1, name);

	if ((result = ncp_request(server, 87)) != 0)
		goto out;
	target->opened = 1;
	target->server_file_handle = ncp_reply_dword(server, 0);
	target->open_create_action = ncp_reply_byte(server, 4);

	if (dir != NULL) {
		/* in target there's a new finfo to fill */
		ncp_extract_file_info(ncp_reply_data(server, 6), &(target->i));
	}
	ConvertToNWfromDWORD(target->server_file_handle, target->file_handle);

out:
	ncp_unlock_server(server);
	return result;
}

int
ncp_initialize_search(struct ncp_server *server, struct inode *dir,
			struct nw_search_sequence *target)
{
	__u8  volnum = NCP_FINFO(dir)->volNumber;
	__u32 dirent = NCP_FINFO(dir)->dirEntNum;
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 2);	/* subfunction */
	ncp_add_byte(server, server->name_space[volnum]);
	ncp_add_byte(server, 0);	/* reserved */
	ncp_add_handle_path(server, volnum, dirent, 1, NULL);

	result = ncp_request(server, 87);
	if (result)
		goto out;
	memcpy(target, ncp_reply_data(server, 0), sizeof(*target));

out:
	ncp_unlock_server(server);
	return result;
}

/* Search for everything */
int ncp_search_for_file_or_subdir(struct ncp_server *server,
				  struct nw_search_sequence *seq,
				  struct nw_info_struct *target)
{
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 3);	/* subfunction */
	ncp_add_byte(server, server->name_space[seq->volNumber]);
	ncp_add_byte(server, 0);	/* data stream (???) */
	ncp_add_word(server, htons(0x0680));	/* Search attribs */
	ncp_add_dword(server, RIM_ALL);		/* return info mask */
	ncp_add_mem(server, seq, 9);
#ifdef CONFIG_NCPFS_NFS_NS
	if (server->name_space[seq->volNumber] == NW_NS_NFS) {
		ncp_add_byte(server, 0);	/* 0 byte pattern */
	} else 
#endif
	{
		ncp_add_byte(server, 2);	/* 2 byte pattern */
		ncp_add_byte(server, 0xff);	/* following is a wildcard */
		ncp_add_byte(server, '*');
	}
	
	if ((result = ncp_request(server, 87)) != 0)
		goto out;
	memcpy(seq, ncp_reply_data(server, 0), sizeof(*seq));
	ncp_extract_file_info(ncp_reply_data(server, 10), target);

out:
	ncp_unlock_server(server);
	return result;
}

int
ncp_RenameNSEntry(struct ncp_server *server,
		  struct inode *old_dir, char *old_name, int old_type,
		  struct inode *new_dir, char *new_name)
{
	int result = -EINVAL;

	if ((old_dir == NULL) || (old_name == NULL) ||
	    (new_dir == NULL) || (new_name == NULL))
		goto out;

	ncp_init_request(server);
	ncp_add_byte(server, 4);	/* subfunction */
	ncp_add_byte(server, server->name_space[NCP_FINFO(old_dir)->volNumber]);
	ncp_add_byte(server, 1);	/* rename flag */
	ncp_add_word(server, old_type);	/* search attributes */

	/* source Handle Path */
	ncp_add_byte(server, NCP_FINFO(old_dir)->volNumber);
	ncp_add_dword(server, NCP_FINFO(old_dir)->dirEntNum);
	ncp_add_byte(server, 1);
	ncp_add_byte(server, 1);	/* 1 source component */

	/* dest Handle Path */
	ncp_add_byte(server, NCP_FINFO(new_dir)->volNumber);
	ncp_add_dword(server, NCP_FINFO(new_dir)->dirEntNum);
	ncp_add_byte(server, 1);
	ncp_add_byte(server, 1);	/* 1 destination component */

	/* source path string */
	ncp_add_pstring(server, old_name);
	/* dest path string */
	ncp_add_pstring(server, new_name);

	result = ncp_request(server, 87);
	ncp_unlock_server(server);
out:
	return result;
}

int ncp_ren_or_mov_file_or_subdir(struct ncp_server *server,
				struct inode *old_dir, char *old_name,
				struct inode *new_dir, char *new_name)
{
        int result;
        int old_type = htons(0x0600);

/* If somebody can do it atomic, call me... vandrove@vc.cvut.cz */
	result = ncp_RenameNSEntry(server, old_dir, old_name, old_type,
	                                   new_dir, new_name);
        if (result == 0xFF)	/* File Not Found, try directory */
	{
		old_type = htons(0x1600);
		result = ncp_RenameNSEntry(server, old_dir, old_name, old_type,
						   new_dir, new_name);
	}
	if (result != 0x92) return result;	/* All except NO_FILES_RENAMED */
	result = ncp_del_file_or_subdir(server, new_dir, new_name);
	if (result != 0) return -EACCES;
	result = ncp_RenameNSEntry(server, old_dir, old_name, old_type,
					   new_dir, new_name);
	return result;
}
	

/* We have to transfer to/from user space */
int
ncp_read(struct ncp_server *server, const char *file_id,
	     __u32 offset, __u16 to_read, char *target, int *bytes_read)
{
	char *source;
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 0);
	ncp_add_mem(server, file_id, 6);
	ncp_add_dword(server, htonl(offset));
	ncp_add_word(server, htons(to_read));

	if ((result = ncp_request(server, 72)) != 0) {
		goto out;
	}
	*bytes_read = ntohs(ncp_reply_word(server, 0));
	source = ncp_reply_data(server, 2 + (offset & 1));

	result = -EFAULT;
	if (!copy_to_user(target, source, *bytes_read))
		result = 0;
out:
	ncp_unlock_server(server);
	return result;
}

int
ncp_write(struct ncp_server *server, const char *file_id,
	      __u32 offset, __u16 to_write,
		const char *source, int *bytes_written)
{
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 0);
	ncp_add_mem(server, file_id, 6);
	ncp_add_dword(server, htonl(offset));
	ncp_add_word(server, htons(to_write));
	ncp_add_mem_fromfs(server, source, to_write);

	if ((result = ncp_request(server, 73)) != 0)
		goto out;
	*bytes_written = to_write;
	result = 0;
out:
	ncp_unlock_server(server);
	return result;
}

#ifdef CONFIG_NCPFS_IOCTL_LOCKING
int
ncp_LogPhysicalRecord(struct ncp_server *server, const char *file_id,
	  __u8 locktype, __u32 offset, __u32 length, __u16 timeout)
{
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, locktype);
	ncp_add_mem(server, file_id, 6);
	ncp_add_dword(server, htonl(offset));
	ncp_add_dword(server, htonl(length));
	ncp_add_word(server, htons(timeout));

	if ((result = ncp_request(server, 0x1A)) != 0)
	{
		ncp_unlock_server(server);
		return result;
	}
	ncp_unlock_server(server);
	return 0;
}

int
ncp_ClearPhysicalRecord(struct ncp_server *server, const char *file_id,
	  __u32 offset, __u32 length)
{
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 0);	/* who knows... lanalyzer says that */
	ncp_add_mem(server, file_id, 6);
	ncp_add_dword(server, htonl(offset));
	ncp_add_dword(server, htonl(length));

	if ((result = ncp_request(server, 0x1E)) != 0)
	{
		ncp_unlock_server(server);
		return result;
	}
	ncp_unlock_server(server);
	return 0;
}
#endif	/* CONFIG_NCPFS_IOCTL_LOCKING */

