/*
 *  linux/init/main.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  GK 2/5/95  -  Changed to support mounting root fs via NFS
 */

#define __KERNEL_SYSCALLS__
#include <stdarg.h>

#include <asm/system.h>
#include <asm/io.h>

#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/head.h>
#include <linux/unistd.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/utsname.h>
#include <linux/ioport.h>
#include <linux/hdreg.h>
#include <linux/mm.h>
#include <linux/major.h>
#ifdef CONFIG_APM
#include <linux/apm_bios.h>
#endif

#include <asm/bugs.h>

extern char _stext, _etext;
extern char *linux_banner;

static char printbuf[1024];

extern int console_loglevel;

static int init(void *);
extern int bdflush(void *);
extern int kswapd(void *);

extern void init_IRQ(void);
extern void init_modules(void);
extern long console_init(long, long);
extern long kmalloc_init(long,long);
extern void sock_init(void);
extern long pci_init(long, long);
extern void sysctl_init(void);

extern void swap_setup(char *str, int *ints);
extern void buff_setup(char *str, int *ints);
extern void bmouse_setup(char *str, int *ints);
extern void eth_setup(char *str, int *ints);
extern void xd_setup(char *str, int *ints);
extern void floppy_setup(char *str, int *ints);
extern void st_setup(char *str, int *ints);
extern void st0x_setup(char *str, int *ints);
extern void advansys_setup(char *str, int *ints);
extern void tmc8xx_setup(char *str, int *ints);
extern void t128_setup(char *str, int *ints);
extern void pas16_setup(char *str, int *ints);
extern void generic_NCR5380_setup(char *str, int *intr);
extern void aha152x_setup(char *str, int *ints);
extern void aha1542_setup(char *str, int *ints);
extern void aic7xxx_setup(char *str, int *ints);
extern void AM53C974_setup(char *str, int *ints);
extern void BusLogic_Setup(char *str, int *ints);
extern void fdomain_setup(char *str, int *ints);
extern void NCR53c406a_setup(char *str, int *ints);
extern void scsi_luns_setup(char *str, int *ints);
extern void sound_setup(char *str, int *ints);
#ifdef CONFIG_CDU31A
extern void cdu31a_setup(char *str, int *ints);
#endif CONFIG_CDU31A
#ifdef CONFIG_MCD
extern void mcd_setup(char *str, int *ints);
#endif CONFIG_MCD
#ifdef CONFIG_MCDX
extern void mcdx_setup(char *str, int *ints);
#endif CONFIG_MCDX
#ifdef CONFIG_SBPCD
extern void sbpcd_setup(char *str, int *ints);
#endif CONFIG_SBPCD
#ifdef CONFIG_AZTCD
extern void aztcd_setup(char *str, int *ints);
#endif CONFIG_AZTCD
#ifdef CONFIG_CDU535
extern void sonycd535_setup(char *str, int *ints);
#endif CONFIG_CDU535
#ifdef CONFIG_GSCD
extern void gscd_setup(char *str, int *ints);
#endif CONFIG_GSCD
#ifdef CONFIG_CM206
extern void cm206_setup(char *str, int *ints);
#endif CONFIG_CM206
#ifdef CONFIG_OPTCD
extern void optcd_setup(char *str, int *ints);
#endif CONFIG_OPTCD
#ifdef CONFIG_SJCD
extern void sjcd_setup(char *str, int *ints);
#endif CONFIG_SJCD
#ifdef CONFIG_BLK_DEV_RAM
static void ramdisk_start_setup(char *str, int *ints);
static void load_ramdisk(char *str, int *ints);
static void prompt_ramdisk(char *str, int *ints);
#endif CONFIG_BLK_DEV_RAM

#if defined(CONFIG_SYSVIPC) || defined(CONFIG_KERNELD)
extern void ipc_init(void);
#endif

/*
 * Boot command-line arguments
 */
#define MAX_INIT_ARGS 8
#define MAX_INIT_ENVS 8

extern void time_init(void);

static unsigned long memory_start = 0;
static unsigned long memory_end = 0;

int rows, cols;

#ifdef CONFIG_BLK_DEV_RAM
extern int rd_doload;		/* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;		/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_image_start;	/* starting block # of image */
#endif

int root_mountflags = MS_RDONLY;
char *execute_command = 0;

#ifdef CONFIG_ROOT_NFS
char nfs_root_name[256] = { NFS_ROOT };
char nfs_root_addrs[128] = { "" };
#endif

extern void dquot_init(void);

static char * argv_init[MAX_INIT_ARGS+2] = { "init", NULL, };
static char * envp_init[MAX_INIT_ENVS+2] = { "HOME=/", "TERM=linux", NULL, };

static char * argv_rc[] = { "/bin/sh", NULL };
static char * envp_rc[] = { "HOME=/", "TERM=linux", NULL };

static char * argv[] = { "-/bin/sh",NULL };
static char * envp[] = { "HOME=/usr/root", "TERM=linux", NULL };

char *get_options(char *str, int *ints)
{
	char *cur = str;
	int i=1;

	while (cur && isdigit(*cur) && i <= 10) {
		ints[i++] = simple_strtoul(cur,NULL,0);
		if ((cur = strchr(cur,',')) != NULL)
			cur++;
	}
	ints[0] = i-1;
	return(cur);
}

static void profile_setup(char *str, int *ints)
{
	if (ints[0] > 0)
		prof_shift = (unsigned long) ints[1];
	else
#ifdef CONFIG_PROFILE_SHIFT
		prof_shift = CONFIG_PROFILE_SHIFT;
#else
		prof_shift = 2;
#endif
}

struct {
	const char *str;
	void (*setup_func)(char *, int *);
} bootsetups[] = {
	{ "reserve=", reserve_setup },
	{ "profile=", profile_setup },
#ifdef CONFIG_BLK_DEV_RAM
	{ "ramdisk_start=", ramdisk_start_setup },
	{ "load_ramdisk=", load_ramdisk },
	{ "prompt_ramdisk=", prompt_ramdisk },
#endif
	{ "swap=", swap_setup },
	{ "buff=", buff_setup },
#ifdef CONFIG_BUGi386
	{ "no-hlt", no_halt },
	{ "no387", no_387 },
#endif
#ifdef CONFIG_INET
	{ "ether=", eth_setup },
#endif
#ifdef CONFIG_SCSI
	{ "max_scsi_luns=", scsi_luns_setup },
#endif
#ifdef CONFIG_SCSI_ADVANSYS
	{ "advansys=", advansys_setup },
#endif
#if defined(CONFIG_BLK_DEV_HD)
	{ "hd=", hd_setup },
#endif
#ifdef CONFIG_CHR_DEV_ST
	{ "st=", st_setup },
#endif
#ifdef CONFIG_BUSMOUSE
	{ "bmouse=", bmouse_setup },
#endif
#ifdef CONFIG_SCSI_SEAGATE
	{ "st0x=", st0x_setup },
	{ "tmc8xx=", tmc8xx_setup },
#endif
#ifdef CONFIG_SCSI_T128
	{ "t128=", t128_setup },
#endif
#ifdef CONFIG_SCSI_PAS16
	{ "pas16=", pas16_setup },
#endif
#ifdef CONFIG_SCSI_GENERIC_NCR5380
	{ "ncr5380=", generic_NCR5380_setup },
#endif
#ifdef CONFIG_SCSI_AHA152X
	{ "aha152x=", aha152x_setup},
#endif
#ifdef CONFIG_SCSI_AHA1542
	{ "aha1542=", aha1542_setup},
#endif
#ifdef CONFIG_SCSI_AIC7XXX
	{ "aic7xxx=", aic7xxx_setup},
#endif
#ifdef CONFIG_SCSI_BUSLOGIC
	{ "BusLogic=", BusLogic_Setup},
#endif
#ifdef CONFIG_SCSI_AM53C974
        { "AM53C974=", AM53C974_setup},
#endif
#ifdef CONFIG_SCSI_NCR53C406A
	{ "ncr53c406a=", NCR53c406a_setup},
#endif
#ifdef CONFIG_SCSI_FUTURE_DOMAIN
	{ "fdomain=", fdomain_setup},
#endif
#ifdef CONFIG_BLK_DEV_XD
	{ "xd=", xd_setup },
#endif
#ifdef CONFIG_BLK_DEV_FD
	{ "floppy=", floppy_setup },
#endif
#ifdef CONFIG_CDU31A
	{ "cdu31a=", cdu31a_setup },
#endif CONFIG_CDU31A
#ifdef CONFIG_MCD
	{ "mcd=", mcd_setup },
#endif CONFIG_MCD
#ifdef CONFIG_MCDX
	{ "mcdx=", mcdx_setup },
#endif CONFIG_MCDX
#ifdef CONFIG_SBPCD
	{ "sbpcd=", sbpcd_setup },
#endif CONFIG_SBPCD
#ifdef CONFIG_AZTCD
	{ "aztcd=", aztcd_setup },
#endif CONFIG_AZTCD
#ifdef CONFIG_CDU535
	{ "sonycd535=", sonycd535_setup },
#endif CONFIG_CDU535
#ifdef CONFIG_GSCD
	{ "gscd=", gscd_setup },
#endif CONFIG_GSCD
#ifdef CONFIG_CM206
	{ "cm206=", cm206_setup },
#endif CONFIG_CM206
#ifdef CONFIG_OPTCD
	{ "optcd=", optcd_setup },
#endif CONFIG_OPTCD
#ifdef CONFIG_SJCD
	{ "sjcd=", sjcd_setup },
#endif CONFIG_SJCD
#ifdef CONFIG_SOUND
	{ "sound=", sound_setup },
#endif
	{ 0, 0 }
};

#ifdef CONFIG_BLK_DEV_RAM
static void ramdisk_start_setup(char *str, int *ints)
{
   if (ints[0] > 0 && ints[1] >= 0)
      rd_image_start = ints[1];
}

static void load_ramdisk(char *str, int *ints)
{
   if (ints[0] > 0 && ints[1] >= 0)
      rd_doload = ints[1] & 1;
}

static void prompt_ramdisk(char *str, int *ints)
{
   if (ints[0] > 0 && ints[1] >= 0)
      rd_prompt = ints[1] & 1;
}
#endif

static int checksetup(char *line)
{
	int i = 0;
	int ints[11];

#ifdef CONFIG_BLK_DEV_IDE
	/* ide driver needs the basic string, rather than pre-processed values */
	if (!strncmp(line,"ide",3) || (!strncmp(line,"hd",2) && line[2] != '=')) {
		ide_setup(line);
		return 1;
	}
#endif
	while (bootsetups[i].str) {
		int n = strlen(bootsetups[i].str);
		if (!strncmp(line,bootsetups[i].str,n)) {
			bootsetups[i].setup_func(get_options(line+n,ints), ints);
			return 1;
		}
		i++;
	}
	return 0;
}

/* this should be approx 2 Bo*oMips to start (note initial shift), and will
   still work even if initally too large, it will just take slightly longer */
unsigned long loops_per_sec = (1<<12);

/* This is the number of bits of precision for the loops_per_second.  Each
   bit takes on average 1.5/HZ seconds.  This (like the original) is a little
   better than 1% */
#define LPS_PREC 8

void calibrate_delay(void)
{
	int ticks;
	int loopbit;
	int lps_precision = LPS_PREC;

	loops_per_sec = (1<<12);

	printk("Calibrating delay loop.. ");
	while (loops_per_sec <<= 1) {
		/* wait for "start of" clock tick */
		ticks = jiffies;
		while (ticks == jiffies)
			/* nothing */;
		/* Go .. */
		ticks = jiffies;
		__delay(loops_per_sec);
		ticks = jiffies - ticks;
		if (ticks)
			break;
		}

/* Do a binary approximation to get loops_per_second set to equal one clock
   (up to lps_precision bits) */
	loops_per_sec >>= 1;
	loopbit = loops_per_sec;
	while ( lps_precision-- && (loopbit >>= 1) ) {
		loops_per_sec |= loopbit;
		ticks = jiffies;
		while (ticks == jiffies);
		ticks = jiffies;
		__delay(loops_per_sec);
		if (jiffies != ticks)	/* longer than 1 tick */
			loops_per_sec &= ~loopbit;
	}

/* finally, adjust loops per second in terms of seconds instead of clocks */	
	loops_per_sec *= HZ;
/* Round the value and print it */	
	printk("ok - %lu.%02lu BogoMIPS\n",
		(loops_per_sec+2500)/500000,
		((loops_per_sec+2500)/5000) % 100);
}

/*
 * This is a simple kernel command line parsing function: it parses
 * the command line, and fills in the arguments/environment to init
 * as appropriate. Any cmd-line option is taken to be an environment
 * variable if it contains the character '='.
 *
 *
 * This routine also checks for options meant for the kernel.
 * These options are not given to init - they are for internal kernel use only.
 */
static void parse_options(char *line)
{
	char *next;
	char *devnames[] = { "nfs", "hda", "hdb", "hdc", "hdd", "sda", "sdb",
		"sdc", "sdd", "sde", "fd", "xda", "xdb", NULL };
	int devnums[]    = { 0x0FF, 0x300, 0x340, 0x1600, 0x1640, 0x800,
		0x810, 0x820, 0x830, 0x840, 0x200, 0xD00, 0xD40, 0};
	int args, envs;
	if (!*line)
		return;
	args = 0;
	envs = 1;	/* TERM is set to 'linux' by default */
	next = line;
	while ((line = next) != NULL) {
		if ((next = strchr(line,' ')) != NULL)
			*next++ = 0;
		/*
		 * check for kernel options first..
		 */
		if (!strncmp(line,"root=",5)) {
			int n;
			line += 5;
			if (strncmp(line,"/dev/",5)) {
				ROOT_DEV = to_kdev_t(
					     simple_strtoul(line,NULL,16));
				continue;
			}
			line += 5;
			for (n = 0 ; devnames[n] ; n++) {
				int len = strlen(devnames[n]);
				if (!strncmp(line,devnames[n],len)) {
					ROOT_DEV = to_kdev_t(devnums[n]+
					     simple_strtoul(line+len,NULL,0));
					break;
				}
			}
			continue;
		}
#ifdef CONFIG_ROOT_NFS
		if (!strncmp(line, "nfsroot=", 8)) {
			int n;
			line += 8;
			ROOT_DEV = MKDEV(UNNAMED_MAJOR, 255);
			if (line[0] == '/' || (line[0] >= '0' && line[0] <= '9')) {
				strncpy(nfs_root_name, line, sizeof(nfs_root_name));
				nfs_root_name[sizeof(nfs_root_name)-1] = '\0';
				continue;
			}
			n = strlen(line) + strlen(NFS_ROOT);
			if (n >= sizeof(nfs_root_name))
				line[sizeof(nfs_root_name) - strlen(NFS_ROOT) - 1] = '\0';
			sprintf(nfs_root_name, NFS_ROOT, line);
			continue;
		}
		if (!strncmp(line, "nfsaddrs=", 9)) {
			line += 9;
			strncpy(nfs_root_addrs, line, sizeof(nfs_root_addrs));
			nfs_root_addrs[sizeof(nfs_root_addrs)] = '\0';
			continue;
		}
#endif
		if (!strcmp(line,"ro")) {
			root_mountflags |= MS_RDONLY;
			continue;
		}
		if (!strcmp(line,"rw")) {
			root_mountflags &= ~MS_RDONLY;
			continue;
		}
		if (!strcmp(line,"debug")) {
			console_loglevel = 10;
			continue;
		}
		if (!strncmp(line,"init=",5)) {
			line += 5;
			execute_command = line;
			continue;
		}
		if (checksetup(line))
			continue;
		/*
		 * Then check if it's an environment variable or
		 * an option.
		 */
		if (strchr(line,'=')) {
			if (envs >= MAX_INIT_ENVS)
				break;
			envp_init[++envs] = line;
		} else {
			if (args >= MAX_INIT_ARGS)
				break;
			argv_init[++args] = line;
		}
	}
	argv_init[args+1] = NULL;
	envp_init[envs+1] = NULL;
}


extern void setup_arch(char **, unsigned long *, unsigned long *);
extern void arch_syms_export(void);

#ifndef __SMP__

/*
 *	Uniprocessor idle thread
 */
 
int cpu_idle(void *unused)
{
	for(;;)
		idle();
}

#else

/*
 *	Multiprocessor idle thread is in arch/...
 */
 
extern int cpu_idle(void * unused);

/*
 *	Activate a secondary processor.
 */
 
asmlinkage void start_secondary(void)
{
	trap_init();
	init_IRQ();
	smp_callin();
	cpu_idle(NULL);
}



/*
 *	Called by CPU#0 to activate the rest.
 */
 
static void smp_init(void)
{
	int i=0;
	smp_boot_cpus();
	
	/*
	 *	Create the slave init tasks as sharing pid 0.
	 */

	for(i=1;i<smp_num_cpus;i++)
	{
		/*
		 *	We use kernel_thread for the idlers which are
		 *	unlocked tasks running in kernel space.
		 */
		kernel_thread(cpu_idle, NULL, CLONE_PID);
		/*
		 *	Assume linear processor numbering
		 */
		current_set[i]=task[i];
		current_set[i]->processor=i;
	}
}		

/*
 *	The autoprobe routines assume CPU#0 on the i386
 *	so we don't actually set the game in motion until
 *	they are finished.
 */
 
static void smp_begin(void)
{
	smp_threads_ready=1;
	smp_commence();
}
	
#endif

/*
 *	Activate the first processor.
 */
 
asmlinkage void start_kernel(void)
{
	char * command_line;

/*
 *	This little check will move.
 */

#ifdef __SMP__
	static int first_cpu=1;
	
	if(!first_cpu)
		start_secondary();
	first_cpu=0;
	
#endif	
/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */
	setup_arch(&command_line, &memory_start, &memory_end);
	memory_start = paging_init(memory_start,memory_end);
	trap_init();
	init_IRQ();
	sched_init();
	time_init();
	parse_options(command_line);
#ifdef CONFIG_MODULES
	init_modules();
#endif
#ifdef CONFIG_PROFILE
	if (!prof_shift)
#ifdef CONFIG_PROFILE_SHIFT
		prof_shift = CONFIG_PROFILE_SHIFT;
#else
		prof_shift = 2;
#endif
#endif
	if (prof_shift) {
		prof_buffer = (unsigned long *) memory_start;
		/* only text is profiled */
		prof_len = (unsigned long) &_etext - (unsigned long) &_stext;
		prof_len >>= prof_shift;
		memory_start += prof_len * sizeof(unsigned long);
	}
	memory_start = console_init(memory_start,memory_end);
#ifdef CONFIG_PCI
	memory_start = pci_init(memory_start,memory_end);
#endif
	memory_start = kmalloc_init(memory_start,memory_end);
	sti();
	calibrate_delay();
	memory_start = inode_init(memory_start,memory_end);
	memory_start = file_table_init(memory_start,memory_end);
	memory_start = name_cache_init(memory_start,memory_end);
	mem_init(memory_start,memory_end);
	buffer_init();
	sock_init();
#if defined(CONFIG_SYSVIPC) || defined(CONFIG_KERNELD)
	ipc_init();
#endif
#ifdef CONFIG_APM
	apm_bios_init();
#endif
	dquot_init();
	arch_syms_export();
	sti();
	check_bugs();

	printk(linux_banner);
#ifdef __SMP__
	smp_init();
#endif
	sysctl_init();
	/* 
	 *	We count on the initial thread going ok 
	 *	Like idlers init is an unlocked kernel thread, which will
	 *	make syscalls (and thus be locked).
	 */
	kernel_thread(init, NULL, 0);
/*
 * task[0] is meant to be used as an "idle" task: it may not sleep, but
 * it might do some general things like count free pages or it could be
 * used to implement a reasonable LRU algorithm for the paging routines:
 * anything that can be useful, but shouldn't take time from the real
 * processes.
 *
 * Right now task[0] just does a infinite idle loop.
 */
 	cpu_idle(NULL);
}

static int printf(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	write(1,printbuf,i=vsprintf(printbuf, fmt, args));
	va_end(args);
	return i;
}

static int do_rc(void * rc)
{
	close(0);
	if (open(rc,O_RDONLY,0))
		return -1;
	return execve("/bin/sh", argv_rc, envp_rc);
}

static int do_shell(void * shell)
{
	close(0);close(1);close(2);
	setsid();
	(void) open("/dev/tty1",O_RDWR,0);
	(void) dup(0);
	(void) dup(0);
	return execve(shell, argv, envp);
}

static int init(void * unused)
{
	int pid,i;

	/* Launch bdflush from here, instead of the old syscall way. */
	kernel_thread(bdflush, NULL, 0);
	/* Start the background pageout daemon. */
	kernel_thread(kswapd, NULL, 0);

	setup();

#ifdef __SMP__
	/*
	 *	With the devices probed and setup we can
	 *	now enter SMP mode.
	 */
	
	smp_begin();
#endif	

	#ifdef CONFIG_UMSDOS_FS
	{
		/*
			When mounting a umsdos fs as root, we detect
			the pseudo_root (/linux) and initialise it here.
			pseudo_root is defined in fs/umsdos/inode.c
		*/
		extern struct inode *pseudo_root;
		if (pseudo_root != NULL){
			current->fs->root = pseudo_root;
			current->fs->pwd  = pseudo_root;
		}
	}
	#endif

	(void) open("/dev/tty1",O_RDWR,0);
	(void) dup(0);
	(void) dup(0);

	if (!execute_command) {
		execve("/etc/init",argv_init,envp_init);
		execve("/bin/init",argv_init,envp_init);
		execve("/sbin/init",argv_init,envp_init);
		/* if this fails, fall through to original stuff */

		pid = kernel_thread(do_rc, "/etc/rc", SIGCHLD);
		if (pid>0)
			while (pid != wait(&i))
				/* nothing */;
		}

	while (1) {
		pid = kernel_thread(do_shell,
			execute_command ? execute_command : "/bin/sh",
			SIGCHLD);
		if (pid < 0) {
			printf("Fork failed in init\n\r");
			continue;
		}
		while (1)
			if (pid == wait(&i))
				break;
		printf("\n\rchild %d died with code %04x\n\r",pid,i);
		sync();
	}
	return -1;
}
