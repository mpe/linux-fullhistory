#ifndef __LINUX_SMP_H
#define __LINUX_SMP_H

/*
 *	Generic SMP support
 *		Alan Cox. <alan@cymru.net>
 */

#ifdef __SMP__
#include <asm/smp.h>

/*
 * main IPI interface, handles INIT, TLB flush, STOP, etc.:
 */ 
extern void smp_message_pass(int target, int msg, unsigned long data, int wait);

/*
 * Boot processor call to load the other CPU's
 */
extern void smp_boot_cpus(void);

/*
 * Processor call in. Must hold processors until ..
 */
extern void smp_callin(void);

/*
 * Multiprocessors may now schedule
 */
extern void smp_commence(void);

/*
 * True once the per process idle is forked
 */
extern int smp_threads_ready;

extern int smp_num_cpus;

extern volatile unsigned long smp_msg_data;
extern volatile int smp_src_cpu;
extern volatile int smp_msg_id;

#define MSG_ALL_BUT_SELF	0x8000	/* Assume <32768 CPU's */
#define MSG_ALL			0x8001

#define MSG_INVALIDATE_TLB	0x0001	/* Remote processor TLB invalidate */
#define MSG_STOP_CPU		0x0002	/* Sent to shut down slave CPU's
					 * when rebooting
					 */
#define MSG_RESCHEDULE		0x0003	/* Reschedule request from master CPU */

#else

/*
 *	These macros fold the SMP functionality into a single CPU system
 */
 
#define smp_num_cpus			1
#define smp_processor_id()		0
#define hard_smp_processor_id()		0
#define smp_message_pass(t,m,d,w)	
#define smp_threads_ready		1
#define kernel_lock()
#endif
#endif
