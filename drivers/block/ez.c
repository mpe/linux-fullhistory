/* 
	ez.c	(c) 1996  Grant R. Guenther <grant@torque.net>
		          Under the terms of the GNU public license.

	This is a driver for the parallel port versions of SyQuest's 
        EZ135 and EZ230 removable media disk drives.  
        
        Special thanks go to Pedro Soria-Rodriguez for his help testing 
	the EZFlyer 230 support.

	The drive is actually SyQuest's IDE product with a
        ShuttleTech IDE <-> parallel converter chip built in.

	To compile the driver, ensure that /usr/include/linux and
        /usr/include/asm are links to the correct include files for 
        the target system. Then compile the driver with 

		cc -D__KERNEL__ -DMODULE -O2 -c ez.c

        If you are using MODVERSIONS, add the following to the cc command:

		-DMODVERSIONS -I /usr/include/linux/modversions.h

	You must then load it with insmod.

	Before attempting to access the new driver, you will need to
        create some device special files.  The following commands will
	do that for you:

		mknod /dev/eza  b 40 0
		mknod /dev/eza1 b 40 1
		mknod /dev/eza2 b 40 2
		mknod /dev/eza3 b 40 3
		mknod /dev/eza4 b 40 4
		chown root:disk /dev/ez*
		chmod 660 /dev/ez*

	You can make devices for more partitions (up to 15) if you need to.

	You can alter the port used by the driver in two ways:  either
        change the definition of EZ_BASE or modify the ez_base variable
        on the insmod command line, for example:

		insmod ez ez_base=0x3bc

	The driver can detect if the parallel port supports 8-bit
        transfers.  If so, it will use them.  You can force it to use
        4-bit (nybble) mode by setting the variable ez_nybble to 1.

	The driver can be used with or without interrupts.  If an IRQ
        is specified in the variable ez_irq, the driver will use it.
        If ez_irq is set to 0, an alternative, polling-based, strategy 
	will be used.

	If you experience timeout errors while using this driver - and
        you have enabled interrupts - try disabling the interrupt.  I
        have heard reports of some parallel ports having exceptionally
        unreliable interrupts.  This could happen on misconfigured 
        systems in which an inactive sound card shares the same IRQ with 
        the parallel port. (Remember that most people do not use the
        parallel port interrupt for printing.)

	It would be advantageous to use multiple mode transfers,
        but ShuttleTech's driver does not appear to use them, so I'm not
        sure that the converter can handle it.

	It is not currently possible to connect a printer to the chained
        port on the EZ135p and expect Linux to use both devices at once.

	When the EZ230 powers on, the "standby timer" is set to about 6
        minutes:  if the drive is idle for that length of time, it will
        put itself into a low power standby mode.  It takes a couple of
        seconds for the drive to come out of standby mode.  So, if you
        load this driver while it is in standby mode, you will notice
        a "freeze" of a second or two as the driver waits for the EZ230
        to come back to life.  Once loaded, this driver disables the
        standby timer (until you next power up the EZ230 ...)

	Keep an eye on http://www.torque.net/ez135.html for news and
        other information about the driver.  If you have any problems
        with this driver, please send me, grant@torque.net, some mail 
        directly before posting into the newsgroups or mailing lists.

*/

#define	EZ_VERSION	"0.11"

#define	EZ_BASE		0x378
#define EZ_IRQ		7
#define EZ_REP		4

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/tqueue.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/ioport.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#define EZ_BITS	   4			/* compatible with SCSI version */
#define EZ_MAJOR   40			/* as assigned by hpa */

#define MAJOR_NR EZ_MAJOR

/* set up defines for blk.h,  why don't all drivers do it this way ? */

#define DEVICE_NAME "ez"
#define DEVICE_REQUEST do_ez_request
#define DEVICE_NR(device) (MINOR(device)>>EZ_BITS)
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#include <linux/blk.h>

#define EZ_PARTNS  (1<<EZ_BITS)

#define EZ_LOG_HEADS	64		
#define EZ_LOG_SECTS	32		/* SCSI compatible logical geometry */

#define EZ_SIGOFF	54
#define EZ_SIG		"ySuQse tZE"
#define EZ_SIGLEN	10
#define EZ_ID_LEN	14

#define EZ_TMO  	250		/* interrupt timeout in jiffies */

#define EZ_SPIN_DEL     50		/* spin delay in micro-seconds  */

#define EZ_SPIN		(10000/EZ_SPIN_DEL)*EZ_TMO  
#define EZ_ISPIN	(10000/EZ_SPIN_DEL)*20
#define EZ_DELAY        udelay(EZ_SPIN_DEL)

#define STAT_ERR	0x00001
#define STAT_INDEX	0x00002
#define STAT_ECC	0x00004
#define STAT_DRQ	0x00008
#define STAT_SEEK	0x00010
#define STAT_WRERR	0x00020
#define STAT_READY	0x00040
#define STAT_BUSY	0x00080

#define ERR_AMNF	0x00100
#define ERR_TK0NF	0x00200
#define ERR_ABRT	0x00400
#define ERR_MCR		0x00800
#define ERR_IDNF	0x01000
#define ERR_MC		0x02000
#define ERR_UNC		0x04000
#define ERR_TMO		0x10000

#define IDE_READ	0x20
#define IDE_WRITE	0x30
#define IDE_STANDBY     0x96
#define IDE_DOORLOCK	0xde
#define IDE_DOORUNLOCK  0xdf
#define IDE_ACKCHANGE   0xdb
#define IDE_IDENTIFY    0xec

int ez_init(void);
void ez_setup(char * str, int * ints);
#ifdef MODULE
void cleanup_module( void );
#endif
static void ez_geninit(struct gendisk *ignored);
static int ez_open(struct inode *inode, struct file *file);
static void do_ez_request(void);
static int ez_ioctl(struct inode *inode,struct file *file,
                    unsigned int cmd, unsigned long arg);
static int ez_release (struct inode *inode, struct file *file);
static int ez_revalidate(kdev_t dev);
static int ez_check_media(kdev_t dev);
static void ez_get_capacity( void );
static int ez_detect(void);
static void do_ez_read(void);
static void do_ez_write(void);
static void ez_media_check(void);
static void ez_doorlock(int func);
static void ez_interrupt( int irq, void * dev_id, struct pt_regs * regs);
static void ez_pseudo( void *data);
static void ez_timer_int( unsigned long data);
static void do_ez_read_drq( void );
static void do_ez_write_done( void );

static struct hd_struct ez[EZ_PARTNS];
static int ez_sizes[EZ_PARTNS];
static int ez_blocksizes[EZ_PARTNS];

static int	ez_base = EZ_BASE;
static int      ez_irq = EZ_IRQ;
static int	ez_rep = EZ_REP;
static int	ez_nybble = 0;		/* force 4-bit mode ? */

static int ez_valid = 0;		/* OK to open */
static int ez_access = 0;		/* count of active opens ... */
static int ez_changed = 0;		/* Did we see new media on open ? */
static int ez_capacity = 512*16*32;     /* Size of this volume in sectors */
static int ez_heads = 16;		/* physical geometry */
static int ez_sectors = 32;
static int ez_mode = 1;			/* 4- or 8-bit mode */
static int ez_loops = 0;		/* counter for pseudo-interrupts */
static int ez_timeout = 0;		/* did the interrupt time out ? */
static int ez_int_seen = 0;		/* have we ever seen an interrupt ? */
static int ez_busy = 0;			/* request being processed ? */
static int ez_block;			/* address of next requested block */
static int ez_count;			/* number of blocks still to do */
static char * ez_buf;			/* buffer for request in progress */
static char ez_scratch[512];		/* scratch block buffer */
static void (*ez_continuation)(void);	/* i/o completion handler */

char	*ez_errs[17] = { "ERR","INDEX","ECC","DRQ","SEEK","WRERR",
			 "READY","BUSY","AMNF","TK0NF","ABRT","MCR",
			 "IDNF","MC","UNC","???","TMO"};

static struct tq_struct ez_tq = {0,0,ez_pseudo,NULL};
static struct timer_list ez_timer = {0,0,0,0,ez_timer_int};
static struct wait_queue *ez_wait_open = NULL;

/* kernel glue structures */

static struct gendisk ez_gendisk = {
	MAJOR_NR,	/* Major number */
	"ez",		/* Major name */
	EZ_BITS,	/* Bits to shift to get real from partition */
	EZ_PARTNS,      /* Number of partitions per real */
	1,		/* maximum number of real */
	ez_geninit,	/* init function */
	ez,		/* hd struct */
	ez_sizes,	/* block sizes */
	0,		/* number */
        NULL,		/* internal */
	NULL		/* next */
};

static struct file_operations ez_fops = {
	NULL,			/* lseek - default */
	block_read,		/* read - general block-dev read */
	block_write,		/* write - general block-dev write */
	NULL,			/* readdir - bad */
	NULL,			/* select */
	ez_ioctl,		/* ioctl */
	NULL,			/* mmap */
	ez_open,		/* open */
	ez_release,		/* release */
	block_fsync,		/* fsync */
	NULL,			/* fasync */
	ez_check_media,         /* media change ? */
	ez_revalidate		/* revalidate new media */
};

__initfunc(int ez_init (void))	/* preliminary initialisation */

{	
	if (register_blkdev(MAJOR_NR,"ez",&ez_fops)) {
		printk("ez_init: unable to get major number %d\n",MAJOR_NR);
		return -1;
	}
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	read_ahead[MAJOR_NR] = 8;	/* 8 sector (4kB) read ahead */
	ez_gendisk.next = gendisk_head;
	gendisk_head = &ez_gendisk;

	return 0;
}

__initfunc(static void ez_geninit (struct gendisk *ignored))    /* real init */

{	int i;

	ez_gendisk.nr_real = 0;
	
	if (ez_detect()) {
		ez_busy = 0;
		ez_valid = 1;
		ez_gendisk.nr_real = 1;
		ez[0].nr_sects = ez_capacity;
		for(i=0;i<EZ_PARTNS;i++) ez_blocksizes[i] = 1024;
		blksize_size[MAJOR_NR] = ez_blocksizes;
	} 
#ifdef MODULE
	  else cleanup_module();
#endif
}

static int ez_open (struct inode *inode, struct file *file)

{	int dev = DEVICE_NR(inode->i_rdev);

	if (dev >= ez_gendisk.nr_real) return -ENODEV;

	MOD_INC_USE_COUNT;

	while (!ez_valid) sleep_on(&ez_wait_open);
	ez_access++;
	ez_media_check();
	ez_doorlock(IDE_DOORLOCK);
	return 0;
}

static void do_ez_request (void)

{       int	dev;

	if (ez_busy) return;
repeat:
	if ((!CURRENT) || (CURRENT->rq_status == RQ_INACTIVE)) return;
	INIT_REQUEST;

	dev = MINOR(CURRENT->rq_dev);
	ez_block = CURRENT->sector;
	ez_count = CURRENT->nr_sectors;

	if ((dev >= EZ_PARTNS) || ((ez_block+ez_count) > ez[dev].nr_sects)) {
		end_request(0);
		goto repeat;
	}

	ez_block += ez[dev].start_sect;
	ez_buf = CURRENT->buffer;

	if (CURRENT->cmd == READ) do_ez_read();
	else if (CURRENT->cmd == WRITE) do_ez_write();
	else {  end_request(0);
		goto repeat;
	}
}

static int ez_ioctl(struct inode *inode,struct file *file,
		    unsigned int cmd, unsigned long arg)

{	struct hd_geometry *geo = (struct hd_geometry *) arg;
	int dev, err;

	if ((!inode) || (!inode->i_rdev)) return -EINVAL;
	dev = MINOR(inode->i_rdev);
	if (dev >= EZ_PARTNS) return -EINVAL;

	switch (cmd) {
	    case HDIO_GETGEO:
		if (!geo) return -EINVAL;
		err = verify_area(VERIFY_WRITE,geo,sizeof(*geo));
		if (err) return err;
		put_user(ez_capacity/(EZ_LOG_HEADS*EZ_LOG_SECTS),
			 (short *) &geo->cylinders);
		put_user(EZ_LOG_HEADS, (char *) &geo->heads);
		put_user(EZ_LOG_SECTS, (char *) &geo->sectors);
	        put_user(ez[dev].start_sect,(long *)&geo->start);
		return 0;
	    case BLKRASET:
		if(!suser()) return -EACCES;
		if(!(inode->i_rdev)) return -EINVAL;
		if(arg > 0xff) return -EINVAL;
		read_ahead[MAJOR(inode->i_rdev)] = arg;
		return 0;
            case BLKRAGET:
		if (!arg) return -EINVAL;
		err = verify_area(VERIFY_WRITE,(long *) arg,sizeof(long));
		if (err) return (err);
		put_user(read_ahead[MAJOR(inode->i_rdev)],(long *) arg);
		return (0);
	    case BLKGETSIZE:
		if (!arg) return -EINVAL;
		err = verify_area(VERIFY_WRITE,(long *) arg,sizeof(long));
		if (err) return (err);
		put_user(ez[dev].nr_sects,(long *) arg);
		return (0);
	    case BLKFLSBUF:
		if(!suser())  return -EACCES;
		if(!(inode->i_rdev)) return -EINVAL;
		fsync_dev(inode->i_rdev);
	        invalidate_buffers(inode->i_rdev);
		return 0;
	    case BLKRRPART:
		return ez_revalidate(inode->i_rdev);
	    RO_IOCTLS(inode->i_rdev,arg);
	    default:
	        return -EINVAL;
	}
}

static int ez_release (struct inode *inode, struct file *file)

{	kdev_t devp;

	devp = inode->i_rdev;
	if (DEVICE_NR(devp) == 0)  {
		fsync_dev(devp);
		invalidate_inodes(devp);
		invalidate_buffers(devp);
		ez_access--;
		if (!ez_access) ez_doorlock(IDE_DOORUNLOCK);
		MOD_DEC_USE_COUNT;
	}
	return 0;
}

static int ez_check_media( kdev_t dev)

{       int	t;

	t = ez_changed;
	ez_changed = 0;
	return t;
}

static int ez_revalidate(kdev_t dev)

{	int p;
	long flags;
	kdev_t devp;

	save_flags(flags);
	cli(); 
	if (ez_access > 1) {
		restore_flags(flags);
		return -EBUSY;
	}
	ez_valid = 0;
	restore_flags(flags);	

	for (p=(EZ_PARTNS-1);p>=0;p--) {
		devp = MKDEV(MAJOR_NR, p);
		fsync_dev(devp);
		invalidate_inodes(devp);
		invalidate_buffers(devp);
		ez[p].start_sect = 0;
		ez[p].nr_sects = 0;
	}

	ez_get_capacity();
	ez[0].nr_sects = ez_capacity;
	resetup_one_dev(&ez_gendisk,0);

	ez_valid = 1;
	wake_up(&ez_wait_open);

	return 0;
}

#ifdef MODULE

/* Glue for modules ... */

void	cleanup_module(void);

int	init_module(void)

{	int	err;
	long    flags;

	save_flags(flags);
	cli();

	err = ez_init();
	if (err) {
	    restore_flags(flags);
	    return err;
	}
	ez_geninit(&ez_gendisk);

	if (!ez_gendisk.nr_real) {
		restore_flags(flags);
		return -1;
	}

	ez_valid = 0;
	resetup_one_dev(&ez_gendisk,0);
	ez_valid = 1;

	restore_flags(flags);
	return 0;
}

void	cleanup_module(void)

{	struct gendisk **gdp;
	long flags;

	save_flags(flags);
	cli();

	unregister_blkdev(MAJOR_NR,"ez");

	for(gdp=&gendisk_head;*gdp;gdp=&((*gdp)->next))
		if (*gdp == &ez_gendisk) break;
	if (*gdp) *gdp = (*gdp)->next;

	if (ez_gendisk.nr_real) {
		release_region(ez_base,3);
		if (ez_irq) free_irq(ez_irq,NULL);
	}

	restore_flags(flags);
}

#else 

/* ez_setup:  process lilo command parameters ...

   syntax:	ez=base[,irq[,rep[,nybble]]]
*/

__initfunc(void ez_setup(char *str, int *ints))

{       if (ints[0] > 0) ez_base = ints[1];
        if (ints[0] > 1) ez_irq = ints[2];
        if (ints[0] > 2) ez_rep = ints[3];
        if (ints[0] > 3) ez_nybble = ints[4];
} 

#endif

/* Now the actual hardware interface to the EZ135p */

static void    out_p( short port, char byte)

{       int i;

	for(i=0;i<ez_rep;i++) outb(byte,ez_base+port);
}

static int     in_p( short port)

{       int i;
	char c;

	c=inb(ez_base+port);
	for(i=1;i<ez_rep;i++) c=inb(ez_base+port);
	return c & 0xff;
}

#define w0(byte)  out_p(0,byte)
#define w2(byte)  out_p(2,byte)
#define r0()      (in_p(0) & 0xff)
#define r1()      (in_p(1) & 0xff)

/*  register access functions */

static int read_regr( char regr )

{	int h, l;

	if (ez_mode == 1) {	/* nybble mode */
	    w0(regr);
	    w2(1); w2(3);
	    l = r1() >> 4;
	    w2(4);
	    h = r1() & 0xf0;
	    return h + l;
	} else {		/* byte mode */
	    w0(regr+0x20);
	    w2(1); w2(0x25);
	    h = r0();
	    w2(4);
	    return h;
	}
}	

static void write_regr( char regr, char val )

{	w0(regr);
	w2(1);
	w0(val);
	w2(4);
}

/* connect / disconnect code */

static void prefix( char byte )

{	w2(4); w0(0x22); w0(0xaa); w0(0x55); w0(0); 
	w0(0xff); w0(0x87); w0(0x78); w0(byte);
        w2(5); w2(4); w0(0xff); 
}

static void connect ( void  )

{	prefix(0x40); prefix(0x50); prefix(0xe0);
        w0(0); w2(1); w2(4);
	read_regr(0xd);
	write_regr(0x6d,0xe8);
	write_regr(0x6c,0x1c);
	write_regr(0x72,0x10);
	write_regr(0x6a,0x38);
	write_regr(0x68,0x10);
	read_regr(0x12);
	write_regr(0x72,0x10);
	read_regr(0xd);
	write_regr(0x6d,0xaa);
	write_regr(0x6d,0xaa);
}

static void disconnect ( void )

{	read_regr(0xd);
	write_regr(0x6d,0xa8);
	prefix(0x30);
} 

/* basic i/o */

static void read_block( char * buf )

/* the nybble mode read has a curious optimisation in it: there are actually
   five bits available on each read.  The extra bit is used to signal that
   the next nybble is identical ...  I wonder how much research went into
   designing this use of the extra bit ?
*/

{	int	j, k, n0, n1, n2, n3;

	read_regr(0xd); write_regr(0x6d,0xe9);

	j = 0;
	if (ez_mode == 1) {		/* nybble mode */

	    w0(7); w2(1); w2(3); w0(0xff);
	    for(k=0;k<256;k++) {
		w2(6); n0 = r1();
		if (n0 & 8) n1 = n0; else { w2(4); n1 = r1(); }
		w2(7); n2 = r1();
		if (n2 & 8) n3 = n2; else { w2(5); n3 = r1(); }
		buf[j++] = (n0 >> 4) + (n1 & 0xf0);
		buf[j++] = (n2 >> 4) + (n3 & 0xf0);
	    }

	} else {			/* byte mode */

	    w0(0x27); w2(1); w2(0x25); w0(0);
	    for(k=0;k<256;k++) {
		w2(0x24); buf[j++] = r0();
       		w2(0x25); buf[j++] = r0();
	    }
	    w2(0x26); w2(0x27); w0(0); w2(0x25); w2(4);

	}
}

static void write_block( char * buf )

{	int	j;

	read_regr(0xd); write_regr(0x6d,0xe9);

	w0(0x67); w2(1); w2(5);
	for(j=0;j<256;j++) {
		w0(buf[2*j]); w2(4);
		w0(buf[2*j+1]); w2(5);
	}
	w2(7); w2(4);
}

/*  ide command interface */

void	ez_print_error( char * msg, int status )

{	char	*e, *p;
        int	i;

	e = ez_scratch;
	for(i=0;i<18;i++) if (status & (1<<i)) {
		p = ez_errs[i];
		while ((*e++=*p++));
		*(e-1) = ' ';
	}
	if (status) e--;
	*e = 0;
	printk("ez: %s: status = 0x%x (%s)\n",msg,status,ez_scratch);
}

static int wait_for( int w, char * msg )    /* polled wait */

{	int	k, r, e;

	k=0;
	while(k < EZ_SPIN) { 
	    r = read_regr(0x1f);
            k++;
	    if (ez_timeout) break;
	    if (((r & w) == w) && !(r & STAT_BUSY)) break;
	    EZ_DELAY;
	}
	e = (read_regr(0x19)<<8) + r;
	if ((k >= EZ_SPIN) || ez_timeout) e |= (ERR_TMO|STAT_ERR);
	if ((e & STAT_ERR) & (msg != NULL)) ez_print_error(msg,e);
	return e;
}

static void send_command( int n, int s, int h, int c0, int c1, int func )

{
	read_regr(0xd); write_regr(0x6d,0xa9);

	write_regr(0x76,0);		
	write_regr(0x79,0);	/* the IDE task file */
	write_regr(0x7a,n);
	write_regr(0x7b,s);
	write_regr(0x7c,c0);
	write_regr(0x7d,c1);
	write_regr(0x7e,0xa0+h);
	write_regr(0x7f,func);

	udelay(1);
}

static void ez_ide_command( int func, int block )

{	int c1, c0, h, s;

	s  = ( block % ez_sectors) + 1;
	h  = ( block / ez_sectors) % ez_heads;
	c0 = ( block / (ez_sectors*ez_heads)) % 256;
	c1 = ( block / (ez_sectors*ez_heads*256));

	send_command(1,s,h,c0,c1,func);
}

static void ez_gate_intr( int flag )

{	if (flag) write_regr(0x6d,0x39);  /* gate interrupt line to bus */
     	if (flag && ez_irq) w2(0x14);	  /* enable IRQ */
	if (!flag) w2(4);	          /* disable IRQ */
}

static int check_int( void )	/* is the interrupt bit set  ?  */

{	return (r1() & 0x40);
}

static void ez_doorlock( int func )

{	connect();
	if (wait_for(STAT_READY,"Lock") & STAT_ERR) {
		disconnect();
		return;
	}
	ez_ide_command(func,0);
	wait_for(STAT_READY,"Lock done");
	disconnect();
}

/* ez_media_check: check for and acknowledge the MC flag */

__initfunc(static void ez_media_check( void ))

{	int r;

	ez_changed = 0;
	connect();
	r = wait_for(STAT_READY,"Media check ready");
	if (!(r & STAT_ERR)) {
		ez_ide_command(IDE_READ,0);  /* try to read block 0 */
		r = wait_for(STAT_DRQ,"Media check");
		if (!(r & STAT_ERR)) read_block(ez_scratch);
	} else ez_changed = 1;   /* say changed if other error */
	if (r & ERR_MC) {
		ez_changed = 1;
		ez_ide_command(IDE_ACKCHANGE,0);
		wait_for(STAT_READY,"Ack. media change");
	}
	disconnect();
}

__initfunc(static int ez_identify( void ))


{	int	k, r;

	connect();
	wait_for(0,NULL);  /* wait until not busy, quietly */
	ez_ide_command(IDE_IDENTIFY,0);

	if (ez_irq) {	  		/* check that the interrupt works */
		ez_gate_intr(1);
		k = 0;
		while ((k++ < EZ_ISPIN) && !ez_int_seen) EZ_DELAY;
		ez_gate_intr(0);
		r = read_regr(0x1f);
		if ((!ez_int_seen) || !(r & STAT_DRQ)) {
			free_irq(ez_irq,NULL);
			ez_irq = 0;
		}
	}

        if (wait_for(STAT_DRQ,NULL) & STAT_ERR) {
		disconnect();
		return 0;
	}
	read_block(ez_scratch);
	disconnect();
	return 1;
}

#define  word_val(n) 	(ez_scratch[2*n]+256*ez_scratch[2*n+1])

__initfunc(static void ez_get_capacity( void ))

{	int	ez_cylinders;

	connect();
	wait_for(0,NULL);
	ez_ide_command(IDE_IDENTIFY,0);
	if (wait_for(STAT_DRQ,"Get capacity") & STAT_ERR) {
		disconnect();
		return;
	}
	read_block(ez_scratch);
	disconnect();
	ez_sectors = word_val(6);
	ez_heads = word_val(3);
	ez_cylinders  = word_val(1);
	ez_capacity = ez_sectors*ez_heads*ez_cylinders;
	printk("ez: Capacity = %d, (%d/%d/%d)\n",ez_capacity,ez_cylinders,
		ez_heads,ez_sectors);
}

__initfunc(static void ez_standby_off( void ))

{	connect();
	wait_for(0,NULL);
	send_command(0,0,0,0,0,IDE_STANDBY);
	wait_for(0,NULL);
	disconnect();
}

__initfunc(static int ez_port_check( void )) 	/* check for 8-bit port */

{	int	r;

        w2(0); 
	w0(0x55); if (r0() != 0x55) return 0;
	w0(0xaa); if (r0() != 0xaa) return 0;
	w2(0x20); w0(0x55); r = r0(); w0(0xaa);
	if (r0() == r) return 2;
	if (r0() == 0xaa) return 1;
	return 0;
}

__initfunc(static int ez_detect( void ))

{	int j, k;
	char sig[EZ_SIGLEN] = EZ_SIG;
	char id[EZ_ID_LEN+1];
	long	flags;

	if (check_region(ez_base,3)) {
		printk("ez: Ports at 0x%x are not available\n",ez_base);
		return 0;
	}

	ez_mode = ez_port_check();
	if (!ez_mode) {
		printk("ez: No parallel port at 0x%x\n",ez_base);
		return 0;
	}

	if (ez_irq && request_irq(ez_irq,ez_interrupt,0,"ez",NULL)) ez_irq = 0;

	if (ez_nybble) ez_mode = 1;
	    
	request_region(ez_base,3,"ez");

	save_flags(flags);
	sti();

	k = 0;
	if (ez_identify()) {
		k = 1;
		for(j=0;j<EZ_SIGLEN;j++) 
		   k &= (ez_scratch[j+EZ_SIGOFF] == sig[j]);
	}
	if (k) { 
	    for(j=0;j<EZ_ID_LEN;j++) id[j^1] = ez_scratch[j+EZ_SIGOFF];
	    id[EZ_ID_LEN] = 0;
	    if (!ez_irq) printk("ez %s: %s at 0x%x, %d-bit mode.\n",
				EZ_VERSION,id,ez_base,4*ez_mode);
	    else printk("ez %s: %s at 0x%x, IRQ %d, %d-bit mode.\n",
                        EZ_VERSION,id,ez_base,ez_irq,4*ez_mode);
	    ez_standby_off();
	    ez_media_check();
	    ez_get_capacity();
	    restore_flags(flags);
	    return 1;
	}
	restore_flags(flags);
	release_region(ez_base,3);
	if (ez_irq) free_irq(ez_irq,NULL);
	printk("ez: Drive not detected\n");
	return 0;
}

/* interrupt management */

static void ez_set_intr( void (*continuation)(void) )

{	ez_continuation = continuation;
	ez_loops = 1;  ez_timeout = 0;
	ez_gate_intr(1);
	if (ez_irq) {
		ez_timer.expires = jiffies + EZ_TMO;
		add_timer(&ez_timer);
	} else queue_task(&ez_tq,&tq_scheduler); 	
}

static void ez_pseudo( void *data )

{	void (*con)(void);

	ez_timeout = (ez_loops >= EZ_TMO);
	if (check_int() || ez_timeout) {
		con = ez_continuation;
		ez_continuation = NULL;
		if (con) con();
	} else {	
		ez_loops++;
	        queue_task(&ez_tq,&tq_scheduler);
	}
}

static void ez_timer_int( unsigned long data)

{	void  (*con)(void);

	con = ez_continuation;
	if (!con) return;
	ez_continuation = NULL;
	ez_gate_intr(0);
	ez_timeout = 1;
	con();
}

static void ez_interrupt( int irq, void * dev_id, struct pt_regs * regs)

{	void  (*con)(void);

	ez_int_seen = 1;
	con = ez_continuation;
	if (!con) return;
	ez_gate_intr(0);
	del_timer(&ez_timer);
	ez_continuation = NULL;
	con();
}

/* The i/o request engine */

#define EZ_DONE(s) { disconnect(); end_request(s); ez_busy = 0;\
		     cli(); do_ez_request(); return; }

static void do_ez_read( void )

{	ez_busy = 1;
	if (!ez_count) {
		ez_busy = 0;
		return;
	}
	sti();
	connect();
	if (wait_for(STAT_READY,"do_ez_read") & STAT_ERR) EZ_DONE(0);
	ez_ide_command(IDE_READ,ez_block);
	ez_set_intr(do_ez_read_drq);
}

static void do_ez_read_drq( void )

{	sti();
	if (wait_for(STAT_DRQ,"do_ez_read_drq") & STAT_ERR) EZ_DONE(0);
	read_block(ez_buf);
	ez_count--;
	if (ez_count) {
		ez_buf += 512;
		ez_block++;
		disconnect();
		do_ez_read();
		return;
	}
	EZ_DONE(1);
}

static void do_ez_write( void )

{	ez_busy = 1;
	if (!ez_count) {
		ez_busy = 0;
		return;
	}
	sti();
	connect();
	if (wait_for(STAT_READY,"do_ez_write") & STAT_ERR) 
	   EZ_DONE(0);
	ez_ide_command(IDE_WRITE,ez_block);
	if (wait_for(STAT_DRQ,"do_ez_write_drq") & STAT_ERR) 
	   EZ_DONE(0);
	write_block(ez_buf);
	ez_set_intr(do_ez_write_done);
}

static void do_ez_write_done( void )

{	sti();
	if (wait_for(STAT_READY,"do_ez_write_done") & STAT_ERR) EZ_DONE(0);
	ez_count--;
	if (ez_count) {
		ez_buf += 512;
		ez_block++;
		disconnect();
		do_ez_write();
		return;
	}
	EZ_DONE(1);
}

/* end of ez.c */
