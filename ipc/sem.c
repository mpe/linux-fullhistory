/*
 * linux/ipc/sem.c
 * Copyright (C) 1992 Krishna Balasubramanian
 * Copyright (C) 1995 Eric Schenk, Bruno Haible
 *
 * IMPLEMENTATION NOTES ON CODE REWRITE (Eric Schenk, January 1995):
 * This code underwent a massive rewrite in order to solve some problems
 * with the original code. In particular the original code failed to
 * wake up processes that were waiting for semval to go to 0 if the
 * value went to 0 and was then incremented rapidly enough. In solving
 * this problem I have also modified the implementation so that it
 * processes pending operations in a FIFO manner, thus give a guarantee
 * that processes waiting for a lock on the semaphore won't starve
 * unless another locking process fails to unlock.
 * In addition the following two changes in behavior have been introduced:
 * - The original implementation of semop returned the value
 *   last semaphore element examined on success. This does not
 *   match the manual page specifications, and effectively
 *   allows the user to read the semaphore even if they do not
 *   have read permissions. The implementation now returns 0
 *   on success as stated in the manual page.
 * - There is some confusion over whether the set of undo adjustments
 *   to be performed at exit should be done in an atomic manner.
 *   That is, if we are attempting to decrement the semval should we queue
 *   up and wait until we can do so legally?
 *   The original implementation attempted to do this.
 *   The current implementation does not do so. This is because I don't
 *   think it is the right thing (TM) to do, and because I couldn't
 *   see a clean way to get the old behavior with the new design.
 *   The POSIX standard and SVID should be consulted to determine
 *   what behavior is mandated.
 */

#include <linux/errno.h>
#include <asm/segment.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/sem.h>
#include <linux/ipc.h>
#include <linux/stat.h>
#include <linux/malloc.h>

extern int ipcperms (struct ipc_perm *ipcp, short semflg);
static int newary (key_t, int, int);
static int findkey (key_t key);
static void freeary (int id);

static struct semid_ds *semary[SEMMNI];
static int used_sems = 0, used_semids = 0;
static struct wait_queue *sem_lock = NULL;
static int max_semid = 0;

static unsigned short sem_seq = 0;

void sem_init (void)
{
	int i;

	sem_lock = NULL;
	used_sems = used_semids = max_semid = sem_seq = 0;
	for (i = 0; i < SEMMNI; i++)
		semary[i] = (struct semid_ds *) IPC_UNUSED;
	return;
}

static int findkey (key_t key)
{
	int id;
	struct semid_ds *sma;

	for (id = 0; id <= max_semid; id++) {
		while ((sma = semary[id]) == IPC_NOID)
			interruptible_sleep_on (&sem_lock);
		if (sma == IPC_UNUSED)
			continue;
		if (key == sma->sem_perm.key)
			return id;
	}
	return -1;
}

static int newary (key_t key, int nsems, int semflg)
{
	int id;
	struct semid_ds *sma;
	struct ipc_perm *ipcp;
	int size;

	if (!nsems)
		return -EINVAL;
	if (used_sems + nsems > SEMMNS)
		return -ENOSPC;
	for (id = 0; id < SEMMNI; id++)
		if (semary[id] == IPC_UNUSED) {
			semary[id] = (struct semid_ds *) IPC_NOID;
			goto found;
		}
	return -ENOSPC;
found:
	size = sizeof (*sma) + nsems * sizeof (struct sem);
	used_sems += nsems;
	sma = (struct semid_ds *) kmalloc (size, GFP_KERNEL);
	if (!sma) {
		semary[id] = (struct semid_ds *) IPC_UNUSED;
		used_sems -= nsems;
		if (sem_lock)
			wake_up (&sem_lock);
		return -ENOMEM;
	}
	memset (sma, 0, size);
	sma->sem_base = (struct sem *) &sma[1];
	ipcp = &sma->sem_perm;
	ipcp->mode = (semflg & S_IRWXUGO);
	ipcp->key = key;
	ipcp->cuid = ipcp->uid = current->euid;
	ipcp->gid = ipcp->cgid = current->egid;
	sma->sem_perm.seq = sem_seq;
	/* sma->sem_pending = NULL; */
	sma->sem_pending_last = &sma->sem_pending;
	/* sma->undo = NULL; */
	sma->sem_nsems = nsems;
	sma->sem_ctime = CURRENT_TIME;
	if (id > max_semid)
		max_semid = id;
	used_semids++;
	semary[id] = sma;
	if (sem_lock)
		wake_up (&sem_lock);
	return (unsigned int) sma->sem_perm.seq * SEMMNI + id;
}

int sys_semget (key_t key, int nsems, int semflg)
{
	int id;
	struct semid_ds *sma;

	if (nsems < 0 || nsems > SEMMSL)
		return -EINVAL;
	if (key == IPC_PRIVATE)
		return newary(key, nsems, semflg);
	if ((id = findkey (key)) == -1) {  /* key not used */
		if (!(semflg & IPC_CREAT))
			return -ENOENT;
		return newary(key, nsems, semflg);
	}
	if (semflg & IPC_CREAT && semflg & IPC_EXCL)
		return -EEXIST;
	sma = semary[id];
	if (nsems > sma->sem_nsems)
		return -EINVAL;
	if (ipcperms(&sma->sem_perm, semflg))
		return -EACCES;
	return (unsigned int) sma->sem_perm.seq * SEMMNI + id;
}

/* Manage the doubly linked list sma->sem_pending as a FIFO:
 * insert new queue elements at the tail sma->sem_pending_last.
 */
static inline void insert_into_queue (struct semid_ds * sma, struct sem_queue * q)
{
	*(q->prev = sma->sem_pending_last) = q;
	*(sma->sem_pending_last = &q->next) = NULL;
}
static inline void remove_from_queue (struct semid_ds * sma, struct sem_queue * q)
{
	*(q->prev) = q->next;
	if (q->next)
		q->next->prev = q->prev;
	else /* sma->sem_pending_last == &q->next */
		sma->sem_pending_last = q->prev;
	q->prev = NULL; /* mark as removed */
}

/* Determine whether a sequence of semaphore operations would succeed
 * all at once. Return 0 if yes, 1 if need to sleep, else return error code.
 */
static int try_semop (struct semid_ds * sma, struct sembuf * sops, int nsops)
{
	int result = 0;
	int i = 0;

	while (i < nsops) {
		struct sembuf * sop = &sops[i];
		struct sem * curr = &sma->sem_base[sop->sem_num];
		if (sop->sem_op + curr->semval > SEMVMX) {
			result = -ERANGE;
			break;
		}
		if (!sop->sem_op && curr->semval) {
			if (sop->sem_flg & IPC_NOWAIT)
				result = -EAGAIN;
			else
				result = 1;
			break;
		}
		i++;
		curr->semval += sop->sem_op;
		if (curr->semval < 0) {
			if (sop->sem_flg & IPC_NOWAIT)
				result = -EAGAIN;
			else
				result = 1;
			break;
		}
	}
	while (--i >= 0) {
		struct sembuf * sop = &sops[i];
		struct sem * curr = &sma->sem_base[sop->sem_num];
		curr->semval -= sop->sem_op;
	}
	return result;
}

/* Actually perform a sequence of semaphore operations. Atomically. */
/* This assumes that try_semop() already returned 0. */
static int do_semop (struct semid_ds * sma, struct sembuf * sops, int nsops,
		     struct sem_undo * un, int pid)
{
	int i;

	for (i = 0; i < nsops; i++) {
		struct sembuf * sop = &sops[i];
		struct sem * curr = &sma->sem_base[sop->sem_num];
		if (sop->sem_op + curr->semval > SEMVMX) {
			printk("do_semop: race\n");
			break;
		}
		if (!sop->sem_op) {
			if (curr->semval) {
				printk("do_semop: race\n");
				break;
			}
		} else {
			curr->semval += sop->sem_op;
			if (curr->semval < 0) {
				printk("do_semop: race\n");
				break;
			}
			if (sop->sem_flg & SEM_UNDO)
				un->semadj[sop->sem_num] -= sop->sem_op;
		}
		curr->sempid = pid;
	}
	sma->sem_otime = CURRENT_TIME;

	/* Previous implementation returned the last semaphore's semval.
	 * This is wrong because we may not have checked read permission,
	 * only write permission.
	 */
	return 0;
}

/* Go through the pending queue for the indicated semaphore
 * looking for tasks that can be completed. Keep cycling through
 * the queue until a pass is made in which no process is woken up.
 */
static void update_queue (struct semid_ds * sma)
{
	int wokeup, error;
	struct sem_queue * q;

	do {
		wokeup = 0;
		for (q = sma->sem_pending; q; q = q->next) {
			error = try_semop(sma, q->sops, q->nsops);
			/* Does q->sleeper still need to sleep? */
			if (error > 0)
				continue;
			/* Perform the operations the sleeper was waiting for */
			if (!error)
				error = do_semop(sma, q->sops, q->nsops, q->undo, q->pid);
			q->status = error;
			/* Remove it from the queue */
			remove_from_queue(sma,q);
			/* Wake it up */
			wake_up_interruptible(&q->sleeper); /* doesn't sleep! */
			wokeup++;
		}
	} while (wokeup);
}

/* The following counts are associated to each semaphore:
 *   semncnt        number of tasks waiting on semval being nonzero
 *   semzcnt        number of tasks waiting on semval being zero
 * This model assumes that a task waits on exactly one semaphore.
 * Since semaphore operations are to be performed atomically, tasks actually
 * wait on a whole sequence of semaphores simultaneously.
 * The counts we return here are a rough approximation, but still
 * warrant that semncnt+semzcnt>0 if the task is on the pending queue.
 */
static int count_semncnt (struct semid_ds * sma, ushort semnum)
{
	int semncnt;
	struct sem_queue * q;

	semncnt = 0;
	for (q = sma->sem_pending; q; q = q->next) {
		struct sembuf * sops = q->sops;
		int nsops = q->nsops;
		int i;
		for (i = 0; i < nsops; i++)
			if (sops[i].sem_num == semnum
			    && (sops[i].sem_op < 0)
			    && !(sops[i].sem_flg & IPC_NOWAIT))
				semncnt++;
	}
	return semncnt;
}
static int count_semzcnt (struct semid_ds * sma, ushort semnum)
{
	int semzcnt;
	struct sem_queue * q;

	semzcnt = 0;
	for (q = sma->sem_pending; q; q = q->next) {
		struct sembuf * sops = q->sops;
		int nsops = q->nsops;
		int i;
		for (i = 0; i < nsops; i++)
			if (sops[i].sem_num == semnum
			    && (sops[i].sem_op == 0)
			    && !(sops[i].sem_flg & IPC_NOWAIT))
				semzcnt++;
	}
	return semzcnt;
}

/* Free a semaphore set. */
static void freeary (int id)
{
	struct semid_ds *sma = semary[id];
	struct sem_undo *un;
	struct sem_queue *q;

	/* Invalidate this semaphore set */
	sma->sem_perm.seq++;
	sem_seq = (sem_seq+1) % ((unsigned)(1<<31)/SEMMNI); /* increment, but avoid overflow */
	used_sems -= sma->sem_nsems;
	if (id == max_semid)
		while (max_semid && (semary[--max_semid] == IPC_UNUSED));
	semary[id] = (struct semid_ds *) IPC_UNUSED;
	used_semids--;

	/* Invalidate the existing undo structures for this semaphore set.
	 * (They will be freed without any further action in sem_exit().)
	 */
	for (un = sma->undo; un; un = un->id_next)
		un->semid = -1;

	/* Wake up all pending processes and let them fail with EIDRM. */
	for (q = sma->sem_pending; q; q = q->next) {
		q->status = -EIDRM;
		q->prev = NULL;
		wake_up_interruptible(&q->sleeper); /* doesn't sleep! */
	}

	kfree(sma);
}

int sys_semctl (int semid, int semnum, int cmd, union semun arg)
{
	struct semid_ds *buf = NULL;
	struct semid_ds tbuf;
	int i, id, val = 0;
	struct semid_ds *sma;
	struct ipc_perm *ipcp;
	struct sem *curr = NULL;
	struct sem_undo *un;
	unsigned int nsems;
	ushort *array = NULL;
	ushort sem_io[SEMMSL];

	if (semid < 0 || semnum < 0 || cmd < 0)
		return -EINVAL;

	switch (cmd) {
	case IPC_INFO:
	case SEM_INFO:
	{
		struct seminfo seminfo, *tmp = arg.__buf;
		seminfo.semmni = SEMMNI;
		seminfo.semmns = SEMMNS;
		seminfo.semmsl = SEMMSL;
		seminfo.semopm = SEMOPM;
		seminfo.semvmx = SEMVMX;
		seminfo.semmnu = SEMMNU;
		seminfo.semmap = SEMMAP;
		seminfo.semume = SEMUME;
		seminfo.semusz = SEMUSZ;
		seminfo.semaem = SEMAEM;
		if (cmd == SEM_INFO) {
			seminfo.semusz = used_semids;
			seminfo.semaem = used_sems;
		}
		i = verify_area(VERIFY_WRITE, tmp, sizeof(struct seminfo));
		if (i)
			return i;
		memcpy_tofs (tmp, &seminfo, sizeof(struct seminfo));
		return max_semid;
	}

	case SEM_STAT:
		buf = arg.buf;
		i = verify_area (VERIFY_WRITE, buf, sizeof (*buf));
		if (i)
			return i;
		if (semid > max_semid)
			return -EINVAL;
		sma = semary[semid];
		if (sma == IPC_UNUSED || sma == IPC_NOID)
			return -EINVAL;
		if (ipcperms (&sma->sem_perm, S_IRUGO))
			return -EACCES;
		id = (unsigned int) sma->sem_perm.seq * SEMMNI + semid;
		tbuf.sem_perm   = sma->sem_perm;
		tbuf.sem_otime  = sma->sem_otime;
		tbuf.sem_ctime  = sma->sem_ctime;
		tbuf.sem_nsems  = sma->sem_nsems;
		memcpy_tofs (buf, &tbuf, sizeof(*buf));
		return id;
	}

	id = (unsigned int) semid % SEMMNI;
	sma = semary [id];
	if (sma == IPC_UNUSED || sma == IPC_NOID)
		return -EINVAL;
	ipcp = &sma->sem_perm;
	nsems = sma->sem_nsems;
	if (sma->sem_perm.seq != (unsigned int) semid / SEMMNI)
		return -EIDRM;

	switch (cmd) {
	case GETVAL:
	case GETPID:
	case GETNCNT:
	case GETZCNT:
	case SETVAL:
		if (semnum >= nsems)
			return -EINVAL;
		curr = &sma->sem_base[semnum];
		break;
	}

	switch (cmd) {
	case GETVAL:
	case GETPID:
	case GETNCNT:
	case GETZCNT:
	case GETALL:
		if (ipcperms (ipcp, S_IRUGO))
			return -EACCES;
		switch (cmd) {
		case GETVAL : return curr->semval;
		case GETPID : return curr->sempid;
		case GETNCNT: return count_semncnt(sma,semnum);
		case GETZCNT: return count_semzcnt(sma,semnum);
		case GETALL:
			array = arg.array;
			i = verify_area (VERIFY_WRITE, array, nsems*sizeof(ushort));
			if (i)
				return i;
		}
		break;
	case SETVAL:
		val = arg.val;
		if (val > SEMVMX || val < 0)
			return -ERANGE;
		break;
	case IPC_RMID:
		if (suser() || current->euid == ipcp->cuid || current->euid == ipcp->uid) {
			freeary (id);
			return 0;
		}
		return -EPERM;
	case SETALL: /* arg is a pointer to an array of ushort */
		array = arg.array;
		if ((i = verify_area (VERIFY_READ, array, nsems*sizeof(ushort))))
			return i;
		memcpy_fromfs (sem_io, array, nsems*sizeof(ushort));
		for (i = 0; i < nsems; i++)
			if (sem_io[i] > SEMVMX)
				return -ERANGE;
		break;
	case IPC_STAT:
		buf = arg.buf;
		if ((i = verify_area (VERIFY_WRITE, buf, sizeof(*buf))))
			return i;
		break;
	case IPC_SET:
		buf = arg.buf;
		if ((i = verify_area (VERIFY_READ, buf, sizeof (*buf))))
			return i;
		memcpy_fromfs (&tbuf, buf, sizeof (*buf));
		break;
	}

	if (semary[id] == IPC_UNUSED || semary[id] == IPC_NOID)
		return -EIDRM;
	if (sma->sem_perm.seq != (unsigned int) semid / SEMMNI)
		return -EIDRM;

	switch (cmd) {
	case GETALL:
		if (ipcperms (ipcp, S_IRUGO))
			return -EACCES;
		for (i = 0; i < sma->sem_nsems; i++)
			sem_io[i] = sma->sem_base[i].semval;
		memcpy_tofs (array, sem_io, nsems*sizeof(ushort));
		break;
	case SETVAL:
		if (ipcperms (ipcp, S_IWUGO))
			return -EACCES;
		for (un = sma->undo; un; un = un->id_next)
			un->semadj[semnum] = 0;
		curr->semval = val;
		sma->sem_ctime = CURRENT_TIME;
		/* maybe some queued-up processes were waiting for this */
		update_queue(sma);
		break;
	case IPC_SET:
		if (suser() || current->euid == ipcp->cuid || current->euid == ipcp->uid) {
			ipcp->uid = tbuf.sem_perm.uid;
			ipcp->gid = tbuf.sem_perm.gid;
			ipcp->mode = (ipcp->mode & ~S_IRWXUGO)
				| (tbuf.sem_perm.mode & S_IRWXUGO);
			sma->sem_ctime = CURRENT_TIME;
			return 0;
		}
		return -EPERM;
	case IPC_STAT:
		if (ipcperms (ipcp, S_IRUGO))
			return -EACCES;
		tbuf.sem_perm   = sma->sem_perm;
		tbuf.sem_otime  = sma->sem_otime;
		tbuf.sem_ctime  = sma->sem_ctime;
		tbuf.sem_nsems  = sma->sem_nsems;
		memcpy_tofs (buf, &tbuf, sizeof(*buf));
		break;
	case SETALL:
		if (ipcperms (ipcp, S_IWUGO))
			return -EACCES;
		for (i = 0; i < nsems; i++)
			sma->sem_base[i].semval = sem_io[i];
		for (un = sma->undo; un; un = un->id_next)
			for (i = 0; i < nsems; i++)
				un->semadj[i] = 0;
		sma->sem_ctime = CURRENT_TIME;
		/* maybe some queued-up processes were waiting for this */
		update_queue(sma);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

int sys_semop (int semid, struct sembuf *tsops, unsigned nsops)
{
	int i, id, size, error;
	struct semid_ds *sma;
	struct sembuf sops[SEMOPM], *sop;
	struct sem_undo *un;
	int undos = 0, alter = 0;

	if (nsops < 1 || semid < 0)
		return -EINVAL;
	if (nsops > SEMOPM)
		return -E2BIG;
	if (!tsops)
		return -EFAULT;
	if ((i = verify_area (VERIFY_READ, tsops, nsops * sizeof(*tsops))))
		return i;
	memcpy_fromfs (sops, tsops, nsops * sizeof(*tsops));
	id = (unsigned int) semid % SEMMNI;
	if ((sma = semary[id]) == IPC_UNUSED || sma == IPC_NOID)
		return -EINVAL;
	if (sma->sem_perm.seq != (unsigned int) semid / SEMMNI)
		return -EIDRM;
	for (i = 0; i < nsops; i++) {
		sop = &sops[i];
		if (sop->sem_num >= sma->sem_nsems)
			return -EFBIG;
		if (sop->sem_flg & SEM_UNDO)
			undos++;
		if (sop->sem_op)
			alter++;
	}
	if (ipcperms(&sma->sem_perm, alter ? S_IWUGO : S_IRUGO))
		return -EACCES;
	error = try_semop(sma, sops, nsops);
	if (error < 0)
		return error;
	if (undos) {
		/* Make sure we have an undo structure
		 * for this process and this semaphore set.
		 */
		for (un = current->semundo; un; un = un->proc_next)
			if (un->semid == semid)
				break;
		if (!un) {
			size = sizeof(struct sem_undo) + sizeof(short)*sma->sem_nsems;
			un = (struct sem_undo *) kmalloc(size, GFP_ATOMIC);
			if (!un)
				return -ENOMEM;
			memset(un, 0, size);
			un->semadj = (short *) &un[1];
			un->semid = semid;
			un->proc_next = current->semundo;
			current->semundo = un;
			un->id_next = sma->undo;
			sma->undo = un;
		}
	} else
		un = NULL;
	if (error == 0) {
		/* the operations go through immediately */
		error = do_semop(sma, sops, nsops, un, current->pid);
		/* maybe some queued-up processes were waiting for this */
		update_queue(sma);
		return error;
	} else {
		/* We need to sleep on this operation, so we put the current
		 * task into the pending queue and go to sleep.
		 */
		struct sem_queue queue;

		queue.sma = sma;
		queue.sops = sops;
		queue.nsops = nsops;
		queue.undo = un;
		queue.pid = current->pid;
		queue.status = 0;
		insert_into_queue(sma,&queue);
		queue.sleeper = NULL;
		current->semsleeping = &queue;
		interruptible_sleep_on(&queue.sleeper);
		current->semsleeping = NULL;
		/* When we wake up, either the operation is finished,
		 * or some kind of error happened.
		 */
		if (!queue.prev) {
			/* operation is finished, update_queue() removed us */
			return queue.status;
		} else {
			remove_from_queue(sma,&queue);
			return -EINTR;
		}
	}
}

/*
 * add semadj values to semaphores, free undo structures.
 * undo structures are not freed when semaphore arrays are destroyed
 * so some of them may be out of date.
 * IMPLEMENTATION NOTE: There is some confusion over whether the
 * set of adjustments that needs to be done should be done in an atomic
 * manner or not. That is, if we are attempting to decrement the semval
 * should we queue up and wait until we can do so legally?
 * The original implementation attempted to do this (queue and wait).
 * The current implementation does not do so. The POSIX standard
 * and SVID should be consulted to determine what behavior is mandated.
 */
void sem_exit (void)
{
	struct sem_queue *q;
	struct sem_undo *u, *un = NULL, **up, **unp;
	struct semid_ds *sma;
	int nsems, i;

	/* If the current process was sleeping for a semaphore,
	 * remove it from the queue.
	 */
	if ((q = current->semsleeping)) {
		if (q->prev)
			remove_from_queue(q->sma,q);
		current->semsleeping = NULL;
	}

	for (up = &current->semundo; (u = *up); *up = u->proc_next, kfree(u)) {
		if (u->semid == -1)
			continue;
		sma = semary[(unsigned int) u->semid % SEMMNI];
		if (sma == IPC_UNUSED || sma == IPC_NOID)
			continue;
		if (sma->sem_perm.seq != (unsigned int) u->semid / SEMMNI)
			continue;
		/* remove u from the sma->undo list */
		for (unp = &sma->undo; (un = *unp); unp = &un->id_next) {
			if (u == un)
				goto found;
		}
		printk ("sem_exit undo list error id=%d\n", u->semid);
		break;
found:
		*unp = un->id_next;
		/* perform adjustments registered in u */
		nsems = sma->sem_nsems;
		for (i = 0; i < nsems; i++) {
			struct sem * sem = &sma->sem_base[i];
			sem->semval += u->semadj[i];
			if (sem->semval < 0)
				sem->semval = 0; /* shouldn't happen */
			sem->sempid = current->pid;
		}
		sma->sem_otime = CURRENT_TIME;
		/* maybe some queued-up processes were waiting for this */
		update_queue(sma);
	}
	current->semundo = NULL;
}
