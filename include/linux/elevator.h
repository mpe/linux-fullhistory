#ifndef _LINUX_ELEVATOR_H
#define _LINUX_ELEVATOR_H

#define ELEVATOR_DEBUG

typedef void (elevator_fn) (struct request *, elevator_t *,
			    struct list_head *,
			    struct list_head *, int);

typedef int (elevator_merge_fn) (request_queue_t *, struct request **,
				 struct buffer_head *, int, int *, int *);

typedef void (elevator_dequeue_fn) (struct request *);

struct elevator_s
{
	int sequence;

	int read_latency;
	int write_latency;
	int max_bomb_segments;

	unsigned int nr_segments;
	int read_pendings;

	elevator_fn * elevator_fn;
	elevator_merge_fn *elevator_merge_fn;
	elevator_dequeue_fn *dequeue_fn;

	unsigned int queue_ID;
};

void elevator_default(struct request *, elevator_t *, struct list_head *, struct list_head *, int);
int elevator_default_merge(request_queue_t *, struct request **, struct buffer_head *, int, int *, int *);
void elevator_default_dequeue(struct request *);
void elevator_noop(struct request *, elevator_t *, struct list_head *, struct list_head *, int);
int elevator_noop_merge(request_queue_t *, struct request **, struct buffer_head *, int, int *, int *);
void elevator_noop_dequeue(struct request *);
void elevator_linus(struct request *, elevator_t *, struct list_head *, struct list_head *, int);
int elevator_linus_merge(request_queue_t *, struct request **, struct buffer_head *, int, int *, int *);

typedef struct blkelv_ioctl_arg_s {
	int queue_ID;
	int read_latency;
	int write_latency;
	int max_bomb_segments;
} blkelv_ioctl_arg_t;

#define BLKELVGET   _IOR(0x12,106,sizeof(blkelv_ioctl_arg_t))
#define BLKELVSET   _IOW(0x12,107,sizeof(blkelv_ioctl_arg_t))

extern int blkelvget_ioctl(elevator_t *, blkelv_ioctl_arg_t *);
extern int blkelvset_ioctl(elevator_t *, const blkelv_ioctl_arg_t *);

extern void elevator_init(elevator_t *, elevator_t);

#ifdef ELEVATOR_DEBUG
extern void elevator_default_debug(request_queue_t *, kdev_t);
#else
#define elevator_default_debug(a,b) do { } while(0)
#endif

#define elevator_sequence_after(a,b) ((int)((b)-(a)) < 0)
#define elevator_sequence_before(a,b) elevator_sequence_after(b,a)
#define elevator_sequence_after_eq(a,b) ((int)((b)-(a)) <= 0)
#define elevator_sequence_before_eq(a,b) elevator_sequence_after_eq(b,a)

/*
 * Return values from elevator merger
 */
#define ELEVATOR_NO_MERGE	0
#define ELEVATOR_FRONT_MERGE	1
#define ELEVATOR_BACK_MERGE	2

/*
 * This is used in the elevator algorithm.  We don't prioritise reads
 * over writes any more --- although reads are more time-critical than
 * writes, by treating them equally we increase filesystem throughput.
 * This turns out to give better overall performance.  -- sct
 */
#define IN_ORDER(s1,s2)				\
	((((s1)->rq_dev == (s2)->rq_dev &&	\
	   (s1)->sector < (s2)->sector)) ||	\
	 (s1)->rq_dev < (s2)->rq_dev)

static inline void elevator_merge_requests(struct request * req, struct request * next)
{
	if (elevator_sequence_before(next->elevator_sequence, req->elevator_sequence))
		req->elevator_sequence = next->elevator_sequence;
	if (req->cmd == READ)
		req->e->read_pendings--;

}

static inline int elevator_sequence(elevator_t * e, int latency)
{
	return latency + e->sequence;
}

#define elevator_merge_before(req, lat)	__elevator_merge((req), (lat), 0)
#define elevator_merge_after(req, lat)	__elevator_merge((req), (lat), 1)
static inline void __elevator_merge(struct request * req, int latency, int after)
{
	int sequence = elevator_sequence(req->e, latency);
	if (after)
		sequence -= req->nr_segments;
	if (elevator_sequence_before(sequence, req->elevator_sequence))
		req->elevator_sequence = sequence;
}

static inline void elevator_account_request(struct request * req)
{
	req->e->sequence++;
	if (req->cmd == READ)
		req->e->read_pendings++;
	req->e->nr_segments++;
}

static inline int elevator_request_latency(elevator_t * elevator, int rw)
{
	int latency;

	latency = elevator->read_latency;
	if (rw != READ)
		latency = elevator->write_latency;

	return latency;
}

#define ELEVATOR_DEFAULT					\
((elevator_t) {							\
	0,				/* sequence */		\
								\
	100000,				/* read_latency */	\
	100000,				/* write_latency */	\
	128,				/* max_bomb_segments */	\
								\
	0,				/* nr_segments */	\
	0,				/* read_pendings */	\
								\
	elevator_default,		/* elevator_fn */	\
	elevator_default_merge,		/* elevator_merge_fn */ \
	elevator_default_dequeue,	/* dequeue_fn */	\
	})

#define ELEVATOR_NOOP						\
((elevator_t) {							\
	0,				/* sequence */		\
								\
	0,				/* read_latency */	\
	0,				/* write_latency */	\
	0,				/* max_bomb_segments */	\
								\
	0,				/* nr_segments */	\
	0,				/* read_pendings */	\
								\
	elevator_noop,			/* elevator_fn */	\
	elevator_noop_merge,		/* elevator_merge_fn */ \
	elevator_noop_dequeue,		/* dequeue_fn */	\
	})

#define ELEVATOR_LINUS						\
((elevator_t) {							\
	0,				/* not used */		\
								\
	1000000,				/* read passovers */	\
	2000000,				/* write passovers */	\
	0,				/* max_bomb_segments */	\
								\
	0,				/* not used */		\
	0,				/* not used */		\
								\
	elevator_linus,			/* elevator_fn */	\
	elevator_linus_merge,		/* elevator_merge_fn */ \
	elevator_noop_dequeue,		/* dequeue_fn */	\
	})

#endif
