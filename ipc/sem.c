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
 *
 * Further notes on refinement (Christoph Rohland, December 1998):
 * - The POSIX standard says, that the undo adjustments simply should
 *   redo. So the current implementation is o.K.
 * - The previous code had two flaws:
 *   1) It actively gave the semaphore to the next waiting process
 *      sleeping on the semaphore. Since this process did not have the
 *      cpu this led to many unnecessary context switches and bad
 *      performance. Now we only check which process should be able to
 *      get the semaphore and if this process wants to reduce some
 *      semaphore value we simply wake it up without doing the
 *      operation. So it has to try to get it later. Thus e.g. the
 *      running process may reaquire the semaphore during the current
 *      time slice. If it only waits for zero or increases the semaphore,
 *      we do the operation in advance and wake it up.
 *   2) It did not wake up all zero waiting processes. We try to do
 *      better but only get the semops right which only wait for zero or
 *      increase. If there are decrement operations in the operations
 *      array we do the same as before.
 */

#include <linux/malloc.h>
#include <linux/smp_lock.h>
#include <linux/init.h>

#include <asm/uaccess.h>

extern int ipcperms (struct ipc_perm *ipcp, short semflg);
static int newary (key_t, int, int);
static int findkey (key_t key);
static void freeary (int id);

static struct semid_ds *semary[SEMMNI];
static int used_sems = 0, used_semids = 0;
static struct wait_queue *sem_lock = NULL;
static int max_semid = 0;

static unsigned short sem_seq = 0;

void __init sem_init (void)
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
	wake_up (&sem_lock);
	return (unsigned int) sma->sem_perm.seq * SEMMNI + id;
}

asmlinkage int sys_semget (key_t key, int nsems, int semflg)
{
	int id, err = -EINVAL;
	struct semid_ds *sma;

	lock_kernel();
	if (nsems < 0 || nsems > SEMMSL)
		goto out;
	if (key == IPC_PRIVATE) {
		err = newary(key, nsems, semflg);
	} else if ((id = findkey (key)) == -1) {  /* key not used */
		if (!(semflg & IPC_CREAT))
			err = -ENOENT;
		else
			err = newary(key, nsems, semflg);
	} else if (semflg & IPC_CREAT && semflg & IPC_EXCL) {
		err = -EEXIST;
	} else {
		sma = semary[id];
		if (nsems > sma->sem_nsems)
			err = -EINVAL;
		else if (ipcperms(&sma->sem_perm, semflg))
			err = -EACCES;
		else
			err = (int) sma->sem_perm.seq * SEMMNI + id;
	}
out:
	unlock_kernel();
	return err;
}

/* Manage the doubly linked list sma->sem_pending as a FIFO:
 * insert new queue elements at the tail sma->sem_pending_last.
 */
static inline void append_to_queue (struct semid_ds * sma,
                                    struct sem_queue * q)
{
	*(q->prev = sma->sem_pending_last) = q;
	*(sma->sem_pending_last = &q->next) = NULL;
}

static inline void prepend_to_queue (struct semid_ds * sma,
                                     struct sem_queue * q)
{
        q->next = sma->sem_pending;
        *(q->prev = &sma->sem_pending) = q;
        if (q->next)
                q->next->prev = &q->next;
        else /* sma->sem_pending_last == &sma->sem_pending */
                sma->sem_pending_last = &q->next;
}

static inline void remove_from_queue (struct semid_ds * sma,
                                      struct sem_queue * q)
{
	*(q->prev) = q->next;
	if (q->next)
		q->next->prev = q->prev;
	else /* sma->sem_pending_last == &q->next */
		sma->sem_pending_last = q->prev;
	q->prev = NULL; /* mark as removed */
}

/*
 * Determine whether a sequence of semaphore operations would succeed
 * all at once. Return 0 if yes, 1 if need to sleep, else return error code.
 */

static int try_atomic_semop (struct semid_ds * sma, struct sembuf * sops,
                             int nsops, struct sem_undo *un, int pid,
                             int do_undo)
{
	int result, sem_op;
	struct sembuf *sop;
	struct sem * curr;

	for (sop = sops; sop < sops + nsops; sop++) {
		curr = sma->sem_base + sop->sem_num;
		sem_op = sop->sem_op;

		if (!sem_op && curr->semval)
			goto would_block;

		curr->sempid = (curr->sempid << 16) | pid;
		curr->semval += sem_op;
		if (sop->sem_flg & SEM_UNDO)
			un->semadj[sop->sem_num] -= sem_op;

		if (curr->semval < 0)
			goto would_block;
		if (curr->semval > SEMVMX)
			goto out_of_range;
	}

        if (do_undo)
        {
                sop--;
                result = 0;
                goto undo;
        }

	sma->sem_otime = CURRENT_TIME;
	return 0;

out_of_range:
	result = -ERANGE;
	goto undo;

would_block:
	if (sop->sem_flg & IPC_NOWAIT)
		result = -EAGAIN;
	else
		result = 1;

undo:
        while (sop >= sops) {
		curr = sma->sem_base + sop->sem_num;
		curr->semval -= sop->sem_op;
		curr->sempid >>= 16;

		if (sop->sem_flg & SEM_UNDO)
			un->semadj[sop->sem_num] += sop->sem_op;
		sop--;
	}

	return result;
}

/* Go through the pending queue for the indicated semaphore
 * looking for tasks that can be completed.
 */
static void update_queue (struct semid_ds * sma)
{
	int error;
	struct sem_queue * q;

        for (q = sma->sem_pending; q; q = q->next) {
                        
                if (q->status == 1)
                        return; /* wait for other process */

                error = try_atomic_semop(sma, q->sops, q->nsops,
                                         q->undo, q->pid, q->alter);

                /* Does q->sleeper still need to sleep? */
                if (error <= 0) {
                                /* Found one, wake it up */
                        wake_up_interruptible(&q->sleeper);
                        if (error == 0 && q->alter) {
                                /* if q-> alter let it self try */
                                q->status = 1;
                                return;
                        }
                        q->status = error;
                        remove_from_queue(sma,q);
                }
        }
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

asmlinkage int sys_semctl (int semid, int semnum, int cmd, union semun arg)
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
	int err = -EINVAL;

	lock_kernel();
	if (semid < 0 || semnum < 0 || cmd < 0)
		goto out;

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
		err = -EFAULT;
		if (copy_to_user (tmp, &seminfo, sizeof(struct seminfo))) 
			goto out;
		err = max_semid;
		goto out;
	}

	case SEM_STAT:
		buf = arg.buf;
		err = -EINVAL;
		if (semid > max_semid)
			goto out;
		sma = semary[semid];
		if (sma == IPC_UNUSED || sma == IPC_NOID)
			goto out;
		err = -EACCES;
		if (ipcperms (&sma->sem_perm, S_IRUGO))
			goto out;
		id = (unsigned int) sma->sem_perm.seq * SEMMNI + semid;
		tbuf.sem_perm   = sma->sem_perm;
		tbuf.sem_otime  = sma->sem_otime;
		tbuf.sem_ctime  = sma->sem_ctime;
		tbuf.sem_nsems  = sma->sem_nsems;
		err = -EFAULT;
		if (copy_to_user (buf, &tbuf, sizeof(*buf)) == 0)
			err = id;
		goto out;
	}

	id = (unsigned int) semid % SEMMNI;
	sma = semary [id];
	err = -EINVAL;
	if (sma == IPC_UNUSED || sma == IPC_NOID)
		goto out;
	ipcp = &sma->sem_perm;
	nsems = sma->sem_nsems;
	err = -EIDRM;
	if (sma->sem_perm.seq != (unsigned int) semid / SEMMNI)
		goto out;

	switch (cmd) {
	case GETVAL:
	case GETPID:
	case GETNCNT:
	case GETZCNT:
	case SETVAL:
		err = -EINVAL;
		if (semnum >= nsems)
			goto out;
		curr = &sma->sem_base[semnum];
		break;
	}

	switch (cmd) {
	case GETVAL:
	case GETPID:
	case GETNCNT:
	case GETZCNT:
	case GETALL:
		err = -EACCES;
		if (ipcperms (ipcp, S_IRUGO))
			goto out;
		switch (cmd) {
		case GETVAL : err = curr->semval; goto out;
		case GETPID : err = curr->sempid & 0xffff; goto out;
		case GETNCNT: err = count_semncnt(sma,semnum); goto out;
		case GETZCNT: err = count_semzcnt(sma,semnum); goto out;
		case GETALL:
			array = arg.array;
			break;
		}
		break;
	case SETVAL:
		val = arg.val;
		err = -ERANGE;
		if (val > SEMVMX || val < 0)
			goto out;
		break;
	case IPC_RMID:
		if (current->euid == ipcp->cuid || 
		    current->euid == ipcp->uid || capable(CAP_SYS_ADMIN)) {
			freeary (id);
			err = 0;
			goto out;
		}
		err = -EPERM;
		goto out;
	case SETALL: /* arg is a pointer to an array of ushort */
		array = arg.array;
		err = -EFAULT;
		if (copy_from_user (sem_io, array, nsems*sizeof(ushort)))
		       goto out;
		err = 0;
		for (i = 0; i < nsems; i++)
			if (sem_io[i] > SEMVMX) {
				err = -ERANGE;
				goto out;
			}
		break;
	case IPC_STAT:
		buf = arg.buf;
		break;
	case IPC_SET:
		buf = arg.buf;
		err = copy_from_user (&tbuf, buf, sizeof (*buf));
		if (err)
			err = -EFAULT;
		break;
	}

	err = -EIDRM;
	if (semary[id] == IPC_UNUSED || semary[id] == IPC_NOID)
		goto out;
	if (sma->sem_perm.seq != (unsigned int) semid / SEMMNI)
		goto out;

	switch (cmd) {
	case GETALL:
		err = -EACCES;
		if (ipcperms (ipcp, S_IRUGO))
			goto out;
		for (i = 0; i < sma->sem_nsems; i++)
			sem_io[i] = sma->sem_base[i].semval;
		if (copy_to_user (array, sem_io, nsems*sizeof(ushort)))
			err = -EFAULT;
		break;
	case SETVAL:
		err = -EACCES;
		if (ipcperms (ipcp, S_IWUGO))
			goto out;
		for (un = sma->undo; un; un = un->id_next)
			un->semadj[semnum] = 0;
		curr->semval = val;
		sma->sem_ctime = CURRENT_TIME;
		/* maybe some queued-up processes were waiting for this */
		update_queue(sma);
		break;
	case IPC_SET:
		if (current->euid == ipcp->cuid || 
		    current->euid == ipcp->uid || capable(CAP_SYS_ADMIN)) {
			ipcp->uid = tbuf.sem_perm.uid;
			ipcp->gid = tbuf.sem_perm.gid;
			ipcp->mode = (ipcp->mode & ~S_IRWXUGO)
				| (tbuf.sem_perm.mode & S_IRWXUGO);
			sma->sem_ctime = CURRENT_TIME;
			err = 0;
			goto out;
		}
		err = -EPERM;
		goto out;
	case IPC_STAT:
		err = -EACCES;
		if (ipcperms (ipcp, S_IRUGO))
			goto out;
		tbuf.sem_perm   = sma->sem_perm;
		tbuf.sem_otime  = sma->sem_otime;
		tbuf.sem_ctime  = sma->sem_ctime;
		tbuf.sem_nsems  = sma->sem_nsems;
		if (copy_to_user (buf, &tbuf, sizeof(*buf)))
			err = -EFAULT;
		break;
	case SETALL:
		err = -EACCES;
		if (ipcperms (ipcp, S_IWUGO))
			goto out;
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
		err = -EINVAL;
		goto out;
	}
	err = 0;
out:
	unlock_kernel();
	return err;
}

asmlinkage int sys_semop (int semid, struct sembuf *tsops, unsigned nsops)
{
	int id, size, error = -EINVAL;
	struct semid_ds *sma;
	struct sembuf sops[SEMOPM], *sop;
	struct sem_undo *un;
	int undos = 0, decrease = 0, alter = 0;
	struct sem_queue queue;

	lock_kernel();
	if (nsops < 1 || semid < 0)
		goto out;
	error = -E2BIG;
	if (nsops > SEMOPM)
		goto out;
	error = -EFAULT;
	if (copy_from_user (sops, tsops, nsops * sizeof(*tsops)))
		goto out;
	id = (unsigned int) semid % SEMMNI;
	error = -EINVAL;
	if ((sma = semary[id]) == IPC_UNUSED || sma == IPC_NOID)
		goto out;
	error = -EIDRM;
	if (sma->sem_perm.seq != (unsigned int) semid / SEMMNI)
		goto out;

		error = -EFBIG;
	for (sop = sops; sop < sops + nsops; sop++) {
		if (sop->sem_num >= sma->sem_nsems)
			goto out;
		if (sop->sem_flg & SEM_UNDO)
			undos++;
		if (sop->sem_op < 0)
			decrease = 1;
                if (sop->sem_op > 0)
                        alter = 1;
	}
        alter |= decrease;

	error = -EACCES;
	if (ipcperms(&sma->sem_perm, alter ? S_IWUGO : S_IRUGO))
		goto out;
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
			if (!un) {
				error = -ENOMEM;
				goto out;
			}
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

	error = try_atomic_semop (sma, sops, nsops, un, current->pid, 0);
	if (error <= 0)
                goto update;

        /* We need to sleep on this operation, so we put the current
         * task into the pending queue and go to sleep.
         */
                
        queue.sma = sma;
        queue.sops = sops;
        queue.nsops = nsops;
        queue.undo = un;
        queue.pid = current->pid;
        queue.alter = decrease;
        current->semsleeping = &queue;
        if (alter)
                append_to_queue(sma ,&queue);
        else
                prepend_to_queue(sma ,&queue);

        for (;;) {
                queue.status = -EINTR;
                queue.sleeper = NULL;
                interruptible_sleep_on(&queue.sleeper);

                /*
                 * If queue.status == 1 we where woken up and
                 * have to retry else we simply return.
                 * If an interrupt occurred we have to clean up the
                 * queue
                 *
                 */
                if (queue.status == 1)
                {
                        error = try_atomic_semop (sma, sops, nsops, un,
                                                  current->pid,0);
                        if (error <= 0) 
                                break;
                } else {
                        error = queue.status;;
                        if (queue.prev) /* got Interrupt */
                                break;
                        /* Everything done by update_queue */
                        current->semsleeping = NULL;
                        goto out;
                }
        }
        current->semsleeping = NULL;
        remove_from_queue(sma,&queue);
update:
        if (alter)
                update_queue (sma);
out:
	unlock_kernel();
	return error;
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
