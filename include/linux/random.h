/*
 * include/linux/random.h
 *
 * Include file for the random number generator.
 */

#ifndef _LINUX_RANDOM_H
#define _LINUX_RANDOM_H

/* ioctl()'s for the random number generator */

/* Get the entropy count. */
#define RNDGETENTCNT	0x01080000

/* Add to (or subtract from) the entropy count.  (Superuser only.) */
#define RNDADDTOENTCNT	0x01080001

/* Get the contents of the entropy pool.  (Superuser only.) */
#define RNDGETPOOL	0x01080002

/* 
 * Write bytes into the entropy pool and add to the entropy count.
 * (Superuser only.)
 */
#define RNDADDENTROPY	0x01080003

/* Clear entropy count to 0.  (Superuser only.) */
#define RNDZAPENTCNT	0x01080004

/* Clear the entropy pool and associated counters.  (Superuser only.) */
#define RNDCLEARPOOL	0x01080006

struct rand_pool_info {
	int	entropy_count;
	int	buf_size;
	__u32	buf[0];
};

/* Exported functions */

#ifdef __KERNEL__

extern void rand_initialize(void);
extern void rand_initialize_irq(int irq);
extern void rand_initialize_blkdev(int irq, int mode);

extern void add_keyboard_randomness(unsigned char scancode);
extern void add_mouse_randomness(__u32 mouse_data);
extern void add_interrupt_randomness(int irq);
extern void add_blkdev_randomness(int major);

extern void get_random_bytes(void *buf, int nbytes);

#ifndef MODULE
extern struct file_operations random_fops, urandom_fops;
#endif

#endif /* __KERNEL___ */

#endif /* _LINUX_RANDOM_H */
