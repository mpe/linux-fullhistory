#ifndef _LINUX_ELEVATOR_H
#define _LINUX_ELEVATOR_H

typedef int (elevator_merge_fn) (request_queue_t *, struct request **,
				 struct bio *);

typedef void (elevator_merge_cleanup_fn) (request_queue_t *, struct request *, int);

typedef void (elevator_merge_req_fn) (struct request *, struct request *);

typedef struct request *(elevator_next_req_fn) (request_queue_t *);

typedef void (elevator_add_req_fn) (request_queue_t *, struct request *, struct list_head *);
typedef int (elevator_queue_empty_fn) (request_queue_t *);
typedef void (elevator_remove_req_fn) (request_queue_t *, struct request *);

typedef int (elevator_init_fn) (request_queue_t *, elevator_t *);
typedef void (elevator_exit_fn) (request_queue_t *, elevator_t *);

struct elevator_s
{
	elevator_merge_fn *elevator_merge_fn;
	elevator_merge_cleanup_fn *elevator_merge_cleanup_fn;
	elevator_merge_req_fn *elevator_merge_req_fn;

	elevator_next_req_fn *elevator_next_req_fn;
	elevator_add_req_fn *elevator_add_req_fn;
	elevator_remove_req_fn *elevator_remove_req_fn;

	elevator_queue_empty_fn *elevator_queue_empty_fn;

	elevator_init_fn *elevator_init_fn;
	elevator_exit_fn *elevator_exit_fn;

	void *elevator_data;
};

/*
 * block elevator interface
 */
extern void __elv_add_request(request_queue_t *, struct request *,
			      struct list_head *);
extern void elv_merge_cleanup(request_queue_t *, struct request *, int);
extern int elv_merge(request_queue_t *, struct request **, struct bio *);
extern void elv_merge_requests(request_queue_t *, struct request *,
			       struct request *);
extern void elv_remove_request(request_queue_t *, struct request *);

/*
 * noop I/O scheduler. always merges, always inserts new request at tail
 */
extern elevator_t elevator_noop;

/*
 * elevator linus. based on linus ideas of starvation control, using
 * sequencing to manage inserts and merges.
 */
extern elevator_t elevator_linus;
#define elv_linus_sequence(rq)	((long)(rq)->elevator_private)

/*
 * use the /proc/iosched interface, all the below is history ->
 */
typedef struct blkelv_ioctl_arg_s {
	int queue_ID;
	int read_latency;
	int write_latency;
	int max_bomb_segments;
} blkelv_ioctl_arg_t;
#define BLKELVGET   _IOR(0x12,106,sizeof(blkelv_ioctl_arg_t))
#define BLKELVSET   _IOW(0x12,107,sizeof(blkelv_ioctl_arg_t))

extern int elevator_init(request_queue_t *, elevator_t *, elevator_t);
extern void elevator_exit(request_queue_t *, elevator_t *);

/*
 * Return values from elevator merger
 */
#define ELEVATOR_NO_MERGE	0
#define ELEVATOR_FRONT_MERGE	1
#define ELEVATOR_BACK_MERGE	2

/*
 * will change once we move to a more complex data structure than a simple
 * list for pending requests
 */
#define elv_queue_empty(q)	list_empty(&(q)->queue_head)

#endif
