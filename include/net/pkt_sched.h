#ifndef __NET_PKT_SCHED_H
#define __NET_PKT_SCHED_H

#include <linux/pkt_sched.h>

struct Qdisc_ops
{
	struct Qdisc_ops	*next;
	char			id[IFNAMSIZ];
	int			refcnt;
	int			priv_size;
	int 			(*enqueue)(struct sk_buff *skb, struct Qdisc *);
	struct sk_buff *	(*dequeue)(struct Qdisc *);
	void			(*reset)(struct Qdisc *);
	void			(*destroy)(struct Qdisc *);
	int			(*init)(struct Qdisc *, void *arg);
	int			(*control)(struct Qdisc *, void *);
	int 			(*requeue)(struct sk_buff *skb, struct Qdisc *);
};

struct Qdisc_head
{
	struct Qdisc_head *forw;
};

extern struct Qdisc_head qdisc_head;

struct Qdisc
{
	struct Qdisc_head	h;
	int 			(*enqueue)(struct sk_buff *skb, struct Qdisc *dev);
	struct sk_buff *	(*dequeue)(struct Qdisc *dev);
	struct Qdisc_ops	*ops;
	int			handle;
	struct Qdisc		*parent;
	struct sk_buff_head	q;
	struct device 		*dev;
	unsigned long		dropped;
	unsigned long		tx_last;
	unsigned long		tx_timeo;

	char			data[0];
};


/* Yes, it is slow for [34]86, but we have no choice.
   10 msec resolution is appropriate only for bandwidth < 32Kbit/sec.

   RULE:
   Timer resolution MUST BE < 10% of min_schedulable_packet_size/bandwidth
   
   Normal IP packet size ~ 512byte, hence:

   0.5Kbyte/1Mbyte/sec = 0.5msec, so that we need 50usec timer for
   10Mbit ethernet.

   10msec resolution -> <50Kbit/sec.
   
   The result: [34]86 is not good choice for QoS router :-(
 */


typedef struct timeval psched_time_t;

/* On 64bit architecures it would be clever to define:
typedef u64 psched_time_t;
   and make all this boring arithmetics directly
 */

#ifndef SCHEDULE_ONLY_LOW_BANDWIDTH
#define PSCHED_GET_TIME(stamp) do_gettimeofday(&(stamp))
#else
#define PSCHED_GET_TIME(stamp) ((stamp) = xtime)
#endif

#define PSCHED_TDIFF(tv1, tv2) \
({ \
	   int __delta_sec = (tv1).tv_sec - (tv2).tv_sec; \
	   int __delta = (tv1).tv_usec - (tv2).tv_usec; \
	   if (__delta_sec) { \
	           switch (__delta_sec) { \
		   default: \
			   __delta = 0; \
		   case 2: \
			   __delta += 1000000; \
		   case 1: \
			   __delta += 1000000; \
	           } \
	   } \
	   __delta; \
})

#define PSCHED_TDIFF_SAFE(tv1, tv2, bound, guard) \
({ \
	   int __delta_sec = (tv1).tv_sec - (tv2).tv_sec; \
	   int __delta = (tv1).tv_usec - (tv2).tv_usec; \
	   switch (__delta_sec) { \
	   default: \
		   __delta = (bound); guard; break; \
	   case 2: \
		   __delta += 1000000; \
	   case 1: \
		   __delta += 1000000; \
	   case 0: ; \
	   } \
	   __delta; \
})

#define PSCHED_US2JIFFIE(usecs) (((usecs)+(1000000/HZ-1))/(1000000/HZ))

#define PSCHED_TLESS(tv1, tv2) (((tv1).tv_usec < (tv2).tv_usec && \
				(tv1).tv_sec <= (tv2).tv_sec) || \
				 (tv1).tv_sec < (tv2).tv_sec)

#define PSCHED_TADD2(tv, delta, tv_res) \
({ \
	   int __delta = (tv).tv_usec + (delta); \
	   (tv_res).tv_sec = (tv).tv_sec; \
	   if (__delta > 1000000) { (tv_res).tv_sec++; __delta -= 1000000; } \
	   (tv_res).tv_usec = __delta; \
})

#define PSCHED_TADD(tv, delta) \
({ \
	   (tv).tv_usec += (delta); \
	   if ((tv).tv_usec > 1000000) { (tv).tv_sec++; \
		 (tv).tv_usec -= 1000000; } \
})

/* Set/check that undertime is in the "past perfect";
   it depends on concrete representation of system time
 */

#define PSCHED_SET_PASTPERFECT(t)	((t).tv_sec = 0)
#define PSCHED_IS_PASTPERFECT(t)	((t).tv_sec == 0)


extern struct Qdisc noop_qdisc;

int register_qdisc(struct Qdisc_ops *qops);
int unregister_qdisc(struct Qdisc_ops *qops);
void dev_init_scheduler(struct device *dev);
void dev_shutdown(struct device *dev);
void dev_activate(struct device *dev);
void dev_deactivate(struct device *dev);
void qdisc_reset(struct Qdisc *qdisc);
void qdisc_destroy(struct Qdisc *qdisc);
int pktsched_init(void);

void qdisc_run_queues(void);
int qdisc_restart(struct device *dev);

extern __inline__ void qdisc_wakeup(struct device *dev)
{
	if (!dev->tbusy) {
		struct Qdisc *q = dev->qdisc;
		if (qdisc_restart(dev) && q->h.forw == NULL) {
			q->h.forw = qdisc_head.forw;
			qdisc_head.forw = &q->h;
		}
	}
}

#endif
