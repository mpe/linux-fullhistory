/* -- sjcd.c
 *
 *   Sanyo CD-ROM device driver implementation, Version 1.5
 *   Copyright (C) 1995  Vadim V. Model
 *
 *   model@cecmow.enet.dec.com
 *   vadim@rbrf.ru
 *   vadim@ipsun.ras.ru
 *
 *   ISP16 detection and configuration.
 *   Copyright (C) 1995  Eric van der Maarel (maarel@marin.nl)
 *                   and Vadim Model (vadim@cecmow.enet.dec.com)
 *
 *
 *  This driver is based on pre-works by Eberhard Moenkeberg (emoenke@gwdg.de);
 *  it was developed under use of mcd.c from Martin Harriss, with help of
 *  Eric van der Maarel (maarel@marin.nl).
 *
 *  ISP16 detection and configuration by Eric van der Maarel (maarel@marin.nl).
 *  Sound configuration by Vadim V. Model (model@cecmow.enet.dec.com)
 *
 *  It is planned to include these routines into sbpcd.c later - to make
 *  a "mixed use" on one cable possible for all kinds of drives which use
 *  the SoundBlaster/Panasonic style CDROM interface. But today, the
 *  ability to install directly from CDROM is more important than flexibility.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  History:
 *  1.1 First public release with kernel version 1.3.7.
 *      Written by Vadim Model.
 *  1.2 Added detection and configuration of cdrom interface
 *      on ISP16 soundcard.
 *      Allow for command line options: sjcd=<io_base>,<irq>,<dma>
 *  1.3 Some minor changes to README.sjcd.
 *  1.4 MSS Sound support!! Listen to a CD through the speakers.
 *  1.5 Module support and bugfixes.
 *      Tray locking.
 *
 */

#include <linux/module.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/cdrom.h>
#include <linux/ioport.h>
#include <linux/string.h>
#include <linux/major.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#define MAJOR_NR SANYO_CDROM_MAJOR
#include <linux/blk.h>
#include <linux/sjcd.h>

/* Some (Media)Magic */
/* define types of drive the interface on an ISP16 card may be looking at */
#define ISP16_DRIVE_X 0x00
#define ISP16_SONY  0x02
#define ISP16_PANASONIC0 0x02
#define ISP16_SANYO0 0x02
#define ISP16_MITSUMI  0x04
#define ISP16_PANASONIC1 0x06
#define ISP16_SANYO1 0x06
#define ISP16_DRIVE_NOT_USED 0x08  /* not used */
#define ISP16_DRIVE_SET_MASK 0xF1  /* don't change 0-bit or 4-7-bits*/
/* ...for port */
#define ISP16_DRIVE_SET_PORT 0xF8D
/* set io parameters */
#define ISP16_BASE_340  0x00
#define ISP16_BASE_330  0x40
#define ISP16_BASE_360  0x80
#define ISP16_BASE_320  0xC0
#define ISP16_IRQ_X  0x00
#define ISP16_IRQ_5  0x04  /* shouldn't be used due to soundcard conflicts */
#define ISP16_IRQ_7  0x08  /* shouldn't be used due to soundcard conflicts */
#define ISP16_IRQ_3  0x0C
#define ISP16_IRQ_9  0x10
#define ISP16_IRQ_10  0x14
#define ISP16_IRQ_11  0x18
#define ISP16_DMA_X  0x03
#define ISP16_DMA_3  0x00
#define ISP16_DMA_5  0x00
#define ISP16_DMA_6  0x01
#define ISP16_DMA_7  0x02
#define ISP16_IO_SET_MASK  0x20  /* don't change 5-bit */
/* ...for port */
#define ISP16_IO_SET_PORT  0xF8E
/* enable the card */
#define ISP16_C928__ENABLE_PORT  0xF90  /* ISP16 with OPTi 82C928 chip */
#define ISP16_C929__ENABLE_PORT  0xF91  /* ISP16 with OPTi 82C929 chip */
#define ISP16_ENABLE_CDROM  0x80  /* seven bit */

/* the magic stuff */
#define ISP16_CTRL_PORT  0xF8F
#define ISP16_C928__CTRL  0xE2  /* ISP16 with OPTi 82C928 chip */
#define ISP16_C929__CTRL  0xE3  /* ISP16 with OPTi 82C929 chip */

static short isp16_detect(void);
static short isp16_c928__detect(void);
static short isp16_c929__detect(void);
static short isp16_cdi_config( int base, u_char drive_type, int irq, int dma );
static void isp16_sound_config( void );
static short isp16_type; /* dependent on type of interface card */
static u_char isp16_ctrl;
static u_short isp16_enable_port;

static int sjcd_present = 0;
static u_char special_mask = 0;

static unsigned char defaults[ 16 ] = {
  0xA8, 0xA8, 0x18, 0x18, 0x18, 0x18, 0x8E, 0x8E,
  0x03, 0x00, 0x02, 0x00, 0x0A, 0x00, 0x00, 0x00
};

#define SJCD_BUF_SIZ 32 /* cdr-h94a has internal 64K buffer */

/*
 * buffer for block size conversion
 */
static char sjcd_buf[ 2048 * SJCD_BUF_SIZ ];
static volatile int sjcd_buf_bn[ SJCD_BUF_SIZ ], sjcd_next_bn;
static volatile int sjcd_buf_in, sjcd_buf_out = -1;

/*
 * Status.
 */
static unsigned short sjcd_status_valid = 0;
static unsigned short sjcd_door_closed;
static unsigned short sjcd_door_was_open;
static unsigned short sjcd_media_is_available;
static unsigned short sjcd_media_is_changed;
static unsigned short sjcd_toc_uptodate = 0;
static unsigned short sjcd_command_failed;
static volatile unsigned char sjcd_completion_status = 0;
static volatile unsigned char sjcd_completion_error = 0;
static unsigned short sjcd_command_is_in_progress = 0;
static unsigned short sjcd_error_reported = 0;

static int sjcd_open_count;

static int sjcd_audio_status;
static struct sjcd_play_msf sjcd_playing;

static int sjcd_port = SJCD_BASE_ADDR;
static int sjcd_irq  = SJCD_INTR_NR;
static int sjcd_dma  = SJCD_DMA_NR;

static struct wait_queue *sjcd_waitq = NULL;

/*
 * Data transfer.
 */
static volatile unsigned short sjcd_transfer_is_active = 0;

enum sjcd_transfer_state {
  SJCD_S_IDLE     = 0,
  SJCD_S_START    = 1,
  SJCD_S_MODE     = 2,
  SJCD_S_READ     = 3,
  SJCD_S_DATA     = 4,
  SJCD_S_STOP     = 5,
  SJCD_S_STOPPING = 6
};
static enum sjcd_transfer_state sjcd_transfer_state = SJCD_S_IDLE;
static long sjcd_transfer_timeout = 0;
static int sjcd_read_count = 0;
static unsigned char sjcd_mode = 0;

#define SJCD_READ_TIMEOUT 5000

#if defined( SJCD_GATHER_STAT )
/*
 * Statistic.
 */
static struct sjcd_stat statistic;
#endif

/*
 * Timer.
 */
static struct timer_list sjcd_delay_timer = { NULL, NULL, 0, 0, NULL };

#define SJCD_SET_TIMER( func, tmout )           \
    ( sjcd_delay_timer.expires = jiffies+tmout,         \
      sjcd_delay_timer.function = ( void * )func, \
      add_timer( &sjcd_delay_timer ) )

#define CLEAR_TIMER del_timer( &sjcd_delay_timer )

/*
 * Set up device, i.e., use command line data to set
 * base address, irq and dma.
 */
void sjcd_setup( char *str, int *ints )
{
   if (ints[0] > 0)
      sjcd_port = ints[1];
   if (ints[0] > 1)      
      sjcd_irq = ints[2];
   if (ints[0] > 2)      
      sjcd_dma = ints[3];
}

/*
 * Special converters.
 */
static unsigned char bin2bcd( int bin ){
  int u, v;

  u = bin % 10; v = bin / 10;
  return( u | ( v << 4 ) );
}

static int bcd2bin( unsigned char bcd ){
    return( ( bcd >> 4 ) * 10 + ( bcd & 0x0F ) );
}

static long msf2hsg( struct msf *mp ){
  return( bcd2bin( mp->frame ) + bcd2bin( mp->sec ) * 75
	 + bcd2bin( mp->min ) * 4500 - 150 );
}

static void hsg2msf( long hsg, struct msf *msf ){
  hsg += 150; msf->min = hsg / 4500;
  hsg %= 4500; msf->sec = hsg / 75; msf->frame = hsg % 75;
  msf->min = bin2bcd( msf->min );       /* convert to BCD */
  msf->sec = bin2bcd( msf->sec );
  msf->frame = bin2bcd( msf->frame );
}

/*
 * Send a command to cdrom. Invalidate status.
 */
static void sjcd_send_cmd( unsigned char cmd ){
#if defined( SJCD_TRACE )
  printk( "sjcd: send_cmd( 0x%x )\n", cmd );
#endif
  outb( cmd, SJCDPORT( 0 ) );
  sjcd_command_is_in_progress = 1;
  sjcd_status_valid = 0;
  sjcd_command_failed = 0;
}

/*
 * Send a command with one arg to cdrom. Invalidate status.
 */
static void sjcd_send_1_cmd( unsigned char cmd, unsigned char a ){
#if defined( SJCD_TRACE )
  printk( "sjcd: send_1_cmd( 0x%x, 0x%x )\n", cmd, a );
#endif
  outb( cmd, SJCDPORT( 0 ) );
  outb( a, SJCDPORT( 0 ) );
  sjcd_command_is_in_progress = 1;
  sjcd_status_valid = 0;
  sjcd_command_failed = 0;
}

/*
 * Send a command with four args to cdrom. Invalidate status.
 */
static void sjcd_send_4_cmd( unsigned char cmd, unsigned char a,
	    unsigned char b, unsigned char c, unsigned char d ){
#if defined( SJCD_TRACE )
  printk( "sjcd: send_4_cmd( 0x%x )\n", cmd );
#endif
  outb( cmd, SJCDPORT( 0 ) );
  outb( a, SJCDPORT( 0 ) );
  outb( b, SJCDPORT( 0 ) );
  outb( c, SJCDPORT( 0 ) );
  outb( d, SJCDPORT( 0 ) );
  sjcd_command_is_in_progress = 1;
  sjcd_status_valid = 0;
  sjcd_command_failed = 0;
}

/*
 * Send a play or read command to cdrom. Invalidate Status.
 */
static void sjcd_send_6_cmd( unsigned char cmd, struct sjcd_play_msf *pms ){
#if defined( SJCD_TRACE )
  printk( "sjcd: send_long_cmd( 0x%x )\n", cmd );
#endif
  outb( cmd, SJCDPORT( 0 ) );
  outb( pms->start.min,   SJCDPORT( 0 ) );
  outb( pms->start.sec,   SJCDPORT( 0 ) );
  outb( pms->start.frame, SJCDPORT( 0 ) );
  outb( pms->end.min,     SJCDPORT( 0 ) );
  outb( pms->end.sec,     SJCDPORT( 0 ) );
  outb( pms->end.frame,   SJCDPORT( 0 ) );
  sjcd_command_is_in_progress = 1;
  sjcd_status_valid = 0;
  sjcd_command_failed = 0;
}

/*
 * Get a value from the data port. Should not block, so we use a little
 * wait for a while. Returns 0 if OK.
 */
static int sjcd_load_response( void *buf, int len ){
  unsigned char *resp = ( unsigned char * )buf;

  for( ; len; --len ){ 
    int i;
    for( i = 200; i-- && !SJCD_STATUS_AVAILABLE( inb( SJCDPORT( 1 ) ) ); );
    if( i > 0 ) *resp++ = ( unsigned char )inb( SJCDPORT( 0 ) );
    else break;
  }
  return( len );
}

/*
 * Load and parse command completion status (drive info byte and maybe error).
 * Sorry, no error classification yet.
 */
static void sjcd_load_status( void ){
  sjcd_media_is_changed = 0;
  sjcd_completion_error = 0;
  sjcd_completion_status = inb( SJCDPORT( 0 ) );
  if( sjcd_completion_status & SST_DOOR_OPENED ){
    sjcd_door_closed = sjcd_media_is_available = 0;
  } else {
    sjcd_door_closed = 1;
    if( sjcd_completion_status & SST_MEDIA_CHANGED )
      sjcd_media_is_available = sjcd_media_is_changed = 1;
    else if( sjcd_completion_status & 0x0F ){
      /*
       * OK, we seem to catch an error ...
       */
      while( !SJCD_STATUS_AVAILABLE( inb( SJCDPORT( 1 ) ) ) );
      sjcd_completion_error = inb( SJCDPORT( 0 ) );
      if( ( sjcd_completion_status & 0x08 ) &&
	 ( sjcd_completion_error & 0x40 ) )
	sjcd_media_is_available = 0;
      else sjcd_command_failed = 1;
    } else sjcd_media_is_available = 1;
  }
  /*
   * Ok, status loaded successfully.
   */
  sjcd_status_valid = 1, sjcd_error_reported = 0;
  sjcd_command_is_in_progress = 0;

  /*
   * If the disk is changed, the TOC is not valid.
   */
  if( sjcd_media_is_changed ) sjcd_toc_uptodate = 0;
#if defined( SJCD_TRACE )
  printk( "sjcd: status %02x.%02x loaded.\n",
	 ( int )sjcd_completion_status, ( int )sjcd_completion_error );
#endif
}

/*
 * Read status from cdrom. Check to see if the status is available.
 */
static int sjcd_check_status( void ){
  /*
   * Try to load the response from cdrom into buffer.
   */
  if( SJCD_STATUS_AVAILABLE( inb( SJCDPORT( 1 ) ) ) ){
    sjcd_load_status();
    return( 1 );
  } else {
    /*
     * No status is available.
     */
    return( 0 );
  }
}

/*
 * This is just timout counter, and nothing more. Surprized ? :-)
 */
static volatile long sjcd_status_timeout;

/*
 * We need about 10 seconds to wait. The longest command takes about 5 seconds
 * to probe the disk (usually after tray closed or drive reset). Other values
 * should be thought of for other commands.
 */
#define SJCD_WAIT_FOR_STATUS_TIMEOUT 1000

static void sjcd_status_timer( void ){
  if( sjcd_check_status() ){
    /*
     * The command completed and status is loaded, stop waiting.
     */
    wake_up( &sjcd_waitq );
  } else if( --sjcd_status_timeout <= 0 ){
    /*
     * We are timed out. 
     */
    wake_up( &sjcd_waitq );
  } else {
    /*
     * We have still some time to wait. Try again.
     */
    SJCD_SET_TIMER( sjcd_status_timer, 1 );
  }
}

/*
 * Wait for status for 10 sec approx. Returns non-positive when timed out.
 * Should not be used while reading data CDs.
 */
static int sjcd_wait_for_status( void ){
  sjcd_status_timeout = SJCD_WAIT_FOR_STATUS_TIMEOUT;
  SJCD_SET_TIMER( sjcd_status_timer, 1 ); 
  sleep_on( &sjcd_waitq );    
#if defined( SJCD_DIAGNOSTIC ) || defined ( SJCD_TRACE )
  if( sjcd_status_timeout <= 0 )
    printk( "sjcd: Error Wait For Status.\n" );
#endif
  return( sjcd_status_timeout );
}

static int sjcd_receive_status( void ){
  int i;
#if defined( SJCD_TRACE )
  printk( "sjcd: receive_status\n" );
#endif
  /*
   * Wait a bit for status available.
   */
  for( i = 200; i-- && ( sjcd_check_status() == 0 ); );
  if( i < 0 ){
#if defined( SJCD_TRACE )
    printk( "sjcd: long wait for status\n" );
#endif
    if( sjcd_wait_for_status() <= 0 )
      printk( "sjcd: Timeout when read status.\n" );
    else i = 0;
  }
  return( i );
}

/*
 * Load the status. Issue get status command and wait for status available.
 */
static void sjcd_get_status( void ){
#if defined( SJCD_TRACE )
  printk( "sjcd: get_status\n" );
#endif
  sjcd_send_cmd( SCMD_GET_STATUS );
  sjcd_receive_status();
}

/*
 * Check the drive if the disk is changed. Should be revised.
 */
static int sjcd_disk_change( kdev_t full_dev ){
#if 0
  printk( "sjcd_disk_change( 0x%x )\n", full_dev );
#endif
  if( MINOR( full_dev ) > 0 ){
    printk( "sjcd: request error: invalid device minor.\n" );
    return 0;
  }
  if( !sjcd_command_is_in_progress )
    sjcd_get_status();
  return( sjcd_status_valid ? sjcd_media_is_changed : 0 );
}

/*
 * Read the table of contents (TOC) and TOC header if necessary.
 * We assume that the drive contains no more than 99 toc entries.
 */
static struct sjcd_hw_disk_info sjcd_table_of_contents[ SJCD_MAX_TRACKS ];
static unsigned char sjcd_first_track_no, sjcd_last_track_no;
#define sjcd_disk_length  sjcd_table_of_contents[0].un.track_msf

static int sjcd_update_toc( void ){
  struct sjcd_hw_disk_info info;
  int i;
#if defined( SJCD_TRACE )
  printk( "sjcd: update toc:\n" );
#endif
  /*
   * check to see if we need to do anything
   */
  if( sjcd_toc_uptodate ) return( 0 );

  /*
   * Get the TOC start information.
   */
  sjcd_send_1_cmd( SCMD_GET_DISK_INFO, SCMD_GET_1_TRACK );
  sjcd_receive_status();

  if( !sjcd_status_valid ){
    printk( "cannot load status.\n" );
    return( -1 );
  }

  if( !sjcd_media_is_available ){
    printk( "no disk in drive\n" );
    return( -1 );
  }

  if( !sjcd_command_failed ){
    if( sjcd_load_response( &info, sizeof( info ) ) != 0 ){
      printk( "cannot load response about TOC start.\n" );
      return( -1 );
    }
    sjcd_first_track_no = bcd2bin( info.un.track_no );
  } else {
    printk( "get first failed\n" );
    return( -1 );
  }
#if defined( SJCD_TRACE )
  printk( "TOC start 0x%02x ", sjcd_first_track_no );
#endif
  /*
   * Get the TOC finish information.
   */
  sjcd_send_1_cmd( SCMD_GET_DISK_INFO, SCMD_GET_L_TRACK );
  sjcd_receive_status();

  if( !sjcd_status_valid ){
    printk( "cannot load status.\n" );
    return( -1 );
  }

  if( !sjcd_media_is_available ){
    printk( "no disk in drive\n" );
    return( -1 );
  }

  if( !sjcd_command_failed ){
    if( sjcd_load_response( &info, sizeof( info ) ) != 0 ){
      printk( "cannot load response about TOC finish.\n" );
      return( -1 );
    }
    sjcd_last_track_no = bcd2bin( info.un.track_no );
  } else {
    printk( "get last failed\n" );
    return( -1 );
  }
#if defined( SJCD_TRACE )
  printk( "TOC finish 0x%02x ", sjcd_last_track_no );
#endif
  for( i = sjcd_first_track_no; i <= sjcd_last_track_no; i++ ){
    /*
     * Get the first track information.
     */
    sjcd_send_1_cmd( SCMD_GET_DISK_INFO, bin2bcd( i ) );
    sjcd_receive_status();

    if( !sjcd_status_valid ){
      printk( "cannot load status.\n" );
      return( -1 );
    }

    if( !sjcd_media_is_available ){
      printk( "no disk in drive\n" );
      return( -1 );
    }

    if( !sjcd_command_failed ){
      if( sjcd_load_response( &sjcd_table_of_contents[ i ],
			     sizeof( struct sjcd_hw_disk_info ) ) != 0 ){
	printk( "cannot load info for %d track\n", i );
	return( -1 );
      }
    } else {
      printk( "get info %d failed\n", i );
      return( -1 );
    }
  }

  /*
   * Get the disk lenght info.
   */
  sjcd_send_1_cmd( SCMD_GET_DISK_INFO, SCMD_GET_D_SIZE );
  sjcd_receive_status();

  if( !sjcd_status_valid ){
    printk( "cannot load status.\n" );
    return( -1 );
  }

  if( !sjcd_media_is_available ){
    printk( "no disk in drive\n" );
    return( -1 );
  }

  if( !sjcd_command_failed ){
    if( sjcd_load_response( &info, sizeof( info ) ) != 0 ){
      printk( "cannot load response about disk size.\n" );
      return( -1 );
    }
    sjcd_disk_length.min = info.un.track_msf.min;
    sjcd_disk_length.sec = info.un.track_msf.sec;
    sjcd_disk_length.frame = info.un.track_msf.frame;
  } else {
    printk( "get size failed\n" );
    return( 1 );
  }
#if defined( SJCD_TRACE )
  printk( "(%02x:%02x.%02x)\n", sjcd_disk_length.min,
	 sjcd_disk_length.sec, sjcd_disk_length.frame );
#endif
  return( 0 );
}

/*
 * Load subchannel information.
 */
static int sjcd_get_q_info( struct sjcd_hw_qinfo *qp ){
  int s;
#if defined( SJCD_TRACE )
  printk( "sjcd: load sub q\n" );
#endif
  sjcd_send_cmd( SCMD_GET_QINFO );
  s = sjcd_receive_status();
  if( s < 0 || sjcd_command_failed || !sjcd_status_valid ){
    sjcd_send_cmd( 0xF2 );
    s = sjcd_receive_status();
    if( s < 0 || sjcd_command_failed || !sjcd_status_valid ) return( -1 );
    sjcd_send_cmd( SCMD_GET_QINFO );
    s = sjcd_receive_status();
    if( s < 0 || sjcd_command_failed || !sjcd_status_valid ) return( -1 );
  }
  if( sjcd_media_is_available )
    if( sjcd_load_response( qp, sizeof( *qp ) ) == 0 ) return( 0 );
  return( -1 );
}

/*
 * Start playing from the specified position.
 */
static int sjcd_play( struct sjcd_play_msf *mp ){
  struct sjcd_play_msf msf;

  /*
   * Turn the device to play mode.
   */
  sjcd_send_1_cmd( SCMD_SET_MODE, SCMD_MODE_PLAY );
  if( sjcd_receive_status() < 0 ) return( -1 );

  /*
   * Seek to the starting point.
   */
  msf.start = mp->start;
  msf.end.min = msf.end.sec = msf.end.frame = 0x00;
  sjcd_send_6_cmd( SCMD_SEEK, &msf );
  if( sjcd_receive_status() < 0 ) return( -1 );

  /*
   * Start playing.
   */
  sjcd_send_6_cmd( SCMD_PLAY, mp );
  return( sjcd_receive_status() );
}

/*
 * Tray control functions.
 */
static int sjcd_tray_close( void ){
#if defined( SJCD_TRACE )
  printk( "sjcd: tray_close\n" );
#endif
  sjcd_send_cmd( SCMD_CLOSE_TRAY );
  return( sjcd_receive_status() );
}

static int sjcd_tray_lock( void ){
#if defined( SJCD_TRACE )
  printk( "sjcd: tray_lock\n" );
#endif
  sjcd_send_cmd( SCMD_LOCK_TRAY );
  return( sjcd_receive_status() );
}

static int sjcd_tray_unlock( void ){
#if defined( SJCD_TRACE )
  printk( "sjcd: tray_unlock\n" );
#endif
  sjcd_send_cmd( SCMD_UNLOCK_TRAY );
  return( sjcd_receive_status() );
}

static int sjcd_tray_open( void ){
#if defined( SJCD_TRACE )
  printk( "sjcd: tray_open\n" );
#endif
  sjcd_send_cmd( SCMD_EJECT_TRAY );
  return( sjcd_receive_status() );
}

/*
 * Do some user commands.
 */
static int sjcd_ioctl( struct inode *ip, struct file *fp,
		       unsigned int cmd, unsigned long arg ){
#if defined( SJCD_TRACE )
  printk( "sjcd:ioctl\n" );
#endif

  if( ip == NULL ) return( -EINVAL );

  sjcd_get_status();
  if( !sjcd_status_valid ) return( -EIO );
  if( sjcd_update_toc() < 0 ) return( -EIO );

  switch( cmd ){
  case CDROMSTART:{
#if defined( SJCD_TRACE )
    printk( "sjcd: ioctl: start\n" );
#endif
    return( 0 );
  }

  case CDROMSTOP:{
#if defined( SJCD_TRACE )
    printk( "sjcd: ioctl: stop\n" );
#endif
    sjcd_send_cmd( SCMD_PAUSE );
    ( void )sjcd_receive_status();
    sjcd_audio_status = CDROM_AUDIO_NO_STATUS;
    return( 0 );
  }

  case CDROMPAUSE:{
    struct sjcd_hw_qinfo q_info;
#if defined( SJCD_TRACE )
    printk( "sjcd: ioctl: pause\n" );
#endif
    if( sjcd_audio_status == CDROM_AUDIO_PLAY ){
      sjcd_send_cmd( SCMD_PAUSE );
      ( void )sjcd_receive_status();
      if( sjcd_get_q_info( &q_info ) < 0 ){
	sjcd_audio_status = CDROM_AUDIO_NO_STATUS;
      } else {
	sjcd_audio_status = CDROM_AUDIO_PAUSED;
	sjcd_playing.start = q_info.abs;
      }
      return( 0 );
    } else return( -EINVAL );
  }

  case CDROMRESUME:{
#if defined( SJCD_TRACE )
    printk( "sjcd: ioctl: resume\n" );
#endif
    if( sjcd_audio_status == CDROM_AUDIO_PAUSED ){
      /*
       * continue play starting at saved location
       */
      if( sjcd_play( &sjcd_playing ) < 0 ){
	sjcd_audio_status = CDROM_AUDIO_ERROR;
	return( -EIO );
      } else {
	sjcd_audio_status = CDROM_AUDIO_PLAY;
	return( 0 );
      }
    } else return( -EINVAL );
  }

  case CDROMPLAYTRKIND:{
    struct cdrom_ti ti; int s;
#if defined( SJCD_TRACE )
    printk( "sjcd: ioctl: playtrkind\n" );
#endif
    if( ( s = verify_area( VERIFY_READ, (void *)arg, sizeof( ti ) ) ) == 0 ){
      memcpy_fromfs( &ti, (void *)arg, sizeof( ti ) );

      if( ti.cdti_trk0 < sjcd_first_track_no ) return( -EINVAL );
      if( ti.cdti_trk1 > sjcd_last_track_no )
	ti.cdti_trk1 = sjcd_last_track_no;
      if( ti.cdti_trk0 > ti.cdti_trk1 ) return( -EINVAL );

      sjcd_playing.start = sjcd_table_of_contents[ ti.cdti_trk0 ].un.track_msf;
      sjcd_playing.end = ( ti.cdti_trk1 < sjcd_last_track_no ) ?
	sjcd_table_of_contents[ ti.cdti_trk1 + 1 ].un.track_msf :
	  sjcd_table_of_contents[ 0 ].un.track_msf;
      
      if( sjcd_play( &sjcd_playing ) < 0 ){
	sjcd_audio_status = CDROM_AUDIO_ERROR;
	return( -EIO );
      } else sjcd_audio_status = CDROM_AUDIO_PLAY;
    }
    return( s );
  }

  case CDROMPLAYMSF:{
    struct cdrom_msf sjcd_msf; int s;
#if defined( SJCD_TRACE )
    printk( "sjcd: ioctl: playmsf\n" );
#endif
    if( ( s = verify_area( VERIFY_READ, (void *)arg, sizeof( sjcd_msf ) ) ) == 0 ){
      if( sjcd_audio_status == CDROM_AUDIO_PLAY ){
	sjcd_send_cmd( SCMD_PAUSE );
	( void )sjcd_receive_status();
	sjcd_audio_status = CDROM_AUDIO_NO_STATUS;
      }

      memcpy_fromfs( &sjcd_msf, (void *)arg, sizeof( sjcd_msf ) );

      sjcd_playing.start.min = bin2bcd( sjcd_msf.cdmsf_min0 );
      sjcd_playing.start.sec = bin2bcd( sjcd_msf.cdmsf_sec0 );
      sjcd_playing.start.frame = bin2bcd( sjcd_msf.cdmsf_frame0 );
      sjcd_playing.end.min = bin2bcd( sjcd_msf.cdmsf_min1 );
      sjcd_playing.end.sec = bin2bcd( sjcd_msf.cdmsf_sec1 );
      sjcd_playing.end.frame = bin2bcd( sjcd_msf.cdmsf_frame1 );

      if( sjcd_play( &sjcd_playing ) < 0 ){
	sjcd_audio_status = CDROM_AUDIO_ERROR;
	return( -EIO );
      } else sjcd_audio_status = CDROM_AUDIO_PLAY;
    }
    return( s );
  }

  case CDROMREADTOCHDR:{
    struct cdrom_tochdr toc_header; int s;
#if defined (SJCD_TRACE )
    printk( "sjcd: ioctl: readtocheader\n" );
#endif
    if( ( s = verify_area( VERIFY_WRITE, (void *)arg, sizeof( toc_header ) ) ) == 0 ){
      toc_header.cdth_trk0 = sjcd_first_track_no;
      toc_header.cdth_trk1 = sjcd_last_track_no;
      memcpy_tofs( (void *)arg, &toc_header, sizeof( toc_header ) );
    }
    return( s );
  }

  case CDROMREADTOCENTRY:{
    struct cdrom_tocentry toc_entry; int s;
#if defined( SJCD_TRACE )
    printk( "sjcd: ioctl: readtocentry\n" );
#endif
    if( ( s = verify_area( VERIFY_WRITE, (void *)arg, sizeof( toc_entry ) ) ) == 0 ){
      struct sjcd_hw_disk_info *tp;

      memcpy_fromfs( &toc_entry, (void *)arg, sizeof( toc_entry ) );

      if( toc_entry.cdte_track == CDROM_LEADOUT )
	tp = &sjcd_table_of_contents[ 0 ];
      else if( toc_entry.cdte_track < sjcd_first_track_no ) return( -EINVAL );
      else if( toc_entry.cdte_track > sjcd_last_track_no ) return( -EINVAL );
      else tp = &sjcd_table_of_contents[ toc_entry.cdte_track ];

      toc_entry.cdte_adr = tp->track_control & 0x0F;
      toc_entry.cdte_ctrl = tp->track_control >> 4;

      switch( toc_entry.cdte_format ){
      case CDROM_LBA:
	toc_entry.cdte_addr.lba = msf2hsg( &( tp->un.track_msf ) );
	break;
      case CDROM_MSF:
	toc_entry.cdte_addr.msf.minute = bcd2bin( tp->un.track_msf.min );
	toc_entry.cdte_addr.msf.second = bcd2bin( tp->un.track_msf.sec );
	toc_entry.cdte_addr.msf.frame = bcd2bin( tp->un.track_msf.frame );
	break;
      default: return( -EINVAL );
      }
      memcpy_tofs( (void *)arg, &toc_entry, sizeof( toc_entry ) );
    }
    return( s );
  }

  case CDROMSUBCHNL:{
    struct cdrom_subchnl subchnl; int s;
#if defined( SJCD_TRACE )
    printk( "sjcd: ioctl: subchnl\n" );
#endif
    if( ( s = verify_area( VERIFY_WRITE, (void *)arg, sizeof( subchnl ) ) ) == 0 ){
      struct sjcd_hw_qinfo q_info;

      memcpy_fromfs( &subchnl, (void *)arg, sizeof( subchnl ) );
      if( sjcd_get_q_info( &q_info ) < 0 ) return( -EIO );

      subchnl.cdsc_audiostatus = sjcd_audio_status;
      subchnl.cdsc_adr = q_info.track_control & 0x0F;
      subchnl.cdsc_ctrl = q_info.track_control >> 4;
      subchnl.cdsc_trk = bcd2bin( q_info.track_no );
      subchnl.cdsc_ind = bcd2bin( q_info.x );

      switch( subchnl.cdsc_format ){
      case CDROM_LBA:
	subchnl.cdsc_absaddr.lba = msf2hsg( &( q_info.abs ) );
	subchnl.cdsc_reladdr.lba = msf2hsg( &( q_info.rel ) );
	break;
      case CDROM_MSF:
	subchnl.cdsc_absaddr.msf.minute = bcd2bin( q_info.abs.min );
	subchnl.cdsc_absaddr.msf.second = bcd2bin( q_info.abs.sec );
	subchnl.cdsc_absaddr.msf.frame = bcd2bin( q_info.abs.frame );
	subchnl.cdsc_reladdr.msf.minute = bcd2bin( q_info.rel.min );
	subchnl.cdsc_reladdr.msf.second = bcd2bin( q_info.rel.sec );
	subchnl.cdsc_reladdr.msf.frame = bcd2bin( q_info.rel.frame );
	break;
      default: return( -EINVAL );
      }
      memcpy_tofs( (void *)arg, &subchnl, sizeof( subchnl ) );
    }
    return( s );
  }

  case CDROMVOLCTRL:{
    struct cdrom_volctrl vol_ctrl; int s;
#if defined( SJCD_TRACE )
    printk( "sjcd: ioctl: volctrl\n" );
#endif
    if( ( s = verify_area( VERIFY_READ, (void *)arg, sizeof( vol_ctrl ) ) ) == 0 ){
      unsigned char dummy[ 4 ];

      memcpy_fromfs( &vol_ctrl, (void *)arg, sizeof( vol_ctrl ) );
      sjcd_send_4_cmd( SCMD_SET_VOLUME, vol_ctrl.channel0, 0xFF,
		      vol_ctrl.channel1, 0xFF );
      if( sjcd_receive_status() < 0 ) return( -EIO );
      ( void )sjcd_load_response( dummy, 4 );
    }
    return( s );
  }

  case CDROMEJECT:{
#if defined( SJCD_TRACE )
    printk( "sjcd: ioctl: eject\n" );
#endif
    if( !sjcd_command_is_in_progress ){
      sjcd_tray_unlock();
      sjcd_send_cmd( SCMD_EJECT_TRAY );
      ( void )sjcd_receive_status();
    }
    return( 0 );
  }

#if defined( SJCD_GATHER_STAT )
  case 0xABCD:{
    int s;
#if defined( SJCD_TRACE )
    printk( "sjcd: ioctl: statistic\n" );
#endif
    if( ( s = verify_area( VERIFY_WRITE, (void *)arg, sizeof( statistic ) ) ) == 0 )
      memcpy_tofs( (void *)arg, &statistic, sizeof( statistic ) );
    return( s );
  }
#endif

  default:
    return( -EINVAL );
  }
}

/*
 * Invalidate internal buffers of the driver.
 */
static void sjcd_invalidate_buffers( void ){
  int i;
  for( i = 0; i < SJCD_BUF_SIZ; sjcd_buf_bn[ i++ ] = -1 );
  sjcd_buf_out = -1;
}

/*
 * Take care of the different block sizes between cdrom and Linux.
 * When Linux gets variable block sizes this will probably go away.
 */

#define CURRENT_IS_VALID                                      \
    ( CURRENT != NULL && MAJOR( CURRENT->rq_dev ) == MAJOR_NR && \
      CURRENT->cmd == READ && CURRENT->sector != -1 )

static void sjcd_transfer( void ){
#if defined( SJCD_TRACE )
  printk( "sjcd: transfer:\n" );
#endif
  if( CURRENT_IS_VALID ){
    while( CURRENT->nr_sectors ){
      int i, bn = CURRENT->sector / 4;
      for( i = 0; i < SJCD_BUF_SIZ && sjcd_buf_bn[ i ] != bn; i++ );
      if( i < SJCD_BUF_SIZ ){
	int offs = ( i * 4 + ( CURRENT->sector & 3 ) ) * 512;
	int nr_sectors = 4 - ( CURRENT->sector & 3 );
	if( sjcd_buf_out != i ){
	  sjcd_buf_out = i;
	  if( sjcd_buf_bn[ i ] != bn ){
	    sjcd_buf_out = -1;
	    continue;
	  }
	}
	if( nr_sectors > CURRENT->nr_sectors )
	  nr_sectors = CURRENT->nr_sectors;
#if defined( SJCD_TRACE )
	printk( "copy out\n" );
#endif
	memcpy( CURRENT->buffer, sjcd_buf + offs, nr_sectors * 512 );
	CURRENT->nr_sectors -= nr_sectors;
	CURRENT->sector += nr_sectors;
	CURRENT->buffer += nr_sectors * 512;
      } else {
	sjcd_buf_out = -1;
	break;
      }
    }
  }
#if defined( SJCD_TRACE )
  printk( "sjcd: transfer: done\n" );
#endif
}

static void sjcd_poll( void ){
#if defined( SJCD_GATHER_STAT )
  /*
   * Update total number of ticks.
   */
  statistic.ticks++;
  statistic.tticks[ sjcd_transfer_state ]++;
#endif

 ReSwitch: switch( sjcd_transfer_state ){
      
  case SJCD_S_IDLE:{
#if defined( SJCD_GATHER_STAT )
    statistic.idle_ticks++;
#endif
#if defined( SJCD_TRACE )
    printk( "SJCD_S_IDLE\n" );
#endif
    return;
  }

  case SJCD_S_START:{
#if defined( SJCD_GATHER_STAT )
    statistic.start_ticks++;
#endif
    sjcd_send_cmd( SCMD_GET_STATUS );
    sjcd_transfer_state =
      sjcd_mode == SCMD_MODE_COOKED ? SJCD_S_READ : SJCD_S_MODE;
    sjcd_transfer_timeout = 500;
#if defined( SJCD_TRACE )
    printk( "SJCD_S_START: goto SJCD_S_%s mode\n",
	   sjcd_transfer_state == SJCD_S_READ ? "READ" : "MODE" );
#endif
    break;
  }
    
  case SJCD_S_MODE:{
    if( sjcd_check_status() ){
      /*
       * Previous command is completed.
       */
      if( !sjcd_status_valid || sjcd_command_failed ){
#if defined( SJCD_TRACE )
	printk( "SJCD_S_MODE: pre-cmd failed: goto to SJCD_S_STOP mode\n" );
#endif
	sjcd_transfer_state = SJCD_S_STOP;
	goto ReSwitch;
      }

      sjcd_mode = 0; /* unknown mode; should not be valid when failed */
      sjcd_send_1_cmd( SCMD_SET_MODE, SCMD_MODE_COOKED );
      sjcd_transfer_state = SJCD_S_READ; sjcd_transfer_timeout = 1000;
#if defined( SJCD_TRACE )
      printk( "SJCD_S_MODE: goto SJCD_S_READ mode\n" );
#endif
    }
#if defined( SJCD_GATHER_STAT )
    else statistic.mode_ticks++;
#endif
    break;
  }

  case SJCD_S_READ:{
    if( sjcd_status_valid ? 1 : sjcd_check_status() ){
      /*
       * Previos command is completed.
       */
      if( !sjcd_status_valid || sjcd_command_failed ){
#if defined( SJCD_TRACE )
	printk( "SJCD_S_READ: pre-cmd failed: goto to SJCD_S_STOP mode\n" );
#endif
	sjcd_transfer_state = SJCD_S_STOP;
	goto ReSwitch;
      }
      if( !sjcd_media_is_available ){
#if defined( SJCD_TRACE )
	printk( "SJCD_S_READ: no disk: goto to SJCD_S_STOP mode\n" );
#endif
	sjcd_transfer_state = SJCD_S_STOP;
	goto ReSwitch;
      }
      if( sjcd_mode != SCMD_MODE_COOKED ){
	/*
	 * We seem to come from set mode. So discard one byte of result.
	 */
	if( sjcd_load_response( &sjcd_mode, 1 ) != 0 ){
#if defined( SJCD_TRACE )
	  printk( "SJCD_S_READ: load failed: goto to SJCD_S_STOP mode\n" );
#endif
	  sjcd_transfer_state = SJCD_S_STOP;
	  goto ReSwitch;
	}
	if( sjcd_mode != SCMD_MODE_COOKED ){
#if defined( SJCD_TRACE )
	  printk( "SJCD_S_READ: mode failed: goto to SJCD_S_STOP mode\n" );
#endif
	  sjcd_transfer_state = SJCD_S_STOP;
	  goto ReSwitch;
	}
      }

      if( CURRENT_IS_VALID ){
	struct sjcd_play_msf msf;

	sjcd_next_bn = CURRENT->sector / 4;
	hsg2msf( sjcd_next_bn, &msf.start );
	msf.end.min = 0; msf.end.sec = 0;            
	msf.end.frame = sjcd_read_count = SJCD_BUF_SIZ;
#if defined( SJCD_TRACE )
	printk( "---reading msf-address %x:%x:%x  %x:%x:%x\n",
	       msf.start.min, msf.start.sec, msf.start.frame,
	       msf.end.min,   msf.end.sec,   msf.end.frame );
	printk( "sjcd_next_bn:%x buf_in:%x buf_out:%x buf_bn:%x\n", \
	     sjcd_next_bn, sjcd_buf_in, sjcd_buf_out,
	     sjcd_buf_bn[ sjcd_buf_in ] );
#endif	
	sjcd_send_6_cmd( SCMD_DATA_READ, &msf );
	sjcd_transfer_state = SJCD_S_DATA;
	sjcd_transfer_timeout = 500;
#if defined( SJCD_TRACE )
	printk( "SJCD_S_READ: go to SJCD_S_DATA mode\n" );
#endif
      } else {
#if defined( SJCD_TRACE )
	printk( "SJCD_S_READ: nothing to read: go to SJCD_S_STOP mode\n" );
#endif
	sjcd_transfer_state = SJCD_S_STOP;
	goto ReSwitch;
      }
    }
#if defined( SJCD_GATHER_STAT )
    else statistic.read_ticks++;
#endif
    break;
  }

  case SJCD_S_DATA:{
    unsigned char stat;

  sjcd_s_data: stat = inb( SJCDPORT( 1 ) );
#if defined( SJCD_TRACE )
    printk( "SJCD_S_DATA: status = 0x%02x\n", stat );
#endif
    if( SJCD_STATUS_AVAILABLE( stat ) ){
      /*
       * No data is waiting for us in the drive buffer. Status of operation
       * completion is available. Read and parse it.
       */
      sjcd_load_status();

      if( !sjcd_status_valid || sjcd_command_failed ){
#if defined( SJCD_TRACE )
	printk( "sjcd: read block %d failed, maybe audio disk? Giving up\n",
	       sjcd_next_bn );
#endif
	if( CURRENT_IS_VALID ) end_request( 0 );
#if defined( SJCD_TRACE )
	printk( "SJCD_S_DATA: pre-cmd failed: go to SJCD_S_STOP mode\n" );
#endif
	sjcd_transfer_state = SJCD_S_STOP;
	goto ReSwitch;
      }

      if( !sjcd_media_is_available ){
	printk( "SJCD_S_DATA: no disk: go to SJCD_S_STOP mode\n" );
	sjcd_transfer_state = SJCD_S_STOP;
	goto ReSwitch;
      }

      sjcd_transfer_state = SJCD_S_READ;
      goto ReSwitch;
    } else if( SJCD_DATA_AVAILABLE( stat ) ){
      /*
       * One frame is read into device buffer. We must copy it to our memory.
       * Otherwise cdrom hangs up. Check to see if we have something to copy
       * to.
       */
      if( !CURRENT_IS_VALID && sjcd_buf_in == sjcd_buf_out ){
#if defined( SJCD_TRACE )
	printk( "SJCD_S_DATA: nothing to read: go to SJCD_S_STOP mode\n" );
	printk( " ... all the date would be discarded\n" );
#endif
	sjcd_transfer_state = SJCD_S_STOP;
	goto ReSwitch;
      }

      /*
       * Everything seems to be OK. Just read the frame and recalculate
       * indecis.
       */
      sjcd_buf_bn[ sjcd_buf_in ] = -1; /* ??? */
      insb( SJCDPORT( 2 ), sjcd_buf + 2048 * sjcd_buf_in, 2048 );
#if defined( SJCD_TRACE )
      printk( "SJCD_S_DATA: next_bn=%d, buf_in=%d, buf_out=%d, buf_bn=%d\n",
	     sjcd_next_bn, sjcd_buf_in, sjcd_buf_out,
	     sjcd_buf_bn[ sjcd_buf_in ] );
#endif
      sjcd_buf_bn[ sjcd_buf_in ] = sjcd_next_bn++;
      if( sjcd_buf_out == -1 ) sjcd_buf_out = sjcd_buf_in;
      if( ++sjcd_buf_in == SJCD_BUF_SIZ ) sjcd_buf_in = 0;

      /*
       * Only one frame is ready at time. So we should turn over to wait for
       * another frame. If we need that, of course.
       */
      if( --sjcd_read_count == 0 ){
	/*
	 * OK, request seems to be precessed. Continue transferring...
	 */
	if( !sjcd_transfer_is_active ){
	  while( CURRENT_IS_VALID ){
	    /*
	     * Continue transferring.
	     */
	    sjcd_transfer();
	    if( CURRENT->nr_sectors == 0 ) end_request( 1 );
	    else break;
	  }
	}
	if( CURRENT_IS_VALID &&
	   ( CURRENT->sector / 4 < sjcd_next_bn ||
	    CURRENT->sector / 4 > sjcd_next_bn + SJCD_BUF_SIZ ) ){
#if defined( SJCD_TRACE )
	  printk( "SJCD_S_DATA: can't read: go to SJCD_S_STOP mode\n" );
#endif
	  sjcd_transfer_state = SJCD_S_STOP;
	  goto ReSwitch;
	}
      }
      /*
       * Now we should turn around rather than wait for while.
       */
      goto sjcd_s_data;
    }
#if defined( SJCD_GATHER_STAT )
    else statistic.data_ticks++;
#endif
    break;
  }

  case SJCD_S_STOP:{
    sjcd_read_count = 0;
    sjcd_send_cmd( SCMD_STOP );
    sjcd_transfer_state = SJCD_S_STOPPING;
    sjcd_transfer_timeout = 500;
#if defined( SJCD_GATHER_STAT )
    statistic.stop_ticks++;
#endif
    break;
  }

  case SJCD_S_STOPPING:{
    unsigned char stat;
    
    stat = inb( SJCDPORT( 1 ) );
#if defined( SJCD_TRACE )
    printk( "SJCD_S_STOP: status = 0x%02x\n", stat );
#endif      
    if( SJCD_DATA_AVAILABLE( stat ) ){
      int i;
#if defined( SJCD_TRACE )
      printk( "SJCD_S_STOP: discard data\n" );
#endif
      /*
       * Discard all the data from the pipe. Foolish method.
       */
      for( i = 2048; i--; ( void )inb( SJCDPORT( 2 ) ) );
      sjcd_transfer_timeout = 500;
    } else if( SJCD_STATUS_AVAILABLE( stat ) ){
      sjcd_load_status();
      if( sjcd_status_valid && sjcd_media_is_changed ) {
	sjcd_toc_uptodate = 0;
	sjcd_invalidate_buffers();
      }
      if( CURRENT_IS_VALID ){
	if( sjcd_status_valid ) sjcd_transfer_state = SJCD_S_READ;
	else sjcd_transfer_state = SJCD_S_START;
      } else sjcd_transfer_state = SJCD_S_IDLE;
      goto ReSwitch;
    }
#if defined( SJCD_GATHER_STAT )
    else statistic.stopping_ticks++;
#endif
    break;
  }

  default:
    printk( "sjcd_poll: invalid state %d\n", sjcd_transfer_state );
    return;
  }
  
  if( --sjcd_transfer_timeout == 0 ){
    printk( "sjcd: timeout in state %d\n", sjcd_transfer_state );
    while( CURRENT_IS_VALID ) end_request( 0 );
    sjcd_send_cmd( SCMD_STOP );
    sjcd_transfer_state = SJCD_S_IDLE;
    goto ReSwitch;
    }

  /*
   * Get back in some time. 1 should be replaced with count variable to
   * avoid unnecessary testings.
   */
  SJCD_SET_TIMER( sjcd_poll, 1 );
}

static void do_sjcd_request( void ){
#if defined( SJCD_TRACE )
  printk( "sjcd: do_sjcd_request(%ld+%ld)\n",
	 CURRENT->sector, CURRENT->nr_sectors );
#endif
  sjcd_transfer_is_active = 1;
  while( CURRENT_IS_VALID ){
    /*
     * Who of us are paranoic?
     */
    if( CURRENT->bh && !buffer_locked(CURRENT->bh) )
      panic( DEVICE_NAME ": block not locked" );

    sjcd_transfer();
    if( CURRENT->nr_sectors == 0 ) end_request( 1 );
    else {
      sjcd_buf_out = -1;         /* Want to read a block not in buffer */
      if( sjcd_transfer_state == SJCD_S_IDLE ){
	if( !sjcd_toc_uptodate ){
	  if( sjcd_update_toc() < 0 ){
	    printk( "sjcd: transfer: discard\n" );
	    while( CURRENT_IS_VALID ) end_request( 0 );
	    break;
	  }
	}
	sjcd_transfer_state = SJCD_S_START;
	SJCD_SET_TIMER( sjcd_poll, HZ/100 );
      }
      break;
    }
  }
  sjcd_transfer_is_active = 0;
#if defined( SJCD_TRACE )
  printk( "sjcd_next_bn:%x sjcd_buf_in:%x sjcd_buf_out:%x sjcd_buf_bn:%x\n",
	 sjcd_next_bn, sjcd_buf_in, sjcd_buf_out, sjcd_buf_bn[ sjcd_buf_in ] );
  printk( "do_sjcd_request ends\n" );
#endif
}

/*
 * Open the device special file. Check disk is in.
 */
int sjcd_open( struct inode *ip, struct file *fp ){
  /*
   * Check the presence of device.
   */
  if( !sjcd_present ) return( -ENXIO );
  
  /*
   * Only read operations are allowed. Really? (:-)
   */
  if( fp->f_mode & 2 ) return( -EROFS );
  
  if( sjcd_open_count == 0 ){
    int s, sjcd_open_tries;
/* We don't know that, do we? */
/*
    sjcd_audio_status = CDROM_AUDIO_NO_STATUS;
*/
    sjcd_mode = 0;
    sjcd_door_was_open = 0;
    sjcd_transfer_state = SJCD_S_IDLE;
    sjcd_invalidate_buffers();
    sjcd_status_valid = 0;

    /*
     * Strict status checking.
     */
    for( sjcd_open_tries = 4; --sjcd_open_tries; ){
      if( !sjcd_status_valid ) sjcd_get_status();
      if( !sjcd_status_valid ){
#if defined( SJCD_DIAGNOSTIC )
	printk( "sjcd: open: timed out when check status.\n" );
#endif
	return( -EIO );
      } else if( !sjcd_media_is_available ){
#if defined( SJCD_DIAGNOSTIC )
	printk("sjcd: open: no disk in drive\n");
#endif
	if( !sjcd_door_closed ){
	  sjcd_door_was_open = 1;
#if defined( SJCD_TRACE )
	  printk("sjcd: open: close the tray\n");
#endif
	  s = sjcd_tray_close();
	  if( s < 0 || !sjcd_status_valid || sjcd_command_failed ){
#if defined( SJCD_DIAGNOSTIC )
	    printk("sjcd: open: tray close attempt failed\n");
#endif
	    return( -EIO );
	  }
	  continue;
	} else return( -EIO );
      }
      break;
    }
    s = sjcd_tray_lock();
    if( s < 0 || !sjcd_status_valid || sjcd_command_failed ){
#if defined( SJCD_DIAGNOSTIC )
      printk("sjcd: open: tray lock attempt failed\n");
#endif
      return( -EIO );
    }
#if defined( SJCD_TRACE )
    printk( "sjcd: open: done\n" );
#endif
  }
  MOD_INC_USE_COUNT;
  ++sjcd_open_count;
  return( 0 );
}

/*
 * On close, we flush all sjcd blocks from the buffer cache.
 */
static void sjcd_release( struct inode *inode, struct file *file ){
  int s;

#if defined( SJCD_TRACE )
  printk( "sjcd: release\n" );
#endif
  MOD_DEC_USE_COUNT;
  if( --sjcd_open_count == 0 ){
    sjcd_invalidate_buffers();
    sync_dev( inode->i_rdev );
    invalidate_buffers( inode->i_rdev );
    s = sjcd_tray_unlock();
    if( s < 0 || !sjcd_status_valid || sjcd_command_failed ){
#if defined( SJCD_DIAGNOSTIC )
      printk("sjcd: release: tray unlock attempt failed.\n");
#endif
    }
    if( sjcd_door_was_open ){
      s = sjcd_tray_open();
      if( s < 0 || !sjcd_status_valid || sjcd_command_failed ){
#if defined( SJCD_DIAGNOSTIC )
	printk("sjcd: release: tray unload attempt failed.\n");
#endif
      }
    }
  }
}

/*
 * A list of file operations allowed for this cdrom.
 */
static struct file_operations sjcd_fops = {
  NULL,               /* lseek - default */
  block_read,         /* read - general block-dev read */
  block_write,        /* write - general block-dev write */
  NULL,               /* readdir - bad */
  NULL,               /* select */
  sjcd_ioctl,         /* ioctl */
  NULL,               /* mmap */
  sjcd_open,          /* open */
  sjcd_release,       /* release */
  NULL,               /* fsync */
  NULL,               /* fasync */
  sjcd_disk_change,   /* media change */
  NULL                /* revalidate */
};

/*
 * Following stuff is intended for initialization of the cdrom. It
 * first looks for presence of device. If the device is present, it
 * will be reset. Then read the version of the drive and load status.
 * The version is two BCD-coded bytes.
 */
static struct {
  unsigned char major, minor;
} sjcd_version;

/*
 * Test for presence of drive and initialize it. Called at boot time.
 * Probe cdrom, find out version and status.
 */
int sjcd_init( void ){
  int i;

  if ( (isp16_type=isp16_detect()) < 0 )
    printk( "No ISP16 cdrom interface found.\n" );
  else {
    u_char expected_drive;

    printk( "ISP16 cdrom interface (with OPTi 82C92%s chip) detected.\n",
      (isp16_type==2)?"9":"8" );

    printk( "ISP16 sound configuration.\n" );
    isp16_sound_config();

    expected_drive = (isp16_type?ISP16_SANYO1:ISP16_SANYO0);

    if ( isp16_cdi_config( sjcd_port, expected_drive, sjcd_irq, sjcd_dma ) < 0 ) {
      printk( "ISP16 cdrom interface has not been properly configured.\n" );
      return( -EIO );
    }
  }

#if defined( SJCD_TRACE )
  printk( "sjcd=0x%x,%d: ", sjcd_port, sjcd_irq );
#endif  

  if( register_blkdev( MAJOR_NR, "sjcd", &sjcd_fops ) != 0 ){
    printk( "Unable to get major %d for Sanyo CD-ROM\n", MAJOR_NR );
    return( -EIO );
  }
  
  blk_dev[ MAJOR_NR ].request_fn = DEVICE_REQUEST;
  read_ahead[ MAJOR_NR ] = 4;
  
  if( check_region( sjcd_port, 4 ) ){
    printk( "Init failed, I/O port (%X) is already in use\n",
      sjcd_port );
    return( -EIO );
  }
  
  /*
   * Check for card. Since we are booting now, we can't use standard
   * wait algorithm.
   */
  printk( "Sanyo: Resetting: " );
  sjcd_send_cmd( SCMD_RESET );
  for( i = 1000; i-- > 0 && !sjcd_status_valid; ){
    unsigned long timer;

    /*
     * Wait 10ms approx.
     */
    for( timer = jiffies; jiffies <= timer; );
    if ( (i % 100) == 0 ) printk( "." );
    ( void )sjcd_check_status();
  }
  if( i == 0 || sjcd_command_failed ){
    printk( " reset failed, no drive found.\n" );
    return( -EIO );
  } else printk( "\n" );

  /*
   * Get and print out cdrom version.
   */
  printk( "Sanyo: Getting version: " );
  sjcd_send_cmd( SCMD_GET_VERSION );
  for( i = 1000; i > 0 && !sjcd_status_valid; --i ){
    unsigned long timer;

    /*
     * Wait 10ms approx.
     */
    for( timer = jiffies; jiffies <= timer; );
    if ( (i % 100) == 0 ) printk( "." );
    ( void )sjcd_check_status();
  }
  if( i == 0 || sjcd_command_failed ){
    printk( " get version failed, no drive found.\n" );
    return( -EIO );
  }

  if( sjcd_load_response( &sjcd_version, sizeof( sjcd_version ) ) == 0 ){
    printk( " %1x.%02x\n", ( int )sjcd_version.major,
	     ( int )sjcd_version.minor );
  } else {
    printk( " read version failed, no drive found.\n" );
    return( -EIO );
  }

  /*
   * Check and print out the tray state. (if it is needed?).
   */
  if( !sjcd_status_valid ){
    printk( "Sanyo: Getting status: " );
    sjcd_send_cmd( SCMD_GET_STATUS );
    for( i = 1000; i > 0 && !sjcd_status_valid; --i ){
      unsigned long timer;

      /*
       * Wait 10ms approx.
       */
      for( timer = jiffies; jiffies <= timer; );
      if ( (i % 100) == 0 ) printk( "." );
      ( void )sjcd_check_status();
    }
    if( i == 0 || sjcd_command_failed ){
      printk( " get status failed, no drive found.\n" );
      return( -EIO );
    } else printk( "\n" );
  }

  printk( "SANYO CDR-H94A: Status: port=0x%x, irq=%d, dma=%d.\n",
	 sjcd_port, sjcd_irq, sjcd_dma );

  sjcd_present++;
  return( 0 );
}

#ifdef MODULE

int init_module(void)
{
	return sjcd_init();
}

void cleanup_module( void ){
  if( ( unregister_blkdev( MAJOR_NR, "sjcd" ) == -EINVAL ) ){
    printk( "sjcd: module: can not unregister device.\n" );
  } else {
    release_region( sjcd_port, 4 );
    printk( "sjcd: module: removed.\n");
  }
}
#endif

/*
 * -- ISP16 detection and configuration
 *
 *    Copyright (c) 1995, Eric van der Maarel <maarel@marin.nl>
 *
 *    Version 0.5
 *
 *    Detect cdrom interface on ISP16 soundcard.
 *    Configure cdrom interface.
 *    Configure sound interface.
 *
 *    Algorithm for the card with OPTi 82C928 taken
 *    from the CDSETUP.SYS driver for MSDOS,
 *    by OPTi Computers, version 2.03.
 *    Algorithm for the card with OPTi 82C929 as communicated
 *    to me by Vadim Model and Leo Spiekman.
 *
 *    Use, modifification or redistribution of this software is
 *    allowed under the terms of the GPL.
 *
 */


#define ISP16_IN(p) (outb(isp16_ctrl,ISP16_CTRL_PORT), inb(p))
#define ISP16_OUT(p,b) (outb(isp16_ctrl,ISP16_CTRL_PORT), outb(b,p))

static short
isp16_detect(void)
{

  if ( !( isp16_c929__detect() < 0 ) )
    return(2);
  else
    return( isp16_c928__detect() );
}

static short
isp16_c928__detect(void)
{
  u_char ctrl;
  u_char enable_cdrom;
  u_char io;
  short i = -1;

  isp16_ctrl = ISP16_C928__CTRL;
  isp16_enable_port = ISP16_C928__ENABLE_PORT;

  /* read' and write' are a special read and write, respectively */

  /* read' ISP16_CTRL_PORT, clear last two bits and write' back the result */
  ctrl = ISP16_IN( ISP16_CTRL_PORT ) & 0xFC;
  ISP16_OUT( ISP16_CTRL_PORT, ctrl );

  /* read' 3,4 and 5-bit from the cdrom enable port */
  enable_cdrom = ISP16_IN( ISP16_C928__ENABLE_PORT ) & 0x38;

  if ( !(enable_cdrom & 0x20) ) {  /* 5-bit not set */
    /* read' last 2 bits of ISP16_IO_SET_PORT */
    io = ISP16_IN( ISP16_IO_SET_PORT ) & 0x03;
    if ( ((io&0x01)<<1) == (io&0x02) ) {  /* bits are the same */
      if ( io == 0 ) {  /* ...the same and 0 */
        i = 0;
        enable_cdrom |= 0x20;
      }
      else {  /* ...the same and 1 */  /* my card, first time 'round */
        i = 1;
        enable_cdrom |= 0x28;
      }
      ISP16_OUT( ISP16_C928__ENABLE_PORT, enable_cdrom );
    }
    else {  /* bits are not the same */
      ISP16_OUT( ISP16_CTRL_PORT, ctrl );
      return(i); /* -> not detected: possibly incorrect conclusion */
    }
  }
  else if ( enable_cdrom == 0x20 )
    i = 0;
  else if ( enable_cdrom == 0x28 )  /* my card, already initialised */
    i = 1;

  ISP16_OUT( ISP16_CTRL_PORT, ctrl );

  return(i);
}

static short
isp16_c929__detect(void)
{
  u_char ctrl;
  u_char tmp;

  isp16_ctrl = ISP16_C929__CTRL;
  isp16_enable_port = ISP16_C929__ENABLE_PORT;

  /* read' and write' are a special read and write, respectively */

  /* read' ISP16_CTRL_PORT and save */
  ctrl = ISP16_IN( ISP16_CTRL_PORT );

  /* write' zero to the ctrl port and get response */
  ISP16_OUT( ISP16_CTRL_PORT, 0 );
  tmp = ISP16_IN( ISP16_CTRL_PORT );

  if ( tmp != 2 )  /* isp16 with 82C929 not detected */
    return(-1);

  /* restore ctrl port value */
  ISP16_OUT( ISP16_CTRL_PORT, ctrl );
  
  return(2);
}

static short
isp16_cdi_config( int base, u_char drive_type, int irq, int dma )
{
  u_char base_code;
  u_char irq_code;
  u_char dma_code;
  u_char i;

  if ( (drive_type == ISP16_MITSUMI) && (dma != 0) )
    printk( "Mitsumi cdrom drive has no dma support.\n" );

  switch (base) {
  case 0x340: base_code = ISP16_BASE_340; break;
  case 0x330: base_code = ISP16_BASE_330; break;
  case 0x360: base_code = ISP16_BASE_360; break;
  case 0x320: base_code = ISP16_BASE_320; break;
  default:
    printk( "Base address 0x%03X not supported by cdrom interface on ISP16.\n", base );
    return(-1);
  }
  switch (irq) {
  case 0: irq_code = ISP16_IRQ_X; break; /* disable irq */
  case 5: irq_code = ISP16_IRQ_5;
          printk( "Irq 5 shouldn't be used by cdrom interface on ISP16,"
            " due to possible conflicts with the soundcard.\n");
          break;
  case 7: irq_code = ISP16_IRQ_7;
          printk( "Irq 7 shouldn't be used by cdrom interface on ISP16,"
            " due to possible conflicts with the soundcard.\n");
          break;
  case 3: irq_code = ISP16_IRQ_3; break;
  case 9: irq_code = ISP16_IRQ_9; break;
  case 10: irq_code = ISP16_IRQ_10; break;
  case 11: irq_code = ISP16_IRQ_11; break;
  default:
    printk( "Irq %d not supported by cdrom interface on ISP16.\n", irq );
    return(-1);
  }
  switch (dma) {
  case 0: dma_code = ISP16_DMA_X; break;  /* disable dma */
  case 1: printk( "Dma 1 cannot be used by cdrom interface on ISP16,"
            " due to conflict with the soundcard.\n");
          return(-1); break;
  case 3: dma_code = ISP16_DMA_3; break;
  case 5: dma_code = ISP16_DMA_5; break;
  case 6: dma_code = ISP16_DMA_6; break;
  case 7: dma_code = ISP16_DMA_7; break;
  default:
    printk( "Dma %d not supported by cdrom interface on ISP16.\n", dma );
    return(-1);
  }

  if ( drive_type != ISP16_SONY && drive_type != ISP16_PANASONIC0 &&
    drive_type != ISP16_PANASONIC1 && drive_type != ISP16_SANYO0 &&
    drive_type != ISP16_SANYO1 && drive_type != ISP16_MITSUMI &&
    drive_type != ISP16_DRIVE_X ) {
    printk( "Drive type (code 0x%02X) not supported by cdrom"
     " interface on ISP16.\n", drive_type );
    return(-1);
  }

  /* set type of interface */
  i = ISP16_IN(ISP16_DRIVE_SET_PORT) & ISP16_DRIVE_SET_MASK;  /* clear some bits */
  ISP16_OUT( ISP16_DRIVE_SET_PORT, i|drive_type );

  /* enable cdrom on interface with 82C929 chip */
  if ( isp16_type > 1 )
    ISP16_OUT( isp16_enable_port, ISP16_ENABLE_CDROM );

  /* set base address, irq and dma */
  i = ISP16_IN(ISP16_IO_SET_PORT) & ISP16_IO_SET_MASK;  /* keep some bits */
  ISP16_OUT( ISP16_IO_SET_PORT, i|base_code|irq_code|dma_code );

  return(0);
}

static void isp16_sound_config( void )
{
  int i;
  u_char saved;

  saved = ISP16_IN( 0xF8D ) & 0x8F;
    
  ISP16_OUT( 0xF8D, 0x40 );

  /*
   * Now we should wait for a while...
   */
  for( i = 16*1024; i--; );
  
  ISP16_OUT( 0xF8D, saved );

  ISP16_OUT( 0xF91, 0x1B );

  for( i = 5*64*1024; i != 0; i-- )
    if( !( inb( 0x534 ) & 0x80 ) ) break;

  if( i > 0 ) {
    saved = ( inb( 0x534 ) & 0xE0 ) | 0x0A;
    outb( saved, 0x534 );

    special_mask = ( inb( 0x535 ) >> 4 ) & 0x08;

    saved = ( inb( 0x534 ) & 0xE0 ) | 0x0C;
    outb( saved, 0x534 );

    switch( inb( 0x535 ) ) {
      case 0x09:
      case 0x0A:
        special_mask |= 0x05;
        break;
      case 0x8A:
        special_mask = 0x0F;
        break;
      default:
        i = 0;
    }
  }
  if ( i == 0 ) {
    printk( "Strange MediaMagic, but\n" );
  }
  else {
    printk( "Conf:" );
    saved = inb( 0x534 ) & 0xE0;
    for( i = 0; i < 16; i++ ) {
      outb( 0x20 | ( u_char )i, 0x534 );
      outb( defaults[i], 0x535 );
    }
    for ( i = 0; i < 16; i++ ) {
      outb( 0x20 | ( u_char )i, 0x534 );
      saved = inb( 0x535 );
      printk( " %02X", saved );
    }
    printk( "\n" );
  }

  ISP16_OUT( 0xF91, 0xA0 | special_mask );

  /*
   * The following have no explaination yet.
   */
  ISP16_OUT( 0xF90, 0xA2 );
  ISP16_OUT( 0xF92, 0x03 );

  /*
   * Turn general sound on and set total volume.
   */
  ISP16_OUT( 0xF93, 0x0A );

/*
  outb( 0x04, 0x224 );
  saved = inb( 0x225 );
  outb( 0x04, 0x224 );
  outb( saved, 0x225 );
*/

}
