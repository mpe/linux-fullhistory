#ifndef _LINUX_BINFMTS_H
#define _LINUX_BINFMTS_H

/*
 * MAX_ARG_PAGES defines the number of pages allocated for arguments
 * and envelope for the new program. 32 should suffice, this gives
 * a maximum env+arg of 128kB !
 */
#define MAX_ARG_PAGES 32

/*
 * This structure is used to hold the arguments that are used when loading binaries.
 */
struct linux_binprm{
  char buf[128];
  unsigned long page[MAX_ARG_PAGES];
  unsigned long p;
  struct inode * inode;
  int e_uid, e_gid;
  int argc, envc;
  char * filename;	   /* Name of binary */
};

/* This structure defines the functions that are used to load the binary formats that
 * linux accepts. */

struct linux_binfmt{
  int (*load_binary)(struct linux_binprm *, struct  pt_regs * regs);
  int (*load_shlib)(int fd);
};

extern struct linux_binfmt formats[];



#endif
