/*
 *  ncplib_kernel.h
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *  Modified for big endian by J.F. Chadima and David S. Miller
 *
 */

#ifndef _NCPLIB_H
#define _NCPLIB_H

#include <linux/fs.h>
#include <linux/ncp.h>
#include <linux/ncp_fs.h>
#include <linux/ncp_fs_sb.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>
#include <asm/string.h>

#include <linux/ncp.h>

int
ncp_negotiate_buffersize(struct ncp_server *server, int size,
			 int *target);
int
ncp_get_volume_info_with_number(struct ncp_server *server, int n,
				struct ncp_volume_info *target);

int
ncp_close_file(struct ncp_server *server, const char *file_id);

int
ncp_read(struct ncp_server *server, const char *file_id,
	 __u32 offset, __u16 to_read,
	 char *target, int *bytes_read);

int
ncp_write(struct ncp_server *server, const char *file_id,
	  __u32 offset, __u16 to_write,
	  const char *source, int *bytes_written);

int
ncp_obtain_info(struct ncp_server *server,
		__u8 vol_num, __u32 dir_base,
		char *path, /* At most 1 component */
		struct nw_info_struct *target);

int
ncp_lookup_volume(struct ncp_server *server,
		  char *volname,
		  struct nw_info_struct *target);


int
ncp_modify_file_or_subdir_dos_info(struct ncp_server *server,
				   struct nw_info_struct *file,
				   __u32 info_mask,
				   struct nw_modify_dos_info *info);

int
ncp_del_file_or_subdir(struct ncp_server *server,
		       struct nw_info_struct *dir, char *name);

int
ncp_open_create_file_or_subdir(struct ncp_server *server,
			       struct nw_info_struct *dir, char *name,
			       int open_create_mode,
			       __u32 create_attributes,
			       int desired_acc_rights,
			       struct nw_file_info *target);

int
ncp_initialize_search(struct ncp_server *server,
		      struct nw_info_struct *dir,
		      struct nw_search_sequence *target);

int
ncp_search_for_file_or_subdir(struct ncp_server *server,
			      struct nw_search_sequence *seq,
			      struct nw_info_struct *target);

int
ncp_ren_or_mov_file_or_subdir(struct ncp_server *server,
			      struct nw_info_struct *old_dir, char *old_name,
			      struct nw_info_struct *new_dir, char *new_name);


#endif /* _NCPLIB_H */
