#include <stdarg.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/kd.h>
#include <linux/tty.h>
#include <linux/console.h>

#include <asm/setup.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/apollohw.h>
#include <asm/irq.h>
#include <asm/machdep.h>

extern void dn_sched_init(void (*handler)(int,void *,struct pt_regs *));
extern int dn_keyb_init(void);
extern int dn_dummy_kbdrate(struct kbd_repeat *);
extern void dn_init_IRQ(void);
#if 0
extern void (*dn_default_handler[])(int,void *,struct pt_regs *);
#endif
extern int dn_request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *), unsigned long flags, const char *devname, void *dev_id);
extern void dn_free_irq(unsigned int irq, void *dev_id);
extern void dn_enable_irq(unsigned int);
extern void dn_disable_irq(unsigned int);
extern int dn_get_irq_list(char *);
extern unsigned long dn_gettimeoffset(void);
extern void dn_gettod(int *, int *, int *, int *, int *, int *);
extern int dn_dummy_hwclk(int, struct hwclk_time *);
extern int dn_dummy_set_clock_mmss(unsigned long);
extern void dn_mksound(unsigned int count, unsigned int ticks);
extern void dn_dummy_reset(void);
extern void dn_dummy_waitbut(void);
extern struct fb_info *dn_fb_init(long *);
extern void dn_dummy_debug_init(void);
extern void (*kd_mksound)(unsigned int, unsigned int);
extern void dn_dummy_video_setup(char *,int *);
extern void dn_process_int(int irq, struct pt_regs *fp);

static struct console dn_console_driver;
static void dn_debug_init(void);
static void dn_timer_int(int irq,void *, struct pt_regs *);
static void (*sched_timer_handler)(int, void *, struct pt_regs *)=NULL;

int dn_serial_console_wait_key(void) {

	while(!(sio01.srb_csrb & 1))
		barrier();
	return sio01.rhrb_thrb;
}

void dn_serial_console_write (const char *str,unsigned int count)
{
   while(count--) {
	if (*str == '\n') { 
    	sio01.rhrb_thrb = (unsigned char)'\r';
       	while (!(sio01.srb_csrb & 0x4))
                ;
 	}
    sio01.rhrb_thrb = (unsigned char)*str++;
    while (!(sio01.srb_csrb & 0x4))
            ;
  }	
}
 
void dn_serial_print (const char *str)
{
    while (*str) {
        if (*str == '\n') {
            sio01.rhrb_thrb = (unsigned char)'\r';
            while (!(sio01.srb_csrb & 0x4))
                ;
        }
        sio01.rhrb_thrb = (unsigned char)*str++;
        while (!(sio01.srb_csrb & 0x4))
            ;
    }
}

void config_apollo(void) {

	dn_serial_print("Config apollo !\n");
#if 0
	dn_debug_init();	
#endif
	printk("Config apollo !\n");


	mach_sched_init=dn_sched_init; /* */
	mach_keyb_init=dn_keyb_init;
	mach_kbdrate=dn_dummy_kbdrate;
	mach_init_IRQ=dn_init_IRQ;
	mach_default_handler=NULL;
	mach_request_irq     = dn_request_irq;
	mach_free_irq        = dn_free_irq;
	enable_irq      = dn_enable_irq;
	disable_irq     = dn_disable_irq;
	mach_get_irq_list    = dn_get_irq_list;
	mach_gettimeoffset   = dn_gettimeoffset;
	mach_gettod	     = dn_gettod; /* */
	mach_max_dma_address = 0xffffffff;
	mach_hwclk           = dn_dummy_hwclk; /* */
	mach_set_clock_mmss  = dn_dummy_set_clock_mmss; /* */
	mach_process_int     = dn_process_int;
#ifdef CONFIG_BLK_DEV_FD
	mach_floppy_init     = dn_dummy_floppy_init;
	mach_floppy_setup    = dn_dummy_floppy_setup;
#endif
	mach_reset	     = dn_dummy_reset;  /* */
#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp	     = &dummy_con;
#endif
#if 0
	mach_fb_init 	     = dn_fb_init; 
	mach_video_setup     = dn_dummy_video_setup; 
#endif
	kd_mksound	     = dn_mksound;

}		

void dn_timer_int(int irq, void *dev_id, struct pt_regs *fp) {

	volatile unsigned char x;

	sched_timer_handler(irq,dev_id,fp);
	
	x=*(volatile unsigned char *)(IO_BASE+0x10803);
	x=*(volatile unsigned char *)(IO_BASE+0x10805);

}

void dn_sched_init(void (*timer_routine)(int, void *, struct pt_regs *)) {

	dn_serial_print("dn sched_init\n");

#if 0
	/* program timer 2 */
	*(volatile unsigned char *)(IO_BASE+0x10803)=0x00;
	*(volatile unsigned char *)(IO_BASE+0x10809)=0;
	*(volatile unsigned char *)(IO_BASE+0x1080b)=50;

	/* program timer 3 */
	*(volatile unsigned char *)(IO_BASE+0x10801)=0x00;
	*(volatile unsigned char *)(IO_BASE+0x1080c)=0;
	*(volatile unsigned char *)(IO_BASE+0x1080f)=50;	
#endif
	/* program timer 1 */       	
	*(volatile unsigned char *)(IO_BASE+0x10803)=0x01;
	*(volatile unsigned char *)(IO_BASE+0x10801)=0x40;
	*(volatile unsigned char *)(IO_BASE+0x10805)=0x09;
	*(volatile unsigned char *)(IO_BASE+0x10807)=0xc4;

	/* enable IRQ of PIC B */
	*(volatile unsigned char *)(IO_BASE+PICA+1)&=(~8);



	printk("*(0x10803) %02x\n",*(volatile unsigned char *)(IO_BASE+0x10803));
	printk("*(0x10803) %02x\n",*(volatile unsigned char *)(IO_BASE+0x10803));

	sched_timer_handler=timer_routine;
	request_irq(0,dn_timer_int,0,NULL,NULL);

	
}

unsigned long dn_gettimeoffset(void) {

	return 0xdeadbeef;

}

void dn_gettod(int *yearp, int *monp, int *dayp,
	       int *hourp, int *minp, int *secp) {

  *yearp=rtc->year;
  *monp=rtc->month;
  *dayp=rtc->day_of_month;
  *hourp=rtc->hours;
  *minp=rtc->minute;
  *secp=rtc->second;

printk("gettod: %d %d %d %d %d %d\n",*yearp,*monp,*dayp,*hourp,*minp,*secp);

}

int dn_dummy_hwclk(int op, struct hwclk_time *t) {

  dn_serial_print("hwclk !\n");

  if(!op) { /* read */
    t->sec=rtc->second;
    t->min=rtc->minute;
    t->hour=rtc->hours;
    t->day=rtc->day_of_month;
    t->wday=rtc->day_of_week;
    t->mon=rtc->month;
    t->year=rtc->year;
  } else {
    rtc->second=t->sec;
    rtc->minute=t->min;
    rtc->hours=t->hour;
    rtc->day_of_month=t->day;
    if(t->wday!=-1)
      rtc->day_of_week=t->wday;
    rtc->month=t->mon;
    rtc->year=t->year;
  }

  dn_serial_print("hwclk end!\n");
  return 0;

}

int dn_dummy_set_clock_mmss(unsigned long nowtime) {

  printk("set_clock_mmss\n");

  return 0;

}

void dn_dummy_reset(void) {

  dn_serial_print("The end !\n");

  for(;;);

}
	
void dn_dummy_waitbut(void) {

  dn_serial_print("waitbut\n");

}

#if 0
void dn_debug_init(void) {

  dn_console_driver.write=dn_serial_console_write;
  register_console(&dn_console_driver);

}
#endif
