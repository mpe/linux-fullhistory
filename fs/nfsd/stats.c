/*
 * nfsstat.c	procfs-based user access to knfsd statistics
 *
 * /proc/net/nfssrv
 * Format:
 *	net <packets> <udp> <tcp> <tcpconn>
 *	rpc <packets> <badfmt> <badclnt>
 *	auth <flavor> <creds> <upcalls> <badauth> <badverf> <authrej>
 *	fh  <hits> <misses> <avg_util> <stale> <cksum> <badcksum>
 *	rc <hits> <misses> <nocache>
 *	proto <version> <nrprocs>
 *	<calls> <time_msec>
 *	... (for each procedure and protocol version)
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>

#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/stats.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/stats.h>

struct nfsd_stats	nfsdstats;

static int		nfsd_get_info(char *, char **, off_t, int, int);

#ifndef PROC_NET_NFSSRV
# define PROC_NET_NFSSRV	0
#endif

static struct proc_dir_entry proc_nfssrv = {
	PROC_NET_NFSSRV, 4, "nfsd",
	S_IFREG | S_IRUGO, 1, 0, 0,
	6, &proc_net_inode_operations,
	nfsd_get_info
};

struct svc_stat		nfsd_svcstats = {
	NULL, &proc_nfssrv, &nfsd_program,
};

static int
nfsd_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	int			len;

	len = sprintf(buffer,
		"rc %d %d %d\n",
			nfsdstats.rchits,
			nfsdstats.rcmisses,
			nfsdstats.rcnocache);

	/*
	 * Append generic nfsd RPC statistics
	 */
	if (offset >= len) {
		offset -= len;
		len = svcstat_get_info(&nfsd_svcstats, buffer, start,
					offset, length);
#if 0
	} else if (len < length) {
		len = svcstat_get_info(&nfsd_svcstats, buffer + len, start,
					offset - len, length - len);
#endif
	}

	if (offset >= len) {
		*start = buffer;
		return 0;
	}
	*start = buffer + offset;
	if ((len -= offset) > length)
		len = length;
	return len;
}

void
nfsd_stat_init(void)
{
	svcstat_register(&nfsd_svcstats);
}

void
nfsd_stat_shutdown(void)
{
	svcstat_unregister(&nfsd_svcstats);
}
