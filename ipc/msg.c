/*
 * linux/ipc/msg.c
 * Copyright (C) 1992 Krishna Balasubramanian 
 *
 * Kerneld extensions by Bjorn Ekwall <bj0rn@blox.se> in May 1995, and May 1996
 *
 * See <linux/kerneld.h> for the (optional) new kerneld protocol
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/msg.h>
#include <linux/stat.h>
#include <linux/malloc.h>
#include <linux/kerneld.h>
#include <linux/interrupt.h>

#include <asm/segment.h>

extern int ipcperms (struct ipc_perm *ipcp, short msgflg);

static void freeque (int id);
static int newque (key_t key, int msgflg);
static int findkey (key_t key);

static struct msqid_ds *msgque[MSGMNI];
static int msgbytes = 0;
static int msghdrs = 0;
static unsigned short msg_seq = 0;
static int used_queues = 0;
static int max_msqid = 0;
static struct wait_queue *msg_lock = NULL;
static int kerneld_msqid = -1;

#define MAX_KERNELDS 20
static int kerneld_arr[MAX_KERNELDS];
static int n_kernelds = 0;

void msg_init (void)
{
	int id;
	
	for (id = 0; id < MSGMNI; id++) 
		msgque[id] = (struct msqid_ds *) IPC_UNUSED;
	msgbytes = msghdrs = msg_seq = max_msqid = used_queues = 0;
	msg_lock = NULL;
	return;
}

/*
 * If the send queue is full, try to free any old messages.
 * These are most probably unwanted, since no one has picked them up...
 */
#define MSG_FLUSH_TIME 10 /* seconds */
static void flush_msg(struct msqid_ds *msq)
{
	struct msg *nmsg;
	unsigned long flags;
	int flushed = 0;

	save_flags(flags);
	cli();

	/* messages were put on the queue in time order */
	while ( (nmsg = msq->msg_first) &&
		((CURRENT_TIME - nmsg->msg_stime) > MSG_FLUSH_TIME)) {
		msgbytes -= nmsg->msg_ts; 
		msghdrs--; 
		msq->msg_cbytes -= nmsg->msg_ts;
		msq->msg_qnum--;
		msq->msg_first = nmsg->msg_next;
		++flushed;
		kfree(nmsg);
	}

	if (msq->msg_qnum == 0)
		msq->msg_first = msq->msg_last = NULL;
	restore_flags(flags);
	if (flushed)
		printk(KERN_WARNING "flushed %d old SYSVIPC messages", flushed);
}

static int real_msgsnd (int msqid, struct msgbuf *msgp, size_t msgsz, int msgflg)
{
	int id, err;
	struct msqid_ds *msq;
	struct ipc_perm *ipcp;
	struct msg *msgh;
	long mtype;
	unsigned long flags;
	
	if (msgsz > MSGMAX || (long) msgsz < 0 || msqid < 0)
		return -EINVAL;
	if (!msgp) 
		return -EFAULT;
	/*
	 * Calls from kernel level (IPC_KERNELD set)
	 * have the message somewhere in kernel space already!
	 */
	if ((msgflg & IPC_KERNELD))
		mtype = msgp->mtype;
	else {
		err = verify_area (VERIFY_READ, msgp->mtext, msgsz);
		if (err) 
			return err;
		if ((mtype = get_user (&msgp->mtype)) < 1)
			return -EINVAL;
	}
	id = (unsigned int) msqid % MSGMNI;
	msq = msgque [id];
	if (msq == IPC_UNUSED || msq == IPC_NOID)
		return -EINVAL;
	ipcp = &msq->msg_perm; 

 slept:
	if (msq->msg_perm.seq != (unsigned int) msqid / MSGMNI) 
		return -EIDRM;
	/*
	 * Non-root kernel level processes may send to kerneld! 
	 * i.e. no permission check if called from the kernel
	 * otoh we don't want user level non-root snoopers...
	 */
	if ((msgflg & IPC_KERNELD) == 0)
		if (ipcperms(ipcp, S_IWUGO)) 
			return -EACCES;
	
	if (msgsz + msq->msg_cbytes > msq->msg_qbytes) { 
		if ((kerneld_msqid != -1) && (kerneld_msqid == msqid))
			flush_msg(msq); /* flush the kerneld channel only */
		if (msgsz + msq->msg_cbytes > msq->msg_qbytes) { 
			/* still no space in queue */
			if (msgflg & IPC_NOWAIT)
				return -EAGAIN;
			if (current->signal & ~current->blocked)
				return -EINTR;
			if (intr_count) {
				/* Very unlikely, but better safe than sorry */
				printk(KERN_WARNING "Ouch, kerneld:msgsnd buffers full!\n");
				return -EINTR;
			}
			interruptible_sleep_on (&msq->wwait);
			goto slept;
		}
	}
	
	/* allocate message header and text space*/ 
	msgh = (struct msg *) kmalloc (sizeof(*msgh) + msgsz, GFP_ATOMIC);
	if (!msgh)
		return -ENOMEM;
	msgh->msg_spot = (char *) (msgh + 1);

	/*
	 * Calls from kernel level (IPC_KERNELD set)
	 * have the message somewhere in kernel space already!
	 */
	if (msgflg & IPC_KERNELD) {
		struct kerneld_msg *kdmp = (struct kerneld_msg *)msgp;

		/*
		 * Note that the kernel supplies a pointer
		 * but the user-level kerneld uses a char array...
		 */
		memcpy(msgh->msg_spot, (char *)(&(kdmp->id)), KDHDR); 
		memcpy(msgh->msg_spot + KDHDR, kdmp->text, msgsz - KDHDR); 
	}
	else
		memcpy_fromfs (msgh->msg_spot, msgp->mtext, msgsz); 
	
	if (msgque[id] == IPC_UNUSED || msgque[id] == IPC_NOID
		|| msq->msg_perm.seq != (unsigned int) msqid / MSGMNI) {
		kfree(msgh);
		return -EIDRM;
	}

	msgh->msg_next = NULL;
	msgh->msg_ts = msgsz;
	msgh->msg_type = mtype;
	msgh->msg_stime = CURRENT_TIME;

	save_flags(flags);
	cli();
	if (!msq->msg_first)
		msq->msg_first = msq->msg_last = msgh;
	else {
		msq->msg_last->msg_next = msgh;
		msq->msg_last = msgh;
	}
	msq->msg_cbytes += msgsz;
	msgbytes  += msgsz;
	msghdrs++;
	msq->msg_qnum++;
	msq->msg_lspid = current->pid;
	msq->msg_stime = CURRENT_TIME;
	restore_flags(flags);
	if (msq->rwait)
		wake_up (&msq->rwait);
	return 0;
}

/*
 * Take care of missing kerneld, especially in case of multiple daemons
 */
#define KERNELD_TIMEOUT 1 * (HZ)
#define DROP_TIMER del_timer(&kd_timer)
/*#define DROP_TIMER if ((msgflg & IPC_KERNELD) && kd_timer.next && kd_timer.prev) del_timer(&kd_timer)*/

static void kd_timeout(unsigned long msgid)
{
	struct msqid_ds *msq;
	struct msg *tmsg;
	unsigned long flags;

	msq = msgque [ (unsigned int) kerneld_msqid % MSGMNI ];
	if (msq == IPC_NOID || msq == IPC_UNUSED)
		return;

	save_flags(flags);
	cli();
	for (tmsg = msq->msg_first; tmsg; tmsg = tmsg->msg_next)
		if (*(long *)(tmsg->msg_spot) == msgid)
			break;
	restore_flags(flags);
	if (tmsg) { /* still there! */
		struct kerneld_msg kmsp = { msgid, NULL_KDHDR, "" };

		printk(KERN_ALERT "Ouch, no kerneld for message %ld\n", msgid);
		kmsp.id = -ENODEV;
		real_msgsnd(kerneld_msqid, (struct msgbuf *)&kmsp, KDHDR,
			S_IRUSR | S_IWUSR | IPC_KERNELD | MSG_NOERROR);
	}
}

static int real_msgrcv (int msqid, struct msgbuf *msgp, size_t msgsz, long msgtyp, int msgflg)
{
	struct timer_list kd_timer = { NULL, NULL, 0, 0, 0};
	struct msqid_ds *msq;
	struct ipc_perm *ipcp;
	struct msg *tmsg, *leastp = NULL;
	struct msg *nmsg = NULL;
	int id, err;
	unsigned long flags;

	if (msqid < 0 || (long) msgsz < 0)
		return -EINVAL;
	if (!msgp || !msgp->mtext)
	    return -EFAULT;
	/*
	 * Calls from kernel level (IPC_KERNELD set)
	 * wants the message put in kernel space!
	 */
	if ((msgflg & IPC_KERNELD) == 0) {
		err = verify_area (VERIFY_WRITE, msgp->mtext, msgsz);
		if (err)
			return err;
	}

	id = (unsigned int) msqid % MSGMNI;
	msq = msgque [id];
	if (msq == IPC_NOID || msq == IPC_UNUSED)
		return -EINVAL;
	ipcp = &msq->msg_perm; 

	/*
	 * Start timer for missing kerneld
	 */
	if (msgflg & IPC_KERNELD) {
		kd_timer.data = (unsigned long)msgtyp;
		kd_timer.expires = jiffies + KERNELD_TIMEOUT;
		kd_timer.function = kd_timeout;
		add_timer(&kd_timer);
	}

	/* 
	 *  find message of correct type.
	 *  msgtyp = 0 => get first.
	 *  msgtyp > 0 => get first message of matching type.
	 *  msgtyp < 0 => get message with least type must be < abs(msgtype).  
	 */
	while (!nmsg) {
		if (msq->msg_perm.seq != (unsigned int) msqid / MSGMNI) {
			DROP_TIMER;
			return -EIDRM;
		}
		if ((msgflg & IPC_KERNELD) == 0) {
			/*
			 * All kernel level processes may receive from kerneld! 
			 * i.e. no permission check if called from the kernel
			 * otoh we don't want user level non-root snoopers...
			 */
			if (ipcperms (ipcp, S_IRUGO)) {
				DROP_TIMER; /* Not needed, but doesn't hurt */
				return -EACCES;
			}
		}

		save_flags(flags);
		cli();
		if (msgtyp == 0) 
			nmsg = msq->msg_first;
		else if (msgtyp > 0) {
			if (msgflg & MSG_EXCEPT) { 
				for (tmsg = msq->msg_first; tmsg; 
				     tmsg = tmsg->msg_next)
					if (tmsg->msg_type != msgtyp)
						break;
				nmsg = tmsg;
			} else {
				for (tmsg = msq->msg_first; tmsg; 
				     tmsg = tmsg->msg_next)
					if (tmsg->msg_type == msgtyp)
						break;
				nmsg = tmsg;
			}
		} else {
			for (leastp = tmsg = msq->msg_first; tmsg; 
			     tmsg = tmsg->msg_next) 
				if (tmsg->msg_type < leastp->msg_type) 
					leastp = tmsg;
			if (leastp && leastp->msg_type <= - msgtyp)
				nmsg = leastp;
		}
		restore_flags(flags);
		
		if (nmsg) { /* done finding a message */
			DROP_TIMER;
			if ((msgsz < nmsg->msg_ts) && !(msgflg & MSG_NOERROR)) {
				return -E2BIG;
			}
			msgsz = (msgsz > nmsg->msg_ts)? nmsg->msg_ts : msgsz;
			save_flags(flags);
			cli();
			if (nmsg ==  msq->msg_first)
				msq->msg_first = nmsg->msg_next;
			else {
				for (tmsg = msq->msg_first; tmsg; 
				     tmsg = tmsg->msg_next)
					if (tmsg->msg_next == nmsg) 
						break;
				tmsg->msg_next = nmsg->msg_next;
				if (nmsg == msq->msg_last)
					msq->msg_last = tmsg;
			}
			if (!(--msq->msg_qnum))
				msq->msg_last = msq->msg_first = NULL;
			
			msq->msg_rtime = CURRENT_TIME;
			msq->msg_lrpid = current->pid;
			msgbytes -= nmsg->msg_ts; 
			msghdrs--; 
			msq->msg_cbytes -= nmsg->msg_ts;
			restore_flags(flags);
			if (msq->wwait)
				wake_up (&msq->wwait);
			/*
			 * Calls from kernel level (IPC_KERNELD set)
			 * wants the message copied to kernel space!
			 */
			if (msgflg & IPC_KERNELD) {
				struct kerneld_msg *kdmp = (struct kerneld_msg *) msgp;

				memcpy((char *)(&(kdmp->id)),
					nmsg->msg_spot, KDHDR); 
				/*
				 * Note that kdmp->text is a pointer
				 * when called from kernel space!
				 */
				if ((msgsz > KDHDR) && kdmp->text)
					memcpy(kdmp->text,
						nmsg->msg_spot + KDHDR,
						msgsz - KDHDR); 
			}
			else {
				put_user (nmsg->msg_type, &msgp->mtype);
				memcpy_tofs (msgp->mtext, nmsg->msg_spot, msgsz);
			}
			kfree(nmsg);
			return msgsz;
		} else {  /* did not find a message */
			if (msgflg & IPC_NOWAIT) {
				DROP_TIMER;
				return -ENOMSG;
			}
			if (current->signal & ~current->blocked) {
				DROP_TIMER;
				return -EINTR; 
			}
			interruptible_sleep_on (&msq->rwait);
		}
	} /* end while */
	DROP_TIMER;
	return -1;
}

asmlinkage int sys_msgsnd (int msqid, struct msgbuf *msgp, size_t msgsz, int msgflg)
{
	/* IPC_KERNELD is used as a marker for kernel level calls */
	return real_msgsnd(msqid, msgp, msgsz, msgflg & ~IPC_KERNELD);
}

asmlinkage int sys_msgrcv (int msqid, struct msgbuf *msgp, size_t msgsz,
	long msgtyp, int msgflg)
{
	/* IPC_KERNELD is used as a marker for kernel level calls */
	return real_msgrcv (msqid, msgp, msgsz, msgtyp, msgflg & ~IPC_KERNELD);
}

static int findkey (key_t key)
{
	int id;
	struct msqid_ds *msq;
	
	for (id = 0; id <= max_msqid; id++) {
		while ((msq = msgque[id]) == IPC_NOID) 
			interruptible_sleep_on (&msg_lock);
		if (msq == IPC_UNUSED)
			continue;
		if (key == msq->msg_perm.key)
			return id;
	}
	return -1;
}

static int newque (key_t key, int msgflg)
{
	int id;
	struct msqid_ds *msq;
	struct ipc_perm *ipcp;

	for (id = 0; id < MSGMNI; id++) 
		if (msgque[id] == IPC_UNUSED) {
			msgque[id] = (struct msqid_ds *) IPC_NOID;
			goto found;
		}
	return -ENOSPC;

found:
	msq = (struct msqid_ds *) kmalloc (sizeof (*msq), GFP_KERNEL);
	if (!msq) {
		msgque[id] = (struct msqid_ds *) IPC_UNUSED;
		if (msg_lock)
			wake_up (&msg_lock);
		return -ENOMEM;
	}
	ipcp = &msq->msg_perm;
	ipcp->mode = (msgflg & S_IRWXUGO);
	ipcp->key = key;
	ipcp->cuid = ipcp->uid = current->euid;
	ipcp->gid = ipcp->cgid = current->egid;
	msq->msg_perm.seq = msg_seq;
	msq->msg_first = msq->msg_last = NULL;
	msq->rwait = msq->wwait = NULL;
	msq->msg_cbytes = msq->msg_qnum = 0;
	msq->msg_lspid = msq->msg_lrpid = 0;
	msq->msg_stime = msq->msg_rtime = 0;
	msq->msg_qbytes = MSGMNB;
	msq->msg_ctime = CURRENT_TIME;
	if (id > max_msqid)
		max_msqid = id;
	msgque[id] = msq;
	used_queues++;
	if (msg_lock)
		wake_up (&msg_lock);
	return (unsigned int) msq->msg_perm.seq * MSGMNI + id;
}

asmlinkage int sys_msgget (key_t key, int msgflg)
{
	int id;
	struct msqid_ds *msq;
	
	/*
	 * If the IPC_KERNELD flag is set, the key is forced to IPC_PRIVATE,
	 * and a designated kerneld message queue is created/referred to
	 */
	if ((msgflg & IPC_KERNELD)) {
		int i;
		if (!suser())
			return -EPERM;
#ifdef NEW_KERNELD_PROTOCOL
		if ((msgflg & IPC_KERNELD) == OLDIPC_KERNELD) {
			printk(KERN_ALERT "Please recompile your kerneld daemons!\n");
			return -EPERM;
		}
#endif
		if ((kerneld_msqid == -1) && (kerneld_msqid =
				newque(IPC_PRIVATE, msgflg & S_IRWXU)) < 0)
			return -ENOSPC;
		for (i = 0; i < MAX_KERNELDS; ++i) {
			if (kerneld_arr[i] == 0) {
				kerneld_arr[i] = current->pid;
				++n_kernelds;
				return kerneld_msqid;
			}
		}
		return -ENOSPC;
	}
	/* else it is a "normal" request */
	if (key == IPC_PRIVATE) 
		return newque(key, msgflg);
	if ((id = findkey (key)) == -1) { /* key not used */
		if (!(msgflg & IPC_CREAT))
			return -ENOENT;
		return newque(key, msgflg);
	}
	if (msgflg & IPC_CREAT && msgflg & IPC_EXCL)
		return -EEXIST;
	msq = msgque[id];
	if (msq == IPC_UNUSED || msq == IPC_NOID)
		return -EIDRM;
	if (ipcperms(&msq->msg_perm, msgflg))
		return -EACCES;
	return (unsigned int) msq->msg_perm.seq * MSGMNI + id;
} 

static void freeque (int id)
{
	struct msqid_ds *msq = msgque[id];
	struct msg *msgp, *msgh;

	msq->msg_perm.seq++;
	msg_seq = (msg_seq+1) % ((unsigned)(1<<31)/MSGMNI); /* increment, but avoid overflow */
	msgbytes -= msq->msg_cbytes;
	if (id == max_msqid)
		while (max_msqid && (msgque[--max_msqid] == IPC_UNUSED));
	msgque[id] = (struct msqid_ds *) IPC_UNUSED;
	used_queues--;
	while (msq->rwait || msq->wwait) {
		if (msq->rwait)
			wake_up (&msq->rwait); 
		if (msq->wwait)
			wake_up (&msq->wwait);
		schedule(); 
	}
	for (msgp = msq->msg_first; msgp; msgp = msgh ) {
		msgh = msgp->msg_next;
		msghdrs--;
		kfree(msgp);
	}
	kfree(msq);
}

asmlinkage int sys_msgctl (int msqid, int cmd, struct msqid_ds *buf)
{
	int id, err;
	struct msqid_ds *msq;
	struct msqid_ds tbuf;
	struct ipc_perm *ipcp;
	
	if (msqid < 0 || cmd < 0)
		return -EINVAL;
	switch (cmd) {
	case IPC_INFO: 
	case MSG_INFO: 
		if (!buf)
			return -EFAULT;
	{ 
		struct msginfo msginfo;
		msginfo.msgmni = MSGMNI;
		msginfo.msgmax = MSGMAX;
		msginfo.msgmnb = MSGMNB;
		msginfo.msgmap = MSGMAP;
		msginfo.msgpool = MSGPOOL;
		msginfo.msgtql = MSGTQL;
		msginfo.msgssz = MSGSSZ;
		msginfo.msgseg = MSGSEG;
		if (cmd == MSG_INFO) {
			msginfo.msgpool = used_queues;
			msginfo.msgmap = msghdrs;
			msginfo.msgtql = msgbytes;
		}
		err = verify_area (VERIFY_WRITE, buf, sizeof (struct msginfo));
		if (err)
			return err;
		memcpy_tofs (buf, &msginfo, sizeof(struct msginfo));
		return max_msqid;
	}
	case MSG_STAT:
		if (!buf)
			return -EFAULT;
		err = verify_area (VERIFY_WRITE, buf, sizeof (*buf));
		if (err)
			return err;
		if (msqid > max_msqid)
			return -EINVAL;
		msq = msgque[msqid];
		if (msq == IPC_UNUSED || msq == IPC_NOID)
			return -EINVAL;
		if (ipcperms (&msq->msg_perm, S_IRUGO))
			return -EACCES;
		id = (unsigned int) msq->msg_perm.seq * MSGMNI + msqid;
		tbuf.msg_perm   = msq->msg_perm;
		tbuf.msg_stime  = msq->msg_stime;
		tbuf.msg_rtime  = msq->msg_rtime;
		tbuf.msg_ctime  = msq->msg_ctime;
		tbuf.msg_cbytes = msq->msg_cbytes;
		tbuf.msg_qnum   = msq->msg_qnum;
		tbuf.msg_qbytes = msq->msg_qbytes;
		tbuf.msg_lspid  = msq->msg_lspid;
		tbuf.msg_lrpid  = msq->msg_lrpid;
		memcpy_tofs (buf, &tbuf, sizeof(*buf));
		return id;
	case IPC_SET:
		if (!buf)
			return -EFAULT;
		err = verify_area (VERIFY_READ, buf, sizeof (*buf));
		if (err)
			return err;
		memcpy_fromfs (&tbuf, buf, sizeof (*buf));
		break;
	case IPC_STAT:
		if (!buf)
			return -EFAULT;
		err = verify_area (VERIFY_WRITE, buf, sizeof(*buf));
		if (err)
			return err;
		break;
	}

	id = (unsigned int) msqid % MSGMNI;
	msq = msgque [id];
	if (msq == IPC_UNUSED || msq == IPC_NOID)
		return -EINVAL;
	if (msq->msg_perm.seq != (unsigned int) msqid / MSGMNI)
		return -EIDRM;
	ipcp = &msq->msg_perm;

	switch (cmd) {
	case IPC_STAT:
		if (ipcperms (ipcp, S_IRUGO))
			return -EACCES;
		tbuf.msg_perm   = msq->msg_perm;
		tbuf.msg_stime  = msq->msg_stime;
		tbuf.msg_rtime  = msq->msg_rtime;
		tbuf.msg_ctime  = msq->msg_ctime;
		tbuf.msg_cbytes = msq->msg_cbytes;
		tbuf.msg_qnum   = msq->msg_qnum;
		tbuf.msg_qbytes = msq->msg_qbytes;
		tbuf.msg_lspid  = msq->msg_lspid;
		tbuf.msg_lrpid  = msq->msg_lrpid;
		memcpy_tofs (buf, &tbuf, sizeof (*buf));
		return 0;
	case IPC_SET:
		if (!suser() && current->euid != ipcp->cuid && 
		    current->euid != ipcp->uid)
			return -EPERM;
		if (tbuf.msg_qbytes > MSGMNB && !suser())
			return -EPERM;
		msq->msg_qbytes = tbuf.msg_qbytes;
		ipcp->uid = tbuf.msg_perm.uid;
		ipcp->gid =  tbuf.msg_perm.gid;
		ipcp->mode = (ipcp->mode & ~S_IRWXUGO) | 
			(S_IRWXUGO & tbuf.msg_perm.mode);
		msq->msg_ctime = CURRENT_TIME;
		return 0;
	case IPC_RMID:
		if (!suser() && current->euid != ipcp->cuid && 
		    current->euid != ipcp->uid)
			return -EPERM;
		/*
		 * There is only one kerneld message queue,
		 * mark it as non-existent
		 */
		if ((kerneld_msqid >= 0) && (msqid == kerneld_msqid))
			kerneld_msqid = -1;
		freeque (id); 
		return 0;
	default:
		return -EINVAL;
	}
}

/*
 * We do perhaps need a "flush" for waiting processes,
 * so that if they are terminated, a call from do_exit
 * will minimize the possibility of orphaned received
 * messages in the queue.  For now we just make sure
 * that the queue is shut down whenever all kernelds have died.
 */
void kerneld_exit(void)
{
	int i;

        if (kerneld_msqid == -1)
		return;
	for (i = 0; i < MAX_KERNELDS; ++i) {
		if (kerneld_arr[i] == current->pid) {
			kerneld_arr[i] = 0;
			--n_kernelds;
			if (n_kernelds == 0)
				sys_msgctl(kerneld_msqid, IPC_RMID, NULL);
			break;
		}
	}
}

/*
 * Kerneld internal message format/syntax:
 *
 * The message type from the kernel to kerneld is used to specify _what_
 * function we want kerneld to perform. 
 *
 * The "normal" message area is divided into a header, followed by a char array.
 * The header is used to hold the sequence number of the request, which will
 * be used as the return message type from kerneld back to the kernel.
 * In the return message, the header will be used to store the exit status
 * of the kerneld "job", or task.
 * The character array is used to pass parameters to kerneld and (optional)
 * return information from kerneld back to the kernel.
 * It is the responsibility of kerneld and the kernel level caller
 * to set usable sizes on the parameter/return value array, since
 * that information is _not_ included in the message format
 */

/*
 * The basic kernel level entry point to kerneld.
 *	msgtype should correspond to a task type for (a) kerneld
 *	ret_size is the size of the (optional) return _value,
 *		OR-ed with KERNELD_WAIT if we want an answer
 *	msgsize is the size (in bytes) of the message, not including
 *		the header that is always sent first in a kerneld message
 *	text is the parameter for the kerneld specific task
 *	ret_val is NULL or the kernel address where an expected answer
 *		from kerneld should be placed.
 *
 * See <linux/kerneld.h> for usage (inline convenience functions)
 *
 */
int kerneld_send(int msgtype, int ret_size, int msgsz,
		const char *text, const char *ret_val)
{
	int status = -ENOSYS;
#ifdef CONFIG_KERNELD
	static int id = KERNELD_MINSEQ;
	struct kerneld_msg kmsp = { msgtype, NULL_KDHDR, (char *)text };
	int msgflg = S_IRUSR | S_IWUSR | IPC_KERNELD | MSG_NOERROR;
	unsigned long flags;

	if (kerneld_msqid == -1)
		return -ENODEV;

	/* Do not wait for an answer at interrupt-time! */
	if (intr_count)
		ret_size &= ~KERNELD_WAIT;
#ifdef NEW_KERNELD_PROTOCOL
	else
		kmsp.pid = current->pid;
#endif

	msgsz += KDHDR;
	if (ret_size & KERNELD_WAIT) {
		save_flags(flags);
		cli();
		if (++id <= 0) /* overflow */
			id = KERNELD_MINSEQ;
		kmsp.id = id;
		restore_flags(flags);
	}

	status = real_msgsnd(kerneld_msqid, (struct msgbuf *)&kmsp, msgsz, msgflg);
	if ((status >= 0) && (ret_size & KERNELD_WAIT)) {
		ret_size &= ~KERNELD_WAIT;
		kmsp.text = (char *)ret_val;
		status = real_msgrcv(kerneld_msqid, (struct msgbuf *)&kmsp,
				KDHDR + ((ret_val)?ret_size:0),
				kmsp.id, msgflg);
		if (status > 0) /* a valid answer contains at least a long */
			status = kmsp.id;
	}

#endif /* CONFIG_KERNELD */
	return status;
}
