#ifndef _NFS_FS_SB
#define _NFS_FS_SB

/*
 * NFS client parameters stored in the superblock.
 */
struct nfs_server {
	struct rpc_clnt *	client;		/* RPC client handle */
	int			flags;		/* various flags */
	unsigned int		rsize;		/* read size */
	unsigned int		wsize;		/* write size */
	unsigned int		dtsize;		/* readdir size */
	unsigned int		bsize;		/* server block size */
	unsigned int		acregmin;	/* attr cache timeouts */
	unsigned int		acregmax;
	unsigned int		acdirmin;
	unsigned int		acdirmax;
	char *			hostname;	/* remote hostname */
	struct nfs_reqlist *	rw_requests;	/* async read/write requests */
};

/*
 * nfs super-block data in memory
 */
struct nfs_sb_info {
	struct nfs_server	s_server;
};

#endif
