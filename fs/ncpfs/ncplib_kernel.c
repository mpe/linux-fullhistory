/*
 *  ncplib_kernel.c
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *
 */

#include "ncplib_kernel.h"

typedef __u8  byte;
typedef __u16 word;
typedef __u32 dword;

static inline int min(int a, int b)
{
	return a<b ? a : b;
}

static void
assert_server_locked(struct ncp_server *server)
{
	if (server->lock == 0)
	{
		DPRINTK("ncpfs: server not locked!\n");
	}
}

static void
ncp_add_byte(struct ncp_server *server, byte x)
{
	assert_server_locked(server);
	*(byte *)(&(server->packet[server->current_size])) = x;
	server->current_size += 1;
	return;
}

static void
ncp_add_word(struct ncp_server *server, word x)
{
	assert_server_locked(server);
	*(word *)(&(server->packet[server->current_size])) = x;
	server->current_size += 2;
	return;
}

static void
ncp_add_dword(struct ncp_server *server, dword x)
{
	assert_server_locked(server);
	*(dword *)(&(server->packet[server->current_size])) = x;
	server->current_size += 4;
	return;
}

static void
ncp_add_mem(struct ncp_server *server, const void *source, int size)
{
	assert_server_locked(server);
	memcpy(&(server->packet[server->current_size]), source, size);
	server->current_size += size;
	return;
}

static void
ncp_add_mem_fromfs(struct ncp_server *server, const char *source, int size)
{
	assert_server_locked(server);
	memcpy_fromfs(&(server->packet[server->current_size]), source, size);
	server->current_size += size;
	return;
}

static void
ncp_add_pstring(struct ncp_server *server, const char *s)
{
	int len = strlen(s);
	assert_server_locked(server);
	if (len > 255)
	{
		DPRINTK("ncpfs: string too long: %s\n", s);
		len = 255;
	}
	ncp_add_byte(server, len);
	ncp_add_mem(server, s, len);
	return;
}

static void
ncp_init_request(struct ncp_server *server)
{
	ncp_lock_server(server);

	server->current_size = sizeof(struct ncp_request_header);
	server->has_subfunction = 0;
}

static void
ncp_init_request_s(struct ncp_server *server, int subfunction)
{
	ncp_init_request(server);
	ncp_add_word(server, 0); /* preliminary size */

	ncp_add_byte(server, subfunction);

	server->has_subfunction = 1;
}

static char *
ncp_reply_data(struct ncp_server *server, int offset)
{
	return &(server->packet[sizeof(struct ncp_reply_header) + offset]);
}

static byte
ncp_reply_byte(struct ncp_server *server, int offset)
{
	return *(byte *)(ncp_reply_data(server, offset));
}

static word
ncp_reply_word(struct ncp_server *server, int offset)
{
	return *(word *)(ncp_reply_data(server, offset));
}

static dword
ncp_reply_dword(struct ncp_server *server, int offset)
{
	return *(dword *)(ncp_reply_data(server, offset));
}

int
ncp_negotiate_buffersize(struct ncp_server *server,
			 int size, int *target)
{
	int result;

	ncp_init_request(server);
	ncp_add_word(server, htons(size));
	
	if ((result = ncp_request(server, 33)) != 0)
	{
		ncp_unlock_server(server);
		return result;
	}

	*target =min(ntohs(ncp_reply_word(server, 0)), size);

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

	if ((result = ncp_request(server, 22)) != 0)
	{
		ncp_unlock_server(server);
		return result;
	}

	target->total_blocks = ncp_reply_dword(server, 0);
	target->free_blocks  = ncp_reply_dword(server, 4);
	target->purgeable_blocks = ncp_reply_dword(server, 8);
	target->not_yet_purgeable_blocks = ncp_reply_dword(server, 12);
	target->total_dir_entries = ncp_reply_dword(server, 16);
	target->available_dir_entries = ncp_reply_dword(server, 20);
	target->sectors_per_block = ncp_reply_byte(server, 28);

	memset(&(target->volume_name), 0, sizeof(target->volume_name));

	len = ncp_reply_byte(server, 29);
	if (len > NCP_VOLNAME_LEN)
	{
		DPRINTK("ncpfs: volume name too long: %d\n", len);
		ncp_unlock_server(server);
		return -EIO;
	}

	memcpy(&(target->volume_name), ncp_reply_data(server, 30), len);
	ncp_unlock_server(server);
	return 0;
}

int
ncp_get_volume_number(struct ncp_server *server, const char *name, int *target)
{
	int result;

	ncp_init_request_s(server, 5);
	ncp_add_pstring(server, name);

	if ((result = ncp_request(server, 22)) != 0)
	{
		ncp_unlock_server(server);
		return result;
	}

	*target = ncp_reply_byte(server, 0);
	ncp_unlock_server(server);
	return 0;
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

static void
ncp_add_handle_path(struct ncp_server *server,
		    __u8 vol_num,
		    __u32 dir_base, int have_dir_base,
		    char *path)
{
	ncp_add_byte(server, vol_num);
	ncp_add_dword(server, dir_base);
	if (have_dir_base != 0)
	{
		ncp_add_byte(server, 1); /* dir_base */
	}
	else
	{
		ncp_add_byte(server, 0xff); /* no handle */
	}
	if (path != NULL)
	{
		ncp_add_byte(server, 1); /* 1 component */
		ncp_add_pstring(server, path);
	}
	else
	{
		ncp_add_byte(server, 0);
	}
}

static void
ncp_extract_file_info(void *structure, struct nw_info_struct *target)
{
	__u8 *name_len;
	const int info_struct_size = sizeof(struct nw_info_struct) - 257;

	memcpy(target, structure, info_struct_size);
	name_len = structure + info_struct_size;
	target->nameLen = *name_len;
	strncpy(target->entryName, name_len+1, *name_len);
	target->entryName[*name_len] = '\0';
	return;
}

int
ncp_obtain_info(struct ncp_server *server,
		__u8 vol_num, __u32 dir_base,
		char *path, /* At most 1 component */
		struct nw_info_struct *target)
{
	int result;

	if (target == NULL)
	{
		return -EINVAL;
	}

	ncp_init_request(server);
	ncp_add_byte(server, 6); /* subfunction */
	ncp_add_byte(server, 0); /* dos name space */
	ncp_add_byte(server, 0); /* dos name space as dest */
	ncp_add_word(server, 0xff); /* get all */
	ncp_add_dword(server, RIM_ALL);
	ncp_add_handle_path(server, vol_num, dir_base, 1, path);

	if ((result = ncp_request(server, 87)) != 0)
	{
		ncp_unlock_server(server);
		return result;
	}

	ncp_extract_file_info(ncp_reply_data(server, 0), target);
	ncp_unlock_server(server);
	return 0;
}

int
ncp_lookup_volume(struct ncp_server *server,
		  char *volname,
		  struct nw_info_struct *target)
{
	int result;
	__u8  vol_num;
	__u32 dir_base;

	DPRINTK("ncp_lookup_volume: looking up vol %s\n", volname);

	ncp_init_request(server);
	ncp_add_byte(server, 22); /* Subfunction: Generate dir handle */
	ncp_add_byte(server, 0); /* DOS name space */
	ncp_add_byte(server, 0); /* reserved */
	ncp_add_byte(server, 0); /* reserved */
	ncp_add_byte(server, 0); /* reserved */

	ncp_add_byte(server, 0); /* faked volume number */
	ncp_add_dword(server, 0); /* faked dir_base */
	ncp_add_byte(server, 0xff); /* Don't have a dir_base */
	ncp_add_byte(server, 1); /* 1 path component */
	ncp_add_pstring(server, volname);

	if ((result = ncp_request(server, 87)) != 0)
	{
		ncp_unlock_server(server);
		return result;
	}

	dir_base = ncp_reply_dword(server, 4);
	vol_num  = ncp_reply_byte(server, 8);
	ncp_unlock_server(server);

	if ((result = ncp_obtain_info(server, vol_num, dir_base, NULL,
				      target)) != 0)
	{
		return result;
	}

	DPRINTK("ncp_lookup_volume: attribs = %X\n", target->attributes);

	target->nameLen = strlen(volname);
	strcpy(target->entryName, volname);
	return 0;
}

int
ncp_modify_file_or_subdir_dos_info(struct ncp_server *server,
				   struct nw_info_struct *file,
				   __u32 info_mask,
				   struct nw_modify_dos_info *info)
{
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 7); /* subfunction */
	ncp_add_byte(server, 0); /* dos name space */
	ncp_add_byte(server, 0); /* reserved */
	ncp_add_word(server, 0x8006); /* search attribs: all */

	ncp_add_dword(server, info_mask);
	ncp_add_mem(server, info, sizeof(*info));
	ncp_add_handle_path(server, file->volNumber,
			    file->DosDirNum, 1, NULL);

	result = ncp_request(server, 87);
	ncp_unlock_server(server);
	return result;
}

int
ncp_del_file_or_subdir(struct ncp_server *server,
		       struct nw_info_struct *dir, char *name)
{
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 8); /* subfunction */
	ncp_add_byte(server, 0); /* dos name space */
	ncp_add_byte(server, 0); /* reserved */
	ncp_add_word(server, 0x8006); /* search attribs: all */
	ncp_add_handle_path(server, dir->volNumber,
			    dir->DosDirNum, 1, name);
	
	result = ncp_request(server, 87);
	ncp_unlock_server(server);
	return result;
}

static inline void
ConvertToNWfromDWORD ( __u32 sfd , __u8 ret[6] )
{
    __u16 *dest = (__u16 *) ret;
    memcpy(&(dest[1]), &sfd, 4);
    dest[0] = dest[1] + 1;
    return;
}

/* If both dir and name are NULL, then in target there's already a
   looked-up entry that wants to be opened. */
int
ncp_open_create_file_or_subdir(struct ncp_server *server,
			       struct nw_info_struct *dir, char *name,
			       int open_create_mode,
			       __u32 create_attributes,
			       int desired_acc_rights,
			       struct nw_file_info *target)
{
	int result;
	__u16 search_attribs = 0x0006;

	if ((create_attributes & aDIR) != 0)
	{
		search_attribs |= 0x8000;
	}

	ncp_init_request(server);
	ncp_add_byte(server, 1); /* subfunction */
	ncp_add_byte(server, 0); /* dos name space */
	ncp_add_byte(server, open_create_mode);
	ncp_add_word(server, search_attribs);
	ncp_add_dword(server, RIM_ALL);
	ncp_add_dword(server, create_attributes);
	/* The desired acc rights seem to be the inherited rights mask
	   for directories */
	ncp_add_word(server, desired_acc_rights);

	if (dir != NULL)
	{
		ncp_add_handle_path(server, dir->volNumber,
				    dir->DosDirNum, 1, name);
	}
	else
	{
		ncp_add_handle_path(server,
				    target->i.volNumber, target->i.DosDirNum,
				    1, NULL);
	}	

	if ((result = ncp_request(server, 87)) != 0)
	{
		ncp_unlock_server(server);
		return result;
	}

	target->opened = 1;
	target->server_file_handle = ncp_reply_dword(server, 0);
	target->open_create_action = ncp_reply_byte(server, 4);

	if (dir != NULL)
	{
		/* in target there's a new finfo to fill */
		ncp_extract_file_info(ncp_reply_data(server, 5), &(target->i));
	}

	ConvertToNWfromDWORD(target->server_file_handle, target->file_handle);

	ncp_unlock_server(server);
	return 0;
}
	

int
ncp_initialize_search(struct ncp_server *server,
		      struct nw_info_struct *dir,
		      struct nw_search_sequence *target)
{
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 2); /* subfunction */
	ncp_add_byte(server, 0); /* dos name space */
	ncp_add_byte(server, 0); /* reserved */
	ncp_add_handle_path(server, dir->volNumber, dir->DosDirNum, 1, NULL);
	
	if ((result = ncp_request(server, 87)) != 0)
	{
		ncp_unlock_server(server);
		return result;
	}

	memcpy(target, ncp_reply_data(server, 0), sizeof(*target));

	ncp_unlock_server(server);
	return 0;
}
	
/* Search for everything */
int
ncp_search_for_file_or_subdir(struct ncp_server *server,
			      struct nw_search_sequence *seq,
			      struct nw_info_struct *target)
{
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 3); /* subfunction */
	ncp_add_byte(server, 0); /* dos name space */
	ncp_add_byte(server, 0); /* data stream (???) */
	ncp_add_word(server, 0xffff); /* Search attribs */
	ncp_add_dword(server, RIM_ALL);	/* return info mask */
	ncp_add_mem(server, seq, 9);
	ncp_add_byte(server, 2); /* 2 byte pattern */
	ncp_add_byte(server, 0xff); /* following is a wildcard */
	ncp_add_byte(server, '*');
	
	if ((result = ncp_request(server, 87)) != 0)
	{
		ncp_unlock_server(server);
		return result;
	}

	memcpy(seq, ncp_reply_data(server, 0), sizeof(*seq));
	ncp_extract_file_info(ncp_reply_data(server, 10), target);

	ncp_unlock_server(server);
	return 0;
}

int
ncp_ren_or_mov_file_or_subdir(struct ncp_server *server,
			      struct nw_info_struct *old_dir, char *old_name,
			      struct nw_info_struct *new_dir, char *new_name)
{
	int result;

	if (   (old_dir == NULL) || (old_name == NULL)
	    || (new_dir == NULL) || (new_name == NULL))
		return -EINVAL;
	
	ncp_init_request(server);
	ncp_add_byte(server, 4); /* subfunction */
	ncp_add_byte(server, 0); /* dos name space */
	ncp_add_byte(server, 1); /* rename flag */
	ncp_add_word(server, 0x8006); /* search attributes */

	/* source Handle Path */
	ncp_add_byte(server, old_dir->volNumber);
	ncp_add_dword(server, old_dir->DosDirNum);
	ncp_add_byte(server, 1);
	ncp_add_byte(server, 1); /* 1 source component */

	/* dest Handle Path */
	ncp_add_byte(server, new_dir->volNumber);
	ncp_add_dword(server, new_dir->DosDirNum);
	ncp_add_byte(server, 1);
	ncp_add_byte(server, 1); /* 1 destination component */

	/* source path string */
	ncp_add_pstring(server, old_name);
	/* dest path string */
	ncp_add_pstring(server, new_name);

	result = ncp_request(server, 87);
	ncp_unlock_server(server);
	return result;
}
	

/* We have to transfer to/from user space */
int
ncp_read(struct ncp_server *server, const char *file_id,
	 __u32 offset, __u16 to_read,
	 char *target, int *bytes_read)
{
	int result;

	ncp_init_request(server);
	ncp_add_byte(server, 0);
	ncp_add_mem(server, file_id, 6);
	ncp_add_dword(server, htonl(offset));
	ncp_add_word(server, htons(to_read));

	if ((result = ncp_request(server, 72)) != 0)
	{
		ncp_unlock_server(server);
		return result;
	}

	*bytes_read = ntohs(ncp_reply_word(server, 0));

	memcpy_tofs(target, ncp_reply_data(server, 2+(offset&1)), *bytes_read);

	ncp_unlock_server(server);
	return 0;
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
	{
		ncp_unlock_server(server);
		return result;
	}

	*bytes_written = to_write;

	ncp_unlock_server(server);
	return 0;
}

