#ifndef _SPARC_MEMERR_H
#define _SPARC_MEMERR_H

/* memerr.h:  Bit fields in the asynchronous and synchronous memory error
              registers used to determine what 'type' of error has just
	      induced a trap.

   Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
*/

/* synchronous error register fields come first... */

#define SYNCER_WRITE    0x8000        /* write error...                               */
#define SYNCER_INVAL    0x0080        /* invalid page access was attempted            */
#define SYNCER_PROT     0x0040        /* protection violation                         */
#define SYNCER_TIMEOUT  0x0020        /* mem-bus access timeout (mem does not exist). */
#define SYNCER_SBUSERR  0x0010        /* same as above, but for an SBUS access        */
#define SYNCER_MEMERR   0x0008        /* Bus parity error, lose lose... panic time    */
#define SYNCER_SZERR    0x0002        /* an attempted access was of BAD size, whoops  */
#define SYNCER_WATCHDOG 0x0001        /* although we never see these, the prom will.. */

/* asynchronous error bits go here */

#define ASYNCER_WBINVAL   0x80        /* situation arose where the cache tried to write
                                       * back a page for which the valid bit was not set
				       * within the mmu. This is due to bad mm kernel bugs.
				       */

#define ASYNCER_TIMEOUT   0x20        /* mem-access bus timeout... */
#define ASYNCER_DVMAERR   0x10        /* dvma transfer to/from memory bombed... */

#endif /* _SPARC_MEMERR_H */
