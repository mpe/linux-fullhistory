/* noquot.c: Quota stubs necessary for when quotas are not
 *           compiled into the kernel.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>

int nr_dquots = 0, nr_free_dquots = 0;
int max_dquots = 0;

asmlinkage int sys_quotactl(int cmd, const char *special, int id, caddr_t addr)
{
	return(-ENOSYS);
}
