/* key.h: authentication token and access key management
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 *
 * See Documentation/keys.txt for information on keys/keyrings.
 */

#ifndef _LINUX_KEY_H
#define _LINUX_KEY_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>

#ifdef __KERNEL__

/* key handle serial number */
typedef int32_t key_serial_t;

/* key handle permissions mask */
typedef uint32_t key_perm_t;

struct key;

#ifdef CONFIG_KEYS

#undef KEY_DEBUGGING

#define KEY_USR_VIEW	0x00010000	/* user can view a key's attributes */
#define KEY_USR_READ	0x00020000	/* user can read key payload / view keyring */
#define KEY_USR_WRITE	0x00040000	/* user can update key payload / add link to keyring */
#define KEY_USR_SEARCH	0x00080000	/* user can find a key in search / search a keyring */
#define KEY_USR_LINK	0x00100000	/* user can create a link to a key/keyring */
#define KEY_USR_ALL	0x001f0000

#define KEY_GRP_VIEW	0x00000100	/* group permissions... */
#define KEY_GRP_READ	0x00000200
#define KEY_GRP_WRITE	0x00000400
#define KEY_GRP_SEARCH	0x00000800
#define KEY_GRP_LINK	0x00001000
#define KEY_GRP_ALL	0x00001f00

#define KEY_OTH_VIEW	0x00000001	/* third party permissions... */
#define KEY_OTH_READ	0x00000002
#define KEY_OTH_WRITE	0x00000004
#define KEY_OTH_SEARCH	0x00000008
#define KEY_OTH_LINK	0x00000010
#define KEY_OTH_ALL	0x0000001f

struct seq_file;
struct user_struct;

struct key_type;
struct key_owner;
struct keyring_list;
struct keyring_name;

/*****************************************************************************/
/*
 * authentication token / access credential / keyring
 * - types of key include:
 *   - keyrings
 *   - disk encryption IDs
 *   - Kerberos TGTs and tickets
 */
struct key {
	atomic_t		usage;		/* number of references */
	key_serial_t		serial;		/* key serial number */
	struct rb_node		serial_node;
	struct key_type		*type;		/* type of key */
	rwlock_t		lock;		/* examination vs change lock */
	struct rw_semaphore	sem;		/* change vs change sem */
	struct key_user		*user;		/* owner of this key */
	time_t			expiry;		/* time at which key expires (or 0) */
	uid_t			uid;
	gid_t			gid;
	key_perm_t		perm;		/* access permissions */
	unsigned short		quotalen;	/* length added to quota */
	unsigned short		datalen;	/* payload data length */
	unsigned short		flags;		/* status flags (change with lock writelocked) */
#define KEY_FLAG_INSTANTIATED	0x00000001	/* set if key has been instantiated */
#define KEY_FLAG_DEAD		0x00000002	/* set if key type has been deleted */
#define KEY_FLAG_REVOKED	0x00000004	/* set if key had been revoked */
#define KEY_FLAG_IN_QUOTA	0x00000008	/* set if key consumes quota */
#define KEY_FLAG_USER_CONSTRUCT	0x00000010	/* set if key is being constructed in userspace */
#define KEY_FLAG_NEGATIVE	0x00000020	/* set if key is negative */

#ifdef KEY_DEBUGGING
	unsigned		magic;
#define KEY_DEBUG_MAGIC		0x18273645u
#define KEY_DEBUG_MAGIC_X	0xf8e9dacbu
#endif

	/* the description string
	 * - this is used to match a key against search criteria
	 * - this should be a printable string
	 * - eg: for krb5 AFS, this might be "afs@REDHAT.COM"
	 */
	char			*description;

	/* type specific data
	 * - this is used by the keyring type to index the name
	 */
	union {
		struct list_head	link;
	} type_data;

	/* key data
	 * - this is used to hold the data actually used in cryptography or
	 *   whatever
	 */
	union {
		unsigned long		value;
		void			*data;
		struct keyring_list	*subscriptions;
	} payload;
};

/*****************************************************************************/
/*
 * kernel managed key type definition
 */
struct key_type {
	/* name of the type */
	const char *name;

	/* default payload length for quota precalculation (optional)
	 * - this can be used instead of calling key_payload_reserve(), that
	 *   function only needs to be called if the real datalen is different
	 */
	size_t def_datalen;

	/* instantiate a key of this type
	 * - this method should call key_payload_reserve() to determine if the
	 *   user's quota will hold the payload
	 */
	int (*instantiate)(struct key *key, const void *data, size_t datalen);

	/* duplicate a key of this type (optional)
	 * - the source key will be locked against change
	 * - the new description will be attached
	 * - the quota will have been adjusted automatically from
	 *   source->quotalen
	 */
	int (*duplicate)(struct key *key, const struct key *source);

	/* update a key of this type (optional)
	 * - this method should call key_payload_reserve() to recalculate the
	 *   quota consumption
	 * - the key must be locked against read when modifying
	 */
	int (*update)(struct key *key, const void *data, size_t datalen);

	/* match a key against a description */
	int (*match)(const struct key *key, const void *desc);

	/* clear the data from a key (optional) */
	void (*destroy)(struct key *key);

	/* describe a key */
	void (*describe)(const struct key *key, struct seq_file *p);

	/* read a key's data (optional)
	 * - permission checks will be done by the caller
	 * - the key's semaphore will be readlocked by the caller
	 * - should return the amount of data that could be read, no matter how
	 *   much is copied into the buffer
	 * - shouldn't do the copy if the buffer is NULL
	 */
	long (*read)(const struct key *key, char __user *buffer, size_t buflen);

	/* internal fields */
	struct list_head	link;		/* link in types list */
};

extern struct key_type key_type_keyring;

extern int register_key_type(struct key_type *ktype);
extern void unregister_key_type(struct key_type *ktype);

extern struct key *key_alloc(struct key_type *type,
			     const char *desc,
			     uid_t uid, gid_t gid, key_perm_t perm,
			     int not_in_quota);
extern int key_payload_reserve(struct key *key, size_t datalen);
extern int key_instantiate_and_link(struct key *key,
				    const void *data,
				    size_t datalen,
				    struct key *keyring);
extern int key_negate_and_link(struct key *key,
			       unsigned timeout,
			       struct key *keyring);
extern void key_revoke(struct key *key);
extern void key_put(struct key *key);

static inline struct key *key_get(struct key *key)
{
	if (key)
		atomic_inc(&key->usage);
	return key;
}

extern struct key *request_key(struct key_type *type,
			       const char *description,
			       const char *callout_info);

extern int key_validate(struct key *key);

extern struct key *key_create_or_update(struct key *keyring,
					const char *type,
					const char *description,
					const void *payload,
					size_t plen,
					int not_in_quota);

extern int key_update(struct key *key,
		      const void *payload,
		      size_t plen);

extern int key_link(struct key *keyring,
		    struct key *key);

extern int key_unlink(struct key *keyring,
		      struct key *key);

extern struct key *keyring_alloc(const char *description, uid_t uid, gid_t gid,
				 int not_in_quota, struct key *dest);

extern int keyring_clear(struct key *keyring);

extern struct key *keyring_search(struct key *keyring,
				  struct key_type *type,
				  const char *description);

extern struct key *search_process_keyrings(struct key_type *type,
					   const char *description);

extern int keyring_add_key(struct key *keyring,
			   struct key *key);

extern struct key *key_lookup(key_serial_t id);

#define key_serial(key) ((key) ? (key)->serial : 0)

/*
 * the userspace interface
 */
extern struct key root_user_keyring, root_session_keyring;
extern int alloc_uid_keyring(struct user_struct *user);
extern void switch_uid_keyring(struct user_struct *new_user);
extern int copy_keys(unsigned long clone_flags, struct task_struct *tsk);
extern void exit_keys(struct task_struct *tsk);
extern int suid_keys(struct task_struct *tsk);
extern int exec_keys(struct task_struct *tsk);
extern void key_fsuid_changed(struct task_struct *tsk);
extern void key_fsgid_changed(struct task_struct *tsk);
extern void key_init(void);

#else /* CONFIG_KEYS */

#define key_validate(k)			0
#define key_serial(k)			0
#define key_get(k) 			NULL
#define key_put(k)			do { } while(0)
#define alloc_uid_keyring(u)		0
#define switch_uid_keyring(u)		do { } while(0)
#define copy_keys(f,t)			0
#define exit_keys(t)			do { } while(0)
#define suid_keys(t)			do { } while(0)
#define exec_keys(t)			do { } while(0)
#define key_fsuid_changed(t)		do { } while(0)
#define key_fsgid_changed(t)		do { } while(0)
#define key_init()			do { } while(0)

#endif /* CONFIG_KEYS */
#endif /* __KERNEL__ */
#endif /* _LINUX_KEY_H */
