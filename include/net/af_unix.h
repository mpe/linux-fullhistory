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

static inline unix_socket *first_unix_socket(int *i)
{
	for (*i = 0; *i <= UNIX_HASH_SIZE; (*i)++) {
		if (unix_socket_table[*i])
			return unix_socket_table[*i];
	}
	return NULL;
}

static inline unix_socket *next_unix_socket(int *i, unix_socket *s)
{
	/* More in this chain? */
	if (s->next)
		return s->next;
	/* Look for next non-empty chain. */
	for ((*i)++; *i <= UNIX_HASH_SIZE; (*i)++) {
		if (unix_socket_table[*i])
			return unix_socket_table[*i];
	}
	return NULL;
}

#define forall_unix_sockets(i, s) \
	for (s = first_unix_socket(&(i)); s; s = next_unix_socket(&(i),(s)))

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
