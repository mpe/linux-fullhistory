/*
 * linux/net/sunrpc/sysctl.c
 *
 * Sysctl interface to sunrpc module. This is for debugging only now.
 *
 * I would prefer to register the sunrpc table below sys/net, but that's
 * impossible at the moment.
 */

#include <linux/version.h>
#include <linux/types.h>
#include <linux/linkage.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/sysctl.h>
#if LINUX_VERSION_CODE >= 0x020100
#include <asm/uaccess.h>
#else
# include <linux/mm.h>
# define copy_from_user		memcpy_fromfs
# define copy_to_user		memcpy_tofs
# define access_ok		!verify_area
#endif
#include <linux/sunrpc/types.h>

/*
 * Declare the debug flags here
 */
unsigned int	rpc_debug  = 0;
unsigned int	nfs_debug  = 0;
unsigned int	nfsd_debug = 0;
unsigned int	nlm_debug  = 0;

#ifdef RPC_DEBUG

static struct ctl_table_header *sunrpc_table_header = NULL;
static ctl_table		sunrpc_table[];

void
rpc_register_sysctl(void)
{
	if (sunrpc_table_header)
		return;
	sunrpc_table_header = register_sysctl_table(sunrpc_table, 1);
}

void
rpc_unregister_sysctl(void)
{
	if (!sunrpc_table_header)
		return;
	unregister_sysctl_table(sunrpc_table_header);
}

int
proc_dodebug(ctl_table *table, int write, struct file *file,
				void *buffer, size_t *lenp)
{
	char		tmpbuf[20], *p, c;
	unsigned int	value;
	int		left, len;

	if ((file->f_pos && !write) || !*lenp) {
		*lenp = 0;
		return 0;
	}

	left = *lenp;

	if (write) {
		if (!access_ok(VERIFY_READ, buffer, left))
			return -EFAULT;
		p = (char *) buffer;
#if LINUX_VERSION_CODE >= 0x020100
		while (left && __get_user(c, p) >= 0 && isspace(c))
			left--, p++;
#else
		while (left && (c = get_fs_byte(p)) >= 0 && isspace(c))
			left--, p++;
#endif
		if (!left)
			goto done;

		if (left > sizeof(tmpbuf) - 1)
			return -EINVAL;
		copy_from_user(tmpbuf, p, left);
		tmpbuf[left] = '\0';

		for (p = tmpbuf, value = 0; '0' <= *p && *p <= '9'; p++, left--)
			value = 10 * value + (*p - '0');
		if (*p && !isspace(*p))
			return -EINVAL;
		while (left && isspace(*p))
			left--, p++;
		*(unsigned int *) table->data = value;
	} else {
		if (!access_ok(VERIFY_WRITE, buffer, left))
			return -EFAULT;
		len = sprintf(tmpbuf, "%d", *(unsigned int *) table->data);
		if (len > left)
			len = left;
		copy_to_user(buffer, tmpbuf, len);
		if ((left -= len) > 0) {
			put_user('\n', (char *)buffer + len);
			left--;
		}
	}

done:
	*lenp -= left;
	file->f_pos += *lenp;
	return 0;
}

#define DIRENTRY(nam1, nam2, child)	\
	{CTL_##nam1, #nam2, NULL, 0, 0555, child }
#define DBGENTRY(nam1, nam2)	\
	{CTL_##nam1##DEBUG, #nam2 "_debug", &nam2##_debug, sizeof(int),\
	 0644, NULL, &proc_dodebug}

static ctl_table		debug_table[] = {
	DBGENTRY(RPC,  rpc),
	DBGENTRY(NFS,  nfs),
	DBGENTRY(NFSD, nfsd),
	DBGENTRY(NLM,  nlm),
	{0}
};

static ctl_table		sunrpc_table[] = {
	DIRENTRY(SUNRPC, sunrpc, debug_table),
	{0}
};

#endif
