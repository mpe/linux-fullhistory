#ifndef __LINUX_SMP_H
#define __LINUX_SMP_H

/*
 *	Generic SMP support
 *		Alan Cox. <alan@cymru.net>
 */

#ifdef __SMP__
#include <asm/smp.h>
 
extern void smp_message_pass(int target, int msg, unsigned long data, int wait);
extern void smp_boot_cpus(void);		/* Boot processor call to load the other CPU's */
extern void smp_callin(void);			/* Processor call in. Must hold processors until .. */
extern void smp_commence(void);			/* Multiprocessors may now schedule */
extern int smp_num_cpus;
extern int smp_threads_ready;			/* True once the per process idle is forked */
extern volatile unsigned long smp_spins;	/* Count of spins */

extern volatile unsigned long smp_msg_data;
extern volatile int smp_src_cpu;
extern volatile int smp_msg_id;

#define MSG_ALL_BUT_SELF	0x8000		/* Assume <32768 CPU's */
#define MSG_ALL			0x8001

#define MSG_INVALIDATE_TLB	0x0001		/* Remote processor TLB invalidate */
#define MSG_STOP_CPU		0x0002		/* Sent to shut down slave CPU's when rebooting */
#define MSG_RESCHEDULE		0x0003		/* Reschedule request from master CPU */

#else

/*
 *	These macros fold the SMP functionality into a single CPU system
 */
 
#define smp_num_cpus			1
#define smp_processor_id()		0
#define smp_message_pass(t,m,d,w)	
#define smp_threads_ready		1
#define kernel_lock()
#endif
#endif
