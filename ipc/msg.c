/*
 * linux/ipc/msg.c
 * Copyright (C) 1992 Krishna Balasubramanian 
 *
 * Removed all the remaining kerneld mess
 * Catch the -EFAULT stuff properly
 * Use GFP_KERNEL for messages as in 1.2
 * Fixed up the unchecked user space derefs
 * Copyright (C) 1998 Alan Cox & Andi Kleen
 *
 * /proc/sysvipc/msg support (c) 1999 Dragos Acostachioaie <dragos@iname.com>
 *
 * mostly rewritten, threaded and wake-one semantics added
 * (c) 1999 Manfred Spraul <manfreds@colorfullife.com>
 */

#include <linux/config.h>
#include <linux/malloc.h>
#include <linux/msg.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/list.h>

#include <asm/uaccess.h>

#define USHRT_MAX	0xffff
/* one ms_receiver structure for each sleeping receiver */
struct msg_receiver {
	struct list_head r_list;
	struct task_struct* r_tsk;

	int r_mode;
	long r_msgtype;
	long r_maxsize;

	struct msg_msg* volatile r_msg;
};

/* one msg_msg structure for each message */
struct msg_msg {
	struct list_head m_list; 
	long  m_type;          
	int m_ts;           /* message text size */
	/* the actual message follows immediately */
};


/* one msq_queue structure for each present queue on the system */
struct msg_queue {
	struct ipc_perm q_perm;
	__kernel_time_t q_stime;	/* last msgsnd time */
	__kernel_time_t q_rtime;	/* last msgrcv time */
	__kernel_time_t q_ctime;	/* last change time */
	unsigned int  q_cbytes;		/* current number of bytes on queue */
	unsigned int  q_qnum;		/* number of messages in queue */
	unsigned int  q_qbytes;		/* max number of bytes on queue */
	__kernel_ipc_pid_t q_lspid;	/* pid of last msgsnd */
	__kernel_ipc_pid_t q_lrpid;	/* last receive pid */

	struct list_head q_messages;
	struct list_head q_receivers;
	wait_queue_head_t q_rwait;
};

/* one msq_array structure for each possible queue on the system */
struct msg_array {
	spinlock_t lock;
	struct msg_queue* q;
};

#define SEARCH_ANY		1
#define SEARCH_EQUAL		2
#define SEARCH_NOTEQUAL		3
#define SEARCH_LESSEQUAL	4

static DECLARE_MUTEX(msg_lock);
static struct msg_array msg_que[MSGMNI];

static unsigned short msg_seq = 0;
static int msg_used_queues = 0;
static int msg_max_id = -1;

static atomic_t msg_bytes = ATOMIC_INIT(0);
static atomic_t msg_hdrs = ATOMIC_INIT(0);

static void freeque (int id);
static int newque (key_t key, int msgflg);
static int findkey (key_t key);
#ifdef CONFIG_PROC_FS
static int sysvipc_msg_read_proc(char *buffer, char **start, off_t offset, int length, int *eof, void *data);
#endif

/* implemented in ipc/util.c, thread-safe */
extern int ipcperms (struct ipc_perm *ipcp, short msgflg);

void __init msg_init (void)
{
	int id;

	for (id = 0; id < MSGMNI; id++) {
		msg_que[id].lock = SPIN_LOCK_UNLOCKED;
		msg_que[id].q = NULL;
	}
#ifdef CONFIG_PROC_FS
	create_proc_read_entry("sysvipc/msg", 0, 0, sysvipc_msg_read_proc, NULL);
#endif
}

static int findkey (key_t key)
{
	int id;
	struct msg_queue *msq;
	
	for (id = 0; id <= msg_max_id; id++) {
		msq = msg_que[id].q;
		if(msq == NULL)
			continue;
		if (key == msq->q_perm.key)
			return id;
	}
	return -1;
}

static int newque (key_t key, int msgflg)
{
	int id;
	struct msg_queue *msq;
	struct ipc_perm *ipcp;

	for (id = 0; id < MSGMNI; id++) {
		if (msg_que[id].q == NULL)
			break;
	}
	if(id == MSGMNI)
		return -ENOSPC;

	msq  = (struct msg_queue *) kmalloc (sizeof (*msq), GFP_KERNEL);
	if (!msq) 
		return -ENOMEM;

	ipcp = &msq->q_perm;
	ipcp->mode = (msgflg & S_IRWXUGO);
	ipcp->key = key;
	ipcp->cuid = ipcp->uid = current->euid;
	ipcp->gid = ipcp->cgid = current->egid;

	/* ipcp->seq*MSGMNI must be a positive integer.
	 * this limits MSGMNI to 32768
	 */
	ipcp->seq = msg_seq++;

	msq->q_stime = msq->q_rtime = 0;
	msq->q_ctime = CURRENT_TIME;
	msq->q_cbytes = msq->q_qnum = 0;
	msq->q_qbytes = MSGMNB;
	msq->q_lspid = msq->q_lrpid = 0;
	INIT_LIST_HEAD(&msq->q_messages);
	INIT_LIST_HEAD(&msq->q_receivers);
	init_waitqueue_head(&msq->q_rwait);

	if (id > msg_max_id)
		msg_max_id = id;
	spin_lock(&msg_que[id].lock);
	msg_que[id].q = msq;
	spin_unlock(&msg_que[id].lock);
	msg_used_queues++;

	return (int)msq->q_perm.seq * MSGMNI + id;
}

static void expunge_all(struct msg_queue* msq, int res)
{
	struct list_head *tmp;

	tmp = msq->q_receivers.next;
	while (tmp != &msq->q_receivers) {
		struct msg_receiver* msr;
		
		msr = list_entry(tmp,struct msg_receiver,r_list);
		tmp = tmp->next;
		msr->r_msg = ERR_PTR(res);
		wake_up_process(msr->r_tsk);
	}
}

static void freeque (int id)
{
	struct msg_queue *msq;
	struct list_head *tmp;

	msq=msg_que[id].q;
	msg_que[id].q = NULL;
	if (id == msg_max_id) {
		while ((msg_que[msg_max_id].q == NULL)) {
			if(msg_max_id--== 0)
				break;
		}
	}
	msg_used_queues--;

	expunge_all(msq,-EIDRM);

	while(waitqueue_active(&msq->q_rwait)) {
		wake_up(&msq->q_rwait);
		spin_unlock(&msg_que[id].lock);
		current->policy |= SCHED_YIELD;
		schedule();
		spin_lock(&msg_que[id].lock);
	}
	spin_unlock(&msg_que[id].lock);
		
	tmp = msq->q_messages.next;
	while(tmp != &msq->q_messages) {
		struct msg_msg* msg = list_entry(tmp,struct msg_msg,m_list);
		tmp = tmp->next;
		atomic_dec(&msg_hdrs);
		kfree(msg);
	}
	atomic_sub(msq->q_cbytes, &msg_bytes);
	kfree(msq);
}


asmlinkage long sys_msgget (key_t key, int msgflg)
{
	int id, ret = -EPERM;
	struct msg_queue *msq;
	
	down(&msg_lock);
	if (key == IPC_PRIVATE) 
		ret = newque(key, msgflg);
	else if ((id = findkey (key)) == -1) { /* key not used */
		if (!(msgflg & IPC_CREAT))
			ret = -ENOENT;
		else
			ret = newque(key, msgflg);
	} else if (msgflg & IPC_CREAT && msgflg & IPC_EXCL) {
		ret = -EEXIST;
	} else {
		msq = msg_que[id].q;
		if (ipcperms(&msq->q_perm, msgflg))
			ret = -EACCES;
		else
			ret = (unsigned int) msq->q_perm.seq * MSGMNI + id;
	}
	up(&msg_lock);
	return ret;
}

asmlinkage long sys_msgctl (int msqid, int cmd, struct msqid_ds *buf)
{
	int id, err;
	struct msg_queue *msq;
	struct msqid_ds tbuf;
	struct ipc_perm *ipcp;
	
	if (msqid < 0 || cmd < 0)
		return -EINVAL;
	id = msqid % MSGMNI;
	switch (cmd) {
	case IPC_INFO: 
	case MSG_INFO: 
	{ 
		struct msginfo msginfo;
		if (!buf)
			return -EFAULT;
		/* We must not return kernel stack data.
		 * due to variable alignment, it's not enough
		 * to set all member fields.
		 */
		memset(&msginfo,0,sizeof(msginfo));	
		msginfo.msgmni = MSGMNI;
		msginfo.msgmax = MSGMAX;
		msginfo.msgmnb = MSGMNB;
		msginfo.msgmap = MSGMAP;
		msginfo.msgpool = MSGPOOL;
		msginfo.msgtql = MSGTQL;
		msginfo.msgssz = MSGSSZ;
		msginfo.msgseg = MSGSEG;
		if (cmd == MSG_INFO) {
			msginfo.msgpool = msg_used_queues;
			msginfo.msgmap = atomic_read(&msg_hdrs);
			msginfo.msgtql = atomic_read(&msg_bytes);
		}

		if (copy_to_user (buf, &msginfo, sizeof(struct msginfo)))
			return -EFAULT;
		return (msg_max_id < 0) ? 0: msg_max_id;
	}
	case MSG_STAT:
	case IPC_STAT:
	{
		int success_return;
		if (!buf)
			return -EFAULT;
		if(cmd == MSG_STAT && msqid > MSGMNI)
			return -EINVAL;

		spin_lock(&msg_que[id].lock);
		msq = msg_que[id].q;
		err = -EINVAL;
		if (msq == NULL)
			goto out_unlock;
		if(cmd == MSG_STAT) {
			success_return = (unsigned int) msq->q_perm.seq * MSGMNI + msqid;
		} else {
			err = -EIDRM;
			if (msq->q_perm.seq != (unsigned int) msqid / MSGMNI)
				goto out_unlock;
			success_return = 0;
		}
		err = -EACCES;
		if (ipcperms (&msq->q_perm, S_IRUGO))
			goto out_unlock;

		memset(&tbuf,0,sizeof(tbuf));
		tbuf.msg_perm   = msq->q_perm;
		/* tbuf.msg_{first,last}: not reported.*/
		tbuf.msg_stime  = msq->q_stime;
		tbuf.msg_rtime  = msq->q_rtime;
		tbuf.msg_ctime  = msq->q_ctime;
		if(msq->q_cbytes > USHRT_MAX)
			tbuf.msg_cbytes = USHRT_MAX;
		 else
			tbuf.msg_cbytes = msq->q_cbytes;
		tbuf.msg_lcbytes = msq->q_cbytes;
		
		if(msq->q_qnum > USHRT_MAX)
			tbuf.msg_qnum = USHRT_MAX;
		 else
			tbuf.msg_qnum   = msq->q_qnum;

		if(msq->q_qbytes > USHRT_MAX)
			tbuf.msg_qbytes = USHRT_MAX;
		 else
			tbuf.msg_qbytes = msq->q_qbytes;
		tbuf.msg_lqbytes = msq->q_qbytes;

		tbuf.msg_lspid  = msq->q_lspid;
		tbuf.msg_lrpid  = msq->q_lrpid;
		spin_unlock(&msg_que[id].lock);
		if (copy_to_user (buf, &tbuf, sizeof(*buf)))
			return -EFAULT;
		return success_return;
	}
	case IPC_SET:
		if (!buf)
			return -EFAULT;
		if (copy_from_user (&tbuf, buf, sizeof (*buf)))
			return -EFAULT;
		break;
	case IPC_RMID:
		break;
	default:
		return  -EINVAL;
	}

	down(&msg_lock);
	spin_lock(&msg_que[id].lock);
	msq = msg_que[id].q;
	err = -EINVAL;
	if (msq == NULL)
		goto out_unlock_up;
	err = -EIDRM;
	if (msq->q_perm.seq != (unsigned int) msqid / MSGMNI)
		goto out_unlock_up;
	ipcp = &msq->q_perm;

	switch (cmd) {
	case IPC_SET:
	{
		int newqbytes;
		err = -EPERM;
		if (current->euid != ipcp->cuid && 
		    current->euid != ipcp->uid && !capable(CAP_SYS_ADMIN))
		    /* We _could_ check for CAP_CHOWN above, but we don't */
			goto out_unlock_up;

		if(tbuf.msg_qbytes == 0)
			newqbytes = tbuf.msg_lqbytes;
		 else
		 	newqbytes = tbuf.msg_qbytes;
		if (newqbytes > MSGMNB && !capable(CAP_SYS_RESOURCE))
			goto out_unlock_up;
		msq->q_qbytes = newqbytes;

		ipcp->uid = tbuf.msg_perm.uid;
		ipcp->gid =  tbuf.msg_perm.gid;
		ipcp->mode = (ipcp->mode & ~S_IRWXUGO) | 
			(S_IRWXUGO & tbuf.msg_perm.mode);
		msq->q_ctime = CURRENT_TIME;
		/* sleeping receivers might be excluded by
		 * stricter permissions.
		 */
		expunge_all(msq,-EAGAIN);
		/* sleeping senders might be able to send
		 * due to a larger queue size.
		 */
		wake_up(&msq->q_rwait);
		spin_unlock(&msg_que[id].lock);
		break;
	}
	case IPC_RMID:
		err = -EPERM;
		if (current->euid != ipcp->cuid && 
		    current->euid != ipcp->uid && !capable(CAP_SYS_ADMIN))
			goto out_unlock;
		freeque (id); 
		break;
	}
	err = 0;
out_up:
	up(&msg_lock);
	return err;
out_unlock_up:
	spin_unlock(&msg_que[id].lock);
	goto out_up;
out_unlock:
	spin_unlock(&msg_que[id].lock);
	return err;
}

static int testmsg(struct msg_msg* msg,long type,int mode)
{
	switch(mode)
	{
		case SEARCH_ANY:
			return 1;
		case SEARCH_LESSEQUAL:
			if(msg->m_type <=type)
				return 1;
			break;
		case SEARCH_EQUAL:
			if(msg->m_type == type)
				return 1;
			break;
		case SEARCH_NOTEQUAL:
			if(msg->m_type != type)
				return 1;
			break;
	}
	return 0;
}

int inline pipelined_send(struct msg_queue* msq, struct msg_msg* msg)
{
	struct list_head* tmp;

	tmp = msq->q_receivers.next;
	while (tmp != &msq->q_receivers) {
		struct msg_receiver* msr;
		msr = list_entry(tmp,struct msg_receiver,r_list);
		tmp = tmp->next;
		if(testmsg(msg,msr->r_msgtype,msr->r_mode)) {
			list_del(&msr->r_list);
			if(msr->r_maxsize < msg->m_ts) {
				msr->r_msg = ERR_PTR(-E2BIG);
				wake_up_process(msr->r_tsk);
			} else {
				msr->r_msg = msg;
				msq->q_lspid = msr->r_tsk->pid;
				msq->q_rtime = CURRENT_TIME;
				wake_up_process(msr->r_tsk);
				return 1;
			}
		}
	}
	return 0;
}

asmlinkage long sys_msgsnd (int msqid, struct msgbuf *msgp, size_t msgsz, int msgflg)
{
	int id;
	struct msg_queue *msq;
	struct msg_msg *msg;
	long mtype;
	int err;
	
	if (msgsz > MSGMAX || (long) msgsz < 0 || msqid < 0)
		return -EINVAL;
	if (get_user(mtype, &msgp->mtype))
		return -EFAULT; 
	if (mtype < 1)
		return -EINVAL;

	msg = (struct msg_msg *) kmalloc (sizeof(*msg) + msgsz, GFP_KERNEL);
	if(msg==NULL)
		return -ENOMEM;

	if (copy_from_user(msg+1, msgp->mtext, msgsz)) {
		kfree(msg);
		return -EFAULT;
	}	
	msg->m_type = mtype;
	msg->m_ts = msgsz;

	id = (unsigned int) msqid % MSGMNI;
	spin_lock(&msg_que[id].lock);
	err= -EINVAL;
retry:
	msq = msg_que[id].q;
	if (msq == NULL)
		goto out_free;

	err= -EIDRM;
	if (msq->q_perm.seq != (unsigned int) msqid / MSGMNI) 
		goto out_free;

	err=-EACCES;
	if (ipcperms(&msq->q_perm, S_IWUGO)) 
		goto out_free;

	if(msgsz + msq->q_cbytes > msq->q_qbytes) {
		DECLARE_WAITQUEUE(wait,current);

		if(msgflg&IPC_NOWAIT) {
			err=-EAGAIN;
			goto out_free;
		}
		current->state = TASK_INTERRUPTIBLE;
		add_wait_queue(&msq->q_rwait,&wait);
		spin_unlock(&msg_que[id].lock);
		schedule();
		current->state= TASK_RUNNING;
		
		remove_wait_queue(&msq->q_rwait,&wait);
		if (signal_pending(current)) {
			kfree(msg);
			return -EINTR;
		}

		spin_lock(&msg_que[id].lock);
		err = -EIDRM;
		goto retry;
	}

	if(!pipelined_send(msq,msg)) {
		/* noone is waiting for this message, enqueue it */
		list_add_tail(&msg->m_list,&msq->q_messages);
		msq->q_cbytes += msgsz;
		msq->q_qnum++;
		atomic_add(msgsz,&msg_bytes);
		atomic_inc(&msg_hdrs);
	}
	
	err = 0;
	msg = NULL;
	msq->q_lspid = current->pid;
	msq->q_stime = CURRENT_TIME;

out_free:
	if(msg!=NULL)
		kfree(msg);
	spin_unlock(&msg_que[id].lock);
	return err;
}

int inline convert_mode(long* msgtyp, int msgflg)
{
	/* 
	 *  find message of correct type.
	 *  msgtyp = 0 => get first.
	 *  msgtyp > 0 => get first message of matching type.
	 *  msgtyp < 0 => get message with least type must be < abs(msgtype).  
	 */
	if(*msgtyp==0)
		return SEARCH_ANY;
	if(*msgtyp<0) {
		*msgtyp=-(*msgtyp);
		return SEARCH_LESSEQUAL;
	}
	if(msgflg & MSG_EXCEPT)
		return SEARCH_NOTEQUAL;
	return SEARCH_EQUAL;
}

asmlinkage long sys_msgrcv (int msqid, struct msgbuf *msgp, size_t msgsz,
			    long msgtyp, int msgflg)
{
	struct msg_queue *msq;
	struct msg_receiver msr_d;
	struct list_head* tmp;
	struct msg_msg* msg, *found_msg;
	int id;
	int err;
	int mode;

	if (msqid < 0 || (long) msgsz < 0)
		return -EINVAL;
	mode = convert_mode(&msgtyp,msgflg);

	id = (unsigned int) msqid % MSGMNI;
	spin_lock(&msg_que[id].lock);
retry:
	msq = msg_que[id].q;
	err=-EINVAL;
	if (msq == NULL)
		goto out_unlock;
	err=-EACCES;
	if (ipcperms (&msq->q_perm, S_IRUGO))
		goto out_unlock;

	tmp = msq->q_messages.next;
	found_msg=NULL;
	while (tmp != &msq->q_messages) {
		msg = list_entry(tmp,struct msg_msg,m_list);
		if(testmsg(msg,msgtyp,mode)) {
			found_msg = msg;
			if(mode == SEARCH_LESSEQUAL && msg->m_type != 1) {
				found_msg=msg;
				msgtyp=msg->m_type-1;
			} else {
				found_msg=msg;
				break;
			}
		}
		tmp = tmp->next;
	}
	if(found_msg) {
		msg=found_msg;
		if ((msgsz < msg->m_ts) && !(msgflg & MSG_NOERROR)) {
			err=-E2BIG;
			goto out_unlock;
		}
		list_del(&msg->m_list);
		msq->q_qnum--;
		msq->q_rtime = CURRENT_TIME;
		msq->q_lrpid = current->pid;
		msq->q_cbytes -= msg->m_ts;
		atomic_sub(msg->m_ts,&msg_bytes);
		atomic_dec(&msg_hdrs);
		if(waitqueue_active(&msq->q_rwait))
			wake_up(&msq->q_rwait);
out_success_unlock:
		spin_unlock(&msg_que[id].lock);
out_success:
		msgsz = (msgsz > msg->m_ts) ? msg->m_ts : msgsz;
		if (put_user (msg->m_type, &msgp->mtype) ||
			    copy_to_user (msgp->mtext, msg+1, msgsz))
		{
			    msgsz = -EFAULT;
		}
		kfree(msg);
		return msgsz;
	} else
	{
		/* no message waiting. Prepare for pipelined
		 * receive.
		 */
		if (msgflg & IPC_NOWAIT) {
			err=-ENOMSG;
			goto out_unlock;
		}
		list_add_tail(&msr_d.r_list,&msq->q_receivers);
		msr_d.r_tsk = current;
		msr_d.r_msgtype = msgtyp;
		msr_d.r_mode = mode;
		if(msgflg & MSG_NOERROR)
			msr_d.r_maxsize = MSGMAX;
		 else
		 	msr_d.r_maxsize = msgsz;
		msr_d.r_msg = ERR_PTR(-EAGAIN);
		current->state = TASK_INTERRUPTIBLE;
		spin_unlock(&msg_que[id].lock);
		schedule();
		current->state = TASK_RUNNING;

		msg = (struct msg_msg*) msr_d.r_msg;
		if(!IS_ERR(msg)) 
			goto out_success;

		spin_lock(&msg_que[id].lock);
		msg = (struct msg_msg*)msr_d.r_msg;
		if(!IS_ERR(msg)) {
			/* our message arived while we waited for
			 * the spinlock. Process it.
			 */
			goto out_success_unlock;
		}
		err = PTR_ERR(msg);
		if(err == -EAGAIN) {
			list_del(&msr_d.r_list);
			if (signal_pending(current))
				err=-EINTR;
			 else
				goto retry;
		}
	}
out_unlock:
	spin_unlock(&msg_que[id].lock);
	return err;
}

#ifdef CONFIG_PROC_FS
static int sysvipc_msg_read_proc(char *buffer, char **start, off_t offset, int length, int *eof, void *data)
{
	off_t pos = 0;
	off_t begin = 0;
	int i, len = 0;

	down(&msg_lock);
	len += sprintf(buffer, "       key      msqid perms cbytes  qnum lspid lrpid   uid   gid  cuid  cgid      stime      rtime      ctime\n");

	for(i = 0; i <= msg_max_id; i++) {
		spin_lock(&msg_que[i].lock);
		if(msg_que[i].q != NULL) {
			len += sprintf(buffer + len, "%10d %10d  %4o  %5u %5u %5u %5u %5u %5u %5u %5u %10lu %10lu %10lu\n",
				msg_que[i].q->q_perm.key,
				msg_que[i].q->q_perm.seq * MSGMNI + i,
				msg_que[i].q->q_perm.mode,
				msg_que[i].q->q_cbytes,
				msg_que[i].q->q_qnum,
				msg_que[i].q->q_lspid,
				msg_que[i].q->q_lrpid,
				msg_que[i].q->q_perm.uid,
				msg_que[i].q->q_perm.gid,
				msg_que[i].q->q_perm.cuid,
				msg_que[i].q->q_perm.cgid,
				msg_que[i].q->q_stime,
				msg_que[i].q->q_rtime,
				msg_que[i].q->q_ctime);
			spin_unlock(&msg_que[i].lock);

			pos += len;
			if(pos < offset) {
				len = 0;
				begin = pos;
			}
			if(pos > offset + length)
				goto done;
		} else {
			spin_unlock(&msg_que[i].lock);
		}
	}
	*eof = 1;
done:
	up(&msg_lock);
	*start = buffer + (offset - begin);
	len -= (offset - begin);
	if(len > length)
		len = length;
	if(len < 0)
		len = 0;
	return len;
}
#endif

