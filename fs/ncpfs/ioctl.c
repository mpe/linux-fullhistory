/*
 *  ioctl.c
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *  Modified 1997 Peter Waltenberg, Bill Hawes, David Woodhouse for 2.1 dcache
 *
 */

#include <linux/config.h>

#include <asm/uaccess.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <linux/ncp.h>
#include <linux/ncp_fs.h>
#include "ncplib_kernel.h"

/* maximum limit for ncp_objectname_ioctl */
#define NCP_OBJECT_NAME_MAX_LEN	4096
/* maximum limit for ncp_privatedata_ioctl */
#define NCP_PRIVATE_DATA_MAX_LEN 8192

int ncp_ioctl(struct inode *inode, struct file *filp,
	      unsigned int cmd, unsigned long arg)
{
	struct ncp_server *server = NCP_SERVER(inode);
	int result;
	struct ncp_ioctl_request request;
	struct ncp_fs_info info;

#ifdef NCP_IOC_GETMOUNTUID_INT
	/* remove after ncpfs-2.0.13/2.2.0 gets released */
	if ((NCP_IOC_GETMOUNTUID != NCP_IOC_GETMOUNTUID_INT) &&
             (cmd == NCP_IOC_GETMOUNTUID_INT)) {
		int tmp = server->m.mounted_uid;

		if (   (permission(inode, MAY_READ) != 0)
		    && (current->uid != server->m.mounted_uid))
		{
			return -EACCES;
		}
		if (put_user(tmp, (unsigned int*) arg)) return -EFAULT;
		return 0;
	}
#endif	/* NCP_IOC_GETMOUNTUID_INT */

	switch (cmd) {
	case NCP_IOC_NCPREQUEST:

		if ((permission(inode, MAY_WRITE) != 0)
		    && (current->uid != server->m.mounted_uid)) {
			return -EACCES;
		}
		if ((result = verify_area(VERIFY_READ, (char *) arg,
					  sizeof(request))) != 0) {
			return result;
		}
		copy_from_user(&request, (struct ncp_ioctl_request *) arg,
			       sizeof(request));

		if ((request.function > 255)
		    || (request.size >
		  NCP_PACKET_SIZE - sizeof(struct ncp_request_header))) {
			return -EINVAL;
		}
		if ((result = verify_area(VERIFY_WRITE, (char *) request.data,
					  NCP_PACKET_SIZE)) != 0) {
			return result;
		}
		ncp_lock_server(server);

		/* FIXME: We hack around in the server's structures
		   here to be able to use ncp_request */

		server->has_subfunction = 0;
		server->current_size = request.size;
		copy_from_user(server->packet, request.data, request.size);

		ncp_request(server, request.function);

		DPRINTK(KERN_DEBUG "ncp_ioctl: copy %d bytes\n",
			server->reply_size);
		copy_to_user(request.data, server->packet, server->reply_size);

		ncp_unlock_server(server);

		return server->reply_size;

	case NCP_IOC_CONN_LOGGED_IN:

		if ((permission(inode, MAY_WRITE) != 0)
		    && (current->uid != server->m.mounted_uid)) {
			return -EACCES;
		}
		if (server->root_setuped) return -EBUSY;
		server->root_setuped = 1;
		return ncp_conn_logged_in(server);

	case NCP_IOC_GET_FS_INFO:

		if ((permission(inode, MAY_WRITE) != 0)
		    && (current->uid != server->m.mounted_uid)) {
			return -EACCES;
		}
		if ((result = verify_area(VERIFY_WRITE, (char *) arg,
					  sizeof(info))) != 0) {
			return result;
		}
		copy_from_user(&info, (struct ncp_fs_info *) arg, sizeof(info));

		if (info.version != NCP_GET_FS_INFO_VERSION) {
			DPRINTK(KERN_DEBUG "info.version invalid: %d\n", info.version);
			return -EINVAL;
		}
		/* TODO: info.addr = server->m.serv_addr; */
		info.mounted_uid	= server->m.mounted_uid;
		info.connection		= server->connection;
		info.buffer_size	= server->buffer_size;
		info.volume_number	= NCP_FINFO(inode)->volNumber;
		info.directory_id	= NCP_FINFO(inode)->DosDirNum;

		copy_to_user((struct ncp_fs_info *) arg, &info, sizeof(info));
		return 0;

	case NCP_IOC_GETMOUNTUID:

		if ((permission(inode, MAY_READ) != 0)
		    && (current->uid != server->m.mounted_uid)) {
			return -EACCES;
		}
		if ((result = verify_area(VERIFY_WRITE, (uid_t *) arg,
					  sizeof(uid_t))) != 0) {
			return result;
		}
		put_user(server->m.mounted_uid, (uid_t *) arg);
		return 0;

#ifdef CONFIG_NCPFS_MOUNT_SUBDIR
	case NCP_IOC_GETROOT:
		{
			struct ncp_setroot_ioctl sr;

			if (   (permission(inode, MAY_READ) != 0)
			    && (current->uid != server->m.mounted_uid))
			{
				return -EACCES;
			}
			if (server->m.mounted_vol[0]) {
				sr.volNumber = server->root.finfo.i.volNumber;
				sr.dirEntNum = server->root.finfo.i.dirEntNum;
				sr.namespace = server->name_space[sr.volNumber];
			} else {
				sr.volNumber = -1;
				sr.namespace = 0;
				sr.dirEntNum = 0;
			}
			if (copy_to_user((struct ncp_setroot_ioctl*)arg, 
				    	  &sr, 
					  sizeof(sr))) return -EFAULT;
			return 0;
		}
	case NCP_IOC_SETROOT:
		{
			struct ncp_setroot_ioctl sr;
			struct dentry* dentry;

			if (   (permission(inode, MAY_WRITE) != 0)
			    && (current->uid != server->m.mounted_uid))
			{
				return -EACCES;
			}
			if (server->root_setuped) return -EBUSY;
			if (copy_from_user(&sr,
					   (struct ncp_setroot_ioctl*)arg, 
					   sizeof(sr))) return -EFAULT;
			if (sr.volNumber < 0) {
				server->m.mounted_vol[0] = 0;
				server->root.finfo.i.volNumber = NCP_NUMBER_OF_VOLUMES + 1;
				server->root.finfo.i.dirEntNum = 0;
				server->root.finfo.i.DosDirNum = 0;
			} else if (sr.volNumber >= NCP_NUMBER_OF_VOLUMES) {
				return -EINVAL;
			} else {
				if (ncp_mount_subdir(server, sr.volNumber, sr.namespace, sr.dirEntNum)) {
					return -ENOENT;
				}
			}
			dentry = server->root_dentry;
			server->root_setuped = 1;
			if (dentry) {
				struct inode* inode = dentry->d_inode;
				
				if (inode) {
					NCP_FINFO(inode)->volNumber = server->root.finfo.i.volNumber;
					NCP_FINFO(inode)->dirEntNum = server->root.finfo.i.dirEntNum;
					NCP_FINFO(inode)->DosDirNum = server->root.finfo.i.DosDirNum;
				} else {
					DPRINTK(KERN_DEBUG "ncpfs: root_dentry->d_inode==NULL\n");
				}
			} else {
				DPRINTK(KERN_DEBUG "ncpfs: root_dentry==NULL\n");
			}
			return 0;
		}
#endif	/* CONFIG_NCPFS_MOUNT_SUBDIR */

#ifdef CONFIG_NCPFS_PACKET_SIGNING	
	case NCP_IOC_SIGN_INIT:
		if ((permission(inode, MAY_WRITE) != 0)
		    && (current->uid != server->m.mounted_uid))
		{
			return -EACCES;
		}
		if (server->sign_active)
		{
			return -EINVAL;
		}
		if (server->sign_wanted)
		{
			struct ncp_sign_init sign;

			if (copy_from_user(&sign, (struct ncp_sign_init *) arg,
			      sizeof(sign))) return -EFAULT;
			memcpy(server->sign_root,sign.sign_root,8);
			memcpy(server->sign_last,sign.sign_last,16);
			server->sign_active = 1;
		}
		/* ignore when signatures not wanted */
		return 0;		
		
        case NCP_IOC_SIGN_WANTED:
		if (   (permission(inode, MAY_READ) != 0)
		    && (current->uid != server->m.mounted_uid))
		{
			return -EACCES;
		}
		
                if (put_user(server->sign_wanted, (int*) arg))
			return -EFAULT;
                return 0;
	case NCP_IOC_SET_SIGN_WANTED:
		{
			int newstate;

			if (   (permission(inode, MAY_WRITE) != 0)
			    && (current->uid != server->m.mounted_uid))
			{
				return -EACCES;
			}
			/* get only low 8 bits... */
			get_user_ret(newstate, (unsigned char*)arg, -EFAULT);
			if (server->sign_active) {
				/* cannot turn signatures OFF when active */
				if (!newstate) return -EINVAL;
			} else {
				server->sign_wanted = newstate != 0;
			}
			return 0;
		}

#endif /* CONFIG_NCPFS_PACKET_SIGNING */

#ifdef CONFIG_NCPFS_IOCTL_LOCKING
	case NCP_IOC_LOCKUNLOCK:
		if (   (permission(inode, MAY_WRITE) != 0)
		    && (current->uid != server->m.mounted_uid))
		{
			return -EACCES;
		}
		{
			struct ncp_lock_ioctl	 rqdata;
			int result;

			if (copy_from_user(&rqdata, (struct ncp_lock_ioctl*)arg,
				sizeof(rqdata))) return -EFAULT;
			if (rqdata.origin != 0)
				return -EINVAL;
			/* check for cmd */
			switch (rqdata.cmd) {
				case NCP_LOCK_EX:
				case NCP_LOCK_SH:
						if (rqdata.timeout == 0)
							rqdata.timeout = NCP_LOCK_DEFAULT_TIMEOUT;
						else if (rqdata.timeout > NCP_LOCK_MAX_TIMEOUT)
							rqdata.timeout = NCP_LOCK_MAX_TIMEOUT;
						break;
				case NCP_LOCK_LOG:
						rqdata.timeout = NCP_LOCK_DEFAULT_TIMEOUT;	/* has no effect */
				case NCP_LOCK_CLEAR:
						break;
				default:
						return -EINVAL;
			}
			if ((result = ncp_make_open(inode, O_RDWR)) != 0)
			{
				return result;
			}
			if (!ncp_conn_valid(server))
			{
				return -EIO;
			}
			if (!S_ISREG(inode->i_mode))
			{
				return -EISDIR;
			}
			if (!NCP_FINFO(inode)->opened)
			{
				return -EBADFD;
			}
			if (rqdata.cmd == NCP_LOCK_CLEAR)
			{
				result = ncp_ClearPhysicalRecord(NCP_SERVER(inode),
							NCP_FINFO(inode)->file_handle, 
							rqdata.offset,
							rqdata.length);
				if (result > 0) result = 0;	/* no such lock */
			}
			else
			{
				int lockcmd;

				switch (rqdata.cmd)
				{
					case NCP_LOCK_EX:  lockcmd=1; break;
					case NCP_LOCK_SH:  lockcmd=3; break;
					default:	   lockcmd=0; break;
				}
				result = ncp_LogPhysicalRecord(NCP_SERVER(inode),
							NCP_FINFO(inode)->file_handle,
							lockcmd,
							rqdata.offset,
							rqdata.length,
							rqdata.timeout);
				if (result > 0) result = -EAGAIN;
			}
			return result;
		}
#endif	/* CONFIG_NCPFS_IOCTL_LOCKING */

#ifdef CONFIG_NCPFS_NDS_DOMAINS
	case NCP_IOC_GETOBJECTNAME:
		if (   (permission(inode, MAY_READ) != 0)
		    && (current->uid != server->m.mounted_uid)) {
			return -EACCES;
		}
		{
			struct ncp_objectname_ioctl user;
			int outl;

			if ((result = verify_area(VERIFY_WRITE,
					   (struct ncp_objectname_ioctl*)arg,
					   sizeof(user))) != 0) {
				return result;
			}
			if (copy_from_user(&user, 
					   (struct ncp_objectname_ioctl*)arg,
					   sizeof(user))) return -EFAULT;
			user.auth_type = server->auth.auth_type;
			outl = user.object_name_len;
			user.object_name_len = server->auth.object_name_len;
			if (outl > user.object_name_len)
				outl = user.object_name_len;
			if (outl) {
				if (copy_to_user(user.object_name,
						 server->auth.object_name,
						 outl)) return -EFAULT;
			}
			if (copy_to_user((struct ncp_objectname_ioctl*)arg,
					 &user,
					 sizeof(user))) return -EFAULT;
			return 0;
		}
	case NCP_IOC_SETOBJECTNAME:
		if (   (permission(inode, MAY_WRITE) != 0)
		    && (current->uid != server->m.mounted_uid)) {
			return -EACCES;
		}
		{
			struct ncp_objectname_ioctl user;
			void* newname;
			void* oldname;
			size_t oldnamelen;
			void* oldprivate;
			size_t oldprivatelen;

			if (copy_from_user(&user, 
					   (struct ncp_objectname_ioctl*)arg,
					   sizeof(user))) return -EFAULT;
			if (user.object_name_len > NCP_OBJECT_NAME_MAX_LEN)
				return -ENOMEM;
			if (user.object_name_len) {
				newname = ncp_kmalloc(user.object_name_len, GFP_USER);
				if (!newname) return -ENOMEM;
				if (copy_from_user(newname, user.object_name, sizeof(user))) {
					ncp_kfree_s(newname, user.object_name_len);
					return -EFAULT;
				}
			} else {
				newname = NULL;
			}
			/* enter critical section */
			/* maybe that kfree can sleep so do that this way */
			/* it is at least more SMP friendly (in future...) */
			oldname = server->auth.object_name;
			oldnamelen = server->auth.object_name_len;
			oldprivate = server->priv.data;
			oldprivatelen = server->priv.len;
			server->auth.auth_type = user.auth_type;
			server->auth.object_name_len = user.object_name_len;
			server->auth.object_name = user.object_name;
			server->priv.len = 0;
			server->priv.data = NULL;
			/* leave critical section */
			if (oldprivate) ncp_kfree_s(oldprivate, oldprivatelen);
			if (oldname) ncp_kfree_s(oldname, oldnamelen);
			return 0;
		}
	case NCP_IOC_GETPRIVATEDATA:
		if (   (permission(inode, MAY_READ) != 0)
		    && (current->uid != server->m.mounted_uid)) {
			return -EACCES;
		}
		{
			struct ncp_privatedata_ioctl user;
			int outl;

			if ((result = verify_area(VERIFY_WRITE,
					   (struct ncp_privatedata_ioctl*)arg,
					   sizeof(user))) != 0) {
				return result;
			}
			if (copy_from_user(&user, 
					   (struct ncp_privatedata_ioctl*)arg,
					   sizeof(user))) return -EFAULT;
			outl = user.len;
			user.len = server->priv.len;
			if (outl > user.len) outl = user.len;
			if (outl) {
				if (copy_to_user(user.data,
						 server->priv.data,
						 outl)) return -EFAULT;
			}
			if (copy_to_user((struct ncp_privatedata_ioctl*)arg,
					 &user,
					 sizeof(user))) return -EFAULT;
			return 0;
		}
	case NCP_IOC_SETPRIVATEDATA:
		if (   (permission(inode, MAY_WRITE) != 0)
		    && (current->uid != server->m.mounted_uid)) {
			return -EACCES;
		}
		{
			struct ncp_privatedata_ioctl user;
			void* new;
			void* old;
			size_t oldlen;

			if (copy_from_user(&user, 
					   (struct ncp_privatedata_ioctl*)arg,
					   sizeof(user))) return -EFAULT;
			if (user.len > NCP_PRIVATE_DATA_MAX_LEN)
				return -ENOMEM;
			if (user.len) {
				new = ncp_kmalloc(user.len, GFP_USER);
				if (!new) return -ENOMEM;
				if (copy_from_user(new, user.data, user.len)) {
					ncp_kfree_s(new, user.len);
					return -EFAULT;
				}
			} else {
				new = NULL;
			}
			/* enter critical section */
			old = server->priv.data;
			oldlen = server->priv.len;
			server->priv.len = user.len;
			server->priv.data = new;
			/* leave critical section */
			if (old) ncp_kfree_s(old, oldlen);
			return 0;
		}
#endif	/* CONFIG_NCPFS_NDS_DOMAINS */
	default:
		return -EINVAL;
	}
}
