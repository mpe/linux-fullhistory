/* cnode related routines for the coda kernel code
   (C) 1996 Peter Braam
   */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/time.h>

#include <linux/coda.h>
#include <linux/coda_linux.h>
#include <linux/coda_fs_i.h>
#include <linux/coda_psdev.h>

extern int coda_debug;
extern int coda_print_entry;

inline int coda_fideq(ViceFid *fid1, ViceFid *fid2)
{
	if (fid1->Vnode != fid2->Vnode)
		return 0;
	if (fid1->Volume != fid2->Volume)
		return 0;
	if (fid1->Unique != fid2->Unique)
		return 0;
	return 1;
}

static struct inode_operations coda_symlink_inode_operations = {
	readlink:	page_readlink,
	follow_link:	page_follow_link,
	setattr:	coda_notify_change,
};

/* cnode.c */
static void coda_fill_inode(struct inode *inode, struct coda_vattr *attr)
{
        CDEBUG(D_SUPER, "ino: %ld\n", inode->i_ino);

        if (coda_debug & D_SUPER ) 
		print_vattr(attr);

        coda_vattr_to_iattr(inode, attr);

        if (S_ISREG(inode->i_mode)) {
                inode->i_op = &coda_file_inode_operations;
                inode->i_fop = &coda_file_operations;
        } else if (S_ISDIR(inode->i_mode)) {
                inode->i_op = &coda_dir_inode_operations;
                inode->i_fop = &coda_dir_operations;
        } else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &coda_symlink_inode_operations;
		inode->i_data.a_ops = &coda_symlink_aops;
	} else
                init_special_inode(inode, inode->i_mode, attr->va_rdev);
}

struct inode * coda_iget(struct super_block * sb, ViceFid * fid,
			 struct coda_vattr * attr)
{
	struct inode *inode;
	struct coda_sb_info *sbi= coda_sbp(sb);
	struct coda_inode_info *cii;
	ino_t ino = attr->va_fileid;

	inode = iget(sb, ino);
	if ( !inode ) { 
		CDEBUG(D_CNODE, "coda_iget: no inode\n");
		return NULL;
	}

	/* check if the inode is already initialized */
	cii = ITOC(inode);
	if (cii->c_magic == CODA_CNODE_MAGIC)
		goto out;

	/* Initialize the Coda inode info structure */
	memset(cii, 0, (int) sizeof(struct coda_inode_info));
	cii->c_magic = CODA_CNODE_MAGIC;
	cii->c_fid   = *fid;
	cii->c_flags = 0;
	cii->c_vnode = inode;
	INIT_LIST_HEAD(&(cii->c_cnhead));
	INIT_LIST_HEAD(&(cii->c_volrootlist));

	coda_fill_inode(inode, attr);

	/* check if it is a weird fid (hashed fid != ino), f.i mountpoints
	   repair object, expanded local-global conflict trees, etc.
	 */
	if ( coda_f2i(fid) == ino )
		goto out;

	/* check if we expect this weird fid */
	if ( !coda_fid_is_weird(fid) )
		printk("Coda: unknown weird fid: ino %ld, fid %s."
		       "Tell Peter.\n", (long)ino, coda_f2s(&cii->c_fid));

	/* add the inode to a global list so we can find it back later */
	list_add(&cii->c_volrootlist, &sbi->sbi_volroothead);
	CDEBUG(D_CNODE, "Added %ld, %s to volroothead\n",
	       (long)ino, coda_f2s(&cii->c_fid));
out:
	return inode;
}

/* this is effectively coda_iget:
   - get attributes (might be cached)
   - get the inode for the fid using vfs iget
   - link the two up if this is needed
   - fill in the attributes
*/
int coda_cnode_make(struct inode **inode, ViceFid *fid, struct super_block *sb)
{
        struct coda_inode_info *cnp;
        struct coda_vattr attr;
        int error;
        
        ENTRY;

	/* We get inode numbers from Venus -- see venus source */

	error = venus_getattr(sb, fid, &attr);
	if ( error ) {
	    CDEBUG(D_CNODE, 
		   "coda_cnode_make: coda_getvattr returned %d for %s.\n", 
		   error, coda_f2s(fid));
	    *inode = NULL;
	    return error;
	} 

	*inode = coda_iget(sb, fid, &attr);
	if ( !(*inode) ) {
		printk("coda_cnode_make: coda_iget failed\n");
                return -ENOMEM;
        }

	cnp = ITOC(*inode);
	/* see if it is the right one (we might have an inode collision) */
	if  ( coda_fideq(fid, &cnp->c_fid) ) {
		CDEBUG(D_DOWNCALL,
		       "Done making inode: ino %ld, count %d with %s\n",
		       (*inode)->i_ino, atomic_read(&(*inode)->i_count), 
		       coda_f2s(&cnp->c_fid));
		EXIT;
		return 0;
	}

	/* collision */
	printk("coda_cnode_make on initialized inode %ld, old %s new %s!\n",
                      (*inode)->i_ino, coda_f2s(&cnp->c_fid), coda_f2s2(fid));
               iput(*inode);
        EXIT;
	return -ENOENT;
}


void coda_replace_fid(struct inode *inode, struct ViceFid *oldfid, 
		      struct ViceFid *newfid)
{
	struct coda_inode_info *cnp;
	struct coda_sb_info *sbi= coda_sbp(inode->i_sb);
	
	cnp = ITOC(inode);

	if ( ! coda_fideq(&cnp->c_fid, oldfid) )
		printk("What? oldfid != cnp->c_fid. Call 911.\n");

	cnp->c_fid = *newfid;

	list_del(&cnp->c_volrootlist);
	if ( !coda_fid_is_weird(newfid) ) 
		list_add(&cnp->c_volrootlist, &sbi->sbi_volroothead);

	return;
}


 

/* convert a fid to an inode. Mostly we can compute
   the inode number from the FID, but not for volume
   mount points: those are in a list */
struct inode *coda_fid_to_inode(ViceFid *fid, struct super_block *sb) 
{
	ino_t nr;
	struct inode *inode;
	struct coda_inode_info *cnp;
	ENTRY;


	if ( !sb ) {
		printk("coda_fid_to_inode: no sb!\n");
		return NULL;
	}

	if ( !fid ) {
		printk("coda_fid_to_inode: no fid!\n");
		return NULL;
	}
	CDEBUG(D_INODE, "%s\n", coda_f2s(fid));


	if ( coda_fid_is_weird(fid) ) {
		struct coda_inode_info *cii;
		struct list_head *lh, *le;
		struct coda_sb_info *sbi = coda_sbp(sb);
		le = lh = &sbi->sbi_volroothead;

		while ( (le = le->next) != lh ) {
			cii = list_entry(le, struct coda_inode_info, 
					 c_volrootlist);
			CDEBUG(D_DOWNCALL, "iterating, now doing %s, ino %ld\n", 
			       coda_f2s(&cii->c_fid), cii->c_vnode->i_ino);
			if ( coda_fideq(&cii->c_fid, fid) ) {
				inode = cii->c_vnode;
				CDEBUG(D_INODE, "volume root, found %ld\n", inode->i_ino);
				if ( cii->c_magic != CODA_CNODE_MAGIC )
					printk("%s: Bad magic in inode, tell Peter.\n", 
					       __FUNCTION__);

				iget(sb, inode->i_ino);
				return inode;
			}
			
		}
		return NULL;
	}

	/* fid is not weird: ino should be computable */
	nr = coda_f2i(fid);
	inode = iget(sb, nr);
	if ( !inode ) {
		printk("coda_fid_to_inode: null from iget, sb %p, nr %ld.\n",
		       sb, (long)nr);
		return NULL;
	}

	/* check if this inode is linked to a cnode */
	cnp = ITOC(inode);
	if ( cnp->c_magic != CODA_CNODE_MAGIC ) {
		CDEBUG(D_INODE, "uninitialized inode. Return.\n");
		iput(inode);
		return NULL;
	}

	/* make sure fid is the one we want; 
	   unfortunately Venus will shamelessly send us mount-symlinks.
	   These have the same inode as the root of the volume they
	   mount, but the fid will be wrong. 
	*/
	if ( !coda_fideq(fid, &(cnp->c_fid)) ) {
		/* printk("coda_fid2inode: bad cnode (ino %ld, fid %s)"
		   "Tell Peter.\n", nr, coda_f2s(fid)); */
		iput(inode);
		return NULL;
	}

	CDEBUG(D_INODE, "found %ld\n", inode->i_ino);
	return inode;
}

/* the CONTROL inode is made without asking attributes from Venus */
int coda_cnode_makectl(struct inode **inode, struct super_block *sb)
{
    int error = 0;

    *inode = iget(sb, CTL_INO);
    if ( *inode ) {
	(*inode)->i_op = &coda_ioctl_inode_operations;
	(*inode)->i_fop = &coda_ioctl_operations;
	(*inode)->i_mode = 00444;
	error = 0;
    } else { 
	error = -ENOMEM;
    }
    
    return error;
}
