/*
linux/arch/m68k/atari/atasound.c

++Geert: Moved almost all stuff to linux/drivers/sound/

The author of atari_nosound, atari_mksound and atari_microwire_cmd is
unknown.
(++roman: That's me... :-)

This file is subject to the terms and conditions of the GNU General Public
License.  See the file COPYING in the main directory of this archive
for more details.

*/


#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/major.h>
#include <linux/fcntl.h>
#include <linux/errno.h>
#include <linux/mm.h>

#include <asm/atarihw.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/atariints.h>


/*
 * stuff from the old atasound.c
 */


static void atari_nosound (unsigned long ignored)
{
	unsigned char	tmp;
	unsigned long flags;
	
	/* turn off generator A in mixer control */
	save_flags(flags);
	cli();
	sound_ym.rd_data_reg_sel = 7;
	tmp = sound_ym.rd_data_reg_sel;
	sound_ym.wd_data = tmp | 0x39;
	restore_flags(flags);
}	
		

void atari_microwire_cmd (int cmd)
{
	tt_microwire.mask = 0x7ff;
	tt_microwire.data = MW_LM1992_ADDR | cmd;

	/* Busy wait for data being completely sent :-( */
	while( tt_microwire.mask != 0x7ff)
		;
}


#define	PC_FREQ		1192180
#define	PSG_FREQ	125000


void atari_mksound (unsigned int count, unsigned int ticks)
{
	static struct timer_list sound_timer = { NULL, NULL, 0, 0,
						     atari_nosound };
	/*
	 * Generates sound of some count for some number of clock ticks
	 * [count = 1193180 / frequency]
	 */
	unsigned long flags;
	unsigned char tmp;

	save_flags(flags);
	cli();

	if (count == 750 && ticks == HZ/8) {
		/* Special case: These values are used by console.c to
		 * generate the console bell. They are cached here and the
		 * sound actually generated is somehow special: it uses the
		 * generator B and an envelope. No timer is needed therefore
		 * and the bell doesn't disturb an other ongoing sound.
		 */

		/* set envelope duration to 492 ms */
		sound_ym.rd_data_reg_sel = 11;
		sound_ym.wd_data = 0;
		sound_ym.rd_data_reg_sel = 12;
		sound_ym.wd_data = 15;
		/* envelope form: max -> min single */
		sound_ym.rd_data_reg_sel = 13;
		sound_ym.wd_data = 9;
		/* set generator B frequency to 2400 Hz */
		sound_ym.rd_data_reg_sel = 2;
		sound_ym.wd_data = 52;
		sound_ym.rd_data_reg_sel = 3;
		sound_ym.wd_data = 0;
		/* set volume of generator B to envelope control */
		sound_ym.rd_data_reg_sel = 9;
		sound_ym.wd_data = 0x10;
		/* enable generator B in the mixer control */
		sound_ym.rd_data_reg_sel = 7;
		tmp = sound_ym.rd_data_reg_sel;
		sound_ym.wd_data = (tmp & ~0x02) | 0x38;

		restore_flags(flags);
		return;
	}

	del_timer( &sound_timer );

	if (!count) {
		atari_nosound( 0 );
	}
	else {

		/* convert from frequency value
		 * to PSG period value (base frequency 125 kHz).
		 */
		int period = PSG_FREQ / count;

		if (period > 0xfff) period = 0xfff;

		/* set generator A frequency to 0 */
		sound_ym.rd_data_reg_sel = 0;
		sound_ym.wd_data = period & 0xff;
		sound_ym.rd_data_reg_sel = 1;
		sound_ym.wd_data = (period >> 8) & 0xf;
		/* turn on generator A in mixer control (but not noise
		 * generator!) */
		sound_ym.rd_data_reg_sel = 7;
		tmp = sound_ym.rd_data_reg_sel;
		sound_ym.wd_data = (tmp & ~0x01) | 0x38;
		/* set generator A level to maximum, no envelope */
		sound_ym.rd_data_reg_sel = 8;
		sound_ym.wd_data = 15;
		
		if (ticks) {
			sound_timer.expires = jiffies + ticks;
			add_timer( &sound_timer );
		}
	}

	restore_flags(flags);
}
