#ifndef NAMETRANS_H
#define NAMETRANS_H
/*
 * $Id: nametrans.h,v 1.1 1997/06/04 08:26:57 davem Exp $
 *
 * include/linux/nametrans.h - context-dependend filename suffixes.
 * Copyright (C) 1997, Thomas Schoebel-Theuer,
 * <schoebel@informatik.uni-stuttgart.de>.
 */

#include <linux/dalloc.h>
#include <linux/sysctl.h>

#define MAX_DEFAULT_TRANSLEN 128

/* only filenames matching the following length restrictions can be
 * translated. I introduced these restrictions because they *greatly*
 * simplify buffer management (no need to allocate kernel pages and free them).
 * The maximal total length of a context-dependend filename is the
 * sum of both constants. */
#define MAX_TRANS_FILELEN 128 /* max len of a name that could be translated */
#define MAX_TRANS_SUFFIX   64 /* max len of a #keyword=value# suffix */

/* max number of translations */
#define MAX_TRANSLATIONS 16

struct translations {
	int count;
	struct qstr name[MAX_TRANSLATIONS];
	struct qstr c_name[MAX_TRANSLATIONS];
};

/* global/default translations */
extern char nametrans_txt[MAX_DEFAULT_TRANSLEN];

/* Any changer of a built-in translation must set this flag */
extern int translations_dirty;


/* called once at boot time */
extern void init_nametrans(void);

/* set global translations */
extern void nametrans_setup(char * line);

/* return reusable global buffer. needed by VFS. */
struct translations * get_translations(char * env);

/* if the _first_ environment variable is "NAMETRANS", return
 * a pointer to the list of appendices.
 * You can set the first environment variable using
 * 'env - NAMETRANS=... "`env`" command ...'
 */
extern char * env_transl(void);

/* if name has the correct suffix "#keyword=correct_context#",
 * return position of the suffix, else 0.
 */
extern char* testname(int restricted, char* name);

/* for use in kernel/sysctrl.h */
extern int nametrans_dostring(ctl_table * table, int write, struct file * filp,
			      void * buffer, size_t * lenp);
extern int nametrans_string(ctl_table * table, int * name, int nlen,
			    void * oldval, size_t * oldlenp,
			    void * newval, size_t newlen, void ** context);


#endif
