/*
 * linux/fs/nfsd/stats.c
 *
 * procfs-based user access to knfsd statistics
 *
 * /proc/net/rpc/nfsd
 *
 * Format:
 *	rc <hits> <misses> <nocache>
 *			Statistsics for the reply cache
 *	plus generic RPC stats (see net/sunrpc/stats.c)
 *
 * Copyright (C) 1995, 1996, 1997 Olaf Kirch <okir@monad.swb.de>
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
struct svc_stat		nfsd_svcstats = { &nfsd_program, };

static int
nfsd_proc_read(char *buffer, char **start, off_t offset, int count,
				int *eof, void *data)
{
	int	len;

	len = sprintf(buffer, "rc %d %d %d  %d %d %d %d %d\n",
			nfsdstats.rchits,
			nfsdstats.rcmisses,
			nfsdstats.rcnocache,
			nfsdstats.fh_cached,
			nfsdstats.fh_valid,
			nfsdstats.fh_fixup,
			nfsdstats.fh_lookup,
			nfsdstats.fh_stale);

	/* Assume we haven't hit EOF yet. Will be set by svc_proc_read. */
	*eof = 0;

	/*
	 * Append generic nfsd RPC statistics if there's room for it.
	 */
	if (len <= offset) {
		len = svc_proc_read(buffer, start, offset - len, count,
				eof, data);
		return len;
	}

	if (len < count) {
		len += svc_proc_read(buffer + len, start, 0, count - len,
				eof, data);
	}

	if (offset >= len) {
		*start = buffer;
		return 0;
	}

	*start = buffer + offset;
	if ((len -= offset) > count)
		return count;
	return len;
}

void
nfsd_stat_init(void)
{
	struct proc_dir_entry	*ent;

	if ((ent = svc_proc_register(&nfsd_svcstats)) != 0) {
		ent->read_proc = nfsd_proc_read;
#ifdef MODULE
		ent->fill_inode = nfsd_modcount;
#endif
	}
}

void
nfsd_stat_shutdown(void)
{
	svc_proc_unregister("nfsd");
}
