/* linux/drivers/cdrom/optcd_isp16.h - ISP16 CDROM interface configuration
   $Id: optcd_isp16.h,v 1.3 1996/01/15 18:43:11 root Exp root $

   Extracts from linux/drivers/cdrom/sjcd.c
   For copyrights see linux/drivers/cdrom/optcd.c
*/


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

/*static int sjcd_present = 0;*/
static u_char special_mask = 0;

static unsigned char defaults[ 16 ] = {
  0xA8, 0xA8, 0x18, 0x18, 0x18, 0x18, 0x8E, 0x8E,
  0x03, 0x00, 0x02, 0x00, 0x0A, 0x00, 0x00, 0x00
};
/* ------------- */
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
