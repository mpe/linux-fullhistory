/*
 *  ioctl.c
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *
 */

#include <asm/segment.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ncp_fs.h>
#include <linux/ioctl.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/ncp.h>

int
ncp_ioctl (struct inode * inode, struct file * filp,
           unsigned int cmd, unsigned long arg)
{
	int result;
	struct ncp_ioctl_request request;
	struct ncp_fs_info info;
	struct ncp_server *server = NCP_SERVER(inode);

	/*
	 * Binary compatible with 1.3.XX releases.
	 * Take this out in 2.1.0 development series.
	 * <mec@duracef.shout.net> 12 Mar 1996
	 */
	switch(cmd) {
	case _IOR('n', 1, unsigned char *):
	    cmd = NCP_IOC_NCPREQUEST;
	    break;
	case _IOR('u', 1, uid_t):
	    cmd = NCP_IOC_GETMOUNTUID;
	    break;
	case _IO('l', 1):
	    cmd = NCP_IOC_CONN_LOGGED_IN;
	    break;
	case _IOWR('i', 1, unsigned char *):
	    cmd = NCP_IOC_GET_FS_INFO;
	    break;
	}

	switch(cmd) {
	case NCP_IOC_NCPREQUEST:

		if (   (permission(inode, MAY_WRITE) != 0)
		    && (current->uid != server->m.mounted_uid))
		{
			return -EACCES;
		}
		
		if ((result = verify_area(VERIFY_READ, (char *)arg,
					  sizeof(request))) != 0)
		{
			return result;
		}

		memcpy_fromfs(&request, (struct ncp_ioctl_request *)arg,
			      sizeof(request));

		if (   (request.function > 255)
		    || (request.size >
			NCP_PACKET_SIZE - sizeof(struct ncp_request_header)))
		{
			return -EINVAL;
		}

		if ((result = verify_area(VERIFY_WRITE, (char *)request.data,
					  NCP_PACKET_SIZE)) != 0)
		{
			return result;
		}

		ncp_lock_server(server);

		/* FIXME: We hack around in the server's structures
                   here to be able to use ncp_request */

		server->has_subfunction = 0;
		server->current_size = request.size;
		memcpy_fromfs(server->packet, request.data, request.size);

		ncp_request(server, request.function);

		DPRINTK("ncp_ioctl: copy %d bytes\n",
			server->reply_size);
		memcpy_tofs(request.data, server->packet, server->reply_size);

		ncp_unlock_server(server);

		return server->reply_size;

	case NCP_IOC_CONN_LOGGED_IN:

		if (   (permission(inode, MAY_WRITE) != 0)
		    && (current->uid != server->m.mounted_uid))
		{
			return -EACCES;
		}

		return ncp_conn_logged_in(server);
		
	case NCP_IOC_GET_FS_INFO:

		if (   (permission(inode, MAY_WRITE) != 0)
		    && (current->uid != server->m.mounted_uid))
		{
			return -EACCES;
		}
		
		if ((result = verify_area(VERIFY_WRITE, (char *)arg,
					  sizeof(info))) != 0)
		{
			return result;
		}

		memcpy_fromfs(&info, (struct ncp_fs_info *)arg,
			      sizeof(info));

		if (info.version != NCP_GET_FS_INFO_VERSION)
		{
			DPRINTK("info.version invalid: %d\n", info.version);
			return -EINVAL;
		}

		info.addr          = server->m.serv_addr;
		info.mounted_uid   = server->m.mounted_uid;
		info.connection    = server->connection;
		info.buffer_size   = server->buffer_size;
		info.volume_number = NCP_ISTRUCT(inode)->volNumber;
		info.directory_id  = NCP_ISTRUCT(inode)->DosDirNum;

		memcpy_tofs((struct ncp_fs_info *)arg, &info, sizeof(info));
		return 0;		

        case NCP_IOC_GETMOUNTUID:

		if (   (permission(inode, MAY_READ) != 0)
		    && (current->uid != server->m.mounted_uid))
		{
			return -EACCES;
		}
		
                if ((result = verify_area(VERIFY_WRITE, (uid_t*) arg,
                                          sizeof(uid_t))) != 0)
		{
                        return result;
                }
                put_fs_word(server->m.mounted_uid, (uid_t*) arg);
                return 0;

	default:
		return -EINVAL;
	}
	
	return -EINVAL;
}
