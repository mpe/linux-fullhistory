/*
 *  ioctl.c
 *
 *  Copyright (C) 1995 by Volker Lendecke
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
	struct ncp_server *server;

	switch(cmd) {
	case NCP_IOC_NCPREQUEST:

		if (!suser())
		{
			return -EPERM;
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

		server = NCP_SERVER(inode);
		ncp_lock_server(server);

		/* FIXME: We hack around in the server's structures
                   here to be able to use ncp_request */

		server->has_subfunction = 0;
		server->current_size =
			request.size + sizeof(struct ncp_request_header);
		memcpy_fromfs(server->packet, request.data,
			      request.size+sizeof(struct ncp_request_header));

		
		ncp_request(server, request.function);

		DPRINTK("ncp_ioctl: copy %d bytes\n",
			server->reply_size);
		memcpy_tofs(request.data, server->packet,
			    server->reply_size);

		ncp_unlock_server(server);

		return server->reply_size;

        case NCP_IOC_GETMOUNTUID:
                if ((result = verify_area(VERIFY_WRITE, (uid_t*) arg,
                                          sizeof(uid_t))) != 0)
		{
                        return result;
                }
                put_fs_word(NCP_SERVER(inode)->m.mounted_uid, (uid_t*) arg);
                return 0;

	default:
		return -EINVAL;
	}
	
	return -EINVAL;
}
