#ifndef _SPARC_MEMREG_H
#define _SPARC_MEMREG_H
/* memreg.h:  Definitions of the values found in the synchronous
 *            and asynchronous memory error registers when a fault
 *            occurs on the sun4c.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

/* First the synchronous error codes, these are usually just
 * normal page faults.
 */

#define SUN4C_SYNC_WDRESET   0x1  /* watchdog reset, only the prom sees this */
#define SUN4C_SYNC_SIZE      0x2  /* bad access size? whuz this? */
#define SUN4C_SYNC_PARITY    0x8  /* bad ram chips caused a parity error */
#define SUN4C_SYNC_SBUS      0x10 /* the SBUS had some problems... */
#define SUN4C_SYNC_NOMEM     0x20 /* translation pointed to non-existant ram */
#define SUN4C_SYNC_PROT      0x40 /* access violated pte protection settings */
#define SUN4C_SYNC_NPRESENT  0x80 /* pte said that page was not present */
#define SUN4C_SYNC_BADWRITE  0x8000  /* while writing something went bogus */

/* Now the asynchronous error codes, these are almost always produced
 * by the cache writing things back to memory and getting a bad translation.
 * Bad DVMA transactions can cause these faults too.
 */

#define SUN4C_ASYNC_BADDVMA  0x10 /* error during DVMA access */
#define SUN4C_ASYNC_NOMEM    0x20 /* write back pointed to bad phys addr */
#define SUN4C_ASYNC_BADWB    0x80 /* write back points to non-present page */

/* These are the values passed as the first arguement to the fault
 * entry c-code from the assembly entry points.
 */
#define FAULT_ASYNC          0x0
#define FAULT_SYNC           0x1

#endif /* !(_SPARC_MEMREG_H) */
