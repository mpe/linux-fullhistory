/*
 *  binfmt_misc.c
 *
 *  Copyright (C) 1997 Richard Günther
 *
 *  binfmt_misc detects binaries via a magic or filename extension and invokes
 *  a specified wrapper. This should obsolete binfmt_java, binfmt_em86 and
 *  binfmt_mz.
 *
 *  1997-04-25 first version
 *  [...]
 *  1997-05-19 cleanup
 *  1997-06-26 hpa: pass the real filename rather than argv[0]
 *  1997-06-30 minor cleanup
 */

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/malloc.h>
#include <linux/binfmts.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <asm/uaccess.h>
#include <asm/spinlock.h>


#define VERBOSE_STATUS /* undef this to save 400 bytes kernel memory */

#ifndef MIN
#define MIN(x,y) (((x)<(y))?(x):(y))
#endif

struct binfmt_entry {
	struct binfmt_entry *next;
	int id;
	int flags;			/* type, status, etc. */
	int offset;			/* offset of magic */
	int size;			/* size of magic/mask */
	char *magic;			/* magic or filename extension */
	char *mask;			/* mask, NULL for exact match */
	char *interpreter;		/* filename of interpreter */
	char *proc_name;
	struct proc_dir_entry *proc_dir;
};

#define ENTRY_ENABLED 1		/* the old binfmt_entry.enabled */
#define	ENTRY_MAGIC 8		/* not filename detection */
#define ENTRY_STRIP_EXT 32	/* strip off last filename extension */

static int load_misc_binary(struct linux_binprm *bprm, struct pt_regs *regs);
static void entry_proc_cleanup(struct binfmt_entry *e);
static int entry_proc_setup(struct binfmt_entry *e);

static struct linux_binfmt misc_format = {
#ifndef MODULE
	NULL, 0, load_misc_binary, NULL, NULL
#else
	NULL, &__this_module, load_misc_binary, NULL, NULL
#endif
};

static struct proc_dir_entry *bm_dir = NULL;

static struct binfmt_entry *entries = NULL;
static int free_id = 1;
static int enabled = 1;

static rwlock_t entries_lock = RW_LOCK_UNLOCKED;


/*
 * Unregister one entry
 */
static void clear_entry(int id)
{
	struct binfmt_entry **ep, *e;

	write_lock(&entries_lock);
	ep = &entries;
	while (*ep && ((*ep)->id != id))
		ep = &((*ep)->next);
	if ((e = *ep)) {
		*ep = e->next;
		entry_proc_cleanup(e);
		kfree(e);
	}
	write_unlock(&entries_lock);
}

/*
 * Clear all registered binary formats
 */
static void clear_entries(void)
{
	struct binfmt_entry *e;

	write_lock(&entries_lock);
	while ((e = entries)) {
		entries = entries->next;
		entry_proc_cleanup(e);
		kfree(e);
	}
	write_unlock(&entries_lock);
}

/*
 * Find entry through id - caller has to do locking
 */
static struct binfmt_entry *get_entry(int id)
{
	struct binfmt_entry *e = entries;

	while (e && (e->id != id))
		e = e->next;
	return e;
}


/* 
 * Check if we support the binfmt
 * if we do, return the binfmt_entry, else NULL
 * locking is done in load_misc_binary
 */
static struct binfmt_entry *check_file(struct linux_binprm *bprm)
{
	struct binfmt_entry *e = entries;
	char *p = strrchr(bprm->filename, '.');
	int j;

	while (e) {
		if (e->flags & ENTRY_ENABLED) {
			if (!(e->flags & ENTRY_MAGIC)) {
				if (p && !strcmp(e->magic, p + 1))
					return e;
			} else {
				j = 0;
				while ((j < e->size) &&
				  !((bprm->buf[e->offset + j] ^ e->magic[j])
				   & (e->mask ? e->mask[j] : 0xff)))
					j++;
				if (j == e->size)
					return e;
			}
		}
		e = e->next;
	};
	return NULL;
}

/*
 * the loader itself
 */
static int load_misc_binary(struct linux_binprm *bprm, struct pt_regs *regs)
{
	struct binfmt_entry *fmt;
	struct dentry * dentry;
	char iname[128];
	char *iname_addr = iname, *p;
	int retval, fmt_flags = 0;

	MOD_INC_USE_COUNT;
	if (!enabled) {
		retval = -ENOEXEC;
		goto _ret;
	}

	/* to keep locking time low, we copy the interpreter string */
	read_lock(&entries_lock);
	if ((fmt = check_file(bprm))) {
		strncpy(iname, fmt->interpreter, 127);
		iname[127] = '\0';
		fmt_flags = fmt->flags;
	}
	read_unlock(&entries_lock);
	if (!fmt) {
		retval = -ENOEXEC;
		goto _ret;
	}

	dput(bprm->dentry);
	bprm->dentry = NULL;

	/* Build args for interpreter */
	if ((fmt_flags & ENTRY_STRIP_EXT) &&
	    (p = strrchr(bprm->filename, '.')))
		*p = '\0';
	remove_arg_zero(bprm);
	bprm->p = copy_strings(1, &bprm->filename, bprm->page, bprm->p, 2);
	bprm->argc++;
	bprm->p = copy_strings(1, &iname_addr, bprm->page, bprm->p, 2);
	bprm->argc++;
	if (!bprm->p) {
		retval = -E2BIG;
		goto _ret;
	}
	bprm->filename = iname;	/* for binfmt_script */

	dentry = open_namei(iname, 0, 0);
	retval = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto _ret;
	bprm->dentry = dentry;

	retval = prepare_binprm(bprm);
	if (retval >= 0)
		retval = search_binary_handler(bprm, regs);
_ret:
	MOD_DEC_USE_COUNT;
	return retval;
}



/*
 * /proc handling routines
 */

/*
 * parses and copies one argument enclosed in del from *sp to *dp,
 * recognising the \x special.
 * returns pointer to the copied argument or NULL in case of an
 * error (and sets err) or null argument length.
 */
static char *copyarg(char **dp, const char **sp, int *count,
		     char del, int special, int *err)
{
	char c, *res = *dp;

	while (!*err && ((c = *((*sp)++)), (*count)--) && (c != del)) {
		switch (c) {
		case '\\':
			if (special && (**sp == 'x')) {
				if (!isxdigit(c = toupper(*(++*sp))))
					*err = -EINVAL;
				**dp = (c - (isdigit(c) ? '0' : 'A' - 10)) * 16;
				if (!isxdigit(c = toupper(*(++*sp))))
					*err = -EINVAL;
				*((*dp)++) += c - (isdigit(c) ? '0' : 'A' - 10);
				++*sp;
				*count -= 3;
				break;
			}
		default:
			*((*dp)++) = c;
		}
	}
	if (*err || (c != del) || (res == *dp))
		res = NULL;
	else if (!special)
		*((*dp)++) = '\0';
	return res;
}

/*
 * This registers a new binary format, it recognises the syntax
 * ':name:type:offset:magic:mask:interpreter:'
 * where the ':' is the IFS, that can be chosen with the first char
 */
static int proc_write_register(struct file *file, const char *buffer,
			       unsigned long count, void *data)
{
	const char *sp;
	char del, *dp;
	struct binfmt_entry *e;
	int memsize, cnt = count - 1, err = 0;

	MOD_INC_USE_COUNT;
	/* some sanity checks */
	if ((count < 11) || (count > 256)) {
		err = -EINVAL;
		goto _err;
	}

	memsize = sizeof(struct binfmt_entry) + count;
	if (!(e = (struct binfmt_entry *) kmalloc(memsize, GFP_USER))) {
		err = -ENOMEM;
		goto _err;
	}

	sp = buffer + 1;
	del = buffer[0];
	dp = (char *)e + sizeof(struct binfmt_entry);

	e->proc_name = copyarg(&dp, &sp, &cnt, del, 0, &err);

	/* we can use bit 3 and 5 of type for ext/magic and ext-strip
	   flag due to the nice encoding of E, M, e and m */
	if ((*sp & 0x92) || (sp[1] != del))
		err = -EINVAL;
	else
		e->flags = (*sp++ & (ENTRY_MAGIC | ENTRY_STRIP_EXT))
			    | ENTRY_ENABLED;
	cnt -= 2; sp++;

	e->offset = 0;
	while (cnt-- && isdigit(*sp))
		e->offset = e->offset * 10 + *sp++ - '0';
	if (*sp++ != del)
		err = -EINVAL;

	e->magic = copyarg(&dp, &sp, &cnt, del, (e->flags & ENTRY_MAGIC), &err);
	e->size = dp - e->magic;
	e->mask = copyarg(&dp, &sp, &cnt, del, 1, &err);
	if (e->mask && ((dp - e->mask) != e->size))
		err = -EINVAL;
	e->interpreter = copyarg(&dp, &sp, &cnt, del, 0, &err);
	e->id = free_id++;

	/* more sanity checks */
	if (err || !(!cnt || (!(--cnt) && (*sp == '\n'))) ||
	    (e->size < 1) || ((e->size + e->offset) > 127) ||
	    !(e->proc_name) || !(e->interpreter) ||
	    entry_proc_setup(e)) {
		kfree(e);
		err = -EINVAL;
		goto _err;
	}

	write_lock(&entries_lock);
	e->next = entries;
	entries = e;
	write_unlock(&entries_lock);

	err = count;
_err:
	MOD_DEC_USE_COUNT;
	return err;
}

/*
 * Get status of entry/binfmt_misc
 * FIXME? should an entry be marked disabled if binfmt_misc is disabled though
 *        entry is enabled?
 */
static int proc_read_status(char *page, char **start, off_t off,
			    int count, int *eof, void *data)
{
	struct binfmt_entry *e;
	char *dp;
	int elen, i;

	MOD_INC_USE_COUNT;
#ifndef VERBOSE_STATUS
	if (data) {
		read_lock(&entries_lock);
		if (!(e = get_entry((int) data)))
			i = 0;
		else
			i = e->flags & ENTRY_ENABLED;
		read_unlock(&entries_lock);
	} else {
		i = enabled;
	} 
	sprintf(page, "%s\n", (i ? "enabled" : "disabled"));
#else
	if (!data)
		sprintf(page, "%s\n", (enabled ? "enabled" : "disabled"));
	else {
		read_lock(&entries_lock);
		if (!(e = get_entry((int) data))) {
			*page = '\0';
			goto _out;
		}
		sprintf(page, "%s\ninterpreter %s\n",
		        (e->flags & ENTRY_ENABLED ? "enabled" : "disabled"),
			e->interpreter);
		dp = page + strlen(page);
		if (!(e->flags & ENTRY_MAGIC)) {
			sprintf(dp, "extension .%s\n", e->magic);
			dp = page + strlen(page);
		} else {
			sprintf(dp, "offset %i\nmagic ", e->offset);
			dp = page + strlen(page);
			for (i = 0; i < e->size; i++) {
				sprintf(dp, "%02x", 0xff & (int) (e->magic[i]));
				dp += 2;
			}
			if (e->mask) {
				sprintf(dp, "\nmask ");
				dp += 6;
				for (i = 0; i < e->size; i++) {
					sprintf(dp, "%02x", 0xff & (int) (e->mask[i]));
					dp += 2;
				}
			}
			*dp++ = '\n';
			*dp = '\0';
		}
		if (e->flags & ENTRY_STRIP_EXT)
			sprintf(dp, "extension stripped\n");
_out:
		read_unlock(&entries_lock);
	}
#endif

	elen = strlen(page) - off;
	if (elen < 0)
		elen = 0;
	*eof = (elen <= count) ? 1 : 0;
	*start = page + off;

	MOD_DEC_USE_COUNT;
	return elen;
}

/*
 * Set status of entry/binfmt_misc:
 * '1' enables, '0' disables and '-1' clears entry/binfmt_misc
 */
static int proc_write_status(struct file *file, const char *buffer,
			     unsigned long count, void *data)
{
	struct binfmt_entry *e;
	int res = count;

	MOD_INC_USE_COUNT;
	if (((buffer[0] == '1') || (buffer[0] == '0')) &&
	    ((count == 1) || ((count == 2) && (buffer[1] == '\n')))) {
		if (data) {
			read_lock(&entries_lock);
			if ((e = get_entry((int) data)))
				e->flags = (e->flags & -2) | (int) (buffer[0] - '0');
			read_unlock(&entries_lock);
		} else {
			enabled = buffer[0] - '0';
		}
	} else if ((buffer[0] == '-') && (buffer[1] == '1') &&
	       ((count == 2) || ((count == 3) && (buffer[2] == '\n')))) {
		if (data)
			clear_entry((int) data);
		else
			clear_entries();
	} else {
		res = -EINVAL;
	}
	MOD_DEC_USE_COUNT;
	return res;
}

/*
 * Remove the /proc-dir entries of one binfmt
 */
static void entry_proc_cleanup(struct binfmt_entry *e)
{
	remove_proc_entry(e->proc_name, bm_dir);
}

/*
 * Create the /proc-dir entry for binfmt
 */
static int entry_proc_setup(struct binfmt_entry *e)
{
	if (!(e->proc_dir = create_proc_entry(e->proc_name,
			 	S_IFREG | S_IRUGO | S_IWUSR, bm_dir)))
		return -ENOMEM;

	e->proc_dir->data = (void *) (e->id);
	e->proc_dir->read_proc = proc_read_status;
	e->proc_dir->write_proc = proc_write_status;

	return 0;
}


__initfunc(int init_misc_binfmt(void))
{
	struct proc_dir_entry *status = NULL, *reg;

	if (!(bm_dir = create_proc_entry("sys/fs/binfmt_misc", S_IFDIR,
					 NULL)) ||
	    !(status = create_proc_entry("status", S_IFREG | S_IRUGO | S_IWUSR,
					 bm_dir)) ||
	    !(reg = create_proc_entry("register", S_IFREG | S_IWUSR,
				      bm_dir))) {
		if (status)
			remove_proc_entry("status", bm_dir);
		if (bm_dir)
			remove_proc_entry("sys/fs/binfmt_misc", NULL);
		return -ENOMEM;
	}
	status->read_proc = proc_read_status;
	status->write_proc = proc_write_status;

	reg->write_proc = proc_write_register;

	return register_binfmt(&misc_format);
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;
int init_module(void)
{
	return init_misc_binfmt();
}

void cleanup_module(void)
{
	unregister_binfmt(&misc_format);
	remove_proc_entry("register", bm_dir);
	remove_proc_entry("status", bm_dir);
	clear_entries();
	remove_proc_entry("sys/fs/binfmt_misc", NULL);
}
#endif
#undef VERBOSE_STATUS
