/*
 * Quota code necessary even when VFS quota support is not compiled
 * into the kernel.  The interesting stuff is over in dquot.c, here
 * we have symbols for initial quotactl(2) handling, the sysctl(2)
 * variables, etc - things needed even when quota support disabled.
 */

#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <asm/current.h>
#include <asm/uaccess.h>
#include <linux/kernel.h>
#include <linux/smp_lock.h>
#include <linux/security.h>

/* Check validity of quotactl */
static int check_quotactl_valid(struct super_block *sb, int type, int cmd, qid_t id)
{
	if (type >= MAXQUOTAS)
		return -EINVAL;
	/* Is operation supported? */
	if (!sb->s_qcop)
		return -ENOSYS;

	switch (cmd) {
		case Q_GETFMT:
			break;
		case Q_QUOTAON:
			if (!sb->s_qcop->quota_on)
				return -ENOSYS;
			break;
		case Q_QUOTAOFF:
			if (!sb->s_qcop->quota_off)
				return -ENOSYS;
			break;
		case Q_SETINFO:
			if (!sb->s_qcop->set_info)
				return -ENOSYS;
			break;
		case Q_GETINFO:
			if (!sb->s_qcop->get_info)
				return -ENOSYS;
			break;
		case Q_SETQUOTA:
			if (!sb->s_qcop->set_dqblk)
				return -ENOSYS;
			break;
		case Q_GETQUOTA:
			if (!sb->s_qcop->get_dqblk)
				return -ENOSYS;
			break;
		case Q_SYNC:
			if (!sb->s_qcop->quota_sync)
				return -ENOSYS;
			break;
		case Q_XQUOTAON:
		case Q_XQUOTAOFF:
		case Q_XQUOTARM:
			if (!sb->s_qcop->set_xstate)
				return -ENOSYS;
			break;
		case Q_XGETQSTAT:
			if (!sb->s_qcop->get_xstate)
				return -ENOSYS;
			break;
		case Q_XSETQLIM:
			if (!sb->s_qcop->set_xquota)
				return -ENOSYS;
			break;
		case Q_XGETQUOTA:
			if (!sb->s_qcop->get_xquota)
				return -ENOSYS;
			break;
		default:
			return -EINVAL;
	}

	/* Is quota turned on for commands which need it? */
	switch (cmd) {
		case Q_GETFMT:
		case Q_GETINFO:
		case Q_QUOTAOFF:
		case Q_SETINFO:
		case Q_SETQUOTA:
		case Q_GETQUOTA:
			if (!sb_has_quota_enabled(sb, type))
				return -ESRCH;
	}
	/* Check privileges */
	if (cmd == Q_GETQUOTA || cmd == Q_XGETQUOTA) {
		if (((type == USRQUOTA && current->euid != id) ||
		     (type == GRPQUOTA && !in_egroup_p(id))) &&
		    !capable(CAP_SYS_ADMIN))
			return -EPERM;
	}
	else if (cmd != Q_GETFMT && cmd != Q_SYNC && cmd != Q_GETINFO && cmd != Q_XGETQSTAT)
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

	return security_ops->quotactl (cmd, type, id, sb);
}

/* Resolve device pathname to superblock */
static struct super_block *resolve_dev(const char *path)
{
	int ret;
	mode_t mode;
	struct nameidata nd;
	struct block_device *bdev;
	struct super_block *sb;

	ret = user_path_walk(path, &nd);
	if (ret)
		goto out;

	bdev = nd.dentry->d_inode->i_bdev;
	mode = nd.dentry->d_inode->i_mode;
	path_release(&nd);

	ret = -ENOTBLK;
	if (!S_ISBLK(mode))
		goto out;
	ret = -ENODEV;
	sb = get_super(bdev);
	if (!sb)
		goto out;
	return sb;
out:
	return ERR_PTR(ret);
}

/* Copy parameters and call proper function */
static int do_quotactl(struct super_block *sb, int type, int cmd, qid_t id, caddr_t addr)
{
	int ret;

	switch (cmd) {
		case Q_QUOTAON: {
			char *pathname;

			if (IS_ERR(pathname = getname(addr)))
				return PTR_ERR(pathname);
			ret = sb->s_qcop->quota_on(sb, type, id, pathname);
			putname(pathname);
			return ret;
		}
		case Q_QUOTAOFF:
			return sb->s_qcop->quota_off(sb, type);

		case Q_GETFMT: {
			__u32 fmt;

			fmt = sb_dqopt(sb)->info[type].dqi_format->qf_fmt_id;
			if (copy_to_user(addr, &fmt, sizeof(fmt)))
				return -EFAULT;
			return 0;
		}
		case Q_GETINFO: {
			struct if_dqinfo info;

			if ((ret = sb->s_qcop->get_info(sb, type, &info)))
				return ret;
			if (copy_to_user(addr, &info, sizeof(info)))
				return -EFAULT;
			return 0;
		}
		case Q_SETINFO: {
			struct if_dqinfo info;

			if (copy_from_user(&info, addr, sizeof(info)))
				return -EFAULT;
			return sb->s_qcop->set_info(sb, type, &info);
		}
		case Q_GETQUOTA: {
			struct if_dqblk idq;

			if ((ret = sb->s_qcop->get_dqblk(sb, type, id, &idq)))
				return ret;
			if (copy_to_user(addr, &idq, sizeof(idq)))
				return -EFAULT;
			return 0;
		}
		case Q_SETQUOTA: {
			struct if_dqblk idq;

			if (copy_from_user(&idq, addr, sizeof(idq)))
				return -EFAULT;
			return sb->s_qcop->set_dqblk(sb, type, id, &idq);
		}
		case Q_SYNC:
			return sb->s_qcop->quota_sync(sb, type);

		case Q_XQUOTAON:
		case Q_XQUOTAOFF:
		case Q_XQUOTARM: {
			__u32 flags;

			if (copy_from_user(&flags, addr, sizeof(flags)))
				return -EFAULT;
			return sb->s_qcop->set_xstate(sb, flags, cmd);
		}
		case Q_XGETQSTAT: {
			struct fs_quota_stat fqs;
		
			if ((ret = sb->s_qcop->get_xstate(sb, &fqs)))
				return ret;
			if (copy_to_user(addr, &fqs, sizeof(fqs)))
				return -EFAULT;
			return 0;
		}
		case Q_XSETQLIM: {
			struct fs_disk_quota fdq;

			if (copy_from_user(&fdq, addr, sizeof(fdq)))
				return -EFAULT;
		       return sb->s_qcop->set_xquota(sb, type, id, &fdq);
		}
		case Q_XGETQUOTA: {
			struct fs_disk_quota fdq;

			if ((ret = sb->s_qcop->get_xquota(sb, type, id, &fdq)))
				return ret;
			if (copy_to_user(addr, &fdq, sizeof(fdq)))
				return -EFAULT;
			return 0;
		}
		/* We never reach here unless validity check is broken */
		default:
			BUG();
	}
	return 0;
}

/*
 * This is the system call interface. This communicates with
 * the user-level programs. Currently this only supports diskquota
 * calls. Maybe we need to add the process quotas etc. in the future,
 * but we probably should use rlimits for that.
 */
asmlinkage long sys_quotactl(unsigned int cmd, const char *special, qid_t id, caddr_t addr)
{
	uint cmds, type;
	struct super_block *sb = NULL;
	int ret = -EINVAL;

	lock_kernel();
	cmds = cmd >> SUBCMDSHIFT;
	type = cmd & SUBCMDMASK;

	if (IS_ERR(sb = resolve_dev(special))) {
		ret = PTR_ERR(sb);
		sb = NULL;
		goto out;
	}
	if ((ret = check_quotactl_valid(sb, type, cmds, id)) < 0)
		goto out;
	ret = do_quotactl(sb, type, cmds, id, addr);
out:
	if (sb)
		drop_super(sb);
	unlock_kernel();
	return ret;
}
