/*
 * linux/ipc/util.h
 * Copyright (C) 1999 Christoph Rohland
 */

/*
 * IPCMNI is the absolute maximum for ipc identifier. This is used to
 * detect stale identifiers
 */
#define IPCMNI (1<<15)          

extern int ipcperms (struct ipc_perm *ipcp, short shmflg);
