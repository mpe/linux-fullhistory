  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/* this defines service requests that can be made by the cells of the 
   front end "bootap" server 

   tridge, March 1996
   */
#ifndef _APSERVICE_H
#define _APSERVICE_H
#ifdef __KERNEL__
#include <linux/sched.h>
#endif

#ifndef _ASM_

/* all requests start with this structure */
struct cap_request {
  unsigned header; /* for the hardware */
  int size; /* the total request size in bytes, including this header */
  int cid; /* the cell it came from */
  int type; /* the type of request */
  int data[4]; /* misc data */
};

/* Initialisation data to be sent to boot cell program */
struct cap_init {
  int bootcid;  /* base cid to boot */
  int numcells; /* number of cells */
  int physcells; /* physical number of cells */
  unsigned long baseIP; /* IP address of cell 0 */
  unsigned long netmask; /* netmask of cells net */
  int gdbcell; /* what cell is the debugger running on */
  unsigned init_time; /* time at startup */
};
#endif

/* what fake host number to use for the aliased IP device */
#define AP_ALIAS_IP 2

/* request types */
#define REQ_WRITE 0
#define REQ_SHUTDOWN 1
#define REQ_LOAD_AOUT 2
#define REQ_PUTCHAR 3
#define REQ_GETBOOTARGS 4       
#define REQ_PUTDEBUGCHAR 5      
#define REQ_GETDEBUGCHAR 6     
#define REQ_OPENNET 7
#define REQ_IP 8
#define REQ_BREAK 9
#define REQ_INIT 10
#define REQ_PUTDEBUGSTRING 11
#define REQ_BREAD 12
#define REQ_BWRITE 13
#define REQ_BOPEN 14
#define REQ_BCLOSE 15
#define REQ_DDVOPEN 16
#define REQ_BIF_TOKEN 17
#define REQ_KILL 18
#define REQ_SCHEDULE 19

/* the bit used to indicate that the host wants the BIF */
#define HOST_STATUS_BIT 2

#ifdef __KERNEL__
/* some prototypes */
extern int ap_dma_wait(int ch);
extern int ap_dma_go(unsigned long ch,unsigned int p,int size,unsigned long cmd);
extern int mpp_cid(void);
extern void ap_start_debugger(void);
extern int bif_queue(struct cap_request *req,char *buf,int bufsize);
extern void write_bif_polled(char *buf1,int len1,char *buf2,int len2);
extern void read_bif(char *buf,int size);
extern void ap_wait_request(struct cap_request *req,int type);
extern void bif_set_poll(int set);
extern void ap_led(unsigned char d);
extern void ap_xor_led(unsigned char d);
extern void ap_set_led(unsigned char d);
extern void ap_unset_led(unsigned char d);
extern void bif_toss(int size);
void ap_msc_init(void);
void mac_dma_complete(void);
void ap_dbg_flush(void);
void bif_queue_flush(void);
/* void ap_printk(char *msg,int a1,int a2,int a3,int a4,int a5); */
void show_mapping_ctx(unsigned *ctp,int context,unsigned Vm);
void async_fault(unsigned long address, int write, int taskid,
		 void (*callback)(int,unsigned long,int,int));
void ap_bif_init(void);
void ap_tnet_init(void);
int wait_on_int(volatile int *p,int x,int interval);
void ap_put(int dest_cell,u_long local_addr,int size,
	    u_long remote_addr,u_long dest_flag,u_long local_flag);
void ap_bput(u_long local_addr,int size,
	     u_long remote_addr,u_long dest_flag,u_long local_flag);
void msc_switch_check(struct task_struct *tsk);
int bif_queue_nocopy(struct cap_request *req,char *buf,int bufsize);
void mpp_set_gang_factor(int factor);
void bif_register_request(int type,void (*fn)(struct cap_request *));
void bif_add_debug_key(char key,void (*fn)(void),char *description);
void ap_complete(struct cap_request *creq);
void ap_reboot(char *bootstr);
#endif


#endif /* _APSERVICE_H */
