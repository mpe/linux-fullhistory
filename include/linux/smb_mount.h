/*
 *  smb_mount.h
 *
 *  Copyright (C) 1995 by Paal-Kr. Engstad and Volker Lendecke
 *
 */

#ifndef _LINUX_SMB_MOUNT_H
#define _LINUX_SMB_MOUNT_H

#include <linux/types.h>
#include <linux/in.h>

#define SMB_MOUNT_VERSION 4

struct smb_mount_data {
	int version;
	unsigned int fd;
        uid_t mounted_uid;      /* Who may umount() this filesystem? */
	struct sockaddr_in addr;

	char server_name[17];
        char client_name[17];
	char service[64];
        char root_path[64];

        char username[64];
	char password[64];

	unsigned short max_xmit;

        uid_t uid;
        gid_t gid;
        mode_t file_mode;
        mode_t dir_mode;
};

#endif
