#ifndef _LINUX_ELEVATOR_H
#define _LINUX_ELEVATOR_H

#define ELEVATOR_DEBUG

struct elevator_s;
typedef struct elevator_s elevator_t;

typedef void (elevator_fn) (struct request *, elevator_t *,
			    struct list_head *,
			    struct list_head *, int);

struct elevator_s
{
	int sequence;

	int read_latency;
	int write_latency;
	int max_bomb_segments;

	unsigned int nr_segments;
	int read_pendings;

	elevator_fn * elevator_fn;

	unsigned int queue_ID;
};

#define ELEVATOR_DEFAULTS				\
((elevator_t) {						\
	0,			/* sequence */		\
							\
	128,			/* read_latency */	\
	8192,			/* write_latency */	\
	32,			/* max_bomb_segments */	\
							\
	0,			/* nr_segments */	\
	0,			/* read_pendings */	\
							\
	elevator_default,	/* elevator_fn */	\
	})


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


extern void elevator_init(elevator_t *);

#ifdef ELEVATOR_DEBUG
extern void elevator_debug(request_queue_t *, kdev_t);
#else
#define elevator_debug(a,b) do { } while(0)
#endif

#define elevator_sequence_after(a,b) ((int)((b)-(a)) < 0)
#define elevator_sequence_before(a,b) elevator_sequence_after(b,a)
#define elevator_sequence_after_eq(a,b) ((int)((b)-(a)) <= 0)
#define elevator_sequence_before_eq(a,b) elevator_sequence_after_eq(b,a)

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

static inline void elevator_merge_requests(elevator_t * e, struct request * req, struct request * next)
{
	if (elevator_sequence_before(next->elevator_sequence, req->elevator_sequence))
		req->elevator_sequence = next->elevator_sequence;
	if (req->cmd == READ)
		e->read_pendings--;

}

static inline int elevator_sequence(elevator_t * e, int latency)
{
	return latency + e->sequence;
}

#define elevator_merge_before(q, req, lat)	__elevator_merge((q), (req), (lat), 0)
#define elevator_merge_after(q, req, lat)	__elevator_merge((q), (req), (lat), 1)
static inline void __elevator_merge(elevator_t * elevator, struct request * req, int latency, int after)
{
	int sequence = elevator_sequence(elevator, latency);
	if (after)
		sequence -= req->nr_segments;
	if (elevator_sequence_before(sequence, req->elevator_sequence))
		req->elevator_sequence = sequence;
}

static inline void elevator_account_request(elevator_t * elevator, struct request * req)
{
	elevator->sequence++;
	if (req->cmd == READ)
		elevator->read_pendings++;
	elevator->nr_segments++;
}

static inline int elevator_request_latency(elevator_t * elevator, int rw)
{
	int latency;

	latency = elevator->read_latency;
	if (rw != READ)
		latency = elevator->write_latency;

	return latency;
}

#endif
