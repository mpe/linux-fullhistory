#ifndef _SPARC_CONTREGS_H
#define _SPARC_CONTREGS_H

/* contregs.h:  Addresses of registers in the ASI_CONTROL alternate address
                space. These are for the mmu's context register, etc.

   Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
*/

#define AC_CONTEXT    0x30000000    /* current mmu-context, handy for invalidate()'s ;-)   */
#define AC_SENABLE    0x40000000    /* system dvma/cache enable, plus special reset poking */
#define AC_CACHETAGS  0x80000000    /* direct access to the VAC cache, unused...          */
#define AC_SYNC_ERR   0x60000000    /* what type of synchronous memory error happened      */
#define AC_SYNC_VA    0x60000004    /* what virtual address caused the error to occur      */
#define AC_ASYNC_ERR  0x60000008    /* what type of asynchronous mem-error happened        */
#define AC_ASYNC_VA   0x6000000c    /* what virtual address caused the async-err to happen */
#define AC_CACHEDDATA 0x90000000    /* where the actual VAC cached data sits               */

#endif /* _SPARC_CONTREGS_H */
