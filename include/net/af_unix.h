#ifndef __LINUX_NET_AFUNIX_H
#define __LINUX_NET_AFUNIX_H
extern void unix_proto_init(struct net_proto *pro);
extern void unix_inflight(struct file *fp);
extern void unix_notinflight(struct file *fp);
typedef struct sock unix_socket;
extern void unix_gc(void);

#define UNIX_HASH_SIZE	256

extern unix_socket *unix_socket_table[UNIX_HASH_SIZE+1];
extern rwlock_t unix_table_lock;

extern atomic_t unix_tot_inflight;


#define forall_unix_sockets(i, s) for (i=0; i<=UNIX_HASH_SIZE; i++) \
                                    for (s=unix_socket_table[i]; s; s=s->next)

struct unix_address
{
	atomic_t	refcnt;
	int		len;
	unsigned	hash;
	struct sockaddr_un name[0];
};

struct unix_skb_parms
{
	struct ucred		creds;		/* Skb credentials	*/
	struct scm_fp_list	*fp;		/* Passed files		*/
};

#define UNIXCB(skb) 	(*(struct unix_skb_parms*)&((skb)->cb))
#define UNIXCREDS(skb)	(&UNIXCB((skb)).creds)

#define unix_state_rlock(s)	read_lock(&unix_sk(s)->lock)
#define unix_state_runlock(s)	read_unlock(&unix_sk(s)->lock)
#define unix_state_wlock(s)	write_lock(&unix_sk(s)->lock)
#define unix_state_wunlock(s)	write_unlock(&unix_sk(s)->lock)

#ifdef __KERNEL__
/* The AF_UNIX socket */
struct unix_sock {
	/* WARNING: sk has to be the first member */
	struct sock		sk;
        struct unix_address     *addr;
        struct dentry		*dentry;
        struct vfsmount		*mnt;
        struct semaphore        readsem;
        struct sock		*other;
        struct sock		**list;
        struct sock		*gc_tree;
        atomic_t                inflight;
        rwlock_t                lock;
        wait_queue_head_t       peer_wait;
};
#define unix_sk(__sk) ((struct unix_sock *)__sk)
#endif
#endif
