/* 
	bpcd.c	(c) 1996  Grant R. Guenther <grant@torque.net>
		          Under the terms of the GNU public license.

	bpcd.c is a driver for the MicroSolutions "backpack" CDrom,
	an external parallel port device.  

	There are apparently two versions of the backpack protocol.  This 
        driver knows about the version 2 protocol - as is used in the 4x 
	and 6x products.  There is no support for the sound hardware that
        is included in some models.  It should not be difficult to add 
        support for the ATAPI audio play functions and the corresponding 
        ioctls.

	The driver was developed by reverse engineering the protocol
        and testing it on the backpack model 164550.  This model
	is actually a stock ATAPI drive packaged with a custom 
	ASIC that implements the IDE over parallel protocol.
	I tested with a backpack that happened to contain a Goldstar
        drive, but I've seen reports of Sony and Mitsumi drives as well.

	bpcd.c can be compiled for version 1.2.13 of the Linux kernel
        and for the 2.0 series.  (It should also work with most of the
        later 1.3 kernels.)

        If you have a copy of the driver that has not been integrated into
        the kernel source tree, you can compile the driver manually, as a 
        module.  Ensure that /usr/include/linux and /usr/include/asm are 
	links to the correct include files for the target system.  Compile 
        the driver with

                cc -D__KERNEL__ -DMODULE -O2 -c bpcd.c

        You must then load it with insmod.  If you are using
        MODVERSIONS, add the following to the cc command:

		-DMODVERSIONS -I /usr/include/linux/modversions.h

	Before attempting to access the new driver, you will need to
        create a new device special file.  The following commands will
	do that for you:

		mknod /dev/bpcd b 41 0
		chown root:disk /dev/bpcd
		chmod 660 /dev/bpcd

	Afterward, you can mount a disk in the usual way:

		mount -t iso9660 /dev/bpcd /cdrom

	(assuming you have made a directory /cdrom to use as a
        mount point).

	The driver will attempt to detect which parallel port your
        backpack is connected to.  If this fails for any reason, you
        can override it by specifying a port on the LILO command line
        (for built in drivers) or the insmod command (for drivers built
        as modules).   If your drive is on the port at 0x3bc, you would
        use one of these commands:

		LILO:	   bpcd=0x3bc

		insmod:    insmod bpcd bp_base=0x3bc

	The driver can detect if the parallel port supports 8-bit
        transfers.  If so, it will use them.  You can force it to use
        4-bit (nybble) mode by setting the variable bp_nybble to 1 on
        an insmod command, or using the following LILO parameters:

		bpcd=0x3bc,1

	(you must specify the correct port address if you use this method.)

	There is currently no support for EPP or ECP modes.  Also,
        as far as I can tell, the MicroSolutions protocol does not
        support interrupts in the 4-bit and 8-bit modes.

	MicroSolutions' protocol allows for several drives to be
        chained together off the same parallel port.  Currently, this
        driver will recognise only one of them.  If you do have more
        than one drive, it will choose the one with the lowest id number,
        where the id number is the last two digits of the product's
        serial number.

	It is not currently possible to connect a printer to the chained
        port on the BackPack and expect Linux to use both devices at once.

	If you need to use this driver together with a printer on the
        same port, build both the bpcd and lp drivers are modules.

	Keep an eye on http://www.torque.net/bpcd.html for news and
        other information about the driver.  If you have any problems
        with this driver, please send me, grant@torque.net, some mail 
        directly before posting into the newsgroups or mailing lists.

*/

#define	BP_VERSION	"0.14" 

#define	BP_BASE		0	/* set to 0 for autoprobe */
#define BP_REP		4

#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/kernel.h>
#include <linux/tqueue.h>
#include <linux/delay.h>
#include <linux/cdrom.h>
#include <linux/ioport.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#ifndef BPCD_MAJOR
#define BPCD_MAJOR   41	
#endif

#define MAJOR_NR BPCD_MAJOR

/* set up defines for blk.h,  why don't all drivers do it this way ? */

#define DEVICE_NAME "BackPack"
#define DEVICE_REQUEST do_bp_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#include <linux/blk.h>

#define BP_TMO		   300		/* timeout in jiffies */
#define BP_DELAY            50          /* spin delay in uS */

#define BP_SPIN		(10000/BP_DELAY)*BP_TMO

int bpcd_init(void);
void bpcd_setup(char * str, int * ints);
void cleanup_module( void );

static int 	bp_open(struct inode *inode, struct file *file);
static void 	do_bp_request(void);
static void 	do_bp_read(void);
static int 	bp_ioctl(struct inode *inode,struct file *file,
                         unsigned int cmd, unsigned long arg);
static void 	bp_release (struct inode *inode, struct file *file);

static int 	bp_detect(void);
static int      bp_lock(void);
static void     bp_unlock(void);
static void     bp_eject(void);
static void     bp_interrupt(void *data);

static int	bp_base = BP_BASE;
static int	bp_rep = BP_REP;
static int	bp_nybble = 0;		/* force 4-bit mode ? */

static int      bp_unit_id;

static int bp_access = 0;		/* count of active opens ... */
static int bp_mode = 1;			/* 4- or 8-bit mode */
static int bp_busy = 0;			/* request being processed ? */
static int bp_timeout;                  /* "interrupt" loop limiter */
static int bp_sector;			/* address of next requested sector */
static int bp_count;			/* number of blocks still to do */
static char * bp_buf;			/* buffer for request in progress */
static char bp_buffer[2048];		/* raw block buffer */
static int bp_bufblk = -1;		/* block in buffer, in CD units,
					   -1 for nothing there */
static  int	nyb_map[256];		/* decodes a nybble */
static	int	PortCache = 0;		/* cache of the control port */

static struct tq_struct bp_tq = {0,0,bp_interrupt,NULL}; 

/* kernel glue structures */

static struct file_operations bp_fops = {
	NULL,			/* lseek - default */
	block_read,		/* read - general block-dev read */
	block_write,		/* write - general block-dev write */
	NULL,			/* readdir - bad */
	NULL,			/* poll */
	bp_ioctl,		/* ioctl */
	NULL,			/* mmap */
	bp_open,		/* open */
	bp_release,		/* release */
	block_fsync,		/* fsync */
	NULL,			/* fasync */
	NULL,                   /* media change ? */
	NULL			/* revalidate new media */
};


/* the MicroSolutions protocol uses bits 3,4,5 & 7 of the status port to
   deliver a nybble in 4 bit mode.  We use a table lookup to extract the
   nybble value.  The following function initialises the table.
*/

static	void init_nyb_map( void )

{	int	i, j;

	for(i=0;i<256;i++) { 
	   j = (i >> 3) & 0x7;
	   if (i & 0x80) j |= 8;
	   nyb_map[i] = j;
	}
}

int bpcd_init (void)	/* preliminary initialisation */

{	init_nyb_map();

        if (bp_detect()) return -1;

	if (register_blkdev(MAJOR_NR,"bpcd",&bp_fops)) {
		printk("bpcd: unable to get major number %d\n",MAJOR_NR);
		return -1;
	}
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	read_ahead[MAJOR_NR] = 8;	/* 8 sector (4kB) read ahead */

	return 0;
}

static int bp_open (struct inode *inode, struct file *file)

{	
	if (file->f_mode & 2) return -EROFS;  /* wants to write ? */

	MOD_INC_USE_COUNT;

	if (!bp_access)
	   if (bp_lock()) {
		MOD_DEC_USE_COUNT;
		return -ENXIO;
	   }
	bp_access++;
	return 0;
}

static void do_bp_request (void)

{       if (bp_busy) return;
        while (1) {
	    if ((!CURRENT) || (CURRENT->rq_status == RQ_INACTIVE)) return;
	    INIT_REQUEST;
	    if (CURRENT->cmd == READ) {
	        bp_sector = CURRENT->sector;
	        bp_count = CURRENT->nr_sectors;
	        bp_buf = CURRENT->buffer;
	        do_bp_read();
		if (bp_busy) return;
	    } 
	    else end_request(0);
	}
}

static int bp_ioctl(struct inode *inode,struct file *file,
		    unsigned int cmd, unsigned long arg)

/* we currently support only the EJECT ioctl. */

{	switch (cmd) {
            case CDROMEJECT: if (bp_access == 1) {
				bp_eject();
				return 0;
			     }
	    default:
	        return -EINVAL;
	}
}

static void bp_release (struct inode *inode, struct file *file)

{	kdev_t	devp;

	bp_access--;
	if (!bp_access) { 
	    devp = inode->i_rdev;
	    fsync_dev(devp);
	    invalidate_inodes(devp);
	    invalidate_buffers(devp);
	    bp_unlock();
	}
	MOD_DEC_USE_COUNT;
}

#ifdef MODULE

/* Glue for modules ... */

int	init_module(void)

{	int	err;
	long    flags;

	save_flags(flags);
	cli();

	err = bpcd_init();

	restore_flags(flags);
	return err;
}

void	cleanup_module(void)

{	long flags;

	save_flags(flags);
	cli();
	unregister_blkdev(MAJOR_NR,"bpcd");
	release_region(bp_base,3);
	restore_flags(flags);
}

#else 

/* bpcd_setup:  process lilo command parameters ...

   syntax:	bpcd=base[,nybble[,rep]]
*/

void    bpcd_setup(char *str, int *ints)

{       if (ints[0] > 0) bp_base = ints[1];
        if (ints[0] > 1) bp_nybble = ints[2];
        if (ints[0] > 2) bp_rep = ints[3];
} 

#endif

static	void    out_p( short port, char byte)

{       int i;

	for(i=0;i<bp_rep;i++) outb(byte,bp_base+port);
}

static	int     in_p( short port)

{       int i;
	char c;

	for(i=0;i<bp_rep;i++) c=inb(bp_base+port);
	c=inb(bp_base+port);
	return c & 0xff;
}

/* Unlike other PP devices I've worked on, the backpack protocol seems
   to be driven by *changes* in the values of certain bits on the control
   port, rather than their absolute value.  Hence the unusual macros ...
*/

#define w0(byte)  		out_p(0,byte)
#define r0()      		(in_p(0) & 0xff)
#define w1(byte)  		out_p(1,byte)
#define r1()      		(in_p(1) & 0xff)
#define w2(byte)  		out_p(2,byte) ; PortCache = byte
#define t2(pat)   		PortCache ^= pat; out_p(2,PortCache)
#define e2()			PortCache &= 0xfe; out_p(2,PortCache)
#define o2()			PortCache |= 1; out_p(2,PortCache)

static int read_byte( void )

{	int l, h;

	t2(4); 
	if (bp_mode == 2) return r0();
	l = nyb_map[r1()];
	t2(4);
	h = nyb_map[r1()];
	return (h << 4) + l;
}

static int read_regr( char regr )

{       int r;

       	w0(regr & 0xf); w0(regr); t2(2);
	if (bp_mode == 2) { e2(); t2(0x20); }
	r = read_byte();
	if (bp_mode == 2) { t2(1); t2(0x20); }
	return r;
}	

static void write_regr( char regr, char val )

{	w0(regr);
	t2(2);
	w0(val);
	o2(); t2(4); t2(1);
}

static void write_cmd( char * cmd, int len )

{	int i, f;

	if (bp_mode == 2) f = 0x10; else f = 0;
	write_regr(4,0x40|f);
	w0(0x40); t2(2); t2(1);
	i = 0;
	while (i < len) {
	   w0(cmd[i++]); 
	   t2(4);
	}
	write_regr(4,f);
}

static void read_data( char * buf, int len )

{	int i, f;

	if (bp_mode == 2) f = 0x50; else f = 0x40;
	write_regr(4,f);
	w0(0x40); t2(2);
	if (bp_mode == 2) t2(0x20);
	for(i=0;i<len;i++) buf[i] = read_byte();
	if (bp_mode == 2) { t2(1); t2(0x20); }
}

static int probe( int id )

{	int l, h, t;
	int r = -1;

	w2(4); w2(0xe); w2(0xec); w1(0x7f);
	if ((r1() & 0xf8) != 0x78) return -1;
	w0(255-id); w2(4); w0(id);
	t2(8); t2(8); t2(8);
	t2(2); t = (r1() & 0xf8);
	if (t != 0x78) {
		l = nyb_map[t];
		t2(2); h = nyb_map[r1()];
		t2(8);
		r = 0;
	}
	w0(0); t2(2); w2(0x4c); w0(0x13);
	return r;
}
	
static void connect ( void  )

{	int	f;

	w0(0xff-bp_unit_id); w2(4); w0(bp_unit_id);
	t2(8); t2(8); t2(8); 
	t2(2); t2(2); t2(8);
	if (bp_mode == 2) f = 0x10; else f = 0;
	write_regr(4,f);
	write_regr(5,8);
	write_regr(0x46,0x10);
	write_regr(0x4c,0x38);
	write_regr(0x4d,0x88);
	write_regr(0x46,0xa0);
	write_regr(0x41,0);
	write_regr(0x4e,8);
}

static void disconnect ( void )

{	w0(0); t2(2); w2(0x4c);
	if (bp_mode == 2) w0(0xff); else  w0(0x13);
} 

static int bp_wait_drq( char * lab, char * fun )

{	int j, r, e;

	j = 0;

	while (1)  {
        	r = read_regr(0x47);
		e = read_regr(0x41);
		if ((r & 9) || (j++ >= BP_SPIN)) break;
		udelay(BP_DELAY);
	}

	if ((j >= BP_SPIN) || (r & 1)) {
	    if (lab && fun) 
		   printk("bpcd: %s (%s): %s status: %x error: %x\n",
			  lab,fun,(j >= BP_SPIN)?"timeout, ":"",r,e);
	    return -1;
	}
	return 0;
}

static int bp_wait( char * lab, char * fun )

{	int j, r, e;

	j = 0;
	while ((!(read_regr(0xb) & 0x80)) && (j++ < BP_SPIN)) udelay(BP_DELAY);
        r = read_regr(0x47);
	e = read_regr(0x41);
	if ((j >= BP_SPIN) || (r & 1)) {
	    if (lab && fun) 
		   printk("bpcd: %s (%s): %s status: %x error: %x\n",
			  lab,fun,(j >= BP_SPIN)?"timeout, ":"",r,e);
	    return -1;
	}
	return 0;
}

static int bp_command( char * cmd, int dlen, char * fun )

{	int	r;

	connect();
	write_regr(0x44,dlen % 256);
	write_regr(0x45,dlen / 256);
	write_regr(0x46,0xa0);		/* drive 0 */
	write_regr(0x47,0xa0);		/* ATAPI packet command */
	if ((r=bp_wait_drq("bp_command",fun))) {
	   disconnect();
	   return r;
	}
	write_cmd(cmd,12);
	return 0;
}

static int bp_completion( char * fun )

{	int r, n;

	if (!(r=bp_wait("bp_completion",fun))) {
	    if (read_regr(0x42) == 2) {
	        n = (read_regr(0x44) + 256*read_regr(0x45));
	        read_data(bp_buffer,n);
	        r=bp_wait("transfer done",fun);
	    }
	}
	disconnect(); 
	return r;
}

static int bp_atapi( char * cmd, int dlen, char * fun )

{	int r;

	if (!(r=bp_command(cmd,dlen,fun)))
	  r = bp_completion(fun);
	return r;
}

static int bp_req_sense( char * msg )

{	char	rs_cmd[12] = { 0x03,0,0,0,18,0,0,0,0,0,0,0 };
	int r;

	r = bp_atapi(rs_cmd,18,"request sense");
	if (msg) printk("bpcd: %s:  sense key: %x, ASC: %x, ASQ: %x\n",msg,
			 bp_buffer[2]&0xf, bp_buffer[12], bp_buffer[13]);
	return r;
}

static int bp_lock(void)

{	char	lo_cmd[12] = { 0x1e,0,0,0,1,0,0,0,0,0,0,0 };
	char	cl_cmd[12] = { 0x1b,0,0,0,3,0,0,0,0,0,0,0 };

	bp_atapi(cl_cmd,0,"close door");
        if (bp_req_sense(NULL)) return -1;  /* check for disk */
	bp_atapi(lo_cmd,0,NULL);
	bp_req_sense(NULL);  /* in case there was a media change */
	bp_atapi(lo_cmd,0,"lock door");
	return 0;
}

static void bp_unlock( void)

{	char	un_cmd[12] = { 0x1e,0,0,0,0,0,0,0,0,0,0,0 };

	bp_atapi(un_cmd,0,"unlock door");
}

static void bp_eject( void)

{	char	ej_cmd[12] = { 0x1b,0,0,0,2,0,0,0,0,0,0,0 };

	bp_unlock();
	bp_atapi(ej_cmd,0,"eject");
}

static int bp_reset( void )

/* the ATAPI standard actually specifies the contents of all 7 registers
   after a reset, but the specification is ambiguous concerning the last
   two bytes, and different drives interpret the standard differently.
*/

{	int	i, flg;
	int	expect[5] = {1,1,1,0x14,0xeb};
	long	flags;

	connect();
	write_regr(0x46,0xa0);
	write_regr(0x47,8);

	save_flags(flags);
	sti();
	udelay(500000);  	/* delay 0.5 seconds */
	restore_flags(flags);

	flg = 1;
	for(i=0;i<5;i++) flg &= (read_regr(i+0x41) == expect[i]);
	
	disconnect();
	return flg-1;	
}

static int bp_identify( char * id )

{	int k;
	char   id_cmd[12] = {0x12,0,0,0,36,0,0,0,0,0,0,0};

	bp_bufblk = -1;
        if (bp_atapi(id_cmd,36,"identify")) return -1;
	for (k=0;k<16;k++) id[k] = bp_buffer[16+k];
	id[16] = 0;
	return 0;
}

static int bp_port_check( void ) 	/* check for 8-bit port */

{	int	r;

        w2(0); 
	w0(0x55); if (r0() != 0x55) return 0;
	w0(0xaa); if (r0() != 0xaa) return 0;
	w2(0x20); w0(0x55); r = r0(); w0(0xaa);
	if (r0() == r) return 2;
	if (r0() == 0xaa) return 1;
	return 0;
}

static int bp_locate( void )

{	int	k;

	for(k=0;k<100;k++) 
	  if (!probe(k)) {
		bp_unit_id = k;
		return 0;
	  }
	return -1;
}

static int bp_do_detect( int autop )

{	char   id[18];

	if (autop) bp_base = autop;

	if (check_region(bp_base,3)) {
		if (!autop) 
		  printk("bpcd: Ports at 0x%x are not available\n",bp_base);
		return -1;
	}

	bp_mode = bp_port_check();

	if (!bp_mode) {
		if (!autop)
		  printk("bpcd: No parallel port at 0x%x\n",bp_base);
		return -1;
	}

	if (bp_nybble) bp_mode = 1;

	if (bp_locate()) {
		if (!autop)
		  printk("bpcd: Couldn't find a backpack adapter at 0x%x\n",
			  bp_base);
		return -1;
	}

	if (bp_reset()) {
		if (!autop)
		  printk("bpcd: Failed to reset CD drive\n");
		return -1;
	}

	if (bp_identify(id)) {
		if (!autop)
		  printk("bpcd: ATAPI inquiry failed\n");
		return -1;
	}

	request_region(bp_base,3,"bpcd");

	printk("bpcd: Found %s, ID %d, using port 0x%x in %d-bit mode\n",
	       id,bp_unit_id,bp_base,4*bp_mode);

	return 0;
}

/* If you know about some other weird parallel port base address,
   add it here ....
*/

static int bp_detect( void )

{	if (bp_base) return bp_do_detect(0);
	if (!bp_do_detect(0x378)) return 0;
	if (!bp_do_detect(0x278)) return 0;
	if (!bp_do_detect(0x3bc)) return 0;
	printk("bpcd: Autoprobe failed\n");
	return -1;
}


static void bp_transfer( void )

{	int	k, o;

	while (bp_count && (bp_sector/4 == bp_bufblk)) {
		o = (bp_sector % 4) * 512;
		for(k=0;k<512;k++) bp_buf[k] = bp_buffer[o+k];
		bp_count--;
		bp_buf += 512;
		bp_sector++;
	}
}

static void do_bp_read( void )

{	int	b, i;
	char	rd_cmd[12] = {0xa8,0,0,0,0,0,0,0,0,1,0,0};

	bp_busy = 1;
	bp_transfer();
	if (!bp_count) {
		end_request(1);
		bp_busy = 0;
		return;
	}
	sti();

	bp_bufblk = bp_sector / 4;
        b = bp_bufblk;
	for(i=0;i<4;i++) { 
	   rd_cmd[5-i] = b & 0xff;
	   b = b >> 8;
	}

	if (bp_command(rd_cmd,2048,"read block")) {
		bp_bufblk = -1; 
		bp_busy = 0;
		cli();
		bp_req_sense("send read command");
		end_request(0);
		return;
	}
	bp_timeout = jiffies + BP_TMO;
	queue_task(&bp_tq,&tq_scheduler); 
}

static void bp_interrupt( void *data)

{	if (!(read_regr(0xb) & 0x80)) {
		if (jiffies > bp_timeout) {
			bp_bufblk = -1;
			bp_busy = 0;
			bp_req_sense("interrupt timeout");
			end_request(0);
			do_bp_request();
		}
		queue_task(&bp_tq,&tq_scheduler); 
		return;
	}
	sti();
	if (bp_completion("read completion")) {
		cli();
		bp_busy = 0;
		bp_bufblk = -1;
		bp_req_sense("read completion");
		end_request(0);
		do_bp_request();
	}
	do_bp_read();
	do_bp_request();
}
	
/* end of bpcd.c */
