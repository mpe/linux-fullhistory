#ifndef _M68K_MACHDEP_H
#define _M68K_MACHDEP_H

struct pt_regs;
struct kbd_repeat;
struct mktime;
struct hwclk_time;
struct gendisk;
struct buffer_head;

#ifndef ISRFUNC_T
typedef void (*isrfunc) (int irq, struct pt_regs *fp, void *data);
#define ISRFUNC_T
#endif /* ISRFUNC_T */

extern void (*mach_sched_init)(isrfunc);
extern int (*mach_keyb_init) (void);
extern int (*mach_kbdrate) (struct kbd_repeat *);
extern void (*mach_kbd_leds) (unsigned int);
extern void (*mach_init_INTS) (void);
extern int (*mach_add_isr) (unsigned long source, isrfunc handler,
			    int pri, void *data, char *name);
extern int (*mach_remove_isr) (unsigned long source, isrfunc handler,
			       void *data);
extern int (*mach_get_irq_list)(char *buf, int len);
extern void (*mach_process_int) (int level, struct pt_regs *fp);
extern void (*mach_enable_irq) (unsigned);
extern void (*mach_disable_irq) (unsigned);
extern unsigned long (*mach_gettimeoffset)(void);
extern void (*mach_gettod)(int *year, int *mon, int *day, int *hour,
			   int *min, int *sec);
extern int (*mach_hwclk)(int, struct hwclk_time*);
extern int (*mach_set_clock_mmss)(unsigned long);
extern void (*mach_mksound)( unsigned int count, unsigned int ticks );
extern void (*mach_reset)( void );
extern int (*mach_floppy_init) (void);
extern unsigned long (*mach_hd_init) (unsigned long, unsigned long);
extern void (*mach_hd_setup)(char *, int *);
extern void (*waitbut)(void);
extern struct fb_info *(*mach_fb_init)(long *);
extern long mach_max_dma_address;
extern void (*mach_debug_init)(void);
extern void (*mach_video_setup)(char *, int *);
extern void (*mach_floppy_setup)(char *, int *);
extern void (*mach_floppy_eject)(void);

#endif /* _M68K_MACHDEP_H */
