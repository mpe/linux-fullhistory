#ifndef _LINUX__INIT_TASK_H
#define _LINUX__INIT_TASK_H

#include <linux/file.h>

#define INIT_FILES \
{ 							\
	count:		ATOMIC_INIT(1), 		\
	file_lock:	RW_LOCK_UNLOCKED, 		\
	max_fds:	NR_OPEN_DEFAULT, 		\
	max_fdset:	__FD_SETSIZE, 			\
	next_fd:	0, 				\
	fd:		&init_files.fd_array[0], 	\
	close_on_exec:	&init_files.close_on_exec_init, \
	open_fds:	&init_files.open_fds_init, 	\
	close_on_exec_init: { { 0, } }, 		\
	open_fds_init:	{ { 0, } }, 			\
	fd_array:	{ NULL, } 			\
}

#define INIT_MM(name) \
{			 				\
	mm_rb:		RB_ROOT,			\
	pgd:		swapper_pg_dir, 		\
	mm_users:	ATOMIC_INIT(2), 		\
	mm_count:	ATOMIC_INIT(1), 		\
	mmap_sem:	__RWSEM_INITIALIZER(name.mmap_sem), \
	page_table_lock: SPIN_LOCK_UNLOCKED, 		\
	mmlist:		LIST_HEAD_INIT(name.mmlist),	\
}

#define INIT_SIGNALS {	\
	count:		ATOMIC_INIT(1), 		\
	action:		{ {{0,}}, }, 			\
	siglock:	SPIN_LOCK_UNLOCKED 		\
}

/*
 *  INIT_TASK is used to set up the first task table, touch at
 * your own risk!. Base=0, limit=0x1fffff (=2MB)
 */
#define INIT_TASK(tsk)	\
{									\
    state:		0,						\
    flags:		0,						\
    sigpending:		0,						\
    addr_limit:		KERNEL_DS,					\
    exec_domain:	&default_exec_domain,				\
    lock_depth:		-1,						\
    __nice:		DEF_USER_NICE,					\
    policy:		SCHED_OTHER,					\
    cpus_allowed:	-1,						\
    mm:			NULL,						\
    active_mm:		&init_mm,					\
    run_list:		LIST_HEAD_INIT(tsk.run_list),			\
    time_slice:		NICE_TO_TIMESLICE(DEF_USER_NICE),		\
    next_task:		&tsk,						\
    prev_task:		&tsk,						\
    p_opptr:		&tsk,						\
    p_pptr:		&tsk,						\
    thread_group:	LIST_HEAD_INIT(tsk.thread_group),		\
    wait_chldexit:	__WAIT_QUEUE_HEAD_INITIALIZER(tsk.wait_chldexit),\
    real_timer:		{						\
	function:		it_real_fn				\
    },									\
    cap_effective:	CAP_INIT_EFF_SET,				\
    cap_inheritable:	CAP_INIT_INH_SET,				\
    cap_permitted:	CAP_FULL_SET,					\
    keep_capabilities:	0,						\
    rlim:		INIT_RLIMITS,					\
    user:		INIT_USER,					\
    comm:		"swapper",					\
    thread:		INIT_THREAD,					\
    fs:			&init_fs,					\
    files:		&init_files,					\
    sigmask_lock:	SPIN_LOCK_UNLOCKED,				\
    sig:		&init_signals,					\
    pending:		{ NULL, &tsk.pending.head, {{0}}},		\
    blocked:		{{0}},						\
    alloc_lock:		SPIN_LOCK_UNLOCKED,				\
    journal_info:	NULL,						\
}



#endif
