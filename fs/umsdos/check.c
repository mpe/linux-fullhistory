/*
 * linux/fs/umsdos/check.c
 *
 * Sanity-checking code
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/umsdos_fs.h>

#include <asm/system.h>

#ifdef CHECK_PAGE_TABLES
static int check_one_table (struct pde *page_dir)
{
	if (pgd_none (*page_dir))
		return 0;
	if (pgd_bad (*page_dir))
		return 1;
	return 0;
}

/*
 * This function checks all page tables of "current"
 */
void check_page_tables (void)
{
	struct pgd *pg_dir;
	static int err = 0;

	int stack_level = (long) (&pg_dir) - current->kernel_stack_page;

	if (stack_level < 1500)
		printk ("** %d ** ", stack_level);
	pg_dir = PAGE_DIR_OFFSET (current, 0);
	if (err == 0) {
		int i;

		for (i = 0; i < PTRS_PER_PAGE; i++, page_dir++) {
			int notok = check_one_table (page_dir);

			if (notok) {
				err++;
				printk ("|%d:%08lx| ", i, page_dir->pgd);
			}
		}
		if (err)
			printk ("\nError MM %d\n", err);
	}
}
#endif


#if UMS_DEBUG
/*
 * check a superblock
 */

void check_sb (struct super_block *sb, const char c)
{
	if (sb) {
		Printk ((" (has %c_sb=%d, %d)", 
			c, MAJOR (sb->s_dev), MINOR (sb->s_dev)));
	} else {
		Printk ((" (%c_sb is NULL)", c));
	}
} 

/*
 * check an inode
 */
extern struct inode_operations umsdos_rdir_inode_operations;

void check_inode (struct inode *inode)
{
	if (inode) {
		Printk ((KERN_DEBUG "*   inode is %lu (i_count=%d)",
			 inode->i_ino, inode->i_count));
		check_sb (inode->i_sb, 'i');
		
		if (inode->i_dentry.next) {	/* FIXME: does this work ? */
			Printk ((" (has i_dentry)"));
		} else {
			Printk ((" (NO i_dentry)"));
		}
		
		if (inode->i_op == NULL) {
			Printk ((" (i_op is NULL)\n"));
		} else if (inode->i_op == &umsdos_dir_inode_operations) {
			Printk ((" (i_op is umsdos_dir_inode_operations)\n"));
		} else if (inode->i_op == &umsdos_file_inode_operations) {
			Printk ((" (i_op is umsdos_file_inode_operations)\n"));
		} else if (inode->i_op == &umsdos_file_inode_operations_no_bmap) {
			Printk ((" (i_op is umsdos_file_inode_operations_no_bmap)\n"));
		} else if (inode->i_op == &umsdos_file_inode_operations_readpage) {
			Printk ((" (i_op is umsdos_file_inode_operations_readpage)\n"));
		} else if (inode->i_op == &umsdos_rdir_inode_operations) {
			Printk ((" (i_op is umsdos_rdir_inode_operations)\n"));
		} else if (inode->i_op == &umsdos_symlink_inode_operations) {
			Printk ((" (i_op is umsdos_symlink_inode_operations)\n"));
		} else {
			Printk ((" (i_op is UNKNOWN: %p)\n", inode->i_op));
		}
	} else {
		Printk ((KERN_DEBUG "*   inode is NULL\n"));
	}
}

/*
 * checks all inode->i_dentry
 *
 */
void checkd_inode (struct inode *inode)
{
	struct dentry *ret;
	struct list_head *cur;
	int count = 0;
	if (!inode) {
		printk (KERN_ERR "checkd_inode: inode is NULL!\n");
		return;
	}

	Printk ((KERN_DEBUG "checkd_inode:  inode %lu\n", inode->i_ino));
	cur = inode->i_dentry.next;
	while (count++ < 10) {
		PRINTK (("1..."));
		if (!cur) {
			Printk ((KERN_ERR "checkd_inode: *** NULL reached. exit.\n"));
			return;
		}
		PRINTK (("2..."));
		ret = list_entry (cur, struct dentry, d_alias);
		PRINTK (("3..."));
		if (cur == cur->next) {
			Printk ((KERN_DEBUG "checkd_inode: *** cur=cur->next: normal exit.\n"));
			return;
		}
		PRINTK (("4..."));
		if (!ret) {
			Printk ((KERN_ERR "checkd_inode: *** ret dentry is NULL. exit.\n"));
			return;
		}
		PRINTK (("5... (ret=%p)...", ret));
		PRINTK (("5.1.. (ret->d_dname=%p)...", &(ret->d_name)));
		PRINTK (("5.1.1. (ret->d_dname.len=%d)...", (int) ret->d_name.len));
		PRINTK (("5.1.2. (ret->d_dname.name=%c)...", ret->d_name.name));
		Printk ((KERN_DEBUG "checkd_inode:   i_dentry is %.*s\n", (int) ret->d_name.len, ret->d_name.name));
		PRINTK (("6..."));
		cur = cur->next;
		PRINTK (("7..."));
#if 1
		Printk ((KERN_DEBUG "checkd_inode: *** finished after count 1 (operator forced)\n"));
		return;
#endif		
	}
	Printk ((KERN_ERR "checkd_inode: *** OVER LIMIT (loop?) !\n"));
	return;
}

/*
 * internal part of check_dentry. does the real job.
 *
 */

void check_dent_int (struct dentry *dentry, int parent)
{
	if (parent) {
		Printk ((KERN_DEBUG "*  parent(%d) dentry: %.*s\n", 
			parent, (int) dentry->d_name.len, dentry->d_name.name));
	} else {
		Printk ((KERN_DEBUG "*  checking dentry: %.*s\n",
			 (int) dentry->d_name.len, dentry->d_name.name));
	}
	check_inode (dentry->d_inode);
	Printk ((KERN_DEBUG "*   d_count=%d", dentry->d_count));
	check_sb (dentry->d_sb, 'd');
	if (dentry->d_op == NULL) {
		Printk ((" (d_op is NULL)\n"));
	} else {
		Printk ((" (d_op is UNKNOWN: %p)\n", dentry->d_op));
	}
}

/*
 * checks dentry with full traceback to root and prints info. Limited to 10 recursive depths to avoid infinite loops.
 *
 */

void check_dentry_path (struct dentry *dentry, const char *desc)
{
	int count=0;
	Printk ((KERN_DEBUG "*** check_dentry_path: %.60s\n", desc));

	if (!dentry) {
		Printk ((KERN_DEBUG "*** checking dentry... it is NULL !\n"));
		return;
	}
	if (IS_ERR(dentry)) {
		Printk ((KERN_DEBUG "*** checking dentry... it is ERR(%ld) !\n",
			 PTR_ERR(dentry)));
		return;
	}
	
	while (dentry && count < 10) {
		check_dent_int (dentry, count++);
		if (dentry == dentry->d_parent) {
			Printk ((KERN_DEBUG "*** end checking dentry (root reached ok)\n"));
			break;
		}
		dentry = dentry->d_parent;
	}

	if (count >= 10) {	/* if infinite loop detected */
		Printk ((KERN_ERR 
			"*** WARNING ! INFINITE LOOP ! check_dentry_path aborted !\n"));
	}
	
	if (!dentry) {
		Printk ((KERN_ERR 
			"*** WARNING ! NULL dentry ! check_dentry_path aborted !\n"));
	}
}
#else
void check_sb (struct super_block *sb, const char c) {};
void check_inode (struct inode *inode) {};
void checkd_inode (struct inode *inode) {};
void check_dentry_path (struct dentry *dentry, const char *desc) {};
#endif	/* UMS_DEBUG */

