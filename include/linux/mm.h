#ifndef _MM_H
#define _MM_H

#define PAGE_SIZE 4096

#include <linux/fs.h>
#include <linux/kernel.h>
#include <signal.h>

extern unsigned int swap_device;
extern struct inode * swap_file;

extern void rw_swap_page(int rw, unsigned int nr, char * buf);

#define read_swap_page(nr,buf) \
	rw_swap_page(READ,(nr),(buf))
#define write_swap_page(nr,buf) \
	rw_swap_page(WRITE,(nr),(buf))

/* memory.c */
	
extern unsigned long get_free_page(void);
extern unsigned long put_dirty_page(unsigned long page,unsigned long address);
extern void free_page(unsigned long addr);
extern int free_page_tables(unsigned long from,unsigned long size);
extern int copy_page_tables(unsigned long from,unsigned long to,long size);
extern int unmap_page_range(unsigned long from, unsigned long size);
extern int remap_page_range(unsigned long from, unsigned long to, unsigned long size,
	 int permiss);
extern void un_wp_page(unsigned long * table_entry);
extern void do_wp_page(unsigned long error_code,unsigned long address);
extern void write_verify(unsigned long address);
extern void do_no_page(unsigned long error_code, unsigned long address,
	struct task_struct *tsk, unsigned long user_esp);
extern void mem_init(long start_mem, long end_mem);
extern void show_mem(void);
extern void do_page_fault(unsigned long *esp, unsigned long error_code);

/* swap.c */

extern void swap_free(int page_nr);
extern void swap_in(unsigned long *table_ptr);

extern inline volatile void oom(void)
{
	printk("out of memory\n\r");
	do_exit(SIGSEGV);
}

#define invalidate() \
__asm__("movl %%eax,%%cr3"::"a" (0))

/* these are not to be changed without changing head.s etc */
#define LOW_MEM 0x100000
extern unsigned long HIGH_MEMORY;
#define PAGING_MEMORY (15*1024*1024)
#define PAGING_PAGES (PAGING_MEMORY>>12)
#define MAP_NR(addr) (((addr)-LOW_MEM)>>12)
#define USED 100

extern unsigned char mem_map [ PAGING_PAGES ];

#define PAGE_DIRTY	0x40
#define PAGE_ACCESSED	0x20
#define PAGE_USER	0x04
#define PAGE_RW		0x02
#define PAGE_PRESENT	0x01

#endif
