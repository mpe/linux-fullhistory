/*
 * hp100.c: Hewlett Packard HP10/100VG ANY LAN ethernet driver for Linux.
 *
 * Author:  Jaroslav Kysela, <perex@pf.jcu.cz>
 *
 * Supports only the following Hewlett Packard cards:
 *
 *  	HP J2577	10/100 EISA card with REVA Cascade chip
 *	HP J2573	10/100 ISA card with REVA Cascade chip
 *	HP 27248B	10 only EISA card with Cascade chip
 *	HP J2577	10/100 EISA card with Cascade chip
 *	HP J2573	10/100 ISA card with Cascade chip
 *
 * Other ATT2MD01 Chip based boards might be supported in the future
 * (there are some minor changes needed).
 *
 * This driver is based on the 'hpfepkt' crynwr packet driver.
 *
 * This source/code is public free; you can distribute it and/or modify 
 * it under terms of the GNU General Public License (published by the
 * Free Software Foundation) either version two of this License, or any 
 * later version.
 * ----------------------------------------------------------------------------
 *
 * Note: Some routines (interrupt handling, transmit) assumes that  
 *       there is the PERFORMANCE page selected...
 *
 * ----------------------------------------------------------------------------
 *
 * If you are going to use the module version of this driver, you may
 * change this values at the "insert time" :
 *
 *   Variable                   Description
 *
 *   hp100_default_rx_ratio	Range 1-99 - onboard memory used for RX 
 *                              packets in %.
 *   hp100_port			Adapter port (for example 0x380).
 *
 * ----------------------------------------------------------------------------
 * MY BEST REGARDS GOING TO:
 *
 * IPEX s.r.o which lend me two HP J2573 cards and
 * the HP AdvanceStack 100VG Hub-15 for debugging.
 *
 * Russel Nellson <nelson@crynwr.com> for help with obtaining sources
 * of the 'hpfepkt' packet driver.
 *
 * Also thanks to Abacus Electric s.r.o which let me to use their 
 * motherboard for my second computer.
 *
 * ----------------------------------------------------------------------------
 *
 * TO DO:
 * ======
 *       - ioctl handling - some runtime setup things
 *       - PCI card support
 *
 * Revision history:
 * =================
 * 
 *    Version   Date	    Description
 *
 *	0.1	14-May-95   Initial writing. ALPHA code was released.
 *                          Only HP J2573 on 10Mb/s (two machines) tested.
 *      0.11    14-Jun-95   Reset interface bug fixed?
 *			    Little bug in hp100_close function fixed.
 *                          100Mb/s connection debugged.
 *
 */

#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#endif /* MODULE */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <asm/bitops.h>
#include <asm/io.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <linux/types.h>

#include "hp100.h"

/*
 *  defines
 */

#define HP100_MAX_PACKET_SIZE	(1536+4)
#define HP100_MIN_PACKET_SIZE	60

#ifndef HP100_RX_RATIO
/* default - 65% onboard memory on the card are used for RX packets */
#define HP100_RX_RATIO		65
#endif

/*
 *  structures
 */

struct hp100_eisa_id {
  u_int id;
  char *name;
};

struct hp100_private {
  struct hp100_eisa_id *id;
  u_short soft_model;
  u_int memory_size;
  u_short rx_ratio;
  short lan_type;		    /* 10Mb/s, 100Mb/s or -1 (error) */
  int hub_status;		    /* login to hub successfull? */
  u_char mac1_mode;
  u_char mac2_mode;
  struct enet_statistics stats;
};

struct hp100_rx_look {
  struct hp100_rx_header header;
  char something[ 24 ];		    /* 2 * MAC @6 + protocol @2+8 + pad to 4 byte */
};

/*
 *  variables
 */
 
static struct hp100_eisa_id hp100_eisa_ids[] = {
  { 0x080F1F022, "HP J2577 rev A" }, /* 10/100 EISA card with REVA Cascade chip */
  { 0x050F1F022, "HP J2573 rev A" }, /* 10/100 ISA card with REVA Cascade chip */
  { 0x02019F022, "HP 27248B" },	     /* 10 only EISA card with Cascade chip */
  { 0x04019F022, "HP J2577" },	     /* 10/100 EISA card with Cascade chip */
  { 0x05019F022, "HP J2573" }	     /* 10/100 ISA card with Cascade chip */
};

#ifdef MODULE
int hp100_default_rx_ratio = HP100_RX_RATIO;
#endif

/*
 *  prototypes
 */

static int hp100_probe1( struct device *dev, int ioaddr );
static int hp100_open( struct device *dev );
static int hp100_close( struct device *dev );
static int hp100_start_xmit( struct sk_buff *skb, struct device *dev );
static void hp100_rx( struct device *dev );
static struct enet_statistics *hp100_get_stats( struct device *dev );
static void hp100_update_stats( struct device *dev );
#ifdef HAVE_MULTICAST
static void hp100_set_multicast_list( struct device *dev, int num_addrs, void *addrs );
#endif
#ifndef LINUX_1_1_52
static void hp100_interrupt( int irq, struct pt_regs *regs );
#else
static void hp100_interrupt( int irq );
#endif

static void hp100_start_interface( struct device *dev );
static void hp100_stop_interface( struct device *dev );
static void hp100_load_eeprom( struct device *dev );
static int hp100_sense_lan( struct device *dev );
static int hp100_login_to_vg_hub( struct device *dev );
static int hp100_down_vg_link( struct device *dev );

/*
 *  probe functions
 */
 
int hp100_probe( struct device *dev )
{
  int base_addr = dev ? dev -> base_addr : 0;
  int ioaddr;
        
  if ( base_addr > 0xff )	/* Check a single specified location. */
    return hp100_probe1(dev, base_addr);
   else 
    if ( base_addr != 0 ) return -ENXIO;
         
  /* at first - probe all EISA possible port regions (if EISA bus present) */
  
  for ( ioaddr = 0x1c38; EISA_bus && ioaddr < 0x10000; ioaddr += 0x400 )
    {
      if ( check_region( ioaddr, 0x20 ) ) continue;
      if ( hp100_probe1( dev, ioaddr ) == 0 ) return 0;
    }
         
  /* at second - probe all ISA possible port regions */
         
  for ( ioaddr = 0x100; ioaddr < 0x400; ioaddr += 0x20 )
    {
      if ( check_region( ioaddr, 0x20 ) ) continue;
      if ( hp100_probe1( dev, ioaddr ) == 0 ) return 0;
    }
                                                                            
  return -ENODEV;
}

static int hp100_probe1( struct device *dev, int ioaddr )
{
  int i;
  u_char uc;
  u_int eisa_id;
  struct hp100_private *lp;
  struct hp100_eisa_id *eid;

  if ( dev == NULL )
    {
#ifdef HP100_DEBUG
      printk( "hp100_probe1: dev == NULL ?\n" );
#endif
      return EIO;
    }

  if ( inb( ioaddr + 0 ) != HP100_HW_ID_0 ||
       inb( ioaddr + 1 ) != HP100_HW_ID_1 ||
       ( inb( ioaddr + 2 ) & 0xf0 ) != HP100_HW_ID_2_REVA ||
       inb( ioaddr + 3 ) != HP100_HW_ID_3 ) 
     return -ENODEV;

  dev -> base_addr = ioaddr;

#ifdef HP100_DEBUG_PROBE1
  printk( "hp100_probe1: card found at port 0x%x\n", ioaddr );
#endif

  hp100_page( ID_MAC_ADDR );
  for ( i = uc = eisa_id = 0; i < 4; i++ )
    {
      eisa_id >>= 8;
      eisa_id |= ( hp100_inb( BOARD_ID + i ) ) << 24;
      uc += eisa_id >> 24;
    }
  uc += hp100_inb( BOARD_ID + 4 );

#ifdef HP100_DEBUG_PROBE1
  printk( "hp100_probe1: EISA ID = 0x%08x  checksum = 0x%02x\n", eisa_id, uc );
#endif

  if ( uc != 0xff )		/* bad checksum? */
    {
      printk( "hp100_probe: bad EISA ID checksum at base port 0x%x\n", ioaddr );
      return -ENODEV;
    }  

  for ( i = 0; i < sizeof( hp100_eisa_ids ) / sizeof( struct hp100_eisa_id ); i++ )
    if ( ( hp100_eisa_ids[ i ].id & 0xf0ffffff ) == ( eisa_id & 0xf0ffffff ) )
      break;
  if ( i >= sizeof( hp100_eisa_ids ) / sizeof( struct hp100_eisa_id ) )
    {
      printk( "hp100_probe1: card at port 0x%x isn't known\n", ioaddr );
      return -ENODEV;
    }
  eid = &hp100_eisa_ids[ i ];
  if ( ( eid -> id & 0x0f000000 ) < ( eisa_id & 0x0f000000 ) )
    {
      printk( "hp100_probe1: newer version of card %s at port 0x%x - unsupported\n", 
	eid -> name, ioaddr );
      return -ENODEV;
    }

  for ( i = uc = 0; i < 7; i++ )
    uc += hp100_inb( LAN_ADDR + i );
  if ( uc != 0xff )
    {
      printk( "hp100_probe1: bad lan address checksum (card %s at port 0x%x)\n", 
	eid -> name, ioaddr );
      return -EIO;
    }

  hp100_page( HW_MAP );
  if ( hp100_inw( OPTION_LSW ) & ( HP100_MEM_EN | HP100_BM_WRITE | HP100_BM_READ ) )
    {
      printk( "hp100_probe1: memory mapped io isn't supported (card %s at port 0x%x)\n",
      	eid -> name, ioaddr );
      return -EIO;
    }

  if ( ( dev -> priv = kmalloc( sizeof( struct hp100_private ), GFP_KERNEL ) ) == NULL )
    return -ENOMEM;
  memset( dev -> priv, 0, sizeof( struct hp100_private ) );

  lp = (struct hp100_private *)dev -> priv;
  lp -> id = eid;
  hp100_page( ID_MAC_ADDR );
  lp -> soft_model = hp100_inb( SOFT_MODEL );
  lp -> mac1_mode = HP100_MAC1MODE3;
  lp -> mac2_mode = HP100_MAC2MODE3;
  
  dev -> base_addr = ioaddr;
  hp100_page( HW_MAP );
  dev -> irq = hp100_inb( IRQ_CHANNEL ) & HP100_IRQ_MASK;
  if ( dev -> irq == 2 ) dev -> irq = 9;
  lp -> memory_size = 0x200 << ( ( hp100_inb( SRAM ) & 0xe0 ) >> 5 );
#ifndef MODULE
  lp -> rx_ratio = HP100_RX_RATIO;
#else
  lp -> rx_ratio = hp100_default_rx_ratio;
#endif

  dev -> open = hp100_open;
  dev -> stop = hp100_close;
  dev -> hard_start_xmit = hp100_start_xmit;
  dev -> get_stats = hp100_get_stats;
#ifdef HAVE_MULTICAST
  dev -> set_multicast_list = &hp100_set_multicast_list;
#endif

#ifndef LINUX_1_1_52
  request_region( dev -> base_addr, 0x20, eid -> name );
#endif

  hp100_page( ID_MAC_ADDR );
  for ( i = uc = 0; i < 6; i++ )
    dev -> dev_addr[ i ] = hp100_inb( LAN_ADDR + i );

  ether_setup( dev );

  lp -> lan_type = hp100_sense_lan( dev );
     
  printk( "%s: %s at 0x%x, IRQ %d, %dkB SRAM (rx/tx %d%%), ",
    dev -> name, lp -> id -> name, ioaddr, dev -> irq, 
    lp -> memory_size >> ( 10 - 4 ), lp -> rx_ratio );
  switch ( lp -> lan_type ) {
    case HP100_LAN_100: printk( "100Mb/s VG TP" ); break;
    case HP100_LAN_10:  printk( "10Mb/s TP" );     break;
    default:	        printk( "link down" );     break;
  }
  printk( ".\n" );

  return 0;
}

/*
 *  open/close functions
 */

static int hp100_open( struct device *dev )
{
  int i;
  int ioaddr = dev -> base_addr;
  struct hp100_private *lp = (struct hp100_private *)dev -> priv;

  if ( ( lp -> lan_type = hp100_sense_lan( dev ) ) < 0 )
    {
      printk( "%s: no connection found - check wire\n", dev -> name );
      return -EIO;
    }

  if ( request_irq( dev -> irq, hp100_interrupt, SA_INTERRUPT, lp -> id -> name ) )
    {
      printk( "%s: unable to get IRQ %d\n", dev -> name, dev -> irq );
      return -EAGAIN;
    }
  irq2dev_map[ dev -> irq ] = dev;

#ifdef MODULE
  MOD_INC_USE_COUNT;
#endif
  
  dev -> tbusy = 0;
  dev -> trans_start = jiffies;
  dev -> interrupt = 0;
  dev -> start = 1;

  lp -> mac1_mode = HP100_MAC1MODE3;
  lp -> mac2_mode = HP100_MAC2MODE3;
  
  hp100_page( MAC_CTRL );
  hp100_orw( HP100_LINK_BEAT_DIS, LAN_CFG_10 );

  hp100_stop_interface( dev );
  hp100_reset_card();
  hp100_load_eeprom( dev );

  hp100_outw( HP100_MMAP_DIS | HP100_SET_HB | 
              HP100_IO_EN | HP100_SET_LB, OPTION_LSW );
  hp100_outw( HP100_DEBUG_EN | HP100_RX_HDR | HP100_EE_EN | HP100_RESET_HB |
              HP100_FAKE_INT | HP100_RESET_LB, OPTION_LSW );
#if 0
  hp100_outw( HP100_PRIORITY_TX | HP100_ADV_NXT_PKT | 
              HP100_TX_CMD | HP100_RESET_LB, OPTION_MSW );
#else
  hp100_outw( HP100_ADV_NXT_PKT | HP100_TX_CMD | HP100_RESET_LB, OPTION_MSW );
#endif

  hp100_page( MAC_ADDRESS );
  for ( i = 0; i < 6; i++ )
    hp100_outb( dev -> dev_addr[ i ], MAC_ADDR + i );
  for ( i = 0; i < 8; i++ )		/* setup multicast filter to receive all */
    hp100_outb( 0xff, HASH_BYTE0 + i );
  hp100_page( PERFORMANCE );
  hp100_outw( 0xfefe, IRQ_MASK );	/* mask off all ints */
  hp100_outw( 0xffff, IRQ_STATUS );	/* ack */
  hp100_outw( (HP100_RX_PACKET | HP100_RX_ERROR | HP100_SET_HB) |
              (HP100_TX_ERROR | HP100_SET_LB ), IRQ_MASK );
              				/* and enable few */
  hp100_reset_card();
  hp100_page( MMU_CFG );
  hp100_outw( ( lp -> memory_size * lp -> rx_ratio ) / 100, RX_MEM_STOP );
  hp100_outw( lp -> memory_size - 1, TX_MEM_STOP );
  hp100_unreset_card();

  if ( lp -> lan_type == HP100_LAN_100 )
    lp -> hub_status = hp100_login_to_vg_hub( dev );

  hp100_start_interface( dev );

  return 0;
}

static int hp100_close( struct device *dev )
{
  int ioaddr = dev -> base_addr;
  struct hp100_private *lp = (struct hp100_private *)dev -> priv;

  hp100_page( PERFORMANCE );
  hp100_outw( 0xfefe, IRQ_MASK );		/* mask off all ints */

  hp100_stop_interface( dev );			/* relogin */

  if ( lp -> lan_type == HP100_LAN_100 )
    hp100_login_to_vg_hub( dev );

  dev -> tbusy = 1;
  dev -> start = 0;

  free_irq( dev -> irq );
  irq2dev_map[ dev -> irq ] = NULL;
#ifdef MODULE
  MOD_DEC_USE_COUNT;
#endif
  return 0;
}

/* 
 *  transmit
 */

static int hp100_start_xmit( struct sk_buff *skb, struct device *dev )
{
  int i;
  int ioaddr = dev -> base_addr;
  u_short val;
  struct hp100_private *lp = (struct hp100_private *)dev -> priv;
  
  if ( ( i = ( hp100_inl( TX_MEM_FREE ) & ~0x7fffffff ) ) < skb -> len + 16 )
    {
#ifdef HP100_DEBUG
      printk( "hp100_start_xmit: rx free mem = 0x%x\n", i );
#endif
      if ( jiffies - dev -> trans_start < 2 * HZ ) return -EAGAIN;
      if ( lp -> lan_type == HP100_LAN_100 && lp -> hub_status < 0 )
 				/* 100Mb/s adapter isn't connected to hub */
        {
          printk( "%s: login to 100Mb/s hub retry\n", dev -> name );
          hp100_ints_off();
          hp100_stop_interface( dev );
          lp -> hub_status = hp100_login_to_vg_hub( dev );
          hp100_start_interface( dev );
          hp100_ints_on();
        }
       else
        {
          hp100_ints_off();
          i = hp100_sense_lan( dev );
          hp100_page( PERFORMANCE );
          hp100_ints_on();
          if ( i == HP100_LAN_ERR )
            printk( "%s: link down detected\n", dev -> name );
           else
          if ( lp -> lan_type != i )
            {
              /* it's very heavy - all network setting must be changed!!! */
              printk( "%s: cable change 10Mb/s <-> 100Mb/s detected\n", dev -> name );
              lp -> lan_type = i;
              hp100_ints_off();
              hp100_stop_interface( dev );
              if ( lp -> lan_type == HP100_LAN_100 )
                lp -> hub_status = hp100_login_to_vg_hub( dev );
              hp100_start_interface( dev );
              hp100_ints_on();
            }
           else
            {
              printk( "%s: interface reset\n", dev -> name );
              hp100_ints_off();
              hp100_stop_interface( dev );
              hp100_start_interface( dev );
              hp100_ints_on();
            }
        }
      dev -> trans_start = jiffies;
      return -EAGAIN;
    }
    
  if ( skb == NULL )
    {
      dev_tint( dev );
      return 0;
    }
    
  if ( skb -> len <= 0 ) return 0;

  for ( i = 0; i < 6000 && ( hp100_inw( OPTION_MSW ) & HP100_TX_CMD ); i++ )
    {
#ifdef HP100_DEBUG_TX
      printk( "hp100_start_xmit: busy\n" );
#endif    
    }
    
  hp100_ints_off();
  val = hp100_inw( IRQ_STATUS );
  hp100_outw( val & HP100_TX_COMPLETE, IRQ_STATUS );
#ifdef HP100_DEBUG_TX
  printk( "hp100_start_xmit: irq_status = 0x%x, len = %d\n", val, (int)skb -> len );
#endif
  if ( skb -> len >= HP100_MIN_PACKET_SIZE )
    {
      hp100_outw( skb -> len, DATA32 );		/* length to memory manager */
      hp100_outw( skb -> len, FRAGMENT_LEN );
      outsl( ioaddr + HP100_REG_DATA32, skb -> data, ( skb -> len + 3 ) >> 2 );
      hp100_outw( HP100_TX_CMD | HP100_SET_LB, OPTION_MSW ); /* send packet */
    }
   else
    {
      hp100_outw( HP100_MIN_PACKET_SIZE, DATA32 ); /* length to memory manager */
      hp100_outw( HP100_MIN_PACKET_SIZE, FRAGMENT_LEN );
      i = skb -> len + 3;
      outsl( ioaddr + HP100_REG_DATA32, skb -> data, i >> 2 );
      for ( i &= ~3; i < HP100_MIN_PACKET_SIZE; i += 4 ) 
        hp100_outl( 0, DATA32 );
      hp100_outw( HP100_TX_CMD | HP100_SET_LB, OPTION_MSW ); /* send packet */
    }
  lp -> stats.tx_packets++;
  dev -> trans_start = jiffies;
  hp100_ints_on();

  dev_kfree_skb( skb, FREE_WRITE );

#ifdef HP100_DEBUG_TX
  printk( "hp100_start_xmit: end\n" );
#endif

  return 0;
}

/*
 *  receive - called from interrupt handler
 */

static void hp100_rx( struct device *dev )
{
  int packets, pkt_len;
  int ioaddr = dev -> base_addr;
  struct hp100_private *lp = (struct hp100_private *)dev -> priv;
  u_int header;
  struct sk_buff *skb;

  packets = hp100_inb( RX_PKT_CNT );
#ifdef HP100_DEBUG
  if ( packets > 1 )
    printk( "hp100_rx: waiting packets = %d\n", packets );
#endif
  while ( packets-- > 0 )
    {
      for ( pkt_len = 0; pkt_len < 6000 && ( hp100_inw( OPTION_MSW ) & HP100_ADV_NXT_PKT ); pkt_len++ )
        {
#ifdef HP100_DEBUG_TX
          printk( "hp100_rx: busy, remaining packets = %d\n", packets );
#endif    
        }
      header = hp100_inl( DATA32 );
      pkt_len = header & HP100_PKT_LEN_MASK;
#ifdef HP100_DEBUG_RX
      printk( "hp100_rx: new packet - length = %d, errors = 0x%x, dest = 0x%x\n",
      	header & HP100_PKT_LEN_MASK, ( header >> 16 ) & 0xfff8, ( header >> 16 ) & 7 );
#endif
      /*
       * NOTE! This (and the skb_put() below) depends on the skb-functions
       * allocating more than asked (notably, aligning the request up to
       * the next 16-byte length).
       */
      skb = dev_alloc_skb(pkt_len);
      if ( skb == NULL )
        {
#ifdef HP100_DEBUG
          printk( "hp100_rx: couldn't allocate a sk_buff of size %d\n", pkt_len );
#endif
          lp -> stats.rx_dropped++;
        }
       else
        {
          skb -> dev = dev;
          insl( ioaddr + HP100_REG_DATA32, skb_put(pkt_len), ( pkt_len + 3 ) >> 2 );
          skb->protocol=eth_type_trans(skb,dev);
          netif_rx( skb );
          lp -> stats.rx_packets++;
        }
      hp100_outw( HP100_ADV_NXT_PKT | HP100_SET_LB, OPTION_MSW );
      switch ( header & 0x00070000 ) {
        case (HP100_MULTI_ADDR_HASH<<16):
        case (HP100_MULTI_ADDR_NO_HASH<<16):
          lp -> stats.multicast++; break;
      }
    }
#ifdef HP100_DEBUG_RX
   printk( "hp100_rx: end\n" );
#endif
}

/*
 *  statistics
 */
 
static struct enet_statistics *hp100_get_stats( struct device *dev )
{
  int ioaddr = dev -> base_addr;

  hp100_ints_off();
  hp100_update_stats( dev );
  hp100_ints_on();
  return &((struct hp100_private *)dev -> priv) -> stats;
}

static void hp100_update_stats( struct device *dev )
{
  int ioaddr = dev -> base_addr;
  u_short val;
  struct hp100_private *lp = (struct hp100_private *)dev -> priv;
         
  hp100_page( MAC_CTRL );		/* get all statistics bytes */
  val = hp100_inw( DROPPED ) & 0x0fff;
  lp -> stats.rx_errors += val;
  lp -> stats.rx_over_errors += val;
  val = hp100_inb( CRC );
  lp -> stats.rx_errors += val;
  lp -> stats.rx_crc_errors += val;
  val = hp100_inb( ABORT );
  lp -> stats.tx_errors += val;
  lp -> stats.tx_aborted_errors += val;
  hp100_page( PERFORMANCE );
}

/*
 *  multicast setup
 */

#ifdef HAVE_MULTICAST

/*
 *  Set or clear the multicast filter for this adapter.
 *
 *  num_addrs == -1             Promiscuous mode, receive all packets
 *  num_addrs == 0              Normal mode, clear multicast list
 *  num_addrs > 0               Multicast mode, receive normal and MC packets,
 *                              best-effort filtering.
 */
                                                          
static void hp100_set_multicast_list( struct device *dev, int num_addrs, void *addrs )
{
  int ioaddr = dev -> base_addr;
  struct hp100_private *lp = (struct hp100_private *)dev -> priv;

#ifdef HP100_DEBUG_MULTI
  printk( "hp100_set_multicast_list: num_addrs = %d\n", num_addrs );
#endif
  hp100_ints_off();
  cli();
  hp100_page( MAC_CTRL );
  hp100_andb( ~(HP100_RX_EN | HP100_TX_EN), MAC_CFG_1 );	/* stop rx/tx */

  if ( num_addrs < 0 )
    {
      lp -> mac2_mode = HP100_MAC2MODE6;  /* promiscuous mode, all good */
      lp -> mac1_mode = HP100_MAC1MODE6;  /* packets on the net */
    }
   else
  if ( num_addrs > 0 )
    {
      lp -> mac2_mode = HP100_MAC2MODE5;  /* multicast mode, packets for me */
      lp -> mac1_mode = HP100_MAC1MODE5;  /* broadcasts and all multicasts */
    }
   else
     {
      lp -> mac2_mode = HP100_MAC2MODE3;  /* normal mode, packets for me */
      lp -> mac1_mode = HP100_MAC1MODE3;  /* and broadcasts */
    }

  hp100_outb( lp -> mac2_mode, MAC_CFG_2 );
  hp100_andb( HP100_MAC1MODEMASK, MAC_CFG_1 );
  hp100_orb( lp -> mac1_mode, MAC_CFG_1 );

  hp100_orb( HP100_RX_EN | HP100_RX_IDLE, MAC_CFG_1 );		/* enable rx */
  hp100_orb( HP100_TX_EN | HP100_TX_IDLE, MAC_CFG_1 );		/* enable tx */
  hp100_page( PERFORMANCE );
  sti();
  hp100_ints_on();
}

#endif /* HAVE_MULTICAST */

/*
 *  hardware interrupt handling
 */

#ifndef LINUX_1_1_52
static void hp100_interrupt( int irq, struct pt_regs *regs )
#else
static void hp100_interrupt( int irq )
#endif
{
  struct device *dev = (struct device *)irq2dev_map[ irq ];
  struct hp100_private *lp;
  int ioaddr;
  u_short val;

  if ( dev == NULL ) return;
  ioaddr = dev -> base_addr;
  if ( dev -> interrupt )
    printk( "%s: re-entering the interrupt handler\n", dev -> name );
  hp100_ints_off();
  dev -> interrupt = 1;
  val = hp100_inw( IRQ_STATUS );
#ifdef HP100_DEBUG_IRQ
  printk( "hp100_interrupt: irq_status = 0x%x\n", val );
#endif
  if ( val & HP100_RX_PACKET )
    {
      hp100_rx( dev );
      hp100_outw( HP100_RX_PACKET, IRQ_STATUS );
    }
  if ( val & (HP100_TX_SPACE_AVAIL | HP100_TX_COMPLETE) )
    {
      hp100_outw( val & (HP100_TX_SPACE_AVAIL | HP100_TX_COMPLETE), IRQ_STATUS );
    }
  if ( val & ( HP100_TX_ERROR | HP100_RX_ERROR ) )
    {
      lp = (struct hp100_private *)dev -> priv;
      hp100_update_stats( dev );
      hp100_outw( val & (HP100_TX_ERROR | HP100_RX_ERROR), IRQ_STATUS );
    }
#ifdef HP100_DEBUG_IRQ
  printk( "hp100_interrupt: end\n" );
#endif
  dev -> interrupt = 0;
  hp100_ints_on();
}

/*
 *  some misc functions
 */

static void hp100_start_interface( struct device *dev )
{
  int ioaddr = dev -> base_addr;
  struct hp100_private *lp = (struct hp100_private *)dev -> priv;

  hp100_unreset_card();
  cli();
  hp100_page( MAC_CTRL );
  hp100_outb( lp -> mac2_mode, MAC_CFG_2 );
  hp100_andb( HP100_MAC1MODEMASK, MAC_CFG_1 );
  hp100_orb( lp -> mac1_mode, MAC_CFG_1 );
  hp100_orb( HP100_RX_EN | HP100_RX_IDLE, MAC_CFG_1 );
  hp100_orb( HP100_TX_EN | HP100_TX_IDLE, MAC_CFG_1 );
  hp100_page( PERFORMANCE );
  hp100_outw( HP100_INT_EN | HP100_SET_LB, OPTION_LSW );
  hp100_outw( HP100_TRI_INT | HP100_RESET_HB, OPTION_LSW );
  sti();
} 

static void hp100_stop_interface( struct device *dev )
{
  int ioaddr = dev -> base_addr;
  u_short val;

  hp100_outw( HP100_INT_EN | HP100_RESET_LB | 
              HP100_TRI_INT | HP100_SET_HB, OPTION_LSW );
  val = hp100_inw( OPTION_LSW );
  hp100_page( HW_MAP );
  hp100_andb( HP100_BM_SLAVE, BM );
  hp100_page( MAC_CTRL );
  hp100_andb( ~(HP100_RX_EN | HP100_TX_EN), MAC_CFG_1 );
  if ( !(val & HP100_HW_RST) ) return;
  for ( val = 0; val < 6000; val++ )
    if ( ( hp100_inb( MAC_CFG_1 ) & (HP100_TX_IDLE | HP100_RX_IDLE) ) ==
                                    (HP100_TX_IDLE | HP100_RX_IDLE) )
      return;
  printk( "%s: hp100_stop_interface - timeout\n", dev -> name );
}

static void hp100_load_eeprom( struct device *dev )
{
  int i;
  int ioaddr = dev -> base_addr;

  hp100_page( EEPROM_CTRL );
  hp100_andw( ~HP100_EEPROM_LOAD, EEPROM_CTRL );
  hp100_orw( HP100_EEPROM_LOAD, EEPROM_CTRL );
  for ( i = 0; i < 6000; i++ )
    if ( !( hp100_inw( OPTION_MSW ) & HP100_EE_LOAD ) ) return;
  printk( "%s: hp100_load_eeprom - timeout\n", dev -> name );
}

/* return values: LAN_10, LAN_100 or LAN_ERR (not connected or hub is down)... */

static int hp100_sense_lan( struct device *dev )
{
  int i;
  int ioaddr = dev -> base_addr;
  u_short val_VG, val_10;
  struct hp100_private *lp = (struct hp100_private *)dev -> priv;

  hp100_page( MAC_CTRL );
  hp100_orw( HP100_VG_RESET, LAN_CFG_VG );
  val_10 = hp100_inw( LAN_CFG_10 );
  val_VG = hp100_inw( LAN_CFG_VG );
#ifdef HP100_DEBUG_SENSE
  printk( "hp100_sense_lan: val_VG = 0x%04x, val_10 = 0x%04x\n", val_VG, val_10 );
#endif
  if ( val_10 & HP100_LINK_BEAT_ST ) return HP100_LAN_10;
  if ( lp -> id -> id == 0x02019F022 ) /* HP J27248B doesn't have 100Mb/s interface */
    return HP100_LAN_ERR;
  for ( i = 0; i < 2500; i++ )
    {
      val_VG = hp100_inw( LAN_CFG_VG );
      if ( val_VG & HP100_LINK_CABLE_ST ) return HP100_LAN_100;
    }
  return HP100_LAN_ERR;
}

static int hp100_down_vg_link( struct device *dev )
{
  int ioaddr = dev -> base_addr;
  unsigned long time;
  int i;

  hp100_page( MAC_CTRL );
  for ( i = 2500; i > 0; i-- )
    if ( hp100_inw( LAN_CFG_VG ) & HP100_LINK_CABLE_ST ) break;
  if ( i <= 0 )				/* not signal - not logout */
    return 0;
  hp100_andw( ~HP100_LINK_CMD, LAN_CFG_VG );
  time = jiffies + 10; 
  while ( time > jiffies )
    if ( !( hp100_inw( LAN_CFG_VG ) & ( HP100_LINK_UP_ST | 
                                        HP100_LINK_CABLE_ST | 
                                        HP100_LINK_GOOD_ST ) ) )
      return 0;
#ifdef HP100_DEBUG
  printk( "hp100_down_vg_link: timeout\n" );
#endif
  return -EIO;
}

static int hp100_login_to_vg_hub( struct device *dev )
{
  int i;
  int ioaddr = dev -> base_addr;
  u_short val;
  unsigned long time;  

  hp100_page( MAC_CTRL );
  hp100_orw( HP100_VG_RESET, LAN_CFG_VG );
  time = jiffies + ( HZ / 2 );
  do {
    if ( hp100_inw( LAN_CFG_VG ) & HP100_LINK_CABLE_ST ) break;
  } while ( time > jiffies );
  if ( time <= jiffies )
    {
#ifdef HP100_DEBUG
      printk( "hp100_login_to_vg_hub: timeout for link\n" );
#endif
      return -EIO;
    }
    
  if ( hp100_down_vg_link( dev ) < 0 )	/* if fail, try reset VG link */
    {
      hp100_andw( ~HP100_VG_RESET, LAN_CFG_VG );
      hp100_orw( HP100_VG_RESET, LAN_CFG_VG );
    }
  /* bring up link */
  hp100_orw( HP100_LOAD_ADDR | HP100_LINK_CMD, LAN_CFG_VG );
  for ( i = 2500; i > 0; i-- )
    if ( hp100_inw( LAN_CFG_VG ) & HP100_LINK_CABLE_ST ) break;
  if ( i <= 0 )
    {
#ifdef HP100_DEBUG
      printk( "hp100_login_to_vg_hub: timeout for link (bring up)\n" );
#endif
      goto down_link;
    }

  time = jiffies + ( HZ / 2 );
  do {   
    val = hp100_inw( LAN_CFG_VG );
    if ( ( val & ( HP100_LINK_UP_ST | HP100_LINK_GOOD_ST ) ) == 
                 ( HP100_LINK_UP_ST | HP100_LINK_GOOD_ST ) )
      return 0;	/* success */
  } while ( time > jiffies );
  if ( val & HP100_LINK_GOOD_ST )
    printk( "%s: 100Mb cable training failed, check cable.\n", dev -> name );
   else
    printk( "%s: 100Mb node not accepted by hub, check frame type or security.\n", dev -> name );

down_link:
  hp100_down_vg_link( dev );
  hp100_page( MAC_CTRL );
  hp100_andw( ~( HP100_LOAD_ADDR | HP100_PROM_MODE ), LAN_CFG_VG );
  hp100_orw( HP100_LINK_CMD, LAN_CFG_VG );
  return -EIO;
}

/*
 *  module section
 */
 
#ifdef MODULE

char kernel_version[] = UTS_RELEASE;

static int hp100_port = -1;

static struct device dev_hp100 = {
  "        ", 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL, hp100_probe
};

int init_module( void )
{
  if ( hp100_port > 0 ) dev_hp100.base_addr = hp100_port;
  if ( register_netdev( &dev_hp100 ) != 0 ) return -EIO;
  return 0;
}         

void cleanup_module( void )
{
  unregister_netdev( &dev_hp100 );
  release_region( dev_hp100.base_addr, 0x20 );
  kfree_s( dev_hp100.priv, sizeof( struct hp100_private ) );
  dev_hp100.priv = NULL;
}

#endif
