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

void ipc_init (void);
asmlinkage int sys_ipc (uint call, int first, int second, int third, void *ptr, long fifth);

#ifdef CONFIG_SYSVIPC

int ipcperms (struct ipc_perm *ipcp, short flag);
extern void sem_init (void), msg_init (void), shm_init (void);
extern int sys_semget (key_t key, int nsems, int semflg);
extern int sys_semop (int semid, struct sembuf *sops, unsigned nsops);
extern int sys_semctl (int semid, int semnum, int cmd, union semun arg);
extern int sys_msgget (key_t key, int msgflg);
extern int sys_msgsnd (int msqid, struct msgbuf *msgp, int msgsz, int msgflg);
extern int sys_msgrcv (int msqid, struct msgbuf *msgp, int msgsz, long msgtyp,
		       int msgflg);
extern int sys_msgctl (int msqid, int cmd, struct msqid_ds *buf);
extern int sys_shmget (key_t key, int size, int flag);
extern int sys_shmat (int shmid, char *shmaddr, int shmflg, ulong *addr);
extern int sys_shmdt (char *shmaddr);
extern int sys_shmctl (int shmid, int cmd, struct shmid_ds *buf);

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

asmlinkage int sys_ipc (uint call, int first, int second, int third, void *ptr, long fifth)
{
	int version;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	if (call <= SEMCTL)
		switch (call) {
		case SEMOP:
			return sys_semop (first, (struct sembuf *)ptr, second);
		case SEMGET:
			return sys_semget (first, second, third);
		case SEMCTL: {
			union semun fourth;
			int err;
			if (!ptr)
				return -EINVAL;
			if ((err = verify_area (VERIFY_READ, ptr, sizeof(long))))
				return err;
			fourth.__pad = (void *) get_fs_long(ptr);
			return sys_semctl (first, second, third, fourth);
			}
		default:
			return -EINVAL;
		}
	if (call <= MSGCTL) 
		switch (call) {
		case MSGSND:
			return sys_msgsnd (first, (struct msgbuf *) ptr, 
					   second, third);
		case MSGRCV:
			switch (version) {
			case 0: {
				struct ipc_kludge tmp;
				int err;
				if (!ptr)
					return -EINVAL;
				if ((err = verify_area (VERIFY_READ, ptr, sizeof(tmp))))
					return err;
				memcpy_fromfs (&tmp,(struct ipc_kludge *) ptr,
					       sizeof (tmp));
				return sys_msgrcv (first, tmp.msgp, second, tmp.msgtyp, third);
				}
			case 1: default:
				return sys_msgrcv (first, (struct msgbuf *) ptr, second, fifth, third);
			}
		case MSGGET:
			return sys_msgget ((key_t) first, second);
		case MSGCTL:
			return sys_msgctl (first, second, (struct msqid_ds *) ptr);
		default:
			return -EINVAL;
		}
	if (call <= SHMCTL) 
		switch (call) {
		case SHMAT:
			switch (version) {
			case 0: default: {
				ulong raddr;
				int err;
				if ((err = verify_area(VERIFY_WRITE, (ulong*) third, sizeof(ulong))))
					return err;
				err = sys_shmat (first, (char *) ptr, second, &raddr);
				if (err)
					return err;
				put_fs_long (raddr, (ulong *) third);
				return 0;
				}
			case 1:	/* iBCS2 emulator entry point */
				if (get_fs() != get_ds())
					return -EINVAL;
				return sys_shmat (first, (char *) ptr, second, (ulong *) third);
			}
		case SHMDT: 
			return sys_shmdt ((char *)ptr);
		case SHMGET:
			return sys_shmget (first, second, third);
		case SHMCTL:
			return sys_shmctl (first, second, (struct shmid_ds *) ptr);
		default:
			return -EINVAL;
		}
	return -EINVAL;
}

#else /* not CONFIG_SYSVIPC */

asmlinkage int sys_ipc (uint call, int first, int second, int third, void *ptr, long fifth) 
{
    return -ENOSYS;
}

void sem_exit (void)
{
    return;
}

int shm_swap (int prio)
{
    return 0;
}

void shm_no_page (unsigned long *ptent)
{
    return;
}

#endif /* CONFIG_SYSVIPC */
