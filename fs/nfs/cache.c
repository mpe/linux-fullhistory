
void nfs_bl_cache_invalidate(nfs_cache *nh)
{
	unsigned long flags;
	save_flags(flags);
	cli();
	if(nh->inuse)
		nh->dead=1;
	else
	{
		kfree_s(nh->data);
		nh->data=NULL;
		nh->free=1;
	}
}

void nfs_bl_cache_revalidate(nfs_cache *nh, struct fattr fa)
{
	nh->fattr=fattr;
	nh->time=jiffies;
}

/*
 *	Find a block in the cache. We know the cache is block sized in block
 *	aligned space.
 */
 
nfs_cache *nfs_cache_find(struct inode *inode, off_t pos)
{
	nfs_cache *nh=&nfs_cache_slot[0];
	nfs_cache *ffree=NULL;
	struct nfs_fattr fattr;
	int ct=0;
	while(ct<NH_CACHE_SIZE)
	{
		if(nh->inode_num==inode->i_no && !nh->dead&&!nh->free&&nh->file_pos==pos)
		{
			if(abs(jiffies-nh->time)<EXPIRE_CACHE)
				return nh;
			/*
			 *	Revalidate
			 */
			
			if(nfs_proc_getattr(NFS_SERVER(inode), NFS_FH(inode), &fattr))
			{
				nfs_bl_cache_invalidate(nh);
				continue;	/* get attr failed */
			}
			if(nh->fattr.modified!=fattr.modified)
			{
				nfs_bl_cache_invalidate(nh);
				continue;	/* cache is out of date */
			}
			nfs_refresh_inode(inode, fattr);
			nh->fattr=fattr;
			nfs_bl_cache_revalidate(nh);
			return nh;
		}
		if(nh->free)
			ffree=nh;
	}
	return ffree;
}	
