extern void unix_proto_init(struct net_proto *pro);

typedef struct sock unix_socket;

extern int unix_gc_free;
extern void unix_gc_add(struct sock *sk, struct file *fp);
extern void unix_gc_remove(struct file *fp);

#define UNIX_MAX_FD	8
