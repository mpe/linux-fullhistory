#ifndef _LINUX_KERNELD_H
#define _LINUX_KERNELD_H

#define KERNELD_SYSTEM 1
#define KERNELD_REQUEST_MODULE 2 /* "insmod" */
#define KERNELD_RELEASE_MODULE 3 /* "rmmod" */
#define KERNELD_DELAYED_RELEASE_MODULE 4 /* "rmmod" */
#define KERNELD_CANCEL_RELEASE_MODULE 5 /* "rmmod" */
#define KERNELD_REQUEST_ROUTE 6 /* from net/ipv4/route.c */
#define KERNELD_BLANKER 7 /* from drivers/char/console.c */
#define KERNELD_ARP 256 /* from net/ipv4/arp.c */

/*
 * Uncomment the following line for the new kerneld protocol
 * This includes the pid of the kernel level requester into the kerneld header
 */
/*
#define NEW_KERNELD_PROTOCOL
 */
#ifdef NEW_KERNELD_PROTOCOL
#define OLDIPC_KERNELD 00040000   /* use the kerneld message channel */
#define IPC_KERNELD 00140000   /* use the kerneld message channel, new protocol */
#define KDHDR (sizeof(long) + sizeof(short) + sizeof(short))
#define NULL_KDHDR 0, 2, 0
#else
#define IPC_KERNELD 00040000   /* use the kerneld message channel */
#define KDHDR (sizeof(long))
#define NULL_KDHDR 0
#endif
#define KERNELD_MAXCMD 0x7ffeffff
#define KERNELD_MINSEQ 0x7fff0000 /* "commands" legal up to 0x7ffeffff */
#define KERNELD_WAIT 0x80000000
#define KERNELD_NOWAIT 0

struct kerneld_msg {
	long mtype;
	long id;
#ifdef NEW_KERNELD_PROTOCOL
	short version;
	short pid;
#endif
#ifdef __KERNEL__
	char *text;
#else
	char text[1];
#endif /* __KERNEL__ */
};

#ifdef __KERNEL__
extern int kerneld_send(int msgtype, int ret_size, int msgsz,
		const char *text, const char *ret_val);

/*
 * Request that a module should be loaded.
 * Wait for the exit status from insmod/modprobe.
 * If it fails, it fails... at least we tried...
 */
static inline int request_module(const char *name)
{
	return kerneld_send(KERNELD_REQUEST_MODULE,
			0 | KERNELD_WAIT,
			strlen(name), name, NULL);
}

/*
 * Request the removal of a module, maybe don't wait for it.
 * It doesn't matter if the removal fails, now does it?
 */
static inline int release_module(const char *name, int waitflag)
{
	return kerneld_send(KERNELD_RELEASE_MODULE,
			0 | (waitflag?KERNELD_WAIT:KERNELD_NOWAIT),
			strlen(name), name, NULL);
}

/*
 * Request a delayed removal of a module, but don't wait for it.
 * The delay is done by kerneld (default: 60 seconds)
 */
static inline int delayed_release_module(const char *name)
{
	return kerneld_send(KERNELD_DELAYED_RELEASE_MODULE,
			0 | KERNELD_NOWAIT,
			strlen(name), name, NULL);
}

/*
 * Attempt to cancel a previous request for removal of a module,
 * but don't wait for it.
 * This call can be made if the kernel wants to prevent a delayed
 * unloading of a module.
 */
static inline int cancel_release_module(const char *name)
{
	return kerneld_send(KERNELD_CANCEL_RELEASE_MODULE,
			0 | KERNELD_NOWAIT,
			strlen(name), name, NULL);
}

/*
 * Perform an "inverted" system call, maybe return the exit status
 */
static inline int ksystem(const char *cmd, int waitflag)
{
	return kerneld_send(KERNELD_SYSTEM,
			0 | (waitflag?KERNELD_WAIT:KERNELD_NOWAIT),
			strlen(cmd), cmd, NULL);
}

/*
 * Try to create a route, possibly by opening a ppp-connection
 */
static inline int kerneld_route(const char *ip_route)
{
	return kerneld_send(KERNELD_REQUEST_ROUTE,
			0 | KERNELD_WAIT,
			strlen(ip_route), ip_route, NULL);
}

/*
 * Handle an external screen blanker
 */
static inline int kerneld_blanker(int on_off) /* 0 => "off", else "on" */
{
	return kerneld_send(KERNELD_BLANKER,
			0 | (on_off?KERNELD_NOWAIT:KERNELD_WAIT),
			strlen(on_off?"on":"off"), on_off?"on":"off", NULL);
}

#endif /* __KERNEL__ */
#endif
