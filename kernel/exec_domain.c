#include <linux/personality.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/mm.h>

static asmlinkage void no_lcall7(struct pt_regs * regs);


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


static asmlinkage void no_lcall7(struct pt_regs * regs)
{
	send_sig(SIGSEGV, current, 1);
}

struct exec_domain *lookup_exec_domain(unsigned long personality)
{
	unsigned long pers = personality & PER_MASK;
	struct exec_domain *it;

	for (it=exec_domains; it; it=it->next)
		if (pers >= it->pers_low
		&& pers <= it->pers_high)
			return it;

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
	for (tmp=exec_domains; tmp; tmp=tmp->next)
		if (tmp == it)
			return -EBUSY;
	it->next = exec_domains;
	exec_domains = it;
	return 0;
}

int unregister_exec_domain(struct exec_domain *it)
{
	struct exec_domain ** tmp;

	tmp = &exec_domains;
	while (*tmp) {
		if (it == *tmp) {
			*tmp = it->next;
			it->next = NULL;
			return 0;
		}
		tmp = &(*tmp)->next;
	}
	return -EINVAL;
}

asmlinkage int sys_personality(unsigned long personality)
{
	struct exec_domain *it;
	unsigned long old_personality;

	if (personality == 0xffffffff)
		return current->personality;

	it = lookup_exec_domain(personality);
	if (!it)
		return -EINVAL;

	old_personality = current->personality;
	if (current->exec_domain && current->exec_domain->use_count)
		(*current->exec_domain->use_count)--;
	current->personality = personality;
	current->exec_domain = it;
	if (current->exec_domain->use_count)
		(*current->exec_domain->use_count)++;

	return old_personality;
}
