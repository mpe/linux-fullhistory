#ifndef _ASM_PPC64_TOPOLOGY_H
#define _ASM_PPC64_TOPOLOGY_H

#include <linux/config.h>
#include <asm/mmzone.h>

#ifdef CONFIG_NUMA

static inline int cpu_to_node(int cpu)
{
	int node;

	node = numa_cpu_lookup_table[cpu];

#ifdef DEBUG_NUMA
	BUG_ON(node == -1);
#endif

	return node;
}

#define parent_node(node)	(node)

static inline cpumask_t node_to_cpumask(int node)
{
	return numa_cpumask_lookup_table[node];
}

static inline int node_to_first_cpu(int node)
{
	cpumask_t tmp;
	tmp = node_to_cpumask(node);
	return first_cpu(tmp);
}

#define pcibus_to_cpumask(bus)	(cpu_online_map)

#define nr_cpus_node(node)	(nr_cpus_in_node[node])

/* sched_domains SD_NODE_INIT for PPC64 machines */
#define SD_NODE_INIT (struct sched_domain) {		\
	.span			= CPU_MASK_NONE,	\
	.parent			= NULL,			\
	.groups			= NULL,			\
	.min_interval		= 8,			\
	.max_interval		= 32,			\
	.busy_factor		= 32,			\
	.imbalance_pct		= 125,			\
	.cache_hot_time		= (10*1000000),		\
	.cache_nice_tries	= 1,			\
	.per_cpu_gain		= 100,			\
	.flags			= SD_LOAD_BALANCE	\
				| SD_BALANCE_EXEC	\
				| SD_BALANCE_NEWIDLE	\
				| SD_WAKE_IDLE		\
				| SD_WAKE_BALANCE,	\
	.last_balance		= jiffies,		\
	.balance_interval	= 1,			\
	.nr_balance_failed	= 0,			\
}

#else /* !CONFIG_NUMA */

#include <asm-generic/topology.h>

#endif /* CONFIG_NUMA */

#endif /* _ASM_PPC64_TOPOLOGY_H */
