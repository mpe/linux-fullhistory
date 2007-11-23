#ifndef __NET_PKT_SCHED_H
#define __NET_PKT_SCHED_H

#define PSCHED_GETTIMEOFDAY	1
#define PSCHED_JIFFIES 		2
#define PSCHED_CPU 		3

#define PSCHED_CLOCK_SOURCE	PSCHED_GETTIMEOFDAY

#include <linux/pkt_sched.h>
#include <net/pkt_cls.h>

struct rtattr;
struct Qdisc;

struct qdisc_walker
{
	int	stop;
	int	skip;
	int	count;
	int	(*fn)(struct Qdisc *, unsigned long cl, struct qdisc_walker *);
};

struct Qdisc_class_ops
{
	/* Child qdisc manipulation */
	int			(*graft)(struct Qdisc *, unsigned long cl, struct Qdisc *, struct Qdisc **);

	/* Class manipulation routines */
	unsigned long		(*get)(struct Qdisc *, u32 classid);
	void			(*put)(struct Qdisc *, unsigned long);
	int			(*change)(struct Qdisc *, u32, u32, struct rtattr **, unsigned long *);
	int			(*delete)(struct Qdisc *, unsigned long);
	void			(*walk)(struct Qdisc *, struct qdisc_walker * arg);

	/* Filter manipulation */
	struct tcf_proto **	(*tcf_chain)(struct Qdisc *, unsigned long);
	unsigned long		(*bind_tcf)(struct Qdisc *, u32 classid);
	void			(*unbind_tcf)(struct Qdisc *, unsigned long);

	/* rtnetlink specific */
	int			(*dump)(struct Qdisc *, unsigned long, struct sk_buff *skb, struct tcmsg*);
};

struct Qdisc_ops
{
	struct Qdisc_ops	*next;
	struct Qdisc_class_ops	*cl_ops;
	char			id[IFNAMSIZ];
	int			priv_size;

	int 			(*enqueue)(struct sk_buff *, struct Qdisc *);
	struct sk_buff *	(*dequeue)(struct Qdisc *);
	int 			(*requeue)(struct sk_buff *, struct Qdisc *);
	int			(*drop)(struct Qdisc *);

	int			(*init)(struct Qdisc *, struct rtattr *arg);
	void			(*reset)(struct Qdisc *);
	void			(*destroy)(struct Qdisc *);

	int			(*dump)(struct Qdisc *, struct sk_buff *);
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
	unsigned		flags;
#define TCQ_F_DEFAULT	1
#define TCQ_F_BUILTIN	2
	struct Qdisc_ops	*ops;
	struct Qdisc		*next;
	u32			handle;
	u32			classid;
	struct Qdisc		*parent;
	struct sk_buff_head	q;
	struct device 		*dev;

	struct tc_stats		stats;
	unsigned long		tx_timeo;
	unsigned long		tx_last;
	int			(*reshape_fail)(struct sk_buff *skb, struct Qdisc *q);

	char			data[0];
};

struct qdisc_rate_table
{
	struct tc_ratespec rate;
	u32		data[256];
	struct qdisc_rate_table *next;
	int		refcnt;
};


/* 
   Timer resolution MUST BE < 10% of min_schedulable_packet_size/bandwidth
   
   Normal IP packet size ~ 512byte, hence:

   0.5Kbyte/1Mbyte/sec = 0.5msec, so that we need 50usec timer for
   10Mbit ethernet.

   10msec resolution -> <50Kbit/sec.
   
   The result: [34]86 is not good choice for QoS router :-(

   The things are not so bad, because we may use artifical
   clock evaluated by integration of network data flow
   in the most critical places.

   Note: we do not use fastgettimeofday.
   The reason is that, when it is not the same thing as
   gettimeofday, it returns invalid timestamp, which is
   not updated, when net_bh is active.

   So, use PSCHED_CLOCK_SOURCE = PSCHED_CPU on alpha and pentiums
   with rtdsc. And PSCHED_JIFFIES on all other architectures, including [34]86
   and pentiums without rtdsc.
   You can use PSCHED_GETTIMEOFDAY on another architectures,
   which have fast and precise clock source, but it is too expensive.
 */


#if PSCHED_CLOCK_SOURCE == PSCHED_GETTIMEOFDAY

typedef struct timeval	psched_time_t;
typedef long		psched_tdiff_t;

#define PSCHED_GET_TIME(stamp) do_gettimeofday(&(stamp))
#define PSCHED_US2JIFFIE(usecs) (((usecs)+(1000000/HZ-1))/(1000000/HZ))

#else /* PSCHED_CLOCK_SOURCE != PSCHED_GETTIMEOFDAY */

typedef u64	psched_time_t;
typedef long	psched_tdiff_t;

extern psched_time_t	psched_time_base;

#if PSCHED_CLOCK_SOURCE == PSCHED_JIFFIES

#define PSCHED_WATCHER unsigned long

extern PSCHED_WATCHER psched_time_mark;

#if HZ == 100
#define PSCHED_JSCALE 13
#elif HZ == 1024
#define PSCHED_JSCALE 10
#else
#define PSCHED_JSCALE 0
#endif

#define PSCHED_GET_TIME(stamp) ((stamp) = psched_time_base + (((unsigned long)(jiffies-psched_time_mark))<<PSCHED_JSCALE))
#define PSCHED_US2JIFFIE(delay) ((delay)>>PSCHED_JSCALE)

#elif PSCHED_CLOCK_SOURCE == PSCHED_CPU

extern psched_tdiff_t psched_clock_per_hz;
extern int psched_clock_scale;

#define PSCHED_US2JIFFIE(delay) (((delay)+psched_clock_per_hz-1)/psched_clock_per_hz)

#if CPU == 586 || CPU == 686

#define PSCHED_GET_TIME(stamp) \
({ u32 hi, lo; \
   __asm__ __volatile__ (".byte 0x0f,0x31" :"=a" (lo), "=d" (hi)); \
   (stamp) = ((((u64)hi)<<32) + lo)>>psched_clock_scale; \
})

#elif defined (__alpha__)

#define PSCHED_WATCHER u32

extern PSCHED_WATCHER psched_time_mark;

#define PSCHED_GET_TIME(stamp) \
({ u32 __res; \
   __asm__ __volatile__ ("rpcc %0" : "r="(__res)); \
   if (__res <= psched_time_mark) psched_time_base += 0x100000000UL; \
   psched_time_mark = __res; \
   (stamp) = (psched_time_base + __res)>>psched_clock_scale; \
})

#else

#error PSCHED_CLOCK_SOURCE=PSCHED_CPU is not supported on this arch.

#endif /* ARCH */

#endif /* PSCHED_CLOCK_SOURCE == PSCHED_JIFFIES */

#endif /* PSCHED_CLOCK_SOURCE == PSCHED_GETTIMEOFDAY */

#if PSCHED_CLOCK_SOURCE == PSCHED_GETTIMEOFDAY
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

/* Set/check that time is in the "past perfect";
   it depends on concrete representation of system time
 */

#define PSCHED_SET_PASTPERFECT(t)	((t).tv_sec = 0)
#define PSCHED_IS_PASTPERFECT(t)	((t).tv_sec == 0)

#define	PSCHED_AUDIT_TDIFF(t) ({ if ((t) > 2000000) (t) = 2000000; })

#else

#define PSCHED_TDIFF(tv1, tv2) (long)((tv1) - (tv2))
#define PSCHED_TDIFF_SAFE(tv1, tv2, bound, guard) \
({ \
	   long __delta = (tv1) - (tv2); \
	   if ( __delta > (bound)) {  __delta = (bound); guard; } \
	   __delta; \
})


#define PSCHED_TLESS(tv1, tv2) ((tv1) < (tv2))
#define PSCHED_TADD2(tv, delta, tv_res) ((tv_res) = (tv) + (delta))
#define PSCHED_TADD(tv, delta) ((tv) += (delta))
#define PSCHED_SET_PASTPERFECT(t)	((t) = 0)
#define PSCHED_IS_PASTPERFECT(t)	((t) == 0)
#define	PSCHED_AUDIT_TDIFF(t)

#endif

struct tcf_police
{
	struct tcf_police *next;
	int		refcnt;
	u32		index;

	int		action;
	u32		burst;
	u32		mtu;

	u32		toks;
	u32		ptoks;
	psched_time_t	t_c;
	struct qdisc_rate_table *R_tab;
	struct qdisc_rate_table *P_tab;
};

extern void tcf_police_destroy(struct tcf_police *p);
extern struct tcf_police * tcf_police_locate(struct rtattr *rta);
extern int tcf_police_dump(struct sk_buff *skb, struct tcf_police *p);
extern int tcf_police(struct sk_buff *skb, struct tcf_police *p);

extern __inline__ void tcf_police_release(struct tcf_police *p)
{
	if (p && --p->refcnt == 0)
		tcf_police_destroy(p);
}

extern struct Qdisc noop_qdisc;
extern struct Qdisc_ops noop_qdisc_ops;
extern struct Qdisc_ops pfifo_qdisc_ops;
extern struct Qdisc_ops bfifo_qdisc_ops;

int register_qdisc(struct Qdisc_ops *qops);
int unregister_qdisc(struct Qdisc_ops *qops);
struct Qdisc *qdisc_lookup(struct device *dev, u32 handle);
struct Qdisc *qdisc_lookup_class(struct device *dev, u32 handle);
void dev_init_scheduler(struct device *dev);
void dev_shutdown(struct device *dev);
void dev_activate(struct device *dev);
void dev_deactivate(struct device *dev);
void qdisc_reset(struct Qdisc *qdisc);
void qdisc_destroy(struct Qdisc *qdisc);
struct Qdisc * qdisc_create_dflt(struct device *dev, struct Qdisc_ops *ops);
struct Qdisc * dev_set_scheduler(struct device *dev, struct Qdisc *qdisc);
int qdisc_new_estimator(struct tc_stats *stats, struct rtattr *opt);
void qdisc_kill_estimator(struct tc_stats *stats);
struct qdisc_rate_table *qdisc_get_rtab(struct tc_ratespec *r, struct rtattr *tab);
void qdisc_put_rtab(struct qdisc_rate_table *tab);
int teql_init(void);
int tc_filter_init(void);
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

extern __inline__ unsigned psched_mtu(struct device *dev)
{
	unsigned mtu = dev->mtu;
	return dev->hard_header ? mtu + dev->hard_header_len : mtu;
}

#endif
