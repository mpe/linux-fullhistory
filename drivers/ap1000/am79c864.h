  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/*
 * Definitions for Am79c864 PLC (Physical Layer Controller)
 */

typedef int	plc_reg;

struct plc {
    plc_reg	ctrl_a;
    plc_reg	ctrl_b;
    plc_reg	intr_mask;
    plc_reg	xmit_vector;
    plc_reg	vec_length;
    plc_reg	le_threshold;
    plc_reg	c_min;
    plc_reg	tl_min;
    plc_reg	tb_min;
    plc_reg	t_out;
    plc_reg	dummy1;
    plc_reg	lc_length;
    plc_reg	t_scrub;
    plc_reg	ns_max;
    plc_reg	tpc_load;
    plc_reg	tne_load;
    plc_reg	status_a;
    plc_reg	status_b;
    plc_reg	tpc;
    plc_reg	tne;
    plc_reg	clk_div;
    plc_reg	bist_sig;
    plc_reg	rcv_vector;
    plc_reg	intr_event;
    plc_reg	viol_sym_ct;
    plc_reg	min_idle_ct;
    plc_reg	link_err_ct;
};

/* Bits in ctrl_a */
#define CA_NOISE_TIMER		0x4000
#define CA_TNE_16BIT		0x2000
#define CA_TPC_16BIT		0x1000
#define CA_REQ_SCRUB		0x0800
#define CA_VSYM_INTR_MODE	0x0200
#define CA_MINI_INTR_MODE	0x0100
#define CA_LOOPBACK		0x0080
#define CA_FOT_OFF		0x0040
#define CA_EB_LOOP		0x0020
#define CA_LM_LOOP		0x0010
#define CA_BYPASS		0x0008
#define CA_REM_LOOP		0x0004
#define CA_RF_DISABLE		0x0002
#define CA_RUN_BIST		0x0001

/* Bits in ctrl_b */
#define CB_CONFIG_CTRL		0x8000
#define CB_MATCH_LS		0x7800
#define CB_MATCH_LS_ANY		0x0000
#define CB_MATCH_LS_QLS		0x4000
#define CB_MATCH_LS_MLS		0x2000
#define CB_MATCH_LS_HLS		0x1000
#define CB_MATCH_LS_ILS		0x0800
#define CB_MAINT_LS		0x0700
#define CB_MAINT_LS_QLS		0x0000
#define CB_MAINT_LS_ILS		0x0100
#define CB_MAINT_LS_HLS		0x0200
#define CB_MAINT_LS_MLS		0x0300
#define CB_MAINT_LS_PDR		0x0600
#define CB_CLASS_S		0x0080
#define CB_PC_LCT		0x0060
#define CB_PC_LCT_NONE		0x0000
#define CB_PC_LCT_PDR		0x0020
#define CB_PC_LCT_IDLE		0x0040
#define CB_PC_LCT_LOOP		0x0060
#define CB_PC_JOIN		0x0010
#define CB_LONG_LCT		0x0008
#define CB_PC_MAINT		0x0004
#define CB_PCM_CTRL		0x0003
#define CB_PC_START		0x0001
#define CB_PC_TRACE		0x0002
#define CB_PC_STOP		0x0003

/* Bits in status_a */
#define SA_SIG_DETECT		0x0400
#define SA_PREV_LS		0x0300
#define SA_PREV_LS_QLS		0x0000
#define SA_PREV_LS_MLS		0x0100
#define SA_PREV_LS_HLS		0x0200
#define SA_PREV_LS_ILS		0x0300
#define SA_LINE_ST		0x00e0
#define SA_LINE_ST_NLS		0x0000
#define SA_LINE_ST_ALS		0x0020
#define SA_LINE_ST_ILS4		0x0060
#define SA_LINE_ST_QLS		0x0080
#define SA_LINE_ST_MLS		0x00a0
#define SA_LINE_ST_HLS		0x00c0
#define SA_LINE_ST_ILS		0x00e0
#define SA_LSM_STATE		0x0010
#define SA_UNKN_LINE_ST		0x0008
#define SA_SYM_PAIR_CTR		0x0007

/* Bits in status_b */
#define SB_RF_STATE		0xc000
#define SB_RF_STATE_REPEAT	0x0000
#define SB_RF_STATE_IDLE	0x4000
#define SB_RF_STATE_HALT1	0x8000
#define SB_RF_STATE_HALT2	0xc000
#define SB_PCI_STATE		0x3000
#define SB_PCI_STATE_REMOVED	0x0000
#define SB_PCI_STATE_INS_SCR	0x1000
#define SB_PCI_STATE_REM_SCR	0x2000
#define SB_PCI_STATE_INSERTED	0x3000
#define SB_PCI_SCRUB		0x0800
#define SB_PCM_STATE		0x0780
#define SB_PCM_STATE_OFF	0x0000
#define SB_PCM_STATE_BREAK	0x0080
#define SB_PCM_STATE_TRACE	0x0100
#define SB_PCM_STATE_CONNECT	0x0180
#define SB_PCM_STATE_NEXT	0x0200
#define SB_PCM_STATE_SIGNAL	0x0280
#define SB_PCM_STATE_JOIN	0x0300
#define SB_PCM_STATE_VERIFY	0x0380
#define SB_PCM_STATE_ACTIVE	0x0400
#define SB_PCM_STATE_MAIN	0x0480
#define SB_PCM_SIGNALING	0x0040
#define SB_LSF			0x0020
#define SB_RCF			0x0010
#define SB_TCF			0x0008
#define SB_BREAK_REASON		0x0007
#define SB_BREAK_REASON_NONE	0x0000
#define SB_BREAK_REASON_START	0x0001
#define SB_BREAK_REASON_T_OUT	0x0002
#define SB_BREAK_REASON_NS_MAX	0x0003
#define SB_BREAK_REASON_QLS	0x0004
#define SB_BREAK_REASON_ILS	0x0005
#define SB_BREAK_REASON_HLS	0x0006

/* Bits in intr_event and intr_mask */
#define IE_NP_ERROR		0x8000
#define IE_SIGNAL_OFF		0x4000
#define IE_LE_CTR		0x2000
#define IE_MINI_CTR		0x1000
#define IE_VSYM_CTR		0x0800
#define IE_PHY_INVALID		0x0400
#define IE_EBUF_ERR		0x0200
#define IE_TNE_EXP		0x0100
#define IE_TPC_EXP		0x0080
#define IE_PCM_ENABLED		0x0040
#define IE_PCM_BREAK		0x0020
#define IE_SELF_TEST		0x0010
#define IE_TRACE_PROP		0x0008
#define IE_PCM_CODE		0x0004
#define IE_LS_MATCH		0x0002
#define IE_PARITY_ERR		0x0001

/* Correct value for BIST signature */
#define BIST_CORRECT		0x6ecd
