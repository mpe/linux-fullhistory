  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/*
 * Routines for controlling the Am79c864 physical layer controller.
 *
 * This chip implements some parts of the FDDI SMT standard
 * (PCM: physical connection management, LEM: link error monitor, etc.)
 * as well as the FDDI PHY standard.
 */
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include "apfddi.h"
#include "smt-types.h"
#include "am79c864.h"
#include "plc.h"
#include "apfddi-reg.h"

typedef enum {
    off,
    signalling,
    doing_lct,
    joining,
    active
} PlcPhase;
    
struct plc_state {
    LoopbackType	loopback;
    char		t_val[16];
    char		r_val[16];
    int			n;
    PortType		peer_type;
    PlcPhase		phase;
};

struct plc_info *this_plc_info;
struct plc_state this_plc_state;

void plc_init(struct plc_info *pip)
{
    int class, x;
    struct plc_state *psp = &this_plc_state;

    this_plc_info = pip;

    /* first turn it off, clear registers */
    class = pip->port_type == pt_s? CB_CLASS_S: 0;
    plc->ctrl_b = CB_PC_STOP + class;
    plc->intr_mask = IE_NP_ERROR;
    x = plc->intr_event;	/* these register clear when read */
    x = plc->viol_sym_ct;
    x = plc->min_idle_ct;
    x = plc->link_err_ct;

    /* initialize registers */
    plc->ctrl_a = 0;
    plc->ctrl_b = class;
    plc->c_min = pip->c_min >> 8;
    plc->tl_min = pip->tl_min >> 8;
    plc->tb_min = pip->tb_min >> 8;
    plc->t_out = pip->t_out >> 8;
    plc->t_scrub = pip->t_scrub >> 8;
    plc->ns_max = pip->ns_max >> 2;

    psp->phase = off;
}

int
plc_inited(struct plc_info *pip)
{
    int class, x;
    struct plc_state *psp = &this_plc_state;

    class = pip->port_type == pt_s? CB_CLASS_S: 0;
    if ((plc->ctrl_a & (CA_LOOPBACK|CA_FOT_OFF|CA_EB_LOOP|CA_LM_LOOP)) != 0)
	return 1;
    if ((plc->ctrl_b & (CB_CONFIG_CTRL|CB_CLASS_S|CB_PC_MAINT)) != class)
	return 2;
    if (plc->status_a & SA_SIG_DETECT)
	return 3;
    if ((plc->status_b & (SB_PCI_STATE|SB_PCM_STATE))
	 != (SB_PCI_STATE_INSERTED|SB_PCM_STATE_ACTIVE))
	return 4;

    /* all seems OK, reset the timers and counters just to be sure */
    plc->intr_mask = IE_NP_ERROR;
    x = plc->intr_event;	/* these register clear when read */
    x = plc->viol_sym_ct;
    x = plc->min_idle_ct;
    x = plc->link_err_ct;

    plc->c_min = pip->c_min >> 8;
    plc->tl_min = pip->tl_min >> 8;
    plc->tb_min = pip->tb_min >> 8;
    plc->t_out = pip->t_out >> 8;
    plc->t_scrub = pip->t_scrub >> 8;
    plc->ns_max = pip->ns_max >> 2;

    psp->phase = active;
    /* XXX should initialize other fields of this_plc_state */

    return 0;
}

void plc_sleep(void)
{
}

void pc_start(LoopbackType loopback)
{
    int x;
    struct plc_info *pip = this_plc_info;
    struct plc_state *psp = &this_plc_state;

    /* make sure it's off */
    plc->ctrl_b &= ~CB_PCM_CTRL;
    plc->ctrl_b |= CB_PC_STOP;

    /* set up loopback required */
    psp->loopback = loopback;
    x = 0;
    switch (loopback) {
    case loop_plc_lm:
	x = CA_LM_LOOP;
	break;
    case loop_plc_eb:
	x = CA_EB_LOOP;
	break;
    case loop_pdx:
	x = CA_LOOPBACK;
	break;
    default:
	x = 0;
    }
    plc->ctrl_a = x;

    /* set up bits to be exchanged */
    psp->t_val[0] = 0;
    psp->t_val[1] = ((int) pip->port_type >> 1) & 1;
    psp->t_val[2] = (int) pip->port_type & 1;
    psp->t_val[4] = 0;		/* XXX assume we want short LCT */
    psp->t_val[5] = 0;
    psp->t_val[6] = 0;		/* XXX too lazy to fire up my MAC for LCT */
    psp->t_val[8] = 0;		/* XXX don't wanna local loop */
    psp->t_val[9] = 1;		/* gotta MAC on port output */

    pc_restart();
}

void pc_restart(void)
{
    struct plc_state *psp = &this_plc_state;

    if (psp->phase != off)
	printk("restarting pcm\n");
    if (psp->phase == active)
	set_cf_join(0);		/* we're down :-( */

    psp->n = 0;
    plc->vec_length = 3 - 1;
    plc->xmit_vector = psp->t_val[0] + (psp->t_val[1] << 1)
	+ (psp->t_val[2] << 2);

    plc->intr_mask = IE_NP_ERROR | IE_PCM_BREAK | IE_PCM_CODE;
    plc->ctrl_b &= ~CB_PCM_CTRL;
    plc->ctrl_b |= CB_PC_START;	/* light blue paper and stand clear */

    psp->phase = signalling;
}

void pc_stop(void)
{
    struct plc_state *psp = &this_plc_state;

    if (psp->phase == active)
	set_cf_join(0);
    plc->ctrl_b &= ~CB_PCM_CTRL;
    plc->ctrl_b |= CB_PC_STOP;
    plc->intr_mask = IE_NP_ERROR;
    psp->phase = off;
}

void plc_poll(void)
{
    struct plc_state *psp = &this_plc_state;
    int events, i;

    if ((*csr0 & CS0_PHY_IRQ) == 0)
	return;
    events = plc->intr_event & plc->intr_mask;
    if (events & IE_NP_ERROR) {
	printk("plc: NP error!\n");
    }
    if (events & IE_PCM_BREAK) {
	i = plc->status_b & SB_BREAK_REASON;
	if (i > SB_BREAK_REASON_START) {
	    if (psp->phase == signalling || psp->phase == doing_lct)
		pcm_dump_rtcodes();
	    printk("pcm: break reason %d\n", i);
	    if (psp->phase != off)
		pc_restart();
	    /* XXX need to check for trace? */
	}
    }
    if (events & IE_PCM_CODE) {
	if (psp->phase == signalling)
	    pcm_pseudo_code();
	else if (psp->phase == doing_lct)
	    pcm_lct_done();
	else
	    printk("XXX pcm_code interrupt in phase %d?\n", psp->phase);
    }
    if (events & IE_PCM_ENABLED) {
	if (psp->phase == joining)
	    pcm_enabled();
	else
	    printk("XXX pcm_enabled interrupt in phase %d?\n", psp->phase);
    }
    if (events & IE_TRACE_PROP) {
	if (psp->phase == active)
	    pcm_trace_prop();
	else
	    printk("XXX trace_prop interrupt in phase %d\n", psp->phase);
    }
}

void pcm_pseudo_code(void)
{
    struct plc_info *pip = this_plc_info;
    struct plc_state *psp = &this_plc_state;
    int i, nb, lct, hislct;

    /* unpack the bits from the peer */
    nb = plc->vec_length + 1;
    i = plc->rcv_vector;
    do {
	psp->r_val[psp->n++] = i & 1;
	i >>= 1;
    } while (--nb > 0);

    /* send some more, do LCT, whatever */
    switch (psp->n) {
    case 3:
	/*
	 * Got escape flag, port type; send compatibility,
	 * LCT duration, MAC for LCT flag.
	 */
	if (psp->r_val[0]) {
	    /* help! what do I do now? */
	    pcm_dump_rtcodes();
	    pc_restart();
	    break;
	}
	psp->peer_type = (PortType) ((psp->r_val[1] << 1) + psp->r_val[2]);
	/* XXX we're type S, we talk to anybody */
	psp->t_val[3] = 1;

	plc->vec_length = 4 - 1;
	plc->xmit_vector = psp->t_val[3] + (psp->t_val[4] << 1)
	    + (psp->t_val[5] << 2) + (psp->t_val[6] << 3);
	break;

    case 7:
	/*
	 * Got compatibility, LCT duration, MAC for LCT flag;
	 * time to do the LCT.
	 */
	lct = (psp->t_val[4] << 1) + psp->t_val[5];
	hislct = (psp->r_val[4] << 1) + psp->r_val[5];
	if (hislct > lct)
	    lct = hislct;

	/* set LCT duration */
	switch (lct) {
	case 0:
	    plc->lc_length = pip->lc_short >> 8;
	    plc->ctrl_b &= ~CB_LONG_LCT;
	    break;
	case 1:
	    plc->lc_length = pip->lc_medium >> 8;
	    plc->ctrl_b &= ~CB_LONG_LCT;
	    break;
	case 2:
	    plc->ctrl_b |= CB_LONG_LCT;
	    /* XXX set up a timeout for pip->lc_long */
	    break;
	case 3:
	    plc->ctrl_b |= CB_LONG_LCT;
	    /* XXX set up a timeout for pip->lc_extended */
	    break;
	}

	/* start the LCT */
	i = plc->link_err_ct;	/* clear the register */
	plc->ctrl_b &= ~CB_PC_LCT;
	/* XXX assume we're not using the MAC for LCT;
	   if he's got a MAC, loop his stuff back, otherwise send idle. */
	if (psp->r_val[6])
	    plc->ctrl_b |= CB_PC_LCT_LOOP;
	else
	    plc->ctrl_b |= CB_PC_LCT_IDLE;
	psp->phase = doing_lct;
	break;

    case 8:
	/*
	 * Got LCT result, send MAC for local loop and MAC on port
	 * output flags.
	 */
	if (psp->t_val[7] || psp->r_val[7]) {
	    printk("LCT failed, restarting.\n");
	    /* LCT failed - do at least a medium length test next time. */
	    if (psp->t_val[4] == 0 && psp->t_val[5] == 0)
		psp->t_val[5] = 1;
	    pcm_dump_rtcodes();
	    pc_restart();
	    break;
	}
	plc->vec_length = 2 - 1;
	plc->xmit_vector = psp->t_val[8] + (psp->t_val[9] << 1);
	break;

    case 10:
	/*
	 * Got MAC for local loop and MAC on port output flags.
	 * Let's join.
	 */
	plc->intr_mask = IE_NP_ERROR | IE_PCM_BREAK | IE_PCM_ENABLED;
	plc->ctrl_b |= CB_PC_JOIN;
	psp->phase = joining;
	/* printk("pcm: joining\n"); */
	break;

    default:
	printk("pcm_pseudo_code bug: n = %d\n", psp->n);
    }
}

void pcm_lct_done(void)
{
    struct plc_state *psp = &this_plc_state;
    int i;

    i = plc->link_err_ct;
    psp->t_val[7] = i > 0;
    printk("pcm: lct %s (%d errors)\n", psp->t_val[7]? "failed": "passed", i);
    plc->ctrl_b &= ~(CB_PC_LCT | CB_LONG_LCT);
    plc->vec_length = 1 - 1;
    plc->xmit_vector = psp->t_val[7];
    psp->phase = signalling;
}

void pcm_dump_rtcodes(void)
{
    struct plc_state *psp = &this_plc_state;
    int i;

    if (psp->n > 0) {
	printk("pcm signalling interrupted after %d bits:\nt_val:", psp->n);
	for (i = 0; i < psp->n; ++i)
	    printk(" %d", psp->t_val[i]);
	printk("\nr_val:");
	for (i = 0; i < psp->n; ++i)
	    printk(" %d", psp->r_val[i]);
	printk("\n");
    }
}

void pcm_enabled(void)
{
    struct plc_state *psp = &this_plc_state;
    int i;

    printk("pcm: enabled\n");
    psp->phase = active;
    i = plc->link_err_ct;	/* clear the register */
    /* XXX should set up LEM here */
    /* XXX do we want to count violation symbols, minimum idle gaps,
       or elasticity buffer errors? */
    plc->intr_mask = IE_NP_ERROR | IE_PCM_BREAK | IE_TRACE_PROP;
    set_cf_join(1);		/* we're up :-) */
}

void pcm_trace_prop(void)
{
    /* XXX help! what do I do now? */
    pc_stop();
}
