/*
 * linux/ipc/util.c
 * Copyright (C) 1992 Krishna Balasubramanian
 *
 * Sep 1997 - Call suser() last after "normal" permission checks so we
 *            get BSD style process accounting right.
 *            Occurs in several places in the IPC code.
 *            Chris Evans, <chris@ferret.lmh.ox.ac.uk>
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/shm.h>
#include <linux/init.h>
#include <linux/msg.h>

#include "util.h"

#if defined(CONFIG_SYSVIPC)

extern void sem_init (void), msg_init (void), shm_init (void);

void __init ipc_init (void)
{
	sem_init();
	msg_init();
	shm_init();
	return;
}

/* 
 * Check user, group, other permissions for access
 * to ipc resources. return 0 if allowed
 */
int ipcperms (struct ipc_perm *ipcp, short flag)
{	/* flag will most probably be 0 or S_...UGO from <linux/stat.h> */
	int requested_mode, granted_mode;

	requested_mode = (flag >> 6) | (flag >> 3) | flag;
	granted_mode = ipcp->mode;
	if (current->euid == ipcp->cuid || current->euid == ipcp->uid)
		granted_mode >>= 6;
	else if (in_group_p(ipcp->cgid) || in_group_p(ipcp->gid))
		granted_mode >>= 3;
	/* is there some bit set in requested_mode but not in granted_mode? */
	if ((requested_mode & ~granted_mode & 0007) && 
	    !capable(CAP_IPC_OWNER))
		return -1;

	return 0;
}

#else
/*
 * Dummy functions when SYSV IPC isn't configured
 */

void sem_exit (void)
{
    return;
}

int shm_swap (int prio, int gfp_mask)
{
    return 0;
}

asmlinkage long sys_semget (key_t key, int nsems, int semflg)
{
	return -ENOSYS;
}

asmlinkage long sys_semop (int semid, struct sembuf *sops, unsigned nsops)
{
	return -ENOSYS;
}

asmlinkage long sys_semctl (int semid, int semnum, int cmd, union semun arg)
{
	return -ENOSYS;
}

asmlinkage long sys_msgget (key_t key, int msgflg)
{
	return -ENOSYS;
}

asmlinkage long sys_msgsnd (int msqid, struct msgbuf *msgp, size_t msgsz, int msgflg)
{
	return -ENOSYS;
}

asmlinkage long sys_msgrcv (int msqid, struct msgbuf *msgp, size_t msgsz, long msgtyp,
		       int msgflg)
{
	return -ENOSYS;
}

asmlinkage long sys_msgctl (int msqid, int cmd, struct msqid_ds *buf)
{
	return -ENOSYS;
}

asmlinkage long sys_shmget (key_t key, int size, int flag)
{
	return -ENOSYS;
}

asmlinkage long sys_shmat (int shmid, char *shmaddr, int shmflg, ulong *addr)
{
	return -ENOSYS;
}

asmlinkage long sys_shmdt (char *shmaddr)
{
	return -ENOSYS;
}

asmlinkage long sys_shmctl (int shmid, int cmd, struct shmid_ds *buf)
{
	return -ENOSYS;
}

void shm_unuse(swp_entry_t entry, struct page *page)
{
}

#endif /* CONFIG_SYSVIPC */
