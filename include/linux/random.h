/*
 * include/linux/random.h
 *
 * Include file for the random number generator.
 */

/*
 * We should always include the random number generator, since it's
 * relatively small, and it's most useful when application developers
 * can assume that all Linux systems have it.  (Ideally, it would be
 * best if we could assume that all Unix systems had it, but oh
 * well....)
 * 
 * Also, many kernel routines will have a use for good random numbers,
 * for example, for truely random TCP sequence numbers, which prevent
 * certain forms of TCP spoofing attacks.
 */
#define CONFIG_RANDOM

/* Exported functions */

#ifdef CONFIG_RANDOM
void rand_initialize(void);

void add_keyboard_randomness(unsigned char scancode);
void add_interrupt_randomness(int irq);

void get_random_bytes(void *buf, int nbytes);
int read_random(struct inode * inode, struct file * file,
		char * buf, int nbytes);
int read_random_unlimited(struct inode * inode, struct file * file,
			  char * buf, int nbytes);
#else
#define add_keyboard_randomness(x)
#define add_interrupt_randomness(x)
#endif
