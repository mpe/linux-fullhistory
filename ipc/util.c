/*
 * linux/ipc/util.c
 * Copyright (C) 1992 Krishna Balasubramanian
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <asm/segment.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>

#if defined(CONFIG_SYSVIPC) || defined(CONFIG_KERNELD)

extern void sem_init (void), msg_init (void), shm_init (void);

void ipc_init (void)
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

	if (suser())
		return 0;
	requested_mode = (flag >> 6) | (flag >> 3) | flag;
	granted_mode = ipcp->mode;
	if (current->euid == ipcp->cuid || current->euid == ipcp->uid)
		granted_mode >>= 6;
	else if (in_group_p(ipcp->cgid) || in_group_p(ipcp->gid))
		granted_mode >>= 3;
	/* is there some bit set in requested_mode but not in granted_mode? */
	if (requested_mode & ~granted_mode & 0007)
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

int shm_swap (int prio, unsigned long limit)
{
    return 0;
}

asmlinkage int sys_semget (key_t key, int nsems, int semflg)
{
	return -ENOSYS;
}

asmlinkage int sys_semop (int semid, struct sembuf *sops, unsigned nsops)
{
	return -ENOSYS;
}

asmlinkage int sys_semctl (int semid, int semnum, int cmd, union semun arg)
{
	return -ENOSYS;
}

asmlinkage int sys_msgget (key_t key, int msgflg)
{
	return -ENOSYS;
}

asmlinkage int sys_msgsnd (int msqid, struct msgbuf *msgp, size_t msgsz, int msgflg)
{
	return -ENOSYS;
}

asmlinkage int sys_msgrcv (int msqid, struct msgbuf *msgp, size_t msgsz, long msgtyp,
		       int msgflg)
{
	return -ENOSYS;
}

asmlinkage int sys_msgctl (int msqid, int cmd, struct msqid_ds *buf)
{
	return -ENOSYS;
}

asmlinkage int sys_shmget (key_t key, int size, int flag)
{
	return -ENOSYS;
}

asmlinkage int sys_shmat (int shmid, char *shmaddr, int shmflg, ulong *addr)
{
	return -ENOSYS;
}

asmlinkage int sys_shmdt (char *shmaddr)
{
	return -ENOSYS;
}

asmlinkage int sys_shmctl (int shmid, int cmd, struct shmid_ds *buf)
{
	return -ENOSYS;
}

void kerneld_exit(void)
{
}
#endif /* CONFIG_SYSVIPC */
