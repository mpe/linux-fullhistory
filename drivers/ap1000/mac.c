  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/*
 * Routines for controlling the FORMAC+
 */
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>	/* For the statistics structure. */
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/inet.h>
#include <net/sock.h>

#include <asm/ap1000/apreg.h>
#include <asm/ap1000/apservice.h>
#include <asm/pgtable.h>

#include "apfddi.h"
#include "smt-types.h"
#include "am79c830.h"
#include "mac.h"
#include "plc.h"
#include "apfddi-reg.h"

#define MAC_DEBUG 0

/* Values for dma_state */
#define IDLE            0
#define XMITTING	1
#define RECVING		2

/*
 * Messages greater than this value are transferred to the FDDI send buffer
 * using DMA.
 */
#define DMA_XMIT_THRESHOLD 64
#define DMA_RECV_THRESHOLD 64

/*
 * If the FDDI receive buffer is occupied by less than this value, then
 * sending has priority.
 */
#define RECV_THRESHOLD (20*1024)

#define DMA_RESET_MASKS ((AP_CLR_INTR_MASK<<DMA_INTR_NORMAL_SH) | \
			 (AP_CLR_INTR_MASK<<DMA_INTR_ERROR_SH))

#define DMA_INTR_REQS ((AP_INTR_REQ<<DMA_INTR_NORMAL_SH) | \
		       (AP_INTR_REQ<<DMA_INTR_ERROR_SH))

static void mac_print_state(void);

typedef unsigned int	mac_status_t;

static volatile struct mac_queue *mac_queue_top = NULL;
static volatile struct mac_queue *mac_queue_bottom = NULL;

struct formac_state {
    LoopbackType loopback;
    int	ring_op;
    int	recv_ptr;
    int	recv_empty;
    int	recv_ovf;
    int	xmit_ptr;
    int	xmit_free;
    int	xmit_start;
    int	xmit_chains;
    int	xmit_more_ptr;
    int	frames_xmitted;
    int	xmit_chain_start[3];
    int frames_recvd;
    int recv_aborted;
    int	xmit_aborted;
    int	wrong_bb;
    int recv_error;
    volatile struct mac_queue *cur_macq; /* Current queue el for send DMA   */
    volatile struct mac_buf cur_mbuf;    /* Current mac_buf for send DMA    */
    struct sk_buff *cur_skb;             /* skb for received packets by DMA */
    int dma_state;
};

#define SPFRAMES_SIZE	64		/* # words for special frames area */
#define RECV_BUF_START	SPFRAMES_SIZE
#define RECV_BUF_END	(BUFFER_SIZE / 2 + 2048)
#define RECV_BUF_SIZE	(RECV_BUF_END - RECV_BUF_START)
#define XMIT_BUF_START	RECV_BUF_END
#define XMIT_BUF_END	BUFFER_SIZE

#define S2_RMT_EVENTS	(S2_CLAIM_STATE | S2_MY_CLAIM | S2_HIGHER_CLAIM | \
			 S2_LOWER_CLAIM | S2_BEACON_STATE | S2_MY_BEACON | \
			 S2_OTHER_BEACON | S2_RING_OP | S2_MULTIPLE_DA | \
			 S2_TOKEN_ERR | S2_DUPL_CLAIM | S2_TRT_EXP_RECOV)

struct mac_info *this_mac_info;
struct formac_state this_mac_state;

int
mac_init(struct mac_info *mip)
{
    struct formac_state *msp = &this_mac_state;

    bif_add_debug_key('f',mac_print_state,"show FDDI mac state");

    this_mac_info = mip;

    mac->cmdreg1 = C1_SOFTWARE_RESET;
    mac->said = (mip->s_address[0] << 8) + mip->s_address[1];
    mac->laim = (mip->l_address[0] << 8) + mip->l_address[1];
    mac->laic = (mip->l_address[2] << 8) + mip->l_address[3];
    mac->lail = (mip->l_address[4] << 8) + mip->l_address[5];
    mac->sagp = (mip->s_group_adrs[0] << 8) + mip->s_group_adrs[1];
    mac->lagm = (mip->l_group_adrs[0] << 8) + mip->l_group_adrs[1];
    mac->lagc = (mip->l_group_adrs[2] << 8) + mip->l_group_adrs[3];
    mac->lagl = (mip->l_group_adrs[4] << 8) + mip->l_group_adrs[5];
    mac->tmax = mip->tmax >> 5;
    mac->tvx = (mip->tvx - 254) / 255;	/* it's -ve, round downwards */
    mac->treq0 = mip->treq;
    mac->treq1 = mip->treq >> 16;
    mac->pri0 = ~0;
    mac->pri1 = ~0;
    mac->pri2 = ~0;
    mac->mdreg2 = /*M2_STRIP_FCS +*/ M2_CHECK_PARITY + M2_EVEN_PARITY
	+ 3 * M2_RCV_BYTE_BDRY + M2_ENABLE_HSREQ
	+ M2_ENABLE_NPDMA + M2_SYNC_NPDMA + M2_RECV_BAD_FRAMES;
    mac->eacb = RECV_BUF_START - 1;
    mac->earv = XMIT_BUF_START - 1;
    mac->eas = mac->earv;
    mac->eaa0 = BUFFER_SIZE - 1;
    mac->eaa1 = mac->eaa0;
    mac->eaa2 = mac->eaa1;
    mac->wpxsf = 0;
    mac->rpr = RECV_BUF_START;
    mac->wpr = RECV_BUF_START + 1;
    mac->swpr = RECV_BUF_START;
    mac->wpxs = mac->eas;
    mac->swpxs = mac->eas;
    mac->rpxs = mac->eas;
    mac->wpxa0 = XMIT_BUF_START;
    mac->rpxa0 = XMIT_BUF_START;

    memset(msp, 0, sizeof(*msp));
    msp->recv_ptr = RECV_BUF_START;
    msp->recv_empty = 1;
    msp->xmit_ptr = XMIT_BUF_START;
    msp->xmit_free = XMIT_BUF_START + 1;
    msp->xmit_start = XMIT_BUF_START;
    msp->xmit_chains = 0;
    msp->frames_xmitted = 0;
    msp->frames_recvd = 0;
    msp->recv_aborted = 0;

    mac->mdreg1 = M1_MODE_MEMORY;

    mac_make_spframes();

    return 0;
}

int
mac_inited(struct mac_info *mip)
{
    struct formac_state *msp = &this_mac_state;
    mac_status_t st1, st2;

    if (mac->said != (mip->s_address[0] << 8) + mip->s_address[1]
	|| mac->laim != (mip->l_address[0] << 8) + mip->l_address[1]
	|| mac->laic != (mip->l_address[2] << 8) + mip->l_address[3]
	|| mac->lail != (mip->l_address[4] << 8) + mip->l_address[5]
	|| mac->sagp != (mip->s_group_adrs[0] << 8) + mip->s_group_adrs[1]
	|| mac->lagm != (mip->l_group_adrs[0] << 8) + mip->l_group_adrs[1]
	|| mac->lagc != (mip->l_group_adrs[2] << 8) + mip->l_group_adrs[3]
	|| mac->lagl != (mip->l_group_adrs[4] << 8) + mip->l_group_adrs[5])
	return 1;
    if ((mac->mdreg1 & ~M1_ADDET) != (M1_MODE_ONLINE | M1_SELECT_RA
				      | M1_FULL_DUPLEX))
	return 3;
    if (mac->treq0 != (mip->treq & 0xffff)
	|| mac->treq1 != ((unsigned)mip->treq >> 16))
	return 4;

    st1 = (mac->st1u << 16) + mac->st1l;
    st2 = (mac->st2u << 16) + mac->st2l;
    if ((st2 & S2_RING_OP) == 0)
	return 5;

    /* It's probably OK, reset some things to be safe. */
    this_mac_info = mip;
    *csr0 &= ~CS0_HREQ;
    mac->tmax = mip->tmax >> 5;
    mac->tvx = (mip->tvx - 254) / 255;	/* it's -ve, round downwards */
    mac->pri0 = ~0;
    mac->pri1 = ~0;
    mac->pri2 = ~0;
    mac->mdreg2 = /*M2_STRIP_FCS +*/ M2_CHECK_PARITY + M2_EVEN_PARITY
	+ 3 * M2_RCV_BYTE_BDRY + M2_ENABLE_HSREQ
	+ M2_ENABLE_NPDMA + M2_SYNC_NPDMA + M2_RECV_BAD_FRAMES;

    /* clear out the receive queue */
    mac->mdreg1 = (mac->mdreg1 & ~M1_ADDET) | M1_ADDET_DISABLE_RECV;
    mac->rpr = RECV_BUF_START;
    mac->wpr = RECV_BUF_START + 1;
    mac->swpr = RECV_BUF_START;

    memset(msp, 0, sizeof(*msp));
    msp->recv_ptr = RECV_BUF_START;
    msp->recv_empty = 1;

    /* XXX reset transmit pointers */
    mac->cmdreg2 = C2_ABORT_XMIT;
    mac->cmdreg2 = C2_RESET_XMITQS;
    mac->wpxa0 = XMIT_BUF_START;
    mac->rpxa0 = XMIT_BUF_START;
    msp->xmit_ptr = XMIT_BUF_START;
    msp->xmit_free = XMIT_BUF_START + 1;
    msp->xmit_start = XMIT_BUF_START;
    msp->xmit_chains = 0;

    mac_make_spframes();
    mac->cmdreg1 = C1_CLR_ALL_LOCKS;

    msp->frames_xmitted = 0;
    msp->frames_recvd = 0;
    msp->recv_aborted = 0;
    msp->ring_op = 1;

    mac->mdreg1 = (mac->mdreg1 & ~M1_ADDET) | M1_ADDET_NSA;
    mac->imsk1u = ~(S1_XMIT_ABORT | S1_END_FRAME_ASYNC0) >> 16;
    mac->imsk1l = ~(S1_PAR_ERROR_ASYNC0 | S1_QUEUE_LOCK_ASYNC0);
    mac->imsk2u = ~(S2_RECV_COMPLETE | S2_RECV_BUF_FULL | S2_RECV_FIFO_OVF
		    | S2_ERR_SPECIAL_FR | S2_RMT_EVENTS
		    | S2_NP_SIMULT_LOAD) >> 16;
    mac->imsk2l = ~(S2_RMT_EVENTS | S2_MISSED_FRAME);

    return 0;
}

void mac_make_spframes(void)
{
    volatile int *bp;
    struct mac_info *mip = this_mac_info;
    int sa;
    struct formac_state *msp = &this_mac_state;

    /* initialize memory to avoid parity errors */
    *csr0 &= ~CS0_HREQ;
    *csr1 &= ~CS1_BUF_WR_TAG;
    for (bp = &buffer_mem[BUFFER_SIZE]; bp > &buffer_mem[XMIT_BUF_START];)
	*--bp = 0xdeadbeef;
    for (; bp > buffer_mem;)
	*--bp = 0xfeedf00d;
    buffer_mem[msp->recv_ptr] = 0;

    bp = buffer_mem;
    *bp++ = 0;			/* auto-void frame pointer (not used) */

    /* make claim frame */
    sa = bp - buffer_mem;
    *bp++ = 0xd8000011;		/* claim frame descr. + length */
    *bp++ = 0xc3;		/* FC value for claim frame, long addr */
    *bp++ = (mip->l_address[0] << 24) + (mip->l_address[1] << 16)
	+ (mip->l_address[2] << 8) + mip->l_address[3];
    *bp++ = (mip->l_address[4] << 24) + (mip->l_address[5] << 16)
	+ (mip->l_address[0] << 8) + mip->l_address[1];
    *bp++ = (mip->l_address[2] << 24) + (mip->l_address[3] << 16)
	+ (mip->l_address[4] << 8) + mip->l_address[5];
    *bp++ = mip->treq;
    mac->sacl = bp - buffer_mem; /* points to pointer to claim frame */
    *bp++ = 0xa0000000 + sa;	/* pointer to start of claim frame */

    /* make beacon frame */
    sa = bp - buffer_mem;
    *bp++ = 0xd8000011;		/* beacon frame descr. + length */
    *bp++ = 0xc2;		/* FC value for beacon frame, long addr */
    *bp++ = 0;			/* DA = 0 */
    *bp++ = (mip->l_address[0] << 8) + mip->l_address[1];
    *bp++ = (mip->l_address[2] << 24) + (mip->l_address[3] << 16)
	+ (mip->l_address[4] << 8) + mip->l_address[5];
    *bp++ = 0;			/* beacon reason = failed claim */
    mac->sabc = bp - buffer_mem;
    *bp++ = 0xa0000000 + sa;	/* pointer to start of beacon frame */
}

void mac_reset(LoopbackType loopback)
{
    int mode;
    struct formac_state *msp = &this_mac_state;

    msp->loopback = loopback;
    switch (loopback) {
    case loop_none:
	mode = M1_MODE_ONLINE;
	break;
    case loop_formac:
	mode = M1_MODE_INT_LOOP;
	break;
    default:
	mode = M1_MODE_EXT_LOOP;
	break;
    }
    mac->mdreg1 = mode | M1_ADDET_NSA | M1_SELECT_RA | M1_FULL_DUPLEX;
    mac->cmdreg1 = C1_IDLE_LISTEN;
    mac->cmdreg1 = C1_CLR_ALL_LOCKS;
    mac->imsk1u = ~(S1_XMIT_ABORT | S1_END_FRAME_ASYNC0) >> 16;
    mac->imsk1l = ~(S1_PAR_ERROR_ASYNC0 | S1_QUEUE_LOCK_ASYNC0);
    mac->imsk2u = ~(S2_RECV_COMPLETE | S2_RECV_BUF_FULL | S2_RECV_FIFO_OVF
		    | S2_ERR_SPECIAL_FR | S2_RMT_EVENTS
		    | S2_NP_SIMULT_LOAD) >> 16;
    mac->imsk2l = ~(S2_RMT_EVENTS | S2_MISSED_FRAME);
}

void mac_claim(void)
{
    mac->cmdreg1 = C1_CLAIM_LISTEN;
}

void mac_disable(void)
{
    mac->mdreg1 = M1_MODE_MEMORY;
    mac->imsk1u = ~0;
    mac->imsk1l = ~0;
    mac->imsk2u = ~0;
    mac->imsk2l = ~0;
    mac->wpr = mac->swpr + 1;
    if (mac->wpr > mac->earv)
	mac->wpr = mac->eacb + 1;
    buffer_mem[mac->swpr] = 0;
}

void mac_stats(void)
{
    struct formac_state *msp = &this_mac_state;

    if (msp->recv_ovf)
	printk("%d receive buffer overflows\n", msp->recv_ovf);
    if (msp->wrong_bb)
	printk("%d frames on wrong byte bdry\n", msp->wrong_bb);
    printk("%d frames transmitted, %d aborted\n", msp->frames_xmitted,
	   msp->xmit_aborted);
    printk("%d frames received, %d aborted\n", msp->frames_recvd,
	   msp->recv_aborted);
    printk("%d frames received with errors\n", msp->recv_error);
}

void mac_sleep(void)
{
    /* disable the receiver */
    mac->mdreg1 = (mac->mdreg1 & ~M1_ADDET) | M1_ADDET_DISABLE_RECV;
}

void mac_poll(void)
{
    mac_status_t st1, st2;
    struct formac_state *msp = &this_mac_state;
    int up, f, d, l, r, e, i;

    st1 = (mac->st1u << 16) + mac->st1l;
    st2 = (mac->st2u << 16) + mac->st2l;

    if (st2 & S2_NP_SIMULT_LOAD)
	panic("NP/formac simultaneous load!!!");

    up = (st2 & S2_RING_OP) != 0;
    if (up != msp->ring_op) {
	/* ring has come up or down */
	msp->ring_op = up;
	printk("mac: ring %s\n", up? "up": "down");
	set_ring_op(up);
    }

    if (up) {
	if (st1 & S1_XMIT_ABORT) {
	    ++msp->xmit_aborted;
	    if (st1 & S1_QUEUE_LOCK_ASYNC0) {
		printk("mac: xmit queue locked, resetting xmit buffer\n");
		mac->cmdreg2 = C2_RESET_XMITQS;	/* XXX bit gross */
		mac->rpxa0 = XMIT_BUF_START;
		buffer_mem[XMIT_BUF_START] = 0;
		msp->xmit_ptr = XMIT_BUF_START;
		msp->xmit_start = XMIT_BUF_START;
		msp->xmit_chains = 0;
		mac->cmdreg1 = C1_CLR_ASYNCQ0_LOCK;
		st1 &= ~(S1_END_CHAIN_ASYNC0 | S1_END_FRAME_ASYNC0
			 | S1_XINSTR_FULL_ASYNC0);
	    } else
		st1 |= S1_END_FRAME_ASYNC0;
	} else if (st1 & S1_QUEUE_LOCK_ASYNC0) {
	    printk("mac: xmit queue locked, why?\n");
	    mac->cmdreg1 = C1_CLR_ASYNCQ0_LOCK;
	}

	if (st1 & S1_END_FRAME_ASYNC0) {
	    /* advance xmit_start */
	    e = msp->xmit_start;
	    while (e != msp->xmit_ptr) {
		/* find the end of the current frame */
		f = buffer_mem[e]; /* read pointer */
		if (f == 0)
		    break;	/* huh?? */
		f &= 0xffff;
		d = buffer_mem[f]; /* read descriptor */
		l = ((d & 0xffff) + ((d >> TD_BYTE_BDRY_LG) & 3) + 3) >> 2;
		e = f + 1 + l;	/* index of ptr at end of frame */
		r = mac->rpxa0;
		if ((r <= msp->xmit_ptr && r < e && e <= msp->xmit_ptr)
		    || (r > msp->xmit_ptr && (r < e || e <= msp->xmit_ptr)))
		    break;	/* up to current frame */
		/* printk("frame @ %x done\n", msp->xmit_start); */
		msp->xmit_start = e;
		if ((st1 & S1_XMIT_ABORT) == 0)
		    ++msp->frames_xmitted;
		if ((msp->xmit_chains == 1 && e == msp->xmit_ptr) ||
		    (msp->xmit_chains > 1 && e == msp->xmit_chain_start[1])) {
		    /* we've finished chain 0 */
		    --msp->xmit_chains;
		    for (i = 0; i < msp->xmit_chains; ++i)
			msp->xmit_chain_start[i] = msp->xmit_chain_start[i+1];
		    if (msp->xmit_chains >= 2) {
			mac->cmdreg2 = C2_XMIT_ASYNCQ0;
			/* printk("mac_poll: xmit chain\n"); */
		    }
		    if (msp->xmit_chains == 0)
			*csr0 &= ~CS0_LED1;
		}
	    }
	    /*
	     * Now that we have a bit more space in the transmit buffer,
	     * see if we want to put another frame in.
	     */
#if MAC_DEBUG
	    printk("Removed space in transmit buffer.\n");
#endif
	    mac_process();
	}
    }

    if (st2 & S2_RMT_EVENTS) {
	rmt_event(st2);
    }

    if (st2 & S2_RECV_COMPLETE) {
	/*
	 * A frame has just finished arriving in the receive buffer.
	 */
	*csr0 |= CS0_LED2;
	msp->recv_empty = 0;
#if MAC_DEBUG
	printk("Frame has just trickled in...\n");
#endif
	mac_process();
    }

    if (st2 & S2_RECV_BUF_FULL) {
	/*
	 * receive buffer overflow: reset and unlock the receive buffer.
	 */
/*	printk("mac: receive buffer full\n"); */
	mac->rpr = RECV_BUF_START;
	mac->wpr = RECV_BUF_START + 1;
	mac->swpr = RECV_BUF_START;
	msp->recv_ptr = RECV_BUF_START;
	msp->recv_empty = 1;
	buffer_mem[RECV_BUF_START] = 0;
	mac->cmdreg1 = C1_CLR_RECVQ_LOCK;
	++msp->recv_ovf;

#if 0
    } else if (st2 & S2_RECV_FIFO_OVF) {
	printk("mac: receive FIFO overflow\n");
	/* any further action required here? */

    } else if (st2 & S2_MISSED_FRAME) {
	printk("mac: missed frame\n");
#endif
    }

    if (st2 & S2_ERR_SPECIAL_FR) {
	printk("mac: bug: error in special frame\n");
	mac_disable();
    }
}

void
mac_xmit_alloc(sp, bb)
    struct mac_buf *sp;
    int bb;
{
    int nwords;

    nwords = (sp->length + bb + 3) >> 2;
    sp->fr_start = mac_xalloc(nwords + 2);
    sp->fr_end = sp->fr_start + nwords + 1;
    sp->ptr = (char *) &buffer_mem[sp->fr_start + 1] + bb;
    buffer_mem[sp->fr_start] = TD_MAGIC + (bb << TD_BYTE_BDRY_LG) + sp->length;
}

void
mac_queue_frame(sp)
    struct mac_buf *sp;
{
    struct formac_state *msp = &this_mac_state;

    buffer_mem[sp->fr_end] = 0;	/* null pointer at end of frame */
    buffer_mem[msp->xmit_ptr] = PT_MAGIC + sp->fr_start;
    if (msp->xmit_chains <= 2) {
	msp->xmit_chain_start[msp->xmit_chains] = msp->xmit_ptr;
	if (msp->xmit_chains < 2)
	    mac->cmdreg2 = C2_XMIT_ASYNCQ0;
	++msp->xmit_chains;
    } else {
	buffer_mem[msp->xmit_more_ptr] |= TD_MORE;
    }
    msp->xmit_ptr = sp->fr_end;
    msp->xmit_more_ptr = sp->fr_start;
    *csr0 |= CS0_LED1;
}

int
mac_xalloc(int nwords)
{
    int fr_start;
    struct formac_state *msp = &this_mac_state;

    /*
     * Find some room in the transmit buffer.
     */
    fr_start = msp->xmit_free;
    if (fr_start > msp->xmit_start) {
	if (fr_start + nwords > XMIT_BUF_END) {
	    /* no space at end - see if we can start again from the front */
	    fr_start = XMIT_BUF_START;
	    if (fr_start + nwords > msp->xmit_start)
		panic("no space in xmit buffer (1)");
	}
    } else {
	if (fr_start + nwords > msp->xmit_start)
	    panic("no space in xmit buffer (2)");
    }

    msp->xmit_free = fr_start + nwords;

    return fr_start;
}

int
mac_recv_frame(sp)
    struct mac_buf *sp;
{
    struct formac_state *msp = &this_mac_state;
    int status, bb, orig_recv_ptr;

    orig_recv_ptr = msp->recv_ptr;
    for (;;) {
	status = buffer_mem[msp->recv_ptr];
	if ((status & RS_VALID) == 0) {
	    if (status != 0) {
		printk("recv buf out of sync: recv_ptr=%x status=%x\n",
		       msp->recv_ptr, status);
		printk(" rpr=%x swpr=%x, buf[rpr]=%x\n", mac->rpr, mac->swpr,
		       buffer_mem[mac->rpr]);
		msp->recv_ptr = mac->swpr;
	    }
	    *csr0 &= ~CS0_LED2;
	    msp->recv_empty = 1;
	    if (mac->rpr == orig_recv_ptr)
		mac->rpr = msp->recv_ptr;
	    return 0;
	}
	if (status & RS_ABORTED)
	    ++msp->recv_aborted;
	else {
	    bb = (status >> RS_BYTE_BDRY_LG) & 3;
	    if (bb != 3) {
		++msp->wrong_bb;
		bb = 3;
	    }
	    if ((status & RS_ERROR) == 0)
		break;
	    ++msp->recv_error;
	    msp->recv_ptr += NWORDS((status & RS_LENGTH) + bb);
	}
	if (++msp->recv_ptr >= RECV_BUF_END)
	    msp->recv_ptr -= RECV_BUF_SIZE;
    }
    ++msp->frames_recvd;
    if (mac->rpr == orig_recv_ptr)
	mac->rpr = msp->recv_ptr;

    sp->fr_start = msp->recv_ptr;
    sp->length = (status & RS_LENGTH) + bb;	/* + 4 (status) - 4 (FCS) */
    sp->ptr = (void *) &buffer_mem[sp->fr_start];
    if ((msp->recv_ptr += NWORDS(sp->length) + 1) >= RECV_BUF_END)
	msp->recv_ptr -= RECV_BUF_SIZE;
    sp->fr_end = msp->recv_ptr;
    sp->wraplen = (RECV_BUF_END - sp->fr_start) * 4;
    sp->wrapptr = (void *) &buffer_mem[RECV_BUF_START];

    return 1;
}

void
mac_discard_frame(sp)
    struct mac_buf *sp;
{
    mac->rpr = sp->fr_end;
}

/*
 * Return the number of bytes free in the async 0 transmit queue.
 */
int
mac_xmit_space(void)
{
    struct formac_state *msp = &this_mac_state;
    int nw;

    if (msp->xmit_free > msp->xmit_start) {
	nw = XMIT_BUF_END - msp->xmit_free;
	if (nw < msp->xmit_start - XMIT_BUF_START)
	    nw = msp->xmit_start - XMIT_BUF_START;
    } else
	nw = msp->xmit_start - msp->xmit_free;
    return nw <= 2? 0: (nw - 2) << 2;
}

/*
 * Return the number of bytes of frames available in the receive queue.
 */
int
mac_recv_level(void)
{
    int nw;

    nw = mac->swpr - mac->rpr;
    if (nw < 0)
	nw += mac->earv - mac->eacb;
    return nw << 2;
}

/*
 * Return 1 iff all transmission has been completed, 0 otherwise.
 */
int mac_xmit_done(void)
{
    struct formac_state *msp = &this_mac_state;

    return msp->xmit_chains == 0;
}

/*
 * Append skbuff packet to queue.
 */
int mac_queue_append (struct sk_buff *skb)
{
    struct mac_queue *el;
    unsigned flags;
    save_flags(flags); cli();

#if MAC_DEBUG
    printk("Appending queue element skb 0x%x\n", skb);
#endif

    if ((el = (struct mac_queue *)kmalloc(sizeof(*el), GFP_ATOMIC)) == NULL) {
	restore_flags(flags);
	return 1;
    }
    el->next = NULL;
    el->skb = skb;
    
    if (mac_queue_top == NULL) {
	mac_queue_top = mac_queue_bottom = el;
    }
    else {
	mac_queue_bottom->next = el;
	mac_queue_bottom = el;
    }
    restore_flags(flags);
    return 0;
}

/*
 * If the packet originated from the same FDDI subnet as we are on,
 * there is no need to perform checksumming as FDDI will does this
 * us.  
 */
#define CHECK_IF_CHECKSUM_REQUIRED(skb) \
    if ((skb)->protocol == ETH_P_IP) { \
	extern struct cap_init cap_init; \
	int *from_ip = (int *)((skb)->data+12); \
	int *to_ip = (int *)((skb)->data+16); \
	if ((*from_ip & cap_init.netmask) == (*to_ip & cap_init.netmask)) \
	    (skb)->ip_summed = CHECKSUM_UNNECESSARY; \
    }

/*
 * Try to send and/or recv frames.
 */
void mac_process(void)
{
    volatile struct dma_chan *dma = (volatile struct dma_chan *) DMA3;
    struct formac_state *msp = &this_mac_state;
    struct mac_queue *el;
    int nw=0, mrl = 0, fstart, send_buffer_full = 0;
    unsigned flags;

    save_flags(flags); cli();

#if MAC_DEBUG
    printk("In mac_process()\n");
#endif

    /*
     * Check if the DMA is being used.
     */
    if (msp->dma_state != IDLE) {
	restore_flags(flags);
	return;
    }

    while (mac_queue_top != NULL  || /* Something to transmit */
	   (mrl = mac_recv_level()) > 0) {  /* Frames in receive buffer */
	send_buffer_full = 0;
#if MAC_DEBUG
	printk("mac_process(): something to do... mqt %x mrl is %d\n", 
	       mac_queue_top, mrl);
#endif
	if (mac_queue_top != NULL && mrl < RECV_THRESHOLD) {
	    el = (struct mac_queue *)mac_queue_top;

	    /*
	     * Check there is enough space in the FDDI send buffer.
	     */
	    if (mac_xmit_space() < el->skb->len) {
#if MAC_DEBUG
		printk("process_queue(): FDDI send buffer is full\n");
#endif
		send_buffer_full = 1;
	    }
	    else {
#if MAC_DEBUG
		printk("mac_process(): sending a frame\n");
#endif
		/*
		 * Update mac_queue_top.
		 */
		mac_queue_top = mac_queue_top->next;

		/*
		 * Allocate space in the FDDI send buffer.
		 */
		msp->cur_mbuf.length = el->skb->len-3;
		mac_xmit_alloc((struct mac_buf *)&msp->cur_mbuf, 3);

		/*
		 * If message size is greater than DMA_XMIT_THRESHOLD, send
		 * using DMA, otherwise use memcpy().
		 */
		if (el->skb->len > DMA_XMIT_THRESHOLD) {
		    /*
		     * Start the DMA.
		     */
#if MAC_DEBUG
		    printk("mac_process(): Starting send DMA...\n");
#endif
		    nw = msp->cur_mbuf.fr_end - msp->cur_mbuf.fr_start + 1;
		    mac->wpxa0 = msp->cur_mbuf.fr_start + 1;
		    
		    *csr0 |= CS0_HREQ_WA0;
		    
		    msp->cur_macq = el;
		    msp->dma_state = XMITTING;
		    dma->st = DMA_DMST_RST;
		    dma->st = DMA_RESET_MASKS;
		    dma->hskip = 1;		/* skip = 0, count = 1 */
		    dma->vskip = 1;		/* skip = 0, count = 1 */
		    dma->maddr = (u_char *)
			mmu_v2p((unsigned long)el->skb->data);
		    dma->cmd = DMA_DCMD_ST + DMA_DCMD_TYP_AUTO + 
			DMA_DCMD_TD_MD + nw;
		    *csr0 &= ~CS0_DMA_RECV;
		    *csr0 |= CS0_DMA_ENABLE;

		    /*
		     * Don't process any more packets since the DMA is 
		     * being used.
		     */
		    break;
		}
		else {   /* el->skb->len <= DMA_XMIT_THRESHOLD */
		    /*
		     * Copy the data directly into the FDDI buffer.
		     */
#if MAC_DEBUG
		    printk("mac_proces(): Copying send data...\n");
#endif
		    memcpy(msp->cur_mbuf.ptr - 3, el->skb->data, 
			   ROUND4(el->skb->len));
		    mac_queue_frame((struct mac_buf *)&msp->cur_mbuf);
		    dev_kfree_skb(el->skb);
		    kfree_s(el, sizeof(*el));
		    continue;
		}
	    }

	    /*
	     * We have reached here if there is not enough space in the
	     * send buffer.  Try to receive some packets instead.
	     */
	}

	if (mac_recv_frame((struct mac_buf *)&msp->cur_mbuf)) {
	    volatile int fc, llc_header_word2;
	    int pkt_len = 0;

#if MAC_DEBUG
	    printk("mac_process(): Receiving frames...\n");
#endif
	    /* 
	     * Get the fc, note only word accesses are allowed from the
	     * FDDI buffers.
	     */
	    if (msp->cur_mbuf.wraplen > 4) {
		fc = *(int *)(msp->cur_mbuf.ptr+4);
	    }
	    else {
		/*
		 * fc_word must be at the start of the FDDI buffer.
		 */
#if MAC_DEBUG
		printk("Grabbed fc_word from wrapptr, wraplen %d\n", 
		       msp->cur_mbuf.wraplen);
#endif
		fc = *(int *)msp->cur_mbuf.wrapptr;
	    }
	    fc &= 0xff;
	    
#if MAC_DEBUG
	    printk("fc is 0x%x\n", fc);
#endif
	    if (fc < 0x50 || fc > 0x57) {
		mac_discard_frame((struct mac_buf *)&msp->cur_mbuf);
		continue;
	    }

	    /*
	     * Determine the size of the packet data and allocate a socket
	     * buffer.
	     */
	    pkt_len = msp->cur_mbuf.length - FDDI_HARDHDR_LEN;
#if MAC_DEBUG
	    printk("Packet of length %d\n", pkt_len);
#endif
	    msp->cur_skb = dev_alloc_skb(ROUND4(pkt_len));
	    
	    if (msp->cur_skb == NULL) {
		printk("mac_process(): Memory squeeze, dropping packet.\n");
		apfddi_stats->rx_dropped++;
		restore_flags(flags);
		return;
	    }
	    msp->cur_skb->dev = apfddi_device;

	    /*
	     * Hardware header isn't copied to skbuff.
	     */
	    msp->cur_skb->mac.raw = msp->cur_skb->data;
	    apfddi_stats->rx_packets++;

	    /*
	     * Determine protocol from llc header.
	     */
	    if (msp->cur_mbuf.wraplen < FDDI_HARDHDR_LEN) {
		llc_header_word2 = *(int *)(msp->cur_mbuf.wrapptr + 
					    (FDDI_HARDHDR_LEN - 
					     msp->cur_mbuf.wraplen - 4));
	    }
	    else {
		llc_header_word2 = *(int *)(msp->cur_mbuf.ptr + 
					    FDDI_HARDHDR_LEN - 4);
	    }
	    msp->cur_skb->protocol = llc_header_word2 & 0xFFFF;
#if MAC_DEBUG
	    printk("Got protocol 0x%x\n", msp->cur_skb->protocol);
#endif
	    
	    /*
	     * Copy data into socket buffer, which may be wrapped around the 
	     * FDDI buffer.  Use memcpy if the size of the data is less
	     * than DMA_RECV_THRESHOLD.  Note if DMA is used, then wrap-
	     * arounds are handled automatically.
	     */
	    if (pkt_len < DMA_RECV_THRESHOLD) {
		if (msp->cur_mbuf.length < msp->cur_mbuf.wraplen) {
		    memcpy(skb_put(msp->cur_skb, ROUND4(pkt_len)), 
			   msp->cur_mbuf.ptr + FDDI_HARDHDR_LEN, 
			   ROUND4(pkt_len));
		} 
		else if (msp->cur_mbuf.wraplen < FDDI_HARDHDR_LEN) {
#if MAC_DEBUG
		    printk("Wrap case 2\n");
#endif
		    memcpy(skb_put(msp->cur_skb, ROUND4(pkt_len)), 
			   msp->cur_mbuf.wrapptr + 
			   (FDDI_HARDHDR_LEN - msp->cur_mbuf.wraplen),
			   ROUND4(pkt_len));
		} 
		else {
#if MAC_DEBUG
		    printk("wrap case 3\n");
#endif
		    memcpy(skb_put(msp->cur_skb, 
				   ROUND4(msp->cur_mbuf.wraplen-
					  FDDI_HARDHDR_LEN)),
			   msp->cur_mbuf.ptr + FDDI_HARDHDR_LEN, 
			   ROUND4(msp->cur_mbuf.wraplen - FDDI_HARDHDR_LEN));
		    memcpy(skb_put(msp->cur_skb, 
				   ROUND4(msp->cur_mbuf.length - 
					  msp->cur_mbuf.wraplen)),
			   msp->cur_mbuf.wrapptr, 
			   ROUND4(msp->cur_mbuf.length - 
				  msp->cur_mbuf.wraplen));
		}

#if MAC_DEBUG
		if (msp->cur_skb->protocol == ETH_P_IP) {
		    dump_packet("apfddi_rx:", msp->cur_skb->data, pkt_len, 0);
		}
		else if (msp->cur_skb->protocol == ETH_P_ARP) {
		    struct arphdr *arp = (struct arphdr *)msp->cur_skb->data;
		    printk("arp->ar_op is 0x%x ar_hrd %d ar_pro 0x%x ar_hln %d ar_ln %d\n", 
			   arp->ar_op, arp->ar_hrd, arp->ar_pro, arp->ar_hln, 
			   arp->ar_pln);
		    printk("sender hardware address: %x:%x:%x:%x:%x:%x\n",
			   *((u_char *)msp->cur_skb->data+8),
			   *((u_char *)msp->cur_skb->data+9),
			   *((u_char *)msp->cur_skb->data+10),
			   *((u_char *)msp->cur_skb->data+11),
			   *((u_char *)msp->cur_skb->data+12),
			   *((u_char *)msp->cur_skb->data+13));
		    printk("sender IP number %d.%d.%d.%d\n", 
			   *((u_char *)msp->cur_skb->data+14),
			   *((u_char *)msp->cur_skb->data+15),
			   *((u_char *)msp->cur_skb->data+16),
			   *((u_char *)msp->cur_skb->data+17));
		    printk("receiver hardware address: %x:%x:%x:%x:%x:%x\n",
			   *((u_char *)msp->cur_skb->data+18),
			   *((u_char *)msp->cur_skb->data+19),
			   *((u_char *)msp->cur_skb->data+20),
			   *((u_char *)msp->cur_skb->data+21),
			   *((u_char *)msp->cur_skb->data+22),
			   *((u_char *)msp->cur_skb->data+23));
		    printk("receiver IP number %d.%d.%d.%d\n", 
			   *((u_char *)msp->cur_skb->data+24),
			   *((u_char *)msp->cur_skb->data+25),
			   *((u_char *)msp->cur_skb->data+26),
			   *((u_char *)msp->cur_skb->data+27));
		}
#endif
		CHECK_IF_CHECKSUM_REQUIRED(msp->cur_skb);

		/*
		 * Inform the network layer of the new packet.
		 */
#if MAC_DEBUG
		printk("Calling netif_rx()\n");
#endif
		netif_rx(msp->cur_skb);

		/*
		 * Remove frame from FDDI buffer.
		 */
		mac_discard_frame((struct mac_buf *)&msp->cur_mbuf);
		continue;
	    }
	    else {
		/*
		 * Set up dma and break.
		 */
#if MAC_DEBUG
		printk("mac_process(): Starting receive DMA...\n");
#endif
		nw = NWORDS(pkt_len);
		msp->dma_state = RECVING;
		*csr0 &= ~(CS0_HREQ | CS0_DMA_ENABLE);
/*		*csr1 |= CS1_RESET_FIFO;
		*csr1 &= ~CS1_RESET_FIFO; */
		if ((*csr1 & CS1_FIFO_LEVEL) != 0) {
		    int x;
		    printk("fifo not empty! (csr1 = 0x%x) emptying...", *csr1);
		    do {
			x = *fifo;
		    } while ((*csr1 & CS1_FIFO_LEVEL) != 0);
		    printk("done\n");
		}
		fstart = msp->cur_mbuf.fr_start + NWORDS(FDDI_HARDHDR_LEN);
		if (fstart >= RECV_BUF_END)
		    fstart -= RECV_BUF_SIZE;
		mac->rpr = fstart;
#if MAC_DEBUG
		printk("rpr=0x%x, nw=0x%x, stat=0x%x\n",
		       mac->rpr, nw, buffer_mem[msp->cur_mbuf.fr_start]);
#endif
		dma->st = DMA_DMST_RST;
		dma->st = DMA_RESET_MASKS;
		dma->hskip = 1;         /* skip = 0, count = 1 */
		dma->vskip = 1;         /* skip = 0, count = 1 */
		dma->maddr = (u_char *)
		    mmu_v2p((unsigned long)
			    skb_put(msp->cur_skb, ROUND4(pkt_len)));
		dma->cmd = DMA_DCMD_ST + DMA_DCMD_TYP_AUTO + DMA_DCMD_TD_DM
		    + nw - 4;
		*csr0 |= CS0_HREQ_RECV | CS0_DMA_RECV;
		*csr0 |= CS0_DMA_ENABLE;
#if MAC_DEBUG
		printk("mac_process(): DMA is away!\n");
#endif
		break;
	    }
	}
	else {
#if MAC_DEBUG
	    printk("mac_recv_frame failed\n");
#endif
	    if (msp->recv_empty && send_buffer_full)
		break;
	}
    }
    /*
     * Update mac_queue_bottom.
     */
    if (mac_queue_top == NULL)
	mac_queue_bottom = NULL;

#if MAC_DEBUG
    printk("End of mac_process()\n");
#endif
    restore_flags(flags);
}
    

#define DMA_IN(reg) (*(volatile unsigned *)(reg))
#define DMA_OUT(reg,v) (*(volatile unsigned *)(reg) = (v))

/*
 * DMA completion handler.
 */
void mac_dma_complete(void)
{
    volatile struct dma_chan *dma;
    struct formac_state *msp = &this_mac_state;
    unsigned a;

    a = DMA_IN(DMA3_DMST);
    if (!(a & DMA_INTR_REQS)) {
	if (msp->dma_state != IDLE && (a & DMA_DMST_AC) == 0) {
	    printk("dma completed but no interrupt!\n");
	    msp->dma_state = IDLE;
	}
	return;
    }

    DMA_OUT(DMA3_DMST,AP_CLR_INTR_REQ<<DMA_INTR_NORMAL_SH);
    DMA_OUT(DMA3_DMST,AP_CLR_INTR_REQ<<DMA_INTR_ERROR_SH);

    dma = (volatile struct dma_chan *) DMA3;

#if MAC_DEBUG
    printk("In mac_dma_complete\n");
#endif

    if (msp->dma_state == XMITTING && ((dma->st & DMA_DMST_AC) == 0)) {
	/*
	 * Transmit DMA finished.
	 */
	int i = 20;
#if MAC_DEBUG
	printk("In mac_dma_complete for transmit complete\n");
#endif
	while (*csr1 & CS1_FIFO_LEVEL) {
	    if (--i <= 0) {
		printk("csr0=0x%x csr1=0x%x: fifo not emptying\n", *csr0,
		       *csr1);
		return;
	    }
	}
	*csr0 &= ~(CS0_HREQ | CS0_DMA_ENABLE);
	msp->dma_state = IDLE;
#if MAC_DEBUG
	printk("mac_dma_complete(): Calling mac_queue_frame\n");
#endif
	mac_queue_frame((struct mac_buf *)&msp->cur_mbuf);
	dev_kfree_skb(msp->cur_macq->skb);
	kfree_s((struct mac_buf *)msp->cur_macq, sizeof(*(msp->cur_macq)));
	msp->cur_macq = NULL;
#if MAC_DEBUG
	printk("mac_dma_complete(): Calling mac_process()\n");
#endif
	mac_process();
#if MAC_DEBUG
	printk("End of mac_dma_complete transmitting\n");
#endif
    }
    else if (msp->dma_state == RECVING && ((dma->st & DMA_DMST_AC) == 0)) {
	/*
	 * Receive DMA finished.  Copy the last four words from the
	 * fifo into the buffer, after turning off the host requests.
	 * We do this to avoid reading past the end of frame.
	 */
	int *ip, i;

#if MAC_DEBUG
	printk("In mac_dma_complete for receive complete\n");
#endif
	msp->dma_state = IDLE;
	ip = (int *)mmu_p2v((unsigned long)dma->cmaddr);

#if MAC_DEBUG
	printk("ip is 0x%x, skb->data is 0x%x\n", ip, msp->cur_skb->data);
#endif

	*csr0 &= ~(CS0_DMA_ENABLE | CS0_HREQ);

	for (i = 0; (*csr1 & CS1_FIFO_LEVEL); ++i)
	    ip[i] = *fifo;
	if (i != 4)
	    printk("mac_dma_complete(): not four words remaining in fifo?\n");
#if MAC_DEBUG
	printk("Copied last four words out of fifo\n");
#endif
	
	/*
	 * Remove the frame from the FDDI receive buffer.
	 */
	mac_discard_frame((struct mac_buf *)&msp->cur_mbuf);

	CHECK_IF_CHECKSUM_REQUIRED(msp->cur_skb);

	/*
	 * Now inject the packet into the network system.
	 */
	netif_rx(msp->cur_skb);

#if MAC_DEBUG
	dump_packet("mac_dma_complete:", msp->cur_skb->data, 0, 0);
#endif

	/*
	 * Check if any more frames can be processed.
	 */
	mac_process();

#if MAC_DEBUG
	printk("End of mac_dma_complete receiving\n");
#endif
    }
#if MAC_DEBUG
    printk("End of mac_dma_complete()\n");
#endif
}
    
static void mac_print_state(void)
{
    struct formac_state *msp = &this_mac_state;

    printk("DMA3_DMST is 0x%x dma_state is %d\n", DMA_IN(DMA3_DMST),
	   msp->dma_state);
    printk("csr0 = 0x%x, csr1 = 0x%x\n", *csr0, *csr1);
}


