/*
 * PowerPC memory management structures
 */

#ifndef _PPC_MMU_H_
#define _PPC_MMU_H_

/* Hardware Page Table Entry */
 
typedef struct _PTE
   {
      unsigned long v:1;	/* Entry is valid */
      unsigned long vsid:24;	/* Virtual segment identifier */
      unsigned long h:1;	/* Hash algorithm indicator */
      unsigned long api:6;	/* Abbreviated page index */
      unsigned long rpn:20;	/* Real (physical) page number */
      unsigned long    :3;	/* Unused */
      unsigned long r:1;	/* Referenced */
      unsigned long c:1;	/* Changed */
      unsigned long w:1;	/* Write-thru cache mode */
      unsigned long i:1;	/* Cache inhibited */
      unsigned long m:1;	/* Memory coherence */
      unsigned long g:1;	/* Guarded */
      unsigned long    :1;	/* Unused */
      unsigned long pp:2;	/* Page protection */
   } PTE; 

/* Values for PP (assumes Ks=0, Kp=1) */
#define PP_RWXX	0	/* Supervisor read/write, User none */
#define PP_RWRX 1	/* Supervisor read/write, User read */
#define PP_RWRW 2	/* Supervisor read/write, User read/write */
#define PP_RXRX 3	/* Supervisor read,       User read */

/* Segment Register */

typedef struct _SEGREG
   {
      unsigned long t:1;	/* Normal or I/O  type */
      unsigned long ks:1;	/* Supervisor 'key' (normally 0) */
      unsigned long kp:1;	/* User 'key' (normally 1) */
      unsigned long n:1;	/* No-execute */
      unsigned long :4;		/* Unused */
      unsigned long vsid:24;	/* Virtual Segment Identifier */
   } SEGREG;

/* Block Address Translation (BAT) Registers */

typedef struct _BATU		/* Upper part of BAT */
   {
      unsigned long bepi:15;	/* Effective page index (virtual address) */
      unsigned long :4;		/* Unused */
      unsigned long bl:11;	/* Block size mask */
      unsigned long vs:1;	/* Supervisor valid */
      unsigned long vp:1;	/* User valid */
   } BATU;   

typedef struct _BATL		/* Lower part of BAT */
   {
      unsigned long brpn:15;	/* Real page index (physical address) */
      unsigned long :10;	/* Unused */
      unsigned long w:1;	/* Write-thru cache */
      unsigned long i:1;	/* Cache inhibit */
      unsigned long m:1;	/* Memory coherence */
      unsigned long g:1;	/* Guarded (MBZ) */
      unsigned long :1;		/* Unused */
      unsigned long pp:2;	/* Page access protections */
   } BATL;

typedef struct _BAT
   {
      BATU batu;		/* Upper register */
      BATL batl;		/* Lower register */
   } BAT;

/* Block size masks */
#define BL_128K	0x000
#define BL_256K 0x001
#define BL_512K 0x003
#define BL_1M   0x007
#define BL_2M   0x00F
#define BL_4M   0x01F
#define BL_8M   0x03F
#define BL_16M  0x07F
#define BL_32M  0x0FF
#define BL_64M  0x1FF
#define BL_128M 0x3FF
#define BL_256M 0x7FF

/* BAT Access Protection */
#define BPP_XX	0x00		/* No access */
#define BPP_RX	0x01		/* Read only */
#define BPP_RW	0x02		/* Read/write */

/*
 * Simulated two-level MMU.  This structure is used by the kernel
 * to keep track of MMU mappings and is used to update/maintain
 * the hardware HASH table which is really a cache of mappings.
 *
 * The simulated structures mimic the hardware available on other
 * platforms, notably the 80x86 and 680x0.
 */

typedef struct _pte
   {
   	unsigned long page_num:20;
   	unsigned long flags:12;		/* Page flags (with some unused bits) */
   } pte;

#define PD_SHIFT (10+12)		/* Page directory */
#define PD_MASK  0x02FF
#define PT_SHIFT (12)			/* Page Table */
#define PT_MASK  0x02FF
#define PG_SHIFT (12)			/* Page Entry */
 

/* MMU context */

typedef struct _MMU_context
   {
      SEGREG	segs[16];	/* Segment registers */
      pte	**pmap;		/* Two-level page-map structure */
   } MMU_context;

#if 0
BAT	ibat[4];	/* Instruction BAT images */
BAT	dbat[4];	/* Data BAT images */
PTE	*hash_table;	/* Hardware hashed page table */
int	hash_table_size;
int	hash_table_mask;
unsigned long sdr;	/* Hardware image of SDR */
#endif   

/* Used to set up SDR register */
#define HASH_TABLE_SIZE_64K	0x00010000
#define HASH_TABLE_SIZE_128K	0x00020000
#define HASH_TABLE_SIZE_256K	0x00040000
#define HASH_TABLE_SIZE_512K	0x00080000
#define HASH_TABLE_SIZE_1M	0x00100000
#define HASH_TABLE_SIZE_2M	0x00200000
#define HASH_TABLE_SIZE_4M	0x00400000
#define HASH_TABLE_MASK_64K	0x000   
#define HASH_TABLE_MASK_128K	0x001   
#define HASH_TABLE_MASK_256K	0x003   
#define HASH_TABLE_MASK_512K	0x007
#define HASH_TABLE_MASK_1M	0x00F   
#define HASH_TABLE_MASK_2M	0x01F   
#define HASH_TABLE_MASK_4M	0x03F   

#define MMU_PAGE_SIZE	4096

extern int MMU_hash_page(struct thread_struct *tss, unsigned long va, pte *pg);

#endif
