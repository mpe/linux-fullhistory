/*
 *  ncplib_kernel.h
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
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
#include <asm/segment.h>
#include <asm/string.h>

#include <linux/ncp.h>

int
ncp_negotiate_buffersize(struct ncp_server *server, int size,
			 int *target);
int
ncp_get_encryption_key(struct ncp_server *server,
		       char *target);
int
ncp_get_bindery_object_id(struct ncp_server *server,
			  int object_type, char *object_name,
			  struct ncp_bindery_object *target);
int
ncp_login_encrypted(struct ncp_server *server,
		    struct ncp_bindery_object *object,
		    unsigned char *key,
		    unsigned char *passwd);
int
ncp_login_user(struct ncp_server *server,
	       unsigned char *username,
	       unsigned char *password);
int
ncp_get_volume_info_with_number(struct ncp_server *server, int n,
				struct ncp_volume_info *target);

int
ncp_get_volume_number(struct ncp_server *server, const char *name,
		      int *target);

int
ncp_file_search_init(struct ncp_server *server,
		     int dir_handle, const char *path,
		     struct ncp_filesearch_info *target);

int
ncp_file_search_continue(struct ncp_server *server,
			 struct ncp_filesearch_info *fsinfo,
			 int attributes, const char *path,
			 struct ncp_file_info *target);

int
ncp_get_finfo(struct ncp_server *server,
	      int dir_handle, const char *path, const char *name,
	      struct ncp_file_info *target);

int
ncp_open_file(struct ncp_server *server,
	      int dir_handle, const char *path,
	      int attr, int access,
	      struct ncp_file_info *target);
int
ncp_close_file(struct ncp_server *server, const char *file_id);

int
ncp_create_newfile(struct ncp_server *server,
		   int dir_handle, const char *path,
		   int attr,
		   struct ncp_file_info *target);

int
ncp_create_file(struct ncp_server *server,
		int dir_handle, const char *path,
		int attr,
		struct ncp_file_info *target);

int
ncp_erase_file(struct ncp_server *server,
	       int dir_handle, const char *path,
	       int attr);

int
ncp_rename_file(struct ncp_server *server,
		int old_handle, const char *old_path,
		int attr,
		int new_handle, const char *new_path);

int
ncp_create_directory(struct ncp_server *server,
		     int dir_handle, const char *path,
		     int inherit_mask);

int
ncp_delete_directory(struct ncp_server *server,
		     int dir_handle, const char *path);

int
ncp_rename_directory(struct ncp_server *server,
		     int dir_handle,
		     const char *old_path, const char *new_path);

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
