/*
 * $Id: dmascc.c,v 1.3 1998/09/07 04:41:56 kudielka Exp $
 *
 * Driver for high-speed SCC boards (those with DMA support)
 * Copyright (C) 1997 Klaus Kudielka
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <linux/module.h>
#include <linux/delay.h>
#include <linux/dmascc.h>
#include <linux/errno.h>
#include <linux/if_arp.h>
#include <linux/in.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/netdevice.h>
#include <linux/sockios.h>
#include <linux/tqueue.h>
#include <linux/version.h>
#include <asm/atomic.h>
#include <asm/bitops.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/segment.h>
#include <net/ax25.h>
#include "z8530.h"


/* Linux 2.0 compatibility */

#if LINUX_VERSION_CODE < 0x20100


#define __init
#define __initdata
#define __initfunc(x) x

#define MODULE_AUTHOR(x)
#define MODULE_DESCRIPTION(x)
#define MODULE_PARM(x,y)

#define copy_to_user(x,y,z) memcpy_tofs(x,y,z)
#define copy_from_user(x,y,z) memcpy_fromfs(x,y,z)
#define test_and_set_bit(x,y) set_bit(x,y)
#define register_netdevice(x) register_netdev(x)
#define unregister_netdevice(x) unregister_netdev(x)
#define dev_kfree_skb(x) dev_kfree_skb(x,FREE_WRITE)
#define SET_DEV_INIT(x) (x=dmascc_dev_init)

#define SHDLCE  0x01 /* WR15 */

#define AUTOEOM 0x02 /* WR7' */
#define RXFIFOH 0x08
#define TXFIFOE 0x20

static int dmascc_dev_init(struct device *dev)
{
  return 0;
}

static void dev_init_buffers(struct device *dev)
{
  int i;

  for (i = 0; i < DEV_NUMBUFFS; i++)
    skb_queue_head_init(&dev->buffs[i]);
}


#else


#include <linux/init.h>
#include <asm/uaccess.h>

#define SET_DEV_INIT(x)


#endif


/* Number of buffers per channel */

#define NUM_TX_BUF      2          /* NUM_TX_BUF >= 1 (2 recommended) */
#define NUM_RX_BUF      2          /* NUM_RX_BUF >= 1 (2 recommended) */
#define BUF_SIZE        2016


/* Cards supported */

#define HW_PI           { "Ottawa PI", 0x300, 0x20, 0x10, 8, \
                            0, 8, 1843200, 3686400 }
#define HW_PI2          { "Ottawa PI2", 0x300, 0x20, 0x10, 8, \
			    0, 8, 3686400, 7372800 }
#define HW_TWIN         { "Gracilis PackeTwin", 0x200, 0x10, 0x10, 32, \
			    0, 4, 6144000, 6144000 }

#define HARDWARE        { HW_PI, HW_PI2, HW_TWIN }

#define TYPE_PI         0
#define TYPE_PI2        1
#define TYPE_TWIN       2
#define NUM_TYPES       3

#define MAX_NUM_DEVS    32


/* SCC chips supported */

#define Z8530           0
#define Z85C30          1
#define Z85230          2

#define CHIPNAMES       { "Z8530", "Z85C30", "Z85230" }


/* I/O registers */

/* 8530 registers relative to card base */
#define SCCB_CMD        0x00
#define SCCB_DATA       0x01
#define SCCA_CMD        0x02
#define SCCA_DATA       0x03

/* 8253/8254 registers relative to card base */
#define TMR_CNT0        0x00
#define TMR_CNT1        0x01
#define TMR_CNT2        0x02
#define TMR_CTRL        0x03

/* Additional PI/PI2 registers relative to card base */
#define PI_DREQ_MASK    0x04

/* Additional PackeTwin registers relative to card base */
#define TWIN_INT_REG    0x08
#define TWIN_CLR_TMR1   0x09
#define TWIN_CLR_TMR2   0x0a
#define TWIN_SPARE_1    0x0b
#define TWIN_DMA_CFG    0x08
#define TWIN_SERIAL_CFG 0x09
#define TWIN_DMA_CLR_FF 0x0a
#define TWIN_SPARE_2    0x0b


/* PackeTwin I/O register values */

/* INT_REG */
#define TWIN_SCC_MSK       0x01
#define TWIN_TMR1_MSK      0x02
#define TWIN_TMR2_MSK      0x04
#define TWIN_INT_MSK       0x07

/* SERIAL_CFG */
#define TWIN_DTRA_ON       0x01
#define TWIN_DTRB_ON       0x02
#define TWIN_EXTCLKA       0x04
#define TWIN_EXTCLKB       0x08
#define TWIN_LOOPA_ON      0x10
#define TWIN_LOOPB_ON      0x20
#define TWIN_EI            0x80

/* DMA_CFG */
#define TWIN_DMA_HDX_T1    0x08
#define TWIN_DMA_HDX_R1    0x0a
#define TWIN_DMA_HDX_T3    0x14
#define TWIN_DMA_HDX_R3    0x16
#define TWIN_DMA_FDX_T3R1  0x1b
#define TWIN_DMA_FDX_T1R3  0x1d


/* Status values */

/* tx_state */
#define TX_IDLE    0
#define TX_OFF     1
#define TX_TXDELAY 2
#define TX_ACTIVE  3
#define TX_SQDELAY 4


/* Data types */

struct scc_hardware {
  char *name;
  int io_region;
  int io_delta;
  int io_size;
  int num_devs;
  int scc_offset;
  int tmr_offset;
  int tmr_hz;
  int pclk_hz;
};

struct scc_priv {
  char name[10];
  struct enet_statistics stats;
  struct scc_info *info;
  int channel;
  int cmd, data, tmr;
  struct scc_param param;
  char rx_buf[NUM_RX_BUF][BUF_SIZE];
  int rx_len[NUM_RX_BUF];
  int rx_ptr;
  struct tq_struct rx_task;
  int rx_head, rx_tail, rx_count;
  int rx_over;
  char tx_buf[NUM_TX_BUF][BUF_SIZE];
  int tx_len[NUM_TX_BUF];
  int tx_ptr;
  int tx_head, tx_tail, tx_count;
  int tx_sem, tx_state;
  unsigned long tx_start;
  int status;
};

struct scc_info {
  int type;
  int chip;
  int open;
  int scc_base;
  int tmr_base;
  int twin_serial_cfg;
  struct device dev[2];
  struct scc_priv priv[2];
  struct scc_info *next;
};


/* Function declarations */

int dmascc_init(void) __init;
static int setup_adapter(int io, int h, int n) __init;

static inline void write_scc(int ctl, int reg, int val);
static inline int read_scc(int ctl, int reg);
static int scc_open(struct device *dev);
static int scc_close(struct device *dev);
static int scc_ioctl(struct device *dev, struct ifreq *ifr, int cmd);
static int scc_send_packet(struct sk_buff *skb, struct device *dev);
static struct enet_statistics *scc_get_stats(struct device *dev);
static int scc_set_mac_address(struct device *dev, void *sa);
static void scc_isr(int irq, void *dev_id, struct pt_regs * regs);
static inline void z8530_isr(struct scc_info *info);
static void rx_isr(struct device *dev);
static void special_condition(struct device *dev, int rc);
static void rx_bh(void *arg);
static void tx_isr(struct device *dev);
static void es_isr(struct device *dev);
static void tm_isr(struct device *dev);
static inline void delay(struct device *dev, int t);
static inline unsigned char random(void);


/* Initialization variables */

static int io[MAX_NUM_DEVS] __initdata = { 0, };
/* Beware! hw[] is also used in cleanup_module(). If __initdata also applies
   to modules, we may not declare hw[] as __initdata */
static struct scc_hardware hw[NUM_TYPES] __initdata = HARDWARE;
static char ax25_broadcast[7] __initdata =
  { 'Q'<<1, 'S'<<1, 'T'<<1, ' '<<1, ' '<<1, ' '<<1, '0'<<1 };
static char ax25_test[7] __initdata =
  { 'L'<<1, 'I'<<1, 'N'<<1, 'U'<<1, 'X'<<1, ' '<<1, '1'<<1 };


/* Global variables */

static struct scc_info *first = NULL;
static unsigned long rand;



/* Module functions */

#ifdef MODULE


MODULE_AUTHOR("Klaus Kudielka");
MODULE_DESCRIPTION("Driver for high-speed SCC boards");
MODULE_PARM(io, "1-" __MODULE_STRING(MAX_NUM_DEVS) "i");


int init_module(void)
{
  return dmascc_init();
}


void cleanup_module(void)
{
  int i;
  struct scc_info *info;

  while (first) {
    info = first;

    /* Unregister devices */
    for (i = 0; i < 2; i++) {
      if (info->dev[i].name)
	unregister_netdevice(&info->dev[i]);
    }

    /* Reset board */
    if (info->type == TYPE_TWIN)
      outb_p(0, info->dev[0].base_addr + TWIN_SERIAL_CFG);
    write_scc(info->priv[0].cmd, R9, FHWRES);
    release_region(info->dev[0].base_addr,
		   hw[info->type].io_size);

    /* Free memory */
    first = info->next;
    kfree_s(info, sizeof(struct scc_info));
  }
}


#else


__initfunc(void dmascc_setup(char *str, int *ints))
{
   int i;

   for (i = 0; i < MAX_NUM_DEVS && i < ints[0]; i++)
      io[i] = ints[i+1];
}


#endif


/* Initialization functions */

__initfunc(int dmascc_init(void))
{
  int h, i, j, n;
  int base[MAX_NUM_DEVS], tcmd[MAX_NUM_DEVS], t0[MAX_NUM_DEVS],
    t1[MAX_NUM_DEVS];
  unsigned t_val;
  unsigned long time, start[MAX_NUM_DEVS], delay[MAX_NUM_DEVS],
    counting[MAX_NUM_DEVS];

  /* Initialize random number generator */
  rand = jiffies;
  /* Cards found = 0 */
  n = 0;
  /* Warning message */
  if (!io[0]) printk("dmascc: autoprobing (dangerous)\n");

  /* Run autodetection for each card type */
  for (h = 0; h < NUM_TYPES; h++) {

    if (io[0]) {
      /* User-specified I/O address regions */
      for (i = 0; i < hw[h].num_devs; i++) base[i] = 0;
      for (i = 0; i < MAX_NUM_DEVS && io[i]; i++) {
	j = (io[i] - hw[h].io_region) / hw[h].io_delta;
	if (j >= 0 &&
	    j < hw[h].num_devs && 
	    hw[h].io_region + j * hw[h].io_delta == io[i]) {
	  base[j] = io[i];
	}
      }
    } else {
      /* Default I/O address regions */
      for (i = 0; i < hw[h].num_devs; i++) {
	base[i] = hw[h].io_region + i * hw[h].io_delta;
      }
    }

    /* Check valid I/O address regions */
    for (i = 0; i < hw[h].num_devs; i++)
      if (base[i]) {
	if (check_region(base[i], hw[h].io_size))
	  base[i] = 0;
	else {
	  tcmd[i] = base[i] + hw[h].tmr_offset + TMR_CTRL;
	  t0[i]   = base[i] + hw[h].tmr_offset + TMR_CNT0;
	  t1[i]   = base[i] + hw[h].tmr_offset + TMR_CNT1;
	}
      }

    /* Start timers */
    for (i = 0; i < hw[h].num_devs; i++)
      if (base[i]) {
	/* Timer 0: LSB+MSB, Mode 3, TMR_0_HZ */
	outb_p(0x36, tcmd[i]);
	outb_p((hw[h].tmr_hz/TMR_0_HZ) & 0xFF, t0[i]);
	outb_p((hw[h].tmr_hz/TMR_0_HZ) >> 8, t0[i]);
	/* Timer 1: LSB+MSB, Mode 0, HZ/10 */
	outb_p(0x70, tcmd[i]);
	outb_p((TMR_0_HZ/HZ*10) & 0xFF, t1[i]);
	outb_p((TMR_0_HZ/HZ*10) >> 8, t1[i]);
	start[i] = jiffies;
	delay[i] = 0;
	counting[i] = 1;
	/* Timer 2: LSB+MSB, Mode 0 */
	outb_p(0xb0, tcmd[i]);
      }
    time = jiffies;
    /* Wait until counter registers are loaded */
    udelay(2000000/TMR_0_HZ);

    /* Timing loop */
    while (jiffies - time < 13) {
      for (i = 0; i < hw[h].num_devs; i++)
	if (base[i] && counting[i]) {
	  /* Read back Timer 1: latch; read LSB; read MSB */
	  outb_p(0x40, tcmd[i]);
	  t_val = inb_p(t1[i]) + (inb_p(t1[i]) << 8);
	  /* Also check whether counter did wrap */
	  if (t_val == 0 || t_val > TMR_0_HZ/HZ*10) counting[i] = 0;
	  delay[i] = jiffies - start[i];
	}
    }

    /* Evaluate measurements */
    for (i = 0; i < hw[h].num_devs; i++)
      if (base[i]) {
	if (delay[i] >= 9 && delay[i] <= 11) {
	  /* Ok, we have found an adapter */
	  if (setup_adapter(base[i], h, n) == 0)
	    n++;
	}
      }

  } /* NUM_TYPES */

  /* If any adapter was successfully initialized, return ok */
  if (n) return 0;

  /* If no adapter found, return error */
  printk("dmascc: no adapters found\n");
  return -EIO;
}


__initfunc(int setup_adapter(int io, int h, int n))
{
  int i, irq, chip;
  struct scc_info *info;
  struct device *dev;
  struct scc_priv *priv;
  unsigned long time;
  unsigned int irqs;
  int tmr = io + hw[h].tmr_offset;
  int scc = io + hw[h].scc_offset;
  int cmd = scc + SCCA_CMD;
  char *chipnames[] = CHIPNAMES;

  /* Reset 8530 */
  write_scc(cmd, R9, FHWRES | MIE | NV);

  /* Determine type of chip by enabling SDLC/HDLC enhancements */
  write_scc(cmd, R15, SHDLCE);
  if (!read_scc(cmd, R15)) {
    /* WR7' not present. This is an ordinary Z8530 SCC. */
    chip = Z8530;
  } else {
    /* Put one character in TX FIFO */
    write_scc(cmd, R8, 0);
    if (read_scc(cmd, R0) & Tx_BUF_EMP) {
      /* TX FIFO not full. This is a Z85230 ESCC with a 4-byte FIFO. */
      chip = Z85230;
    } else {
      /* TX FIFO full. This is a Z85C30 SCC with a 1-byte FIFO. */
      chip = Z85C30;
    }
  }
  write_scc(cmd, R15, 0);

  /* Start IRQ auto-detection */
  sti();
  irqs = probe_irq_on();

  /* Enable interrupts */
  switch (h) {
  case TYPE_PI:
  case TYPE_PI2:
    outb_p(0, io + PI_DREQ_MASK);
    write_scc(cmd, R15, CTSIE);
    write_scc(cmd, R0, RES_EXT_INT);
    write_scc(cmd, R1, EXT_INT_ENAB);
    break;
  case TYPE_TWIN:
    outb_p(0, io + TWIN_DMA_CFG);
    inb_p(io + TWIN_CLR_TMR1);
    inb_p(io + TWIN_CLR_TMR2);
    outb_p(TWIN_EI, io + TWIN_SERIAL_CFG);
    break;
  }

  /* Start timer */
  outb_p(1, tmr + TMR_CNT1);
  outb_p(0, tmr + TMR_CNT1);
  /* Wait and detect IRQ */
  time = jiffies; while (jiffies - time < 2 + HZ / TMR_0_HZ);
  irq = probe_irq_off(irqs);

  /* Clear pending interrupt, disable interrupts */
  switch (h) {
  case TYPE_PI:
  case TYPE_PI2:
    write_scc(cmd, R1, 0);
    write_scc(cmd, R15, 0);
    write_scc(cmd, R0, RES_EXT_INT);
    break;
  case TYPE_TWIN:
    inb_p(io + TWIN_CLR_TMR1);
    outb_p(0, io + TWIN_SERIAL_CFG);
    break;
  }

  if (irq <= 0) {
    printk("dmascc: could not find irq of %s at %#3x (irq=%d)\n",
	   hw[h].name, io, irq);
    return -1;
  }

  /* Allocate memory */
  info = kmalloc(sizeof(struct scc_info), GFP_KERNEL | GFP_DMA);
  if (!info) {
    printk("dmascc: could not allocate memory for %s at %#3x\n",
	   hw[h].name, io);
    return -1;
  }

  /* Set up data structures */
  memset(info, 0, sizeof(struct scc_info));
  info->type = h;
  info->chip = chip;
  info->scc_base = io + hw[h].scc_offset;
  info->tmr_base = io + hw[h].tmr_offset;
  info->twin_serial_cfg = 0;
  for (i = 0; i < 2; i++) {
    dev = &info->dev[i];
    priv = &info->priv[i];
    sprintf(priv->name, "dmascc%i", 2*n+i);
    priv->info = info;
    priv->channel = i;
    priv->cmd = info->scc_base + (i ? SCCB_CMD : SCCA_CMD);
    priv->data = info->scc_base + (i ? SCCB_DATA : SCCA_DATA);
    priv->tmr = info->tmr_base + (i ? TMR_CNT2 : TMR_CNT1);
    priv->param.pclk_hz = hw[h].pclk_hz;
    priv->param.brg_tc = -1;
    priv->param.clocks = TCTRxCP | RCRTxCP;
    priv->param.txdelay = TMR_0_HZ * 10 / 1000;
    priv->param.txtime = HZ * 3;
    priv->param.sqdelay = TMR_0_HZ * 1 / 1000;
    priv->param.slottime = TMR_0_HZ * 10 / 1000;
    priv->param.waittime = TMR_0_HZ * 100 / 1000;
    priv->param.persist = 32;
    priv->rx_task.routine = rx_bh;
    priv->rx_task.data = dev;
    dev->priv = priv;
    dev->name = priv->name;
    dev->base_addr = io;
    dev->irq = irq;
    dev->open = scc_open;
    dev->stop = scc_close;
    dev->do_ioctl = scc_ioctl;
    dev->hard_start_xmit = scc_send_packet;
    dev->get_stats = scc_get_stats;
    dev->hard_header = ax25_encapsulate;
    dev->rebuild_header = ax25_rebuild_header;
    dev->set_mac_address = scc_set_mac_address;
    SET_DEV_INIT(dev->init);
    dev->type = ARPHRD_AX25;
    dev->hard_header_len = 73;
    dev->mtu = 1500;
    dev->addr_len = 7;
    dev->tx_queue_len = 64;
    memcpy(dev->broadcast, ax25_broadcast, 7);
    memcpy(dev->dev_addr, ax25_test, 7);
    dev_init_buffers(dev);
    if (register_netdevice(dev)) {
      printk("dmascc: could not register %s\n", dev->name);
      dev->name = NULL;
    }
  }

  request_region(io, hw[h].io_size, "dmascc");

  info->next = first;
  first = info;
  printk("dmascc: found %s (%s) at %#3x, irq %d\n", hw[h].name,
	 chipnames[chip], io, irq);
  return 0;
}


/* Driver functions */

static inline void write_scc(int ctl, int reg, int val)
{
  outb_p(reg, ctl);
  outb_p(val, ctl);
}


static inline int read_scc(int ctl, int reg)
{
  outb_p(reg, ctl);
  return inb_p(ctl);
}


static int scc_open(struct device *dev)
{
  struct scc_priv *priv = dev->priv;
  struct scc_info *info = priv->info;
  int io = dev->base_addr;
  int cmd = priv->cmd;

  /* Request IRQ if not already used by other channel */
  if (!info->open) {
    if (request_irq(dev->irq, scc_isr, SA_INTERRUPT, "dmascc", info))
      return -EAGAIN;
  }

  /* Request DMA if required */
  if (dev->dma && request_dma(dev->dma, "dmascc")) {
    if (!info->open) free_irq(dev->irq, info);
    return -EAGAIN;
  }

  /* Initialize local variables */
  dev->tbusy = 0;
  priv->rx_ptr = 0;
  priv->rx_over = 0;
  priv->rx_head = priv->rx_tail = priv->rx_count = 0;
  priv->tx_state = TX_IDLE;
  priv->tx_head = priv->tx_tail = priv->tx_count = 0;
  priv->tx_ptr = 0;
  priv->tx_sem = 0;

  /* Reset channel */
  write_scc(cmd, R9, (priv->channel ? CHRB : CHRA) | MIE | NV);
  /* X1 clock, SDLC mode */
  write_scc(cmd, R4, SDLC | X1CLK);
  /* DMA */
  write_scc(cmd, R1, EXT_INT_ENAB | WT_FN_RDYFN);
  /* 8 bit RX char, RX disable */
  write_scc(cmd, R3, Rx8);
  /* 8 bit TX char, TX disable */
  write_scc(cmd, R5, Tx8);
  /* SDLC address field */
  write_scc(cmd, R6, 0);
  /* SDLC flag */
  write_scc(cmd, R7, FLAG);
  switch (info->chip) {
  case Z85C30:
    /* Select WR7' */
    write_scc(cmd, R15, SHDLCE);
    /* Auto EOM reset */
    write_scc(cmd, R7, AUTOEOM);
    write_scc(cmd, R15, 0);
    break;
  case Z85230:
    /* Select WR7' */
    write_scc(cmd, R15, SHDLCE);
    /* RX FIFO half full (interrupt only), Auto EOM reset,
       TX FIFO empty (DMA only) */
    write_scc(cmd, R7, AUTOEOM | (dev->dma ? TXFIFOE : RXFIFOH));
    write_scc(cmd, R15, 0);
    break;
  }
  /* Preset CRC, NRZ(I) encoding */
  write_scc(cmd, R10, CRCPS | (priv->param.nrzi ? NRZI : NRZ));

  /* Configure baud rate generator */
  if (priv->param.brg_tc >= 0) {
    /* Program BR generator */
    write_scc(cmd, R12, priv->param.brg_tc & 0xFF);
    write_scc(cmd, R13, (priv->param.brg_tc>>8) & 0xFF);
    /* BRG source = SYS CLK; enable BRG; DTR REQ function (required by
       PackeTwin, not connected on the PI2); set DPLL source to BRG */
    write_scc(cmd, R14, SSBR | DTRREQ | BRSRC | BRENABL);
    /* Enable DPLL */
    write_scc(cmd, R14, SEARCH | DTRREQ | BRSRC | BRENABL);
  } else {
    /* Disable BR generator */
    write_scc(cmd, R14, DTRREQ | BRSRC);
  }

  /* Configure clocks */
  if (info->type == TYPE_TWIN) {
    /* Disable external TX clock receiver */
    outb_p((info->twin_serial_cfg &=
	    ~(priv->channel ? TWIN_EXTCLKB : TWIN_EXTCLKA)), 
	   io + TWIN_SERIAL_CFG);
  }
  write_scc(cmd, R11, priv->param.clocks);
  if ((info->type == TYPE_TWIN) && !(priv->param.clocks & TRxCOI)) {
    /* Enable external TX clock receiver */
    outb_p((info->twin_serial_cfg |=
	    (priv->channel ? TWIN_EXTCLKB : TWIN_EXTCLKA)),
	   io + TWIN_SERIAL_CFG);
  }

  /* Configure PackeTwin */
  if (info->type == TYPE_TWIN) {
    /* Assert DTR, enable interrupts */
    outb_p((info->twin_serial_cfg |= TWIN_EI |
	    (priv->channel ? TWIN_DTRB_ON : TWIN_DTRA_ON)),
	   io + TWIN_SERIAL_CFG);
  }

  /* Read current status */
  priv->status = read_scc(cmd, R0);
  /* Enable SYNC, DCD, and CTS interrupts */
  write_scc(cmd, R15, DCDIE | CTSIE | SYNCIE);

  /* Configure PI2 DMA */
  if (info->type <= TYPE_PI2) outb_p(1, io + PI_DREQ_MASK);

  dev->start = 1;
  info->open++;
  MOD_INC_USE_COUNT;

  return 0;
}


static int scc_close(struct device *dev)
{
  struct scc_priv *priv = dev->priv;
  struct scc_info *info = priv->info;
  int io = dev->base_addr;
  int cmd = priv->cmd;

  dev->start = 0;
  info->open--;
  MOD_DEC_USE_COUNT;

  if (info->type == TYPE_TWIN)
    /* Drop DTR */
    outb_p((info->twin_serial_cfg &=
	    (priv->channel ? ~TWIN_DTRB_ON : ~TWIN_DTRA_ON)),
	   io + TWIN_SERIAL_CFG);

  /* Reset channel, free DMA */
  write_scc(cmd, R9, (priv->channel ? CHRB : CHRA) | MIE | NV);
  if (dev->dma) {
    if (info->type == TYPE_TWIN) outb_p(0, io + TWIN_DMA_CFG);
    free_dma(dev->dma);
  }

  if (!info->open) {
    if (info->type <= TYPE_PI2) outb_p(0, io + PI_DREQ_MASK);
    free_irq(dev->irq, info);
  }
  return 0;
}


static int scc_ioctl(struct device *dev, struct ifreq *ifr, int cmd)
{
  int rc;
  struct scc_priv *priv = dev->priv;
  
  switch (cmd) {
  case SIOCGSCCPARAM:
    rc = verify_area(VERIFY_WRITE, ifr->ifr_data, sizeof(struct scc_param));
    if (rc) return rc;
    copy_to_user(ifr->ifr_data, &priv->param, sizeof(struct scc_param));
    return 0;
  case SIOCSSCCPARAM:
    if (!suser()) return -EPERM;
    rc = verify_area(VERIFY_READ, ifr->ifr_data, sizeof(struct scc_param));
    if (rc) return rc;
    if (dev->start) return -EAGAIN;
    copy_from_user(&priv->param, ifr->ifr_data, sizeof(struct scc_param));
    dev->dma = priv->param.dma;
    return 0;
  default:
    return -EINVAL;
  }
}


static int scc_send_packet(struct sk_buff *skb, struct device *dev)
{
  struct scc_priv *priv = dev->priv;
  struct scc_info *info = priv->info;
  int cmd = priv->cmd;
  unsigned long flags;
  int i;

  /* Block a timer-based transmit from overlapping */
  if (test_and_set_bit(0, (void *) &priv->tx_sem) != 0) {
    atomic_inc((void *) &priv->stats.tx_dropped);
    dev_kfree_skb(skb);
    return 0;
  }

  /* Return with an error if we cannot accept more data */
  if (dev->tbusy) {
    priv->tx_sem = 0;
    return -1;
  }

  /* Transfer data to DMA buffer */
  i = priv->tx_head;
  memcpy(priv->tx_buf[i], skb->data+1, skb->len-1);
  priv->tx_len[i] = skb->len-1;

  save_flags(flags);
  cli();

  /* Set the busy flag if we just filled up the last buffer */
  priv->tx_head = (i + 1) % NUM_TX_BUF;
  priv->tx_count++;
  if (priv->tx_count == NUM_TX_BUF) dev->tbusy = 1;

  /* Set new TX state */
  if (priv->tx_state == TX_IDLE) {
    /* Assert RTS, start timer */
    priv->tx_state = TX_TXDELAY;
    if (info->type <= TYPE_PI2) outb_p(0, dev->base_addr + PI_DREQ_MASK);
    write_scc(cmd, R5, TxCRC_ENAB | RTS | TxENAB | Tx8);
    if (info->type <= TYPE_PI2) outb_p(1, dev->base_addr + PI_DREQ_MASK);
    priv->tx_start = jiffies;
    delay(dev, priv->param.txdelay);
  }

  restore_flags(flags);

  dev_kfree_skb(skb);

  priv->tx_sem = 0;
  return 0;
}


static struct enet_statistics *scc_get_stats(struct device *dev)
{
  struct scc_priv *priv = dev->priv;

  return &priv->stats;
}


static int scc_set_mac_address(struct device *dev, void *sa)
{
  memcpy(dev->dev_addr, ((struct sockaddr *)sa)->sa_data, dev->addr_len);
  return 0;
}


static void scc_isr(int irq, void *dev_id, struct pt_regs * regs)
{
  struct scc_info *info = dev_id;
  int is, io = info->dev[0].base_addr;

  /* We're a fast IRQ handler and are called with interrupts disabled */

  /* IRQ sharing doesn't make sense due to ISA's edge-triggered
     interrupts, hence it is safe to return if we have found and
     processed a single device. */

  /* Interrupt processing: We loop until we know that the IRQ line is
     low. If another positive edge occurs afterwards during the ISR,
     another interrupt will be triggered by the interrupt controller
     as soon as the IRQ level is enabled again (see asm/irq.h). */

  switch (info->type) {
  case TYPE_PI:
  case TYPE_PI2:
    outb_p(0, io + PI_DREQ_MASK);
    z8530_isr(info);
    outb_p(1, io + PI_DREQ_MASK);
    return;
  case TYPE_TWIN:
    while ((is = ~inb_p(io + TWIN_INT_REG)) &
	   TWIN_INT_MSK) {
      if (is & TWIN_SCC_MSK) {
	z8530_isr(info);
      } else if (is & TWIN_TMR1_MSK) {
	inb_p(io + TWIN_CLR_TMR1);
	tm_isr(&info->dev[0]);
      } else {
	inb_p(io + TWIN_CLR_TMR2);
	tm_isr(&info->dev[1]);
      }
    }
    /* No interrupts pending from the PackeTwin */
    return;
  }
}


static inline void z8530_isr(struct scc_info *info)
{
  int is, a_cmd;
  
  a_cmd = info->scc_base + SCCA_CMD;

  while ((is = read_scc(a_cmd, R3))) {
    if (is & CHARxIP) {
      rx_isr(&info->dev[0]);
    } else if (is & CHATxIP) {
      tx_isr(&info->dev[0]);
    } else if (is & CHAEXT) {
      es_isr(&info->dev[0]);
    } else if (is & CHBRxIP) {
      rx_isr(&info->dev[1]);
    } else if (is & CHBTxIP) {
      tx_isr(&info->dev[1]);
    } else {
      es_isr(&info->dev[1]);
    }
  }
  /* Ok, no interrupts pending from this 8530. The INT line should
     be inactive now. */
}


static void rx_isr(struct device *dev)
{
  struct scc_priv *priv = dev->priv;
  int cmd = priv->cmd;

  if (dev->dma) {
    /* Check special condition and perform error reset. See 2.4.7.5. */
    special_condition(dev, read_scc(cmd, R1));
    write_scc(cmd, R0, ERR_RES);
  } else {
    /* Check special condition for each character. Error reset not necessary.
       Same algorithm for SCC and ESCC. See 2.4.7.1 and 2.4.7.4. */
    int rc;
    while (read_scc(cmd, R0) & Rx_CH_AV) {
      rc = read_scc(cmd, R1);
      if (priv->rx_ptr < BUF_SIZE)
	priv->rx_buf[priv->rx_head][priv->rx_ptr++] = read_scc(cmd, R8);
      else {
	priv->rx_over = 2;
	read_scc(cmd, R8);
      }
      special_condition(dev, rc);
    }
  }
}


static void special_condition(struct device *dev, int rc)
{
  struct scc_priv *priv = dev->priv;
  int cb, cmd = priv->cmd;
  unsigned long flags;

  /* See Figure 2-15. Only overrun and EOF need to be checked. */
  
  if (rc & Rx_OVR) {
    /* Receiver overrun */
    priv->rx_over = 1;
    if (!dev->dma) write_scc(cmd, R0, ERR_RES);
  } else if (rc & END_FR) {
    /* End of frame. Get byte count */
    if (dev->dma) {
        flags=claim_dma_lock();
	disable_dma(dev->dma);
	clear_dma_ff(dev->dma);
	cb = BUF_SIZE - get_dma_residue(dev->dma) - 2;
	release_dma_lock(flags);
	
    } else {
	cb = priv->rx_ptr - 2;
    }
    if (priv->rx_over) {
      /* We had an overrun */
      priv->stats.rx_errors++;
      if (priv->rx_over == 2) priv->stats.rx_length_errors++;
      else priv->stats.rx_fifo_errors++;
      priv->rx_over = 0;
    } else if (rc & CRC_ERR) {
      /* Count invalid CRC only if packet length >= minimum */
      if (cb >= 8) {
	priv->stats.rx_errors++;
	priv->stats.rx_crc_errors++;
      }
    } else {
      if (cb >= 8) {
	/* Put good frame in FIFO */
	priv->rx_len[priv->rx_head] = cb;
	priv->rx_head = (priv->rx_head + 1) % NUM_RX_BUF;
	priv->rx_count++;
	if (priv->rx_count == NUM_RX_BUF) {
	  /* Disable receiver if FIFO full */
	  write_scc(cmd, R3, Rx8);
	  priv->stats.rx_errors++;
	  priv->stats.rx_over_errors++;
	}
	/* Mark bottom half handler */
	queue_task(&priv->rx_task, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
      }
    }
    /* Get ready for new frame */
    if (dev->dma) {
      
      flags=claim_dma_lock();
      set_dma_addr(dev->dma, (int) priv->rx_buf[priv->rx_head]);
      set_dma_count(dev->dma, BUF_SIZE);
      enable_dma(dev->dma);
      release_dma_lock(flags);
      
    } else {
      priv->rx_ptr = 0;
    }
  }
}


static void rx_bh(void *arg)
{
  struct device *dev = arg;
  struct scc_priv *priv = dev->priv;
  struct scc_info *info = priv->info;
  int cmd = priv->cmd;
  int i = priv->rx_tail;
  int cb;
  unsigned long flags;
  struct sk_buff *skb;
  unsigned char *data;

  save_flags(flags);
  cli();

  while (priv->rx_count) {
    restore_flags(flags);
    cb = priv->rx_len[i];
    /* Allocate buffer */
    skb = dev_alloc_skb(cb+1);
    if (skb == NULL) {
      /* Drop packet */
      priv->stats.rx_dropped++;
    } else {
      /* Fill buffer */
      data = skb_put(skb, cb+1);
      data[0] = 0;
      memcpy(&data[1], priv->rx_buf[i], cb);
      skb->dev = dev;
      skb->protocol = ntohs(ETH_P_AX25);
      skb->mac.raw = skb->data;
      netif_rx(skb);
      priv->stats.rx_packets++;
    }
    save_flags(flags);
    cli();
    /* Enable receiver if RX buffers have been unavailable */
    if ((priv->rx_count == NUM_RX_BUF) && (priv->status & DCD)) {
      if (info->type <= TYPE_PI2) outb_p(0, dev->base_addr + PI_DREQ_MASK);
      write_scc(cmd, R3, RxENABLE | Rx8 | RxCRC_ENAB);
      if (info->type <= TYPE_PI2) outb_p(1, dev->base_addr + PI_DREQ_MASK);
    }
    /* Move tail */
    priv->rx_tail = i = (i + 1) % NUM_RX_BUF;
    priv->rx_count--;
  }

  restore_flags(flags);
}


static void tx_isr(struct device *dev)
{
  struct scc_priv *priv = dev->priv;
  int cmd = priv->cmd;
  int i = priv->tx_tail, p = priv->tx_ptr;

  /* Suspend TX interrupts if we don't want to send anything.
     See Figure 2-22. */
  if (p ==  priv->tx_len[i]) {
    write_scc(cmd, R0, RES_Tx_P);
    return;
  }

  /* Write characters */
  while ((read_scc(cmd, R0) & Tx_BUF_EMP) && p < priv->tx_len[i]) {
    write_scc(cmd, R8, priv->tx_buf[i][p++]);
  }
  priv->tx_ptr = p;

}


static void es_isr(struct device *dev)
{
  struct scc_priv *priv = dev->priv;
  struct scc_info *info = priv->info;
  int i, cmd = priv->cmd;
  int st, dst, res;
  unsigned long flags;

  /* Read status and reset interrupt bit */
  st = read_scc(cmd, R0);
  write_scc(cmd, R0, RES_EXT_INT);
  dst = priv->status ^ st;
  priv->status = st;

  /* Since the EOM latch is reset automatically, we assume that
     it has been zero if and only if we are in the TX_ACTIVE state.
     Otherwise we follow 2.4.9.6. */

  /* Transmit underrun */
  if ((priv->tx_state == TX_ACTIVE) && (st & TxEOM)) {
    /* Get remaining bytes */
    i = priv->tx_tail;
    if (dev->dma) {
      flags=claim_dma_lock();
      disable_dma(dev->dma);
      clear_dma_ff(dev->dma);
      res = get_dma_residue(dev->dma);
      release_dma_lock(flags);
    } else {
      res = priv->tx_len[i] - priv->tx_ptr;
      if (res) write_scc(cmd, R0, RES_Tx_P);
      priv->tx_ptr = 0;
    }
    /* Remove frame from FIFO */
    priv->tx_tail = (i + 1) % NUM_TX_BUF;
    priv->tx_count--;
    dev->tbusy = 0;
    /* Check if another frame is available and we are allowed to transmit */
    if (priv->tx_count && (jiffies - priv->tx_start) < priv->param.txtime) {
      if (dev->dma) {
        flags=claim_dma_lock();
	set_dma_addr(dev->dma, (int) priv->tx_buf[priv->tx_tail]);
	set_dma_count(dev->dma, priv->tx_len[priv->tx_tail]);
	enable_dma(dev->dma);
	release_dma_lock(flags);
      } else {
	/* If we have an ESCC, we are allowed to write data bytes
	   immediately. Otherwise we have to wait for the next
	   TX interrupt. See Figure 2-22. */
	if (info->chip == Z85230) {
	  tx_isr(dev);
	}
      }
    } else {
      /* No frame available. Disable interrupts. */
      priv->tx_state = TX_SQDELAY;
      delay(dev, priv->param.sqdelay);
      write_scc(cmd, R15, DCDIE | CTSIE | SYNCIE);
      write_scc(cmd, R1, EXT_INT_ENAB | WT_FN_RDYFN);
    }
    /* Update packet statistics */
    if (res) {
      priv->stats.tx_errors++;
      priv->stats.tx_fifo_errors++;
    } else {
      priv->stats.tx_packets++;
    }
    /* Inform upper layers */
    mark_bh(NET_BH);
  }

  /* DCD transition */
  if ((priv->tx_state < TX_TXDELAY) && (dst & DCD)) {
    /* Transmitter state change */
    priv->tx_state = TX_OFF;
    /* Enable or disable receiver */
    if (st & DCD) {
      if (dev->dma) {
	/* Program DMA controller */
	flags=claim_dma_lock();
	disable_dma(dev->dma);
	clear_dma_ff(dev->dma);
	set_dma_mode(dev->dma, DMA_MODE_READ);
	set_dma_addr(dev->dma, (int) priv->rx_buf[priv->rx_head]);
	set_dma_count(dev->dma, BUF_SIZE);
	enable_dma(dev->dma);
	release_dma_lock(flags);
	/* Configure PackeTwin DMA */
	if (info->type == TYPE_TWIN) {
	  outb_p((dev->dma == 1) ? TWIN_DMA_HDX_R1 : TWIN_DMA_HDX_R3,
		 dev->base_addr + TWIN_DMA_CFG);
	}
	/* Sp. cond. intr. only, ext int enable */
	write_scc(cmd, R1, EXT_INT_ENAB | INT_ERR_Rx |
		  WT_RDY_RT | WT_FN_RDYFN | WT_RDY_ENAB);
      } else {
	/* Intr. on all Rx characters and Sp. cond., ext int enable */
	write_scc(cmd, R1, EXT_INT_ENAB | INT_ALL_Rx | WT_RDY_RT |
		  WT_FN_RDYFN);
      }
      if (priv->rx_count < NUM_RX_BUF) {
	/* Enable receiver */
	write_scc(cmd, R3, RxENABLE | Rx8 | RxCRC_ENAB);
      }
    } else {
      /* Disable DMA */
      if (dev->dma)
      {
      	flags=claim_dma_lock();
      	disable_dma(dev->dma);
      	release_dma_lock(flags);
      }
      /* Disable receiver */
      write_scc(cmd, R3, Rx8);
      /* DMA disable, RX int disable, Ext int enable */
      write_scc(cmd, R1, EXT_INT_ENAB | WT_RDY_RT | WT_FN_RDYFN);
      /* Transmitter state change */
      if (random() > priv->param.persist)
	delay(dev, priv->param.slottime);
      else {
	if (priv->tx_count) {
	  priv->tx_state = TX_TXDELAY;
	  write_scc(cmd, R5, TxCRC_ENAB | RTS | TxENAB | Tx8);
	  priv->tx_start = jiffies;
	  delay(dev, priv->param.txdelay);
	} else {
	  priv->tx_state = TX_IDLE;
	}
      }
    }
  }

  /* CTS transition */
  if ((info->type <= TYPE_PI2) && (dst & CTS) && (~st & CTS)) {
    /* Timer has expired */
    tm_isr(dev);
  }

  /* /SYNC/HUNT transition */
  if ((dst & SYNC_HUNT) && (~st & SYNC_HUNT)) {
    /* Reset current frame and clear RX FIFO */
    while (read_scc(cmd, R0) & Rx_CH_AV) read_scc(cmd, R8);
    priv->rx_over = 0;
    if (dev->dma) {
      flags=claim_dma_lock();
      disable_dma(dev->dma);
      clear_dma_ff(dev->dma);
      set_dma_addr(dev->dma, (int) priv->rx_buf[priv->rx_head]);
      set_dma_count(dev->dma, BUF_SIZE);
      enable_dma(dev->dma);
      release_dma_lock(flags);
    } else {
      priv->rx_ptr = 0;
    }
  }
}


static void tm_isr(struct device *dev)
{
  struct scc_priv *priv = dev->priv;
  struct scc_info *info = priv->info;
  int cmd = priv->cmd;
  unsigned long flags;

  switch (priv->tx_state) {
  case TX_OFF:
    if (~priv->status & DCD) {
      if (random() > priv->param.persist) delay(dev, priv->param.slottime);
      else {
	if (priv->tx_count) {
	  priv->tx_state = TX_TXDELAY;
	  write_scc(cmd, R5, TxCRC_ENAB | RTS | TxENAB | Tx8);
	  priv->tx_start = jiffies;
	  delay(dev, priv->param.txdelay);
	} else {
	  priv->tx_state = TX_IDLE;
	}
      }
    }
    break;
  case TX_TXDELAY:
    priv->tx_state = TX_ACTIVE;
    if (dev->dma) {
      /* Program DMA controller */
      
      flags=claim_dma_lock();
      disable_dma(dev->dma);
      clear_dma_ff(dev->dma);
      set_dma_mode(dev->dma, DMA_MODE_WRITE);
      set_dma_addr(dev->dma, (int) priv->tx_buf[priv->tx_tail]);
      set_dma_count(dev->dma, priv->tx_len[priv->tx_tail]);
      enable_dma(dev->dma);
      release_dma_lock(flags);
      
      /* Configure PackeTwin DMA */
      if (info->type == TYPE_TWIN) {
	outb_p((dev->dma == 1) ? TWIN_DMA_HDX_T1 : TWIN_DMA_HDX_T3,
	       dev->base_addr + TWIN_DMA_CFG);
      }
      /* Enable interrupts and DMA. On the PackeTwin, the DTR//REQ pin
	 is used for TX DMA requests, but we enable the WAIT/DMA request
	 pin, anyway */
      write_scc(cmd, R15, TxUIE | DCDIE | CTSIE | SYNCIE);
      write_scc(cmd, R1, EXT_INT_ENAB | WT_FN_RDYFN | WT_RDY_ENAB);
    } else {
      write_scc(cmd, R15, TxUIE | DCDIE | CTSIE | SYNCIE);
      write_scc(cmd, R1, EXT_INT_ENAB | WT_FN_RDYFN | TxINT_ENAB);
      tx_isr(dev);
    }
    if (info->chip == Z8530) write_scc(cmd, R0, RES_EOM_L);
    break;
  case TX_SQDELAY:
    /* Disable transmitter */
    write_scc(cmd, R5, TxCRC_ENAB | Tx8);
    /* Transmitter state change: Switch to TX_OFF and wait at least
       1 slottime. */
    priv->tx_state = TX_OFF;    
    if (~priv->status & DCD) delay(dev, priv->param.waittime);
  }
}


static inline void delay(struct device *dev, int t)
{
  struct scc_priv *priv = dev->priv;
  int tmr = priv->tmr;

  outb_p(t & 0xFF, tmr);
  outb_p((t >> 8) & 0xFF, tmr);
}


static inline unsigned char random(void)
{
  /* See "Numerical Recipes in C", second edition, p. 284 */
  rand = rand * 1664525L + 1013904223L;
  return (unsigned char) (rand >> 24);
}


