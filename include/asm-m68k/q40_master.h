/* 
 * Q40 master Chip Control 
 * RTC stuff merged for compactnes..
*/

#if 1
#define q40_master_addr 0xff000000
#define q40_rtc_addr    0xff021ffc
#else
extern unsigned long q40_master_addr;  /* wherever it is mapped ... */
extern unsigned long q40_rtc_addr;
#endif

#define IIRQ_REG            0x0       /* internal IRQ reg */
#define EIRQ_REG            0x4       /* external ... */
#define KEYCODE_REG         0x1c      /* value of received scancode  */
#define DISPLAY_CONTROL_REG 0x18
#define FRAME_CLEAR_REG     0x24

#define INTERRUPT_REG       IIRQ_REG  /* "native" ints */
#define KEY_IRQ_ENABLE_REG  0x08      /**/
#define KEYBOARD_UNLOCK_REG 0x20      /* clear kb int */

#define SAMPLE_ENABLE_REG   0x14      /* generate SAMPLE ints */
#define SAMPLE_RATE_REG     0x28
#define SAMPLE_CLEAR_REG    0x28
#define SAMPLE_LOW          0x00
#define SAMPLE_HIGH         0x01

#define FRAME_RATE_REG       0x38      /* generate FRAME ints at 200 HZ rate */

#if 0
#define SER_ENABLE_REG      0x0c      /* allow serial ints to be generated */
#endif
#define EXT_ENABLE_REG      0x10      /* ... rest of the ISA ints ... */

#define master_inb(_reg_)           (*(((unsigned char *)q40_master_addr)+_reg_))
#define master_outb(_b_,_reg_)      (*(((unsigned char *)q40_master_addr)+_reg_)=(_b_))


/* define some Q40 specific ints */
#include "q40ints.h"

/* RTC defines */

#define Q40_RTC_BASE (q40_rtc_addr)

#define RTC_YEAR        (*(unsigned char *)(Q40_RTC_BASE+0))
#define RTC_MNTH        (*(unsigned char *)(Q40_RTC_BASE-4))
#define RTC_DATE        (*(unsigned char *)(Q40_RTC_BASE-8))
#define RTC_DOW         (*(unsigned char *)(Q40_RTC_BASE-12))
#define RTC_HOUR        (*(unsigned char *)(Q40_RTC_BASE-16))
#define RTC_MINS        (*(unsigned char *)(Q40_RTC_BASE-20))
#define RTC_SECS        (*(unsigned char *)(Q40_RTC_BASE-24))
#define RTC_CTRL        (*(unsigned char *)(Q40_RTC_BASE-28))


#if 0
struct RTC_STRUCT{ 
  unsigned char bcd_year;
  unsigned char bcd_mth;
  unsigned char bcd_dom;
  unsigned char bcd_dayofweek;
  unsigned char bcd_hr;
  unsigned char bcd_min;
  unsigned char bcd_sec;
  unsigned char ctrl;
};
typedef struct RTC_STRUCT *RtcPtr_t;
#endif


/* some control bits */
#define RTC_READ   64  /* prepare for reading */
#define RTC_WRITE  128
