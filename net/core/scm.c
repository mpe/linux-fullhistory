/* scm.c - Socket level control messages processing.
 *
 * Author:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *              Alignment and value checking mods by Craig Metz
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/stat.h>
#include <linux/socket.h>
#include <linux/fcntl.h>
#include <linux/net.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/rarp.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/scm.h>


/*
 *	Only allow a user to send credentials, that they could set with 
 *	setu(g)id.
 */

static __inline__ int scm_check_creds(struct ucred *creds)
{
	if (suser())
		return 0;
	if (creds->pid != current->pid ||
	    (creds->uid != current->uid && creds->uid != current->euid &&
	     creds->uid != current->suid) ||
	    (creds->gid != current->gid && creds->gid != current->egid &&
	     creds->gid != current->sgid))
		return -EPERM;
	return 0;
}


static int scm_fp_copy(struct cmsghdr *cmsg, struct scm_fp_list **fplp)
{
	int num;
	struct scm_fp_list *fpl = *fplp;
	struct file **fpp;
	int *fdp = (int*)CMSG_DATA(cmsg);
	int i;

	num = (cmsg->cmsg_len - CMSG_ALIGN(sizeof(struct cmsghdr)))/sizeof(int);

	if (num <= 0)
		return 0;

	if (num > SCM_MAX_FD)
		return -EINVAL;

	if (!fpl)
	{
		fpl = kmalloc(sizeof(struct scm_fp_list), GFP_KERNEL);
		if (!fpl)
			return -ENOMEM;
		*fplp = fpl;
		fpl->count = 0;
	}
	fpp = &fpl->fp[fpl->count];

	if (fpl->count + num > SCM_MAX_FD)
		return -EINVAL;
	
	/*
	 *	Verify the descriptors.
	 */
	 
	for (i=0; i< num; i++)
	{
		int fd;
		
		fd = fdp[i];
		if (fd < 0 || fd >= NR_OPEN)
			return -EBADF;
		if (current->files->fd[fd]==NULL)
			return -EBADF;
		fpp[i] = current->files->fd[fd];
	}
	
        /* add another reference to these files */
	for (i=0; i< num; i++, fpp++)
		(*fpp)->f_count++;
	fpl->count += num;
	
	return num;
}

void __scm_destroy(struct scm_cookie *scm)
{
	int i;
	struct scm_fp_list *fpl = scm->fp;

	if (!fpl)
		return;

	for (i=fpl->count-1; i>=0; i--)
		close_fp(fpl->fp[i]);

	kfree(fpl);
}



extern __inline__ int not_one_bit(unsigned val)
{
	return (val-1) & val;
}


int __scm_send(struct socket *sock, struct msghdr *msg, struct scm_cookie *p)
{
	int err;
	struct cmsghdr *cmsg;
	struct file *file;
	int acc_fd;
	unsigned scm_flags=0;

	for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg))
	{
		if (cmsg->cmsg_level != SOL_SOCKET)
			continue;

		err = -EINVAL;

		switch (cmsg->cmsg_type)
		{
		case SCM_RIGHTS:
			err=scm_fp_copy(cmsg, &p->fp);
			if (err<0)
				goto error;
			break;
		case SCM_CREDENTIALS:
			if (cmsg->cmsg_len != CMSG_LEN(sizeof(struct ucred)))
				goto error;
			memcpy(&p->creds, CMSG_DATA(cmsg), sizeof(struct ucred));
			err = scm_check_creds(&p->creds);
			if (err)
				goto error;
			break;
		case SCM_CONNECT:
			if (scm_flags)
				goto error;
			if (cmsg->cmsg_len != CMSG_LEN(sizeof(int)))
				goto error;
			memcpy(&acc_fd, CMSG_DATA(cmsg), sizeof(int));
			p->sock = NULL;
			if (acc_fd != -1) {
				if (acc_fd < 0 || acc_fd >= NR_OPEN ||
				    (file=current->files->fd[acc_fd])==NULL)
					return -EBADF;
				if (!file->f_dentry->d_inode || !file->f_dentry->d_inode->i_sock)
					return -ENOTSOCK;
				p->sock = &file->f_dentry->d_inode->u.socket_i;
				if (p->sock->state != SS_UNCONNECTED) 
					return -EINVAL;
			}
			scm_flags |= MSG_SYN;
			break;
		default:
			goto error;
		}
	}

	if (p->fp && !p->fp->count)
	{
		kfree(p->fp);
		p->fp = NULL;
	}
	
	err = -EINVAL;
	msg->msg_flags |= scm_flags;
	scm_flags = msg->msg_flags&MSG_CTLFLAGS;
	if (not_one_bit(scm_flags))
		goto error;

	if (!(scm_flags && p->fp))
		return 0;

error:
	scm_destroy(p);
	return err;
}

void put_cmsg(struct msghdr * msg, int level, int type, int len, void *data)
{
	struct cmsghdr *cm = (struct cmsghdr*)msg->msg_control;
	int cmlen = CMSG_LEN(len);
	int err;

	if (cm==NULL || msg->msg_controllen < sizeof(*cm)) {
		msg->msg_flags |= MSG_CTRUNC;
		return;
	}
	if (msg->msg_controllen < cmlen) {
		msg->msg_flags |= MSG_CTRUNC;
		cmlen = msg->msg_controllen;
	}
	err = put_user(level, &cm->cmsg_level);
	if (!err)
		err = put_user(type, &cm->cmsg_type);
	if (!err)
		err = put_user(cmlen, &cm->cmsg_len);
	if (!err)
		err = copy_to_user(CMSG_DATA(cm), data, cmlen - sizeof(struct cmsghdr));
	if (!err) {
		cmlen = CMSG_SPACE(len);
		msg->msg_control += cmlen;
		msg->msg_controllen -= cmlen;
	}
}

void scm_detach_fds(struct msghdr *msg, struct scm_cookie *scm)
{
	struct cmsghdr *cm = (struct cmsghdr*)msg->msg_control;

	int fdmax = (msg->msg_controllen - sizeof(struct cmsghdr))/sizeof(int);
	int fdnum = scm->fp->count;
	int *cmfptr;
	int err = 0;
	int i;
	struct file **fp = scm->fp->fp;

	if (fdnum < fdmax)
		fdmax = fdnum;

	for (i=0, cmfptr=(int*)CMSG_DATA(cm); i<fdmax; i++, cmfptr++)
	{
		int new_fd = get_unused_fd();
		if (new_fd < 0)
			break;
		current->files->fd[new_fd] = fp[i];
		err = put_user(new_fd, cmfptr);
	}

	if (i > 0)
	{
		int cmlen = CMSG_LEN(i*sizeof(int));
		if (!err)
			err = put_user(SOL_SOCKET, &cm->cmsg_level);
		if (!err)
			err = put_user(SCM_RIGHTS, &cm->cmsg_type);
		if (!err)
			err = put_user(cmlen, &cm->cmsg_len);
		if (!err) {
			cmlen = CMSG_SPACE(i*sizeof(int));
			msg->msg_control += cmlen;
			msg->msg_controllen -= cmlen;
		}
	}

	if (err)
		i = 0;

	/*
	 *	Dump those that don't fit.
	 */
	for ( ; i < fdnum; i++)	{
		msg->msg_flags |= MSG_CTRUNC;
		close_fp(fp[i]);
	}

	kfree (scm->fp);
	scm->fp = NULL;
}

struct scm_fp_list *scm_fp_dup(struct scm_fp_list *fpl)
{
	int i;
	struct scm_fp_list *new_fpl;

	if (!fpl)
		return NULL;

	new_fpl = kmalloc(fpl->count*sizeof(int) + sizeof(*fpl), GFP_KERNEL);
	if (!new_fpl)
		return NULL;

	memcpy(new_fpl, fpl, fpl->count*sizeof(int) + sizeof(*fpl));

	for (i=fpl->count-1; i>=0; i--)
		fpl->fp[i]->f_count++;

	return new_fpl;
}
