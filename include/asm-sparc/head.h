#define KERNSIZE	134217728
#define KERNBASE	0           /* new strategy */
#define LOAD_ADDR       0x4000
#define C_STACK         96
#define SUN4C_SEGSZ     (1 << 18)
#define NBPG		4096	
#define UPAGES		2
#define PROM_BASE	-1568768
#define PAGESHIFT_SUN4C 12          /* This is good for sun4m's also */
#define ASI_CONTROL     0x02        /* for cache enable, context registers, etc. */
#define ASI_SEGMAP      0x03
#define ASI_PTE         0x04
#define AC_CONTEXT      0x30000000
#define USRSTACK        KERNBASE
#define IE_REG_PTE_PG   -201326592
#define INT_ENABLE_REG_PHYSADR      0xf5000000
#define IE_ALLIE        0x01

/* This crap should go elsewhere */

#define PSR_IMPL        0xf0000000      /* implementation */
#define PSR_VER         0x0f000000      /* version */
#define PSR_ICC         0x00f00000      /* integer condition codes */
#define PSR_N           0x00800000      /* negative */
#define PSR_Z           0x00400000      /* zero */
#define PSR_O           0x00200000      /* overflow */
#define PSR_C           0x00100000      /* carry */
#define PSR_EC          0x00002000      /* coprocessor enable */
#define PSR_EF          0x00001000      /* FP enable */
#define PSR_PIL         0x00000f00      /* interrupt level */
#define PSR_S           0x00000080      /* supervisor (kernel) mode */
#define PSR_PS          0x00000040      /* previous supervisor mode (traps) */
#define PSR_ET          0x00000020      /* trap enable */
#define PSR_CWP         0x0000001f      /* current window pointer */

#define PCB_WIM         20

#define HZ              100

/* Offsets into the proc structure for window info, etc. Just dummies
   for now.
*/

#define TASK_WIM      0x8
#define TASK_UW       0x16
#define TASK_SIZE     8192
#define TASK_RW       0x32
#define TASK_NSAVED   0x40
#define PG_VSHIFT     0x16
#define PG_PROTSHIFT  0x6
#define PG_PROTUWRITE 0x4
