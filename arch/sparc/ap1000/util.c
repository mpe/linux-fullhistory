  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/* general utility functions for the AP1000 */

#include <linux/sched.h>
#include <asm/ap1000/apservice.h>
#include <asm/ap1000/apreg.h>
#include <asm/asi.h>
#include <asm/delay.h>
#include <asm/pgtable.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/mpp.h>

#define APLOG 0

struct cap_init cap_init;

/* find what cell id we are running on */
int mpp_cid(void)
{
  return(BIF_IN(BIF_CIDR1));
}

/* find how many cells there are */
int mpp_num_cells(void)
{
  return(cap_init.numcells);
}

/* this can be used to ensure some data is readable before DMAing
   it. */
int ap_verify_data(char *d,int len)
{
	int res = 0;
	while (len--) res += *d++;
	return res;
}

/* How many BogoMIPS in the entire machine
Don't worry about float because when it gets this big, it's irrelevant */
int mpp_agg_bogomips(void)
{
  return mpp_num_cells()*loops_per_sec/500000; /* cheat in working it out */
}

/* Puts multiprocessor configuration info into a buffer */
int get_mppinfo(char *buffer)
{
  return sprintf(buffer,
		 "Machine Type:\t\t: %s\nNumber of Cells\t\t: %d\nAggregate BogoMIPS\t: %d\n",
		 "Fujitsu AP1000+",
		 mpp_num_cells(),
		 mpp_agg_bogomips());
}

#if APLOG
static int do_logging = 0;


void ap_log(char *buf,int len)
{
#define LOG_MAGIC 0x8736526
	static char *logbase;
	static char *logptr;
	static int logsize = 1024;
	int l,i;

	if (buf == NULL && len == -1) {
		logbase = kmalloc(logsize + 8,GFP_ATOMIC);
		
		if (!logbase) {
			printk("log init failed\n");
			return;
		}
		for (i=0;i<logsize;i++)
			if (logbase[8+i] == '|')
				logbase[8+i] = '_';

		if ((*(int *)logbase) == LOG_MAGIC) {
			int oldoffset = *(int *)(logbase + 4);
			printk("==%3d== START OLD LOG ==\n",mpp_cid());
			ap_write(1,logbase + 8 + oldoffset,logsize - oldoffset);
			ap_write(1,logbase+8,oldoffset);
			printk("==%3d== END OLD LOG ==\n",mpp_cid());
		}
		*(int *)logbase = LOG_MAGIC;
		*(int *)(logbase+4) = 0;
		logbase += 8;
		logptr = logbase;
		memset(logbase,0,logsize);
		do_logging = 1;
		return;
	}

	if (!do_logging) return;

	while (len) {
		l = logsize - (logptr - logbase);
		if (l > len) l = len;
		memcpy(logptr,buf,l);
		len -= l;
		logptr += l;
		if (logptr == logbase + logsize) 
			logptr = logbase;
	}
	*(int *)(logbase - 4) = (logptr - logbase);
}
#endif

int ap_current_uid = -1;

/* set output only to a particular uid */
void ap_set_user(int uid)
{
	ap_current_uid = uid;
}

/* write some data to a filedescriptor on the front end */
int ap_write(int fd,char *buf,int nbytes)
{
	struct cap_request req;

	if (nbytes == 0) return 0;

#if APLOG
	ap_log(buf,nbytes);
	
	if (buf[0] == '|') return nbytes;
#endif

	req.cid = mpp_cid();
	req.type = REQ_WRITE;
	req.size = nbytes + sizeof(req);
	req.data[0] = fd;
	if (ap_current_uid == -1 && current && current->pid) {
		req.data[1] = current->uid;
	} else {
		req.data[1] = ap_current_uid;
	}
	req.header = MAKE_HEADER(HOST_CID);
	
	bif_queue(&req,buf,nbytes);

	return(nbytes);
}

/* write one character to stdout on the front end */
int ap_putchar(char c)
{
  struct cap_request req;

#if APLOG
  ap_log(&c,1);
#endif

  req.cid = mpp_cid();
  req.type = REQ_PUTCHAR;
  req.size = sizeof(req);
  req.data[0] = c;
  req.header = MAKE_HEADER(HOST_CID);

  bif_queue(&req,0,0);

  return(0);
}

/* start the debugger (kgdb) on this cell */
void ap_start_debugger(void)
{
	static int done = 0;
	extern void set_debug_traps(void);
	extern void breakpoint(void);
	if (!done)
		set_debug_traps();
	done = 1;
	breakpoint();
}

void ap_panic(char *msg,int a1,int a2,int a3,int a4,int a5)
{
	ap_led(0xAA);
	printk(msg,a1,a2,a3,a4,a5);
	ap_start_debugger();
}

void ap_printk(char *msg,int a1,int a2,int a3,int a4,int a5)
{
  printk(msg,a1,a2,a3,a4,a5);
  /* bif_queue_flush(); */
}

/* get the command line arguments from the front end */
void ap_getbootargs(char *buf)
{
    struct cap_request req;
    int size;

    req.cid = mpp_cid();
    req.type = REQ_GETBOOTARGS;
    req.size = sizeof(req);
    req.header = MAKE_HEADER(HOST_CID);
    
    write_bif_polled((char *)&req,sizeof(req),NULL,0);

    ap_wait_request(&req,REQ_GETBOOTARGS); 

    size = req.size - sizeof(req);
    if (size == 0)
      buf[0] = '\0';
    else {
      read_bif(buf, size);
    }

    req.cid = mpp_cid();
    req.type = REQ_INIT;
    req.size = sizeof(req);
    req.header = MAKE_HEADER(HOST_CID);

    write_bif_polled((char *)&req,sizeof(req),NULL,0);

    ap_wait_request(&req,REQ_INIT);

    if (req.size != sizeof(req))
      read_bif((char *)&cap_init,req.size - sizeof(req));
    if ((req.size - sizeof(req)) != sizeof(cap_init))
      printk("WARNING: Init structure is wrong size, recompile util.c\n");

    if (cap_init.gdbcell == mpp_cid())
      ap_start_debugger();

    printk("Got command line arguments from server\n");
}

/* a useful utility for debugging pagetable setups */
void show_mapping_ctx(unsigned *ctp,int context,unsigned Vm)
{  
  unsigned *pgtable;
  int entry[3];
  int level = 0;

  if (!ctp) ctp = (unsigned *)mmu_p2v(srmmu_get_ctable_ptr());

  printk("ctp=0x%x ",(int)ctp);

  pgtable = ctp + context;

  /* get the virtual page */
  Vm = Vm>>12;

  printk("Vm page 0x%x is ",Vm);  

  entry[0] = Vm>>12;
  entry[1] = (Vm>>6) & 0x3f;
  entry[2] = Vm & 0x3f;

  while (1) {

#if 1
    printk("(%08x) ",pgtable[0]);
#endif

  if ((pgtable[0] & 3) == 2) {
    printk("mapped at level %d to 0x%x\n",level,pgtable[0]>>8);
    return;
  }

  if ((pgtable[0] & 3) == 0) {
    printk("unmapped at level %d\n",level);
    return;
  }

  if ((pgtable[0] & 3) == 3) {
    printk("invalid at level %d\n",level);
    return;
  }

  if ((pgtable[0] & 3) == 1) {
    pgtable = (unsigned *)(((pgtable[0]>>2)<<6)|0xf0000000);
    pgtable += entry[level];
    level++;
  }
  } 
}



static unsigned char current_led = 0;

void ap_led(unsigned char d)
{
  unsigned paddr = 0x1000;
  unsigned word = 0xff & ~d;
  current_led = d;
  __asm__ __volatile__("sta %0, [%1] %2\n\t" : :
		       "r" (word), "r" (paddr), "i" (0x2c) :
		       "memory");
}

void ap_xor_led(unsigned char d)
{
	ap_led(current_led ^ d);
}

void ap_set_led(unsigned char d)
{
	ap_led(current_led | d);
}

void ap_unset_led(unsigned char d)
{
	ap_led(current_led & ~d);
}


void kbd_put_char(char c)
{
  ap_putchar(c);
}


void ap_enter_irq(int irq)
{
  unsigned char v = current_led;
  switch (irq) {
  case 2: v |= (1<<1); break;
  case 4: v |= (1<<2); break;
  case 8: v |= (1<<3); break;
  case 9: v |= (1<<4); break;
  case 10: v |= (1<<5); break;
  case 11: v |= (1<<6); break;
  default: v |= (1<<7); break;
  }
  ap_led(v);
}

void ap_exit_irq(int irq)
{
  unsigned char v = current_led;
  switch (irq) {
  case 2: v &= ~(1<<1); break;
  case 4: v &= ~(1<<2); break;
  case 8: v &= ~(1<<3); break;
  case 9: v &= ~(1<<4); break;
  case 10: v &= ~(1<<5); break;
  case 11: v &= ~(1<<6); break;
  default: v &= ~(1<<7); break;
  }
  ap_led(v);
}


static struct wait_queue *timer_wait = NULL;

static void wait_callback(unsigned long _ignored)
{
  wake_up(&timer_wait);
}

/* wait till x == *p */
int wait_on_int(volatile int *p,int x,int interval)
{
	struct timer_list *timer = kmalloc(sizeof(*timer),GFP_KERNEL);
	if (!timer) panic("out of memory in wait_on_int()\n");
	timer->next = NULL;
	timer->prev = NULL;
	timer->data = 0;
	timer->function = wait_callback;
	while (*p != x) {
		timer->expires = jiffies + interval;
		add_timer(timer);
		interruptible_sleep_on(&timer_wait);
		del_timer(timer);
		if (signal_pending(current))
			return -EINTR;
	}
	kfree_s(timer,sizeof(*timer));
	return 0;
}


/* an ugly hack to get nfs booting from a central cell to work */
void ap_nfs_hook(unsigned long server)
{
  unsigned cid = server - cap_init.baseIP;
  if (cid < cap_init.bootcid + cap_init.numcells &&
      cid != mpp_cid()) {
    unsigned end = jiffies + 20*HZ;
    /* we are booting from another cell */
    printk("waiting for the master cell\n");
    while (time_before(jiffies, end)) ;
    printk("continuing\n");
  }
}

/* convert a IP address to a cell id */
int ap_ip_to_cid(u_long ip)
{
  unsigned cid;

  if ((ip & cap_init.netmask) != (cap_init.baseIP & cap_init.netmask))
    return -1;

  if ((ip & ~cap_init.netmask) == AP_ALIAS_IP)
    cid = cap_init.bootcid;
  else
    cid = ip - cap_init.baseIP;
  if (cid >= cap_init.bootcid + cap_init.numcells)
    return -1;
  return cid;
}


void ap_reboot(char *bootstr)
{
	printk("cell(%d) - don't know how to reboot\n",mpp_cid());
	sti();
	while (1) ;
}


void dumb_memset(char *buf,char val,int len)
{
	while (len--) *buf++ = val;
}

void ap_init_time(struct timeval *xtime)
{
	xtime->tv_sec = cap_init.init_time;
	xtime->tv_usec = 0;
}
