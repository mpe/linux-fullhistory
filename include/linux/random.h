/*
 * include/linux/random.h
 *
 * Include file for the random number generator.
 */

#ifndef _LINUX_RANDOM_H
#define _LINUX_RANDOM_H

/* ioctl()'s for the random number generator */

#define RNDGETENTCNT	0x01080000
#define RNDADDTOENTCNT	0x01080001
#define RNDGETPOOL	0x01080002

struct rand_pool_state {
	int	entropy_count;
	int	pool_size;
	__u32	pool[0];
};

/* Exported functions */

#ifdef __KERNEL__

void rand_initialize(void);
void rand_initialize_irq(int irq);
void rand_initialize_blkdev(int irq);

void add_keyboard_randomness(unsigned char scancode);
void add_mouse_randomness(__u32 mouse_data);
void add_interrupt_randomness(int irq);
void add_blkdev_randomness(int major);

void get_random_bytes(void *buf, int nbytes);
int read_random(struct inode * inode, struct file * file,
		char * buf, int nbytes);
int read_random_unlimited(struct inode * inode, struct file * file,
			  char * buf, int nbytes);
int write_random(struct inode * inode, struct file * file,
		 const char * buffer, int count);
int random_ioctl(struct inode * inode, struct file * file,
		 unsigned int cmd, unsigned long arg);

#endif /* __KERNEL___ */

#endif /* _LINUX_RANDOM_H */
