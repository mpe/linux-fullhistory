/* process_keys.c: management of a process's keyrings
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/keyctl.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <asm/uaccess.h>
#include "internal.h"

/* session keyring create vs join semaphore */
static DECLARE_MUTEX(key_session_sem);

/* the root user's tracking struct */
struct key_user root_key_user = {
	.usage		= ATOMIC_INIT(3),
	.consq		= LIST_HEAD_INIT(root_key_user.consq),
	.lock		= SPIN_LOCK_UNLOCKED,
	.nkeys		= ATOMIC_INIT(2),
	.nikeys		= ATOMIC_INIT(2),
	.uid		= 0,
};

/* the root user's UID keyring */
struct key root_user_keyring = {
	.usage		= ATOMIC_INIT(1),
	.serial		= 2,
	.type		= &key_type_keyring,
	.user		= &root_key_user,
	.lock		= RW_LOCK_UNLOCKED,
	.sem		= __RWSEM_INITIALIZER(root_user_keyring.sem),
	.perm		= KEY_USR_ALL,
	.flags		= KEY_FLAG_INSTANTIATED,
	.description	= "_uid.0",
#ifdef KEY_DEBUGGING
	.magic		= KEY_DEBUG_MAGIC,
#endif
};

/* the root user's default session keyring */
struct key root_session_keyring = {
	.usage		= ATOMIC_INIT(1),
	.serial		= 1,
	.type		= &key_type_keyring,
	.user		= &root_key_user,
	.lock		= RW_LOCK_UNLOCKED,
	.sem		= __RWSEM_INITIALIZER(root_session_keyring.sem),
	.perm		= KEY_USR_ALL,
	.flags		= KEY_FLAG_INSTANTIATED,
	.description	= "_uid_ses.0",
#ifdef KEY_DEBUGGING
	.magic		= KEY_DEBUG_MAGIC,
#endif
};

/*****************************************************************************/
/*
 * allocate the keyrings to be associated with a UID
 */
int alloc_uid_keyring(struct user_struct *user)
{
	struct key *uid_keyring, *session_keyring;
	char buf[20];
	int ret;

	/* concoct a default session keyring */
	sprintf(buf, "_uid_ses.%u", user->uid);

	session_keyring = keyring_alloc(buf, user->uid, (gid_t) -1, 0, NULL);
	if (IS_ERR(session_keyring)) {
		ret = PTR_ERR(session_keyring);
		goto error;
	}

	/* and a UID specific keyring, pointed to by the default session
	 * keyring */
	sprintf(buf, "_uid.%u", user->uid);

	uid_keyring = keyring_alloc(buf, user->uid, (gid_t) -1, 0,
				    session_keyring);
	if (IS_ERR(uid_keyring)) {
		key_put(session_keyring);
		ret = PTR_ERR(uid_keyring);
		goto error;
	}

	/* install the keyrings */
	user->uid_keyring = uid_keyring;
	user->session_keyring = session_keyring;
	ret = 0;

 error:
	return ret;

} /* end alloc_uid_keyring() */

/*****************************************************************************/
/*
 * deal with the UID changing
 */
void switch_uid_keyring(struct user_struct *new_user)
{
#if 0 /* do nothing for now */
	struct key *old;

	/* switch to the new user's session keyring if we were running under
	 * root's default session keyring */
	if (new_user->uid != 0 &&
	    current->session_keyring == &root_session_keyring
	    ) {
		atomic_inc(&new_user->session_keyring->usage);

		task_lock(current);
		old = current->session_keyring;
		current->session_keyring = new_user->session_keyring;
		task_unlock(current);

		key_put(old);
	}
#endif

} /* end switch_uid_keyring() */

/*****************************************************************************/
/*
 * install a fresh thread keyring, discarding the old one
 */
int install_thread_keyring(struct task_struct *tsk)
{
	struct key *keyring, *old;
	char buf[20];
	int ret;

	sprintf(buf, "_tid.%u", tsk->pid);

	keyring = keyring_alloc(buf, tsk->uid, tsk->gid, 1, NULL);
	if (IS_ERR(keyring)) {
		ret = PTR_ERR(keyring);
		goto error;
	}

	task_lock(tsk);
	old = tsk->thread_keyring;
	tsk->thread_keyring = keyring;
	task_unlock(tsk);

	ret = 0;

	key_put(old);
 error:
	return ret;

} /* end install_thread_keyring() */

/*****************************************************************************/
/*
 * install a fresh process keyring, discarding the old one
 */
static int install_process_keyring(struct task_struct *tsk)
{
	struct key *keyring, *old;
	char buf[20];
	int ret;

	sprintf(buf, "_pid.%u", tsk->tgid);

	keyring = keyring_alloc(buf, tsk->uid, tsk->gid, 1, NULL);
	if (IS_ERR(keyring)) {
		ret = PTR_ERR(keyring);
		goto error;
	}

	task_lock(tsk);
	old = tsk->process_keyring;
	tsk->process_keyring = keyring;
	task_unlock(tsk);

	ret = 0;

	key_put(old);
 error:
	return ret;

} /* end install_process_keyring() */

/*****************************************************************************/
/*
 * install a session keyring, discarding the old one
 * - if a keyring is not supplied, an empty one is invented
 */
static int install_session_keyring(struct task_struct *tsk,
				   struct key *keyring)
{
	struct key *old;
	char buf[20];
	int ret;

	/* create an empty session keyring */
	if (!keyring) {
		sprintf(buf, "_ses.%u", tsk->tgid);

		keyring = keyring_alloc(buf, tsk->uid, tsk->gid, 1, NULL);
		if (IS_ERR(keyring)) {
			ret = PTR_ERR(keyring);
			goto error;
		}
	}
	else {
		atomic_inc(&keyring->usage);
	}

	/* install the keyring */
	task_lock(tsk);
	old = tsk->session_keyring;
	tsk->session_keyring = keyring;
	task_unlock(tsk);

	ret = 0;

	key_put(old);
 error:
	return ret;

} /* end install_session_keyring() */

/*****************************************************************************/
/*
 * copy the keys for fork
 */
int copy_keys(unsigned long clone_flags, struct task_struct *tsk)
{
	int ret = 0;

	key_check(tsk->session_keyring);
	key_check(tsk->process_keyring);
	key_check(tsk->thread_keyring);

	if (tsk->session_keyring)
		atomic_inc(&tsk->session_keyring->usage);

	if (tsk->process_keyring) {
		if (clone_flags & CLONE_THREAD) {
			atomic_inc(&tsk->process_keyring->usage);
		}
		else {
			tsk->process_keyring = NULL;
			ret = install_process_keyring(tsk);
		}
	}

	tsk->thread_keyring = NULL;
	return ret;

} /* end copy_keys() */

/*****************************************************************************/
/*
 * dispose of keys upon exit
 */
void exit_keys(struct task_struct *tsk)
{
	key_put(tsk->session_keyring);
	key_put(tsk->process_keyring);
	key_put(tsk->thread_keyring);

} /* end exit_keys() */

/*****************************************************************************/
/*
 * deal with execve()
 */
int exec_keys(struct task_struct *tsk)
{
	struct key *old;

	/* newly exec'd tasks don't get a thread keyring */
	task_lock(tsk);
	old = tsk->thread_keyring;
	tsk->thread_keyring = NULL;
	task_unlock(tsk);

	key_put(old);

	/* newly exec'd tasks get a fresh process keyring */
	return install_process_keyring(tsk);

} /* end exec_keys() */

/*****************************************************************************/
/*
 * deal with SUID programs
 * - we might want to make this invent a new session keyring
 */
int suid_keys(struct task_struct *tsk)
{
	return 0;

} /* end suid_keys() */

/*****************************************************************************/
/*
 * the filesystem user ID changed
 */
void key_fsuid_changed(struct task_struct *tsk)
{
	/* update the ownership of the process keyring */
	if (tsk->process_keyring) {
		down_write(&tsk->process_keyring->sem);
		write_lock(&tsk->process_keyring->lock);
		tsk->process_keyring->uid = tsk->fsuid;
		write_unlock(&tsk->process_keyring->lock);
		up_write(&tsk->process_keyring->sem);
	}

	/* update the ownership of the thread keyring */
	if (tsk->thread_keyring) {
		down_write(&tsk->thread_keyring->sem);
		write_lock(&tsk->thread_keyring->lock);
		tsk->thread_keyring->uid = tsk->fsuid;
		write_unlock(&tsk->thread_keyring->lock);
		up_write(&tsk->thread_keyring->sem);
	}

} /* end key_fsuid_changed() */

/*****************************************************************************/
/*
 * the filesystem group ID changed
 */
void key_fsgid_changed(struct task_struct *tsk)
{
	/* update the ownership of the process keyring */
	if (tsk->process_keyring) {
		down_write(&tsk->process_keyring->sem);
		write_lock(&tsk->process_keyring->lock);
		tsk->process_keyring->gid = tsk->fsgid;
		write_unlock(&tsk->process_keyring->lock);
		up_write(&tsk->process_keyring->sem);
	}

	/* update the ownership of the thread keyring */
	if (tsk->thread_keyring) {
		down_write(&tsk->thread_keyring->sem);
		write_lock(&tsk->thread_keyring->lock);
		tsk->thread_keyring->gid = tsk->fsgid;
		write_unlock(&tsk->thread_keyring->lock);
		up_write(&tsk->thread_keyring->sem);
	}

} /* end key_fsgid_changed() */

/*****************************************************************************/
/*
 * search the process keyrings for the first matching key
 * - we use the supplied match function to see if the description (or other
 *   feature of interest) matches
 * - we return -EAGAIN if we didn't find any matching key
 * - we return -ENOKEY if we found only negative matching keys
 */
struct key *search_process_keyrings_aux(struct key_type *type,
					const void *description,
					key_match_func_t match)
{
	struct task_struct *tsk = current;
	struct key *key, *ret, *err, *session;

	/* we want to return -EAGAIN or -ENOKEY if any of the keyrings were
	 * searchable, but we failed to find a key or we found a negative key;
	 * otherwise we want to return a sample error (probably -EACCES) if
	 * none of the keyrings were searchable
	 *
	 * in terms of priority: success > -ENOKEY > -EAGAIN > other error
	 */
	key = NULL;
	ret = NULL;
	err = ERR_PTR(-EAGAIN);

	/* search the thread keyring first */
	if (tsk->thread_keyring) {
		key = keyring_search_aux(tsk->thread_keyring, type,
					 description, match);
		if (!IS_ERR(key))
			goto found;

		switch (PTR_ERR(key)) {
		case -EAGAIN: /* no key */
			if (ret)
				break;
		case -ENOKEY: /* negative key */
			ret = key;
			break;
		default:
			err = key;
			break;
		}
	}

	/* search the process keyring second */
	if (tsk->process_keyring) {
		key = keyring_search_aux(tsk->process_keyring, type,
					 description, match);
		if (!IS_ERR(key))
			goto found;

		switch (PTR_ERR(key)) {
		case -EAGAIN: /* no key */
			if (ret)
				break;
		case -ENOKEY: /* negative key */
			ret = key;
			break;
		default:
			err = key;
			break;
		}
	}

	/* search the session keyring last */
	session = tsk->session_keyring;
	if (!session)
		session = tsk->user->session_keyring;

	key = keyring_search_aux(session, type,
				 description, match);
	if (!IS_ERR(key))
		goto found;

	switch (PTR_ERR(key)) {
	case -EAGAIN: /* no key */
		if (ret)
			break;
	case -ENOKEY: /* negative key */
		ret = key;
		break;
	default:
		err = key;
		break;
	}

	/* no key - decide on the error we're going to go for */
	key = ret ? ret : err;

 found:
	return key;

} /* end search_process_keyrings_aux() */

/*****************************************************************************/
/*
 * search the process keyrings for the first matching key
 * - we return -EAGAIN if we didn't find any matching key
 * - we return -ENOKEY if we found only negative matching keys
 */
struct key *search_process_keyrings(struct key_type *type,
				    const char *description)
{
	return search_process_keyrings_aux(type, description, type->match);

} /* end search_process_keyrings() */

/*****************************************************************************/
/*
 * lookup a key given a key ID from userspace with a given permissions mask
 * - don't create special keyrings unless so requested
 * - partially constructed keys aren't found unless requested
 */
struct key *lookup_user_key(key_serial_t id, int create, int partial,
			    key_perm_t perm)
{
	struct task_struct *tsk = current;
	struct key *key;
	int ret;

	key = ERR_PTR(-ENOKEY);

	switch (id) {
	case KEY_SPEC_THREAD_KEYRING:
		if (!tsk->thread_keyring) {
			if (!create)
				goto error;

			ret = install_thread_keyring(tsk);
			if (ret < 0) {
				key = ERR_PTR(ret);
				goto error;
			}
		}

		key = tsk->thread_keyring;
		atomic_inc(&key->usage);
		break;

	case KEY_SPEC_PROCESS_KEYRING:
		if (!tsk->process_keyring) {
			if (!create)
				goto error;

			ret = install_process_keyring(tsk);
			if (ret < 0) {
				key = ERR_PTR(ret);
				goto error;
			}
		}

		key = tsk->process_keyring;
		atomic_inc(&key->usage);
		break;

	case KEY_SPEC_SESSION_KEYRING:
		if (!tsk->session_keyring) {
			/* always install a session keyring upon access if one
			 * doesn't exist yet */
			ret = install_session_keyring(
			       tsk, tsk->user->session_keyring);
			if (ret < 0)
				goto error;
		}

		key = tsk->session_keyring;
		atomic_inc(&key->usage);
		break;

	case KEY_SPEC_USER_KEYRING:
		key = tsk->user->uid_keyring;
		atomic_inc(&key->usage);
		break;

	case KEY_SPEC_USER_SESSION_KEYRING:
		key = tsk->user->session_keyring;
		atomic_inc(&key->usage);
		break;

	case KEY_SPEC_GROUP_KEYRING:
		/* group keyrings are not yet supported */
		key = ERR_PTR(-EINVAL);
		goto error;

	default:
		key = ERR_PTR(-EINVAL);
		if (id < 1)
			goto error;

		key = key_lookup(id);
		if (IS_ERR(key))
			goto error;
		break;
	}

	/* check the status and permissions */
	if (perm) {
		ret = key_validate(key);
		if (ret < 0)
			goto invalid_key;
	}

	ret = -EIO;
	if (!partial && !(key->flags & KEY_FLAG_INSTANTIATED))
		goto invalid_key;

	ret = -EACCES;
	if (!key_permission(key, perm))
		goto invalid_key;

 error:
	return key;

 invalid_key:
	key_put(key);
	key = ERR_PTR(ret);
	goto error;

} /* end lookup_user_key() */

/*****************************************************************************/
/*
 * join the named keyring as the session keyring if possible, or attempt to
 * create a new one of that name if not
 * - if the name is NULL, an empty anonymous keyring is installed instead
 * - named session keyring joining is done with a semaphore held
 */
long join_session_keyring(const char *name)
{
	struct task_struct *tsk = current;
	struct key *keyring;
	long ret;

	/* if no name is provided, install an anonymous keyring */
	if (!name) {
		ret = install_session_keyring(tsk, NULL);
		if (ret < 0)
			goto error;

		ret = tsk->session_keyring->serial;
		goto error;
	}

	/* allow the user to join or create a named keyring */
	down(&key_session_sem);

	/* look for an existing keyring of this name */
	keyring = find_keyring_by_name(name, 0);
	if (PTR_ERR(keyring) == -ENOKEY) {
		/* not found - try and create a new one */
		keyring = keyring_alloc(name, tsk->uid, tsk->gid, 0, NULL);
		if (IS_ERR(keyring)) {
			ret = PTR_ERR(keyring);
			goto error;
		}
	}
	else if (IS_ERR(keyring)) {
		ret = PTR_ERR(keyring);
		goto error2;
	}

	/* we've got a keyring - now to install it */
	ret = install_session_keyring(tsk, keyring);
	if (ret < 0)
		goto error2;

	key_put(keyring);

	ret = tsk->session_keyring->serial;

 error2:
	up(&key_session_sem);
 error:
	return ret;

} /* end join_session_keyring() */
