/*
 * mediabay.h: definitions for using the media bay
 * on PowerBook 3400 and similar computers.
 *
 * Copyright (C) 1997 Paul Mackerras.
 */
#ifndef _PPC_MEDIABAY_H
#define _PPC_MEDIABAY_H

#define MB_FD	0		/* media bay contains floppy drive */
#define MB_CD	3		/* media bay contains ATA drive such as CD */
#define MB_NO	7		/* media bay contains nothing */

#ifdef __KERNEL__

void media_bay_init(void);
int check_media_bay(int what);
int media_bay_task(void *);

extern int media_bay_present;	/* 1 if this machine has a media bay */

/*
 * The following give information about the IDE interface
 * of the media bay: the base virtual address and IRQ number,
 * and the index that the IDE driver has assigned to it
 * (or -1 if it is not currently registered with the driver).
 */
extern unsigned long mb_cd_base;
extern int mb_cd_irq;
extern int mb_cd_index;

#endif /* __KERNEL__ */
#endif /* _PPC_MEDIABAY_H */
