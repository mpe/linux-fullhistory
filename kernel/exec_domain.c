#include <linux/mm.h>
#include <linux/smp_lock.h>
#include <linux/module.h>

static asmlinkage void no_lcall7(int segment, struct pt_regs * regs);


static unsigned long ident_map[32] = {
	0,	1,	2,	3,	4,	5,	6,	7,
	8,	9,	10,	11,	12,	13,	14,	15,
	16,	17,	18,	19,	20,	21,	22,	23,
	24,	25,	26,	27,	28,	29,	30,	31
};

struct exec_domain default_exec_domain = {
	"Linux",	/* name */
	no_lcall7,	/* lcall7 causes a seg fault. */
	0, 0xff,	/* All personalities. */
	ident_map,	/* Identity map signals. */
	ident_map,	/*  - both ways. */
	NULL,		/* No usage counter. */
	NULL		/* Nothing after this in the list. */
};

static struct exec_domain *exec_domains = &default_exec_domain;
static spinlock_t exec_domains_lock = SPIN_LOCK_UNLOCKED;

static asmlinkage void no_lcall7(int segment, struct pt_regs * regs)
{
  /*
   * This may have been a static linked SVr4 binary, so we would have the
   * personality set incorrectly.  Check to see whether SVr4 is available,
   * and use it, otherwise give the user a SEGV.
   */
	put_exec_domain(current->exec_domain);
	current->personality = PER_SVR4;
	current->exec_domain = lookup_exec_domain(current->personality);

	if (current->exec_domain && current->exec_domain->handler
	&& current->exec_domain->handler != no_lcall7) {
		current->exec_domain->handler(segment, regs);
		return;
	}

	send_sig(SIGSEGV, current, 1);
}

struct exec_domain *lookup_exec_domain(unsigned long personality)
{
	unsigned long pers = personality & PER_MASK;
	struct exec_domain *it;

	spin_lock(&exec_domains_lock);
	for (it=exec_domains; it; it=it->next)
		if (pers >= it->pers_low && pers <= it->pers_high) {
			if (!try_inc_mod_count(it->module))
				continue;
			spin_unlock(&exec_domains_lock);
			return it;
		}
	spin_unlock(&exec_domains_lock);

	/* Should never get this far. */
	printk(KERN_ERR "No execution domain for personality 0x%02lx\n", pers);
	return NULL;
}

int register_exec_domain(struct exec_domain *it)
{
	struct exec_domain *tmp;

	if (!it)
		return -EINVAL;
	if (it->next)
		return -EBUSY;
	spin_lock(&exec_domains_lock);
	for (tmp=exec_domains; tmp; tmp=tmp->next)
		if (tmp == it) {
			spin_unlock(&exec_domains_lock);
			return -EBUSY;
		}
	it->next = exec_domains;
	exec_domains = it;
	spin_unlock(&exec_domains_lock);
	return 0;
}

int unregister_exec_domain(struct exec_domain *it)
{
	struct exec_domain ** tmp;

	tmp = &exec_domains;
	spin_lock(&exec_domains_lock);
	while (*tmp) {
		if (it == *tmp) {
			*tmp = it->next;
			it->next = NULL;
			spin_unlock(&exec_domains_lock);
			return 0;
		}
		tmp = &(*tmp)->next;
	}
	spin_unlock(&exec_domains_lock);
	return -EINVAL;
}

asmlinkage long sys_personality(unsigned long personality)
{
	struct exec_domain *it;
	unsigned long old_personality;
	int ret;

	if (personality == 0xffffffff)
		return current->personality;

	ret = -EINVAL;
	lock_kernel();
	it = lookup_exec_domain(personality);
	if (!it)
		goto out;

	old_personality = current->personality;
	put_exec_domain(current->exec_domain);
	current->personality = personality;
	current->exec_domain = it;
	ret = old_personality;
out:
	unlock_kernel();
	return ret;
}

int get_exec_domain_list(char * page)
{
	int len = 0;
	struct exec_domain * e;

	spin_lock(&exec_domains_lock);
	for (e=exec_domains; e && len < PAGE_SIZE - 80; e=e->next)
		len += sprintf(page+len, "%d-%d\t%-16s\t[%s]\n",
			e->pers_low, e->pers_high, e->name,
			e->module ? e->module->name : "kernel");
	spin_unlock(&exec_domains_lock);
	return len;
}
