extern void unix_proto_init(struct net_proto *pro);
extern struct proto_ops unix_proto_ops;
extern void unix_inflight(struct file *fp);
extern void unix_notinflight(struct file *fp);
typedef struct sock unix_socket;

unix_socket *unix_socket_list;

#define UNIX_MAX_FD	8
