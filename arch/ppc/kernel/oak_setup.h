/*
 *
 *    Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *
 *    Module name: oak_setup.c
 *
 *    Description:
 *      Architecture- / platform-specific boot-time initialization code for
 *      the IBM PowerPC 403GCX "Oak" evaluation board. Adapted from original
 *      code by Gary Thomas, Cort Dougan <cort@cs.nmt.edu>, and Dan Malek
 *      <dmalek@jlc.net>.
 *
 */

#ifndef	__OAK_SETUP_H__
#define	__OAK_SETUP_H__

#ifdef __cplusplus
extern "C" {
#endif

extern void	 oak_init(unsigned long r3,
			  unsigned long ird_start, unsigned long ird_end,
			  unsigned long cline_start, unsigned long cline_end);
extern void	 oak_setup_arch(void);


#ifdef __cplusplus
}
#endif

#endif /* __OAK_SETUP_H__ */
