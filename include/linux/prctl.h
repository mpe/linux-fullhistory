#ifndef _LINUX_PRCTL_H
#define _LINUX_PRCTL_H

/* Values to pass as first argument to prctl() */

#define PR_SET_PDEATHSIG  1  /* Second arg is a signal */
#define PR_GET_PDEATHSIG  2  /* Second arg is a ptr to return the signal */

/* Get/set current->dumpable */
#define PR_GET_DUMPABLE   3
#define PR_SET_DUMPABLE   4

#endif /* _LINUX_PRCTL_H */
