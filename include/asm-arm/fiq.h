/* Support for FIQ on ARM architectures.
 * Written by Philip Blundell <philb@gnu.org>, 1998
 */

#ifndef __ASM_FIQ_H
#define __ASM_FIQ_H

struct fiq_handler {
	 const char *name;
	 int (*callback)(void);
};

extern int claim_fiq(struct fiq_handler *f);
extern void release_fiq(struct fiq_handler *f);

#endif
