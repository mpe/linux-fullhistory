#ifndef __LINUX_PKT_SCHED_H
#define __LINUX_PKT_SCHED_H

#define PSCHED_TC_INIT		1
#define PSCHED_TC_DESTROY	2
#define PSCHED_TC_ATTACH	3
#define PSCHED_TC_DETACH	4


/* "Logical" priority bands, not depending of concrete packet scheduler.
   Every scheduler will map them to real traffic classes, if it have
   no more precise machanism.
 */

#define TC_PRIO_BESTEFFORT		0
#define TC_PRIO_FILLER			1
#define TC_PRIO_BULK			2
#define TC_PRIO_INTERACTIVE_BULK	4
#define TC_PRIO_INTERACTIVE		6
#define TC_PRIO_CONTROL			7


struct pschedctl
{
	int		command;
	int		handle;
	int		child;
	int		ifindex;
	char		id[IFNAMSIZ];
	int		arglen;
	char		args[0];
};

/* CBQ section */

#define CBQ_MAXPRIO	8
#define CBQ_MAXLEVEL	8

/* CSZ section */

struct cszctl
{
	int		flow_id;
	int		handle;
	unsigned long	rate;
	unsigned long	max_bytes;
	unsigned long	depth;
	unsigned long	L_tab[256];
};

struct cszinitctl
{
	int		flows;
	unsigned	cell_log;
};

/* TBF section */

struct tbfctl
{
	unsigned	cell_log;
	unsigned long	bytes;
	unsigned long	depth;
	unsigned long	L_tab[256];
};

/* SFQ section */

struct sfqctl
{
	unsigned	quantum;
	unsigned	depth;
	unsigned	divisor;
	unsigned	flows;
};

/* RED section */

struct redctl
{
	unsigned	qmaxbytes;	/* HARD maximal queue length	*/
	unsigned	qth_min;	/* Min average length threshold: A scaled */
	unsigned	qth_max;	/* Max average length threshold: A scaled */
	char		Alog;		/* Point position in average lengths */
	char		Wlog;		/* log(W)		*/
	char		Rlog;		/* random number bits	*/
	char		C1log;		/* log(1/C1)		*/
	char		Slog;
	char		Stab[256];
};


#endif
