/*
 * linux/include/linux/lockd/nlm.h
 *
 * Declarations for the Network Lock Manager protocol.
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#ifndef LINUX_LOCKD_NLM_H
#define LINUX_LOCKD_NLM_H

/* Maximum file offset in file_lock.fl_end */
#ifdef OFFSET_MAX
# define NLM_OFFSET_MAX		OFFSET_MAX
#else
# define NLM_OFFSET_MAX		((off_t) 0x7fffffff)
#endif

/* Return states for NLM */
enum {
	NLM_LCK_GRANTED = 0,
	NLM_LCK_DENIED,
	NLM_LCK_DENIED_NOLOCKS,
	NLM_LCK_BLOCKED,
	NLM_LCK_DENIED_GRACE_PERIOD,
};

#define NLM_PROGRAM		100021

#define NLMPROC_NULL		0
#define NLMPROC_TEST		1
#define NLMPROC_LOCK		2
#define NLMPROC_CANCEL		3
#define NLMPROC_UNLOCK		4
#define NLMPROC_GRANTED		5
#define NLMPROC_TEST_MSG	6
#define NLMPROC_LOCK_MSG	7
#define NLMPROC_CANCEL_MSG	8
#define NLMPROC_UNLOCK_MSG	9
#define NLMPROC_GRANTED_MSG	10
#define NLMPROC_TEST_RES	11
#define NLMPROC_LOCK_RES	12
#define NLMPROC_CANCEL_RES	13
#define NLMPROC_UNLOCK_RES	14
#define NLMPROC_GRANTED_RES	15
#define NLMPROC_SHARE		20
#define NLMPROC_UNSHARE		21
#define NLMPROC_NM_LOCK		22
#define NLMPROC_FREE_ALL	23
#define NLMPROC_NSM_NOTIFY	24		/* statd callback */

#endif /* LINUX_LOCKD_NLM_H */
