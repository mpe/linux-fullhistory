/* Support for the latches on the old Archimedes which control the floppy,
 * hard disc and printer
 *
 * (c) David Alan Gilbert 1995/1996
 */
#include <linux/kernel.h>

#include <asm/io.h>
#include <asm/hardware.h>

#ifdef LATCHAADDR
/*
 * They are static so that everyone who accesses them has to go through here
 */
static unsigned char LatchACopy;

/* newval=(oldval & ~mask)|newdata */
void oldlatch_aupdate(unsigned char mask,unsigned char newdata)
{
    LatchACopy=(LatchACopy & ~mask)|newdata;
    outb(LatchACopy, LATCHAADDR);
#ifdef DEBUG
    printk("oldlatch_A:0x%2x\n",LatchACopy);
#endif

}
#endif

#ifdef LATCHBADDR
static unsigned char LatchBCopy;

/* newval=(oldval & ~mask)|newdata */
void oldlatch_bupdate(unsigned char mask,unsigned char newdata)
{
    LatchBCopy=(LatchBCopy & ~mask)|newdata;
    outb(LatchBCopy, LATCHBADDR);
#ifdef DEBUG
    printk("oldlatch_B:0x%2x\n",LatchBCopy);
#endif
}
#endif

void oldlatch_init(void)
{
    printk("oldlatch: init\n");
#ifdef LATCHAADDR
    oldlatch_aupdate(0xff,0xff);
#endif
#ifdef LATCHBADDR
    oldlatch_bupdate(0xff,0x8); /* Thats no FDC reset...*/
#endif
    return ;
}
