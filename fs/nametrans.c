/*
 * $Id: nametrans.c,v 1.2 1997/06/04 23:45:44 davem Exp $
 *
 * linux/fs/nametrans.c - context-dependend filename suffixes.
 * Copyright (C) 1997, Thomas Schoebel-Theuer,
 * <schoebel@informatik.uni-stuttgart.de>.
 *
 * translates names of the form "filename#host=myhost#" to "filename"
 * as if both names were hardlinked to the same file.
 * benefit: diskless clients can mount the / filesystem of the
 * server if /etc/fstab (and other config files) are organized using
 * context suffixes.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/utsname.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/uaccess.h>
#include <linux/nametrans.h>

char  nametrans_txt[MAX_DEFAULT_TRANSLEN] = "";
static struct translations * global_trans = NULL;
static int default_trans = 1;
static const char version[] = "revision: 2.3 <schoebel@informatik.uni-stuttgart.de>";
int translations_dirty = 1;
static char * transl_names[] = {
#ifdef CONFIG_TR_NODENAME
  "host=",        system_utsname.nodename,
#endif
#ifdef CONFIG_TR_KERNNAME
  "kname=",  CONFIG_KERNNAME,
#endif
#ifdef CONFIG_TR_KERNTYPE
  "ktype=",  CONFIG_KERNTYPE,
#endif
#ifdef CONFIG_TR_MACHINE
  "machine=",     system_utsname.machine,
#endif
#ifdef CONFIG_TR_SYSNAME
  "system=",      system_utsname.sysname,
#endif
  0, 0
};

/* Convert and do syntax checking. */
static void convert(char * txt, struct translations * res)
{
	char * tmp = txt;
	char * space = (char*)res + sizeof(struct translations);

	res->count = 0;
	while(*tmp) {
		struct qstr * name = &res->name[res->count];
		struct qstr * c_name = &res->c_name[res->count];
		int len;
		char * p = tmp;

		if(*p++ != '#')
			goto next;
		while(*p && *p != '=' && *p != ':')
			p++;
		if(*p != '=')
			goto next;
		p++;
		len = (unsigned long)p - (unsigned long)tmp;
		c_name->name = space;
		memcpy(space, tmp, len);
		memcpy(space + len, "CREATE#", 8);
		c_name->len = len + 7;
		if(c_name->len >= MAX_TRANS_SUFFIX)
			goto next;
		while(*p && *p != '#' && *p != ':')
			p++;
		if(*p != '#')
			goto next;
		p++;
		if(*p != ':' && *p)
			goto next;
		space += len + 8;
		name->len = len = (unsigned long)p - (unsigned long)tmp;
		if(len >= MAX_TRANS_SUFFIX)
			goto next;
		name->name = space;
		memcpy(space, tmp, len);
		space[len] = '\0';
		space += len + 1;
		res->count++;
		if(res->count >= MAX_TRANSLATIONS ||
		   (unsigned long)space - (unsigned long)res >= PAGE_SIZE-2*MAX_TRANS_SUFFIX)
			return;
	next:
		while(*p && *p++ != ':') ;
		tmp = p;
	}
}

static inline void trans_to_string(struct translations * trans, char * buf, int maxlen)
{
	int i;

	for(i = 0; i < trans->count; i++) {
		int len = trans->name[i].len;
		if(len < maxlen) {
			memcpy(buf, trans->name[i].name, len);
			buf += len;
			maxlen -= len;
			*buf++ = ':';
			maxlen--;
		}
	}
	buf--;
	*buf = '\0';
}

static inline void default_nametrans(char * buf)
{
	char * res = buf;
	char ** entry;
	char * ptr;

	for (entry = transl_names; *entry; entry++) {
		*res++ = '#';
		for(ptr = *entry; *ptr; ptr++)
			*res++ = *ptr;
		entry++;
		for(ptr = *entry; *ptr; ptr++)
			*res++ = *ptr;
		*res++ = '#';
		*res++ = ':';
	}
	res--;
	*res = '\0';
}

void nametrans_setup(char * line)
{
	if(line) {
		default_trans = (!line[0]);
		if(!global_trans) {
			/* This can happen at boot time, and there is no chance
			 * to allocate memory at this early stage.
			 */
			strncpy(nametrans_txt, line, MAX_DEFAULT_TRANSLEN);
		} else {
			if(default_trans) {
				default_nametrans(nametrans_txt);
				line = nametrans_txt;
			}
			convert(line, global_trans);

			/* Show what really was recognized after parsing... */
			trans_to_string(global_trans, nametrans_txt, MAX_DEFAULT_TRANSLEN);
		}
	}
}

/* If the _first_ environment variable is "NAMETRANS", return
 * a pointer to the list of appendices.
 * You can set the first environment variable using
 * 'env - NAMETRANS=... "`env`" command ...'
 */
char* env_transl(void)
{
	char* env;
	int i;

	if(current && current->mm && (env = (char*)current->mm->env_start)
	   && !segment_eq(get_ds(), get_fs())
	   && current->mm->env_end>=current->mm->env_start+10
	   && !verify_area(VERIFY_READ,env,10)) {
		for(i=0; i<10; i++) {
			char c;

			get_user(c, env++);
			if(c != "NAMETRANS="[i])
				return 0;
		}
		return env;
	}
	return 0;
}

/* If name has the correct suffix "#keyword=correct_context#",
 * return position of the suffix, else 0.
 */
char *testname(int restricted, char* name)
{
	char * ptr = name;
	char * cut;
	char * env;
	struct translations * trans;
	int i, len;
	char c, tmp;
	
	env = env_transl();
#ifdef CONFIG_TRANS_RESTRICT
	if(!env && restricted)
		goto done;
#else
	(void)restricted; /* inhibit parameter usage warning */
#endif
	if(get_user(c, ptr))
		goto done;
	while(c && c != '#') {
		ptr++;
		__get_user(c, ptr);
	}
	if(!c)
		goto done;
	cut = ptr++;
	if(get_user(c, ptr))
		goto done;
	while (c && c != '#') {
		ptr++;
		get_user(c, ptr);
	}
	if(!c)
		goto done;
	get_user(tmp, ptr);
	if(tmp)
		goto done;
	trans = get_translations(env);
	len = (unsigned long)ptr - (unsigned long)cut;
	for(i = 0; i < trans->count; i++)
		if(trans->name[i].len == len) {
			const char * p1 = cut;
			const char * p2 = trans->name[i].name;
			get_user(c, p1);
			while(c && c == *p2++) {
				p1++;
				get_user(c, p1);
			}
			if(!c)
				return cut;
		}
done:
	return NULL;
}

static inline void check_dirty(void)
{
	if(translations_dirty && default_trans) {
		nametrans_setup("");
		translations_dirty = 0;
	}
}

struct translations * get_translations(char * env)
{
	struct translations * res;

	if(env) {
		char * env_txt = (char*)__get_free_page(GFP_KERNEL);

		strncpy_from_user(env_txt, env, PAGE_SIZE);
		res = (struct translations *)__get_free_page(GFP_KERNEL);
		convert(env_txt, res);
		free_page((unsigned long)env_txt);
	} else {
		check_dirty();
		res = global_trans;
	}
	return res;
}

int nametrans_dostring(ctl_table * table, int write, struct file * filp,
		       void * buffer, size_t * lenp)
{
	int res;
	check_dirty();
	res = proc_dostring(table, write, filp, buffer, lenp);
	if(!res && write)
		nametrans_setup(nametrans_txt);

	return res;
}

int nametrans_string(ctl_table * table, int * name, int nlen,
		     void * oldval, size_t * oldlenp,
		     void * newval, size_t newlen, void ** context)
{
	int res;
	check_dirty();
	res = sysctl_string(table, name, nlen, oldval, oldlenp, newval, newlen, context);
	if(!res && newval && newlen)
		nametrans_setup(nametrans_txt);

	return res;
}

void init_nametrans(void)
{
	if(!global_trans)
		global_trans = (struct translations*)__get_free_page(GFP_KERNEL);
	if(!global_trans) {
		printk("NAMETRANS: No free memory\n");
		return;
	}
	nametrans_setup(nametrans_txt);

	/* Notify user for the default/supplied translations.
	 * Extremely useful for finding translation problems.
	 */
	printk("Nametrans %s\nNametrans %s: %s\n", version,
	       default_trans ? "default translations" : "external parameter",
	       nametrans_txt);
}
