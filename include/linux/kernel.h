#ifndef _LINUX_KERNEL_H
#define _LINUX_KERNEL_H

/*
 * 'kernel.h' contains some often-used function prototypes etc
 */

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/linkage.h>

#define INT_MAX		((int)(~0U>>1))
#define UINT_MAX	(~0U)
#define LONG_MAX	((long)(~0UL>>1))
#define ULONG_MAX	(~0UL)

#define VERIFY_READ 0
#define VERIFY_WRITE 1

int verify_area(int type, void * addr, unsigned long count);

extern void math_error(void);
volatile void panic(const char * fmt, ...)
	__attribute__ ((format (printf, 1, 2)));
volatile void do_exit(long error_code);
unsigned long simple_strtoul(const char *,char **,unsigned int);
int sprintf(char * buf, const char * fmt, ...);

asmlinkage int printk(const char * fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

#ifdef CONFIG_DEBUG_MALLOC
#define kmalloc(a,b) deb_kmalloc(__FILE__,__LINE__, a,b)
#define kfree_s(a,b) deb_kfree_s(__FILE__,__LINE__,a,b)

void *deb_kmalloc(const char *deb_file, unsigned short deb_line,unsigned int size, int priority);
void deb_kfree_s (const char *deb_file, unsigned short deb_line,void * obj, int size);
void deb_kcheck_s(const char *deb_file, unsigned short deb_line,void * obj, int size);

#define kfree(a) deb_kfree_s(__FILE__,__LINE__, a,0)
#define kcheck(a) deb_kcheck_s(__FILE__,__LINE__, a,0)
#define kcheck_s(a,b) deb_kcheck_s(__FILE__,__LINE__, a,b)

#else /* !debug */

void * kmalloc(unsigned int size, int priority);
void kfree_s(void * obj, int size);

#define kcheck_s(a,b) 0

#define kfree(x) kfree_s((x), 0)
#define kcheck(x) kcheck_s((x), 0)

#endif


/*
 * This is defined as a macro, but at some point this might become a
 * real subroutine that sets a flag if it returns true (to do
 * BSD-style accounting where the process is flagged if it uses root
 * privs).  The implication of this is that you should do normal
 * permissions checks first, and check suser() last.
 */
#define suser() (current->euid == 0)

#endif /* __KERNEL__ */

#define SI_LOAD_SHIFT	16
struct sysinfo {
	long uptime;			/* Seconds since boot */
	unsigned long loads[3];		/* 1, 5, and 15 minute load averages */
	unsigned long totalram;		/* Total usable main memory size */
	unsigned long freeram;		/* Available memory size */
	unsigned long sharedram;	/* Amount of shared memory */
	unsigned long bufferram;	/* Memory used by buffers */
	unsigned long totalswap;	/* Total swap space size */
	unsigned long freeswap;		/* swap space still available */
	unsigned short procs;		/* Number of current processes */
	char _f[22];			/* Pads structure to 64 bytes */
};

#endif
