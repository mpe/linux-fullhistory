  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/*
 * Definitions for PLC state structures etc.
 */

struct plc_info {
    PortType		port_type;
    TimerTwosComplement	c_min;
    TimerTwosComplement	tl_min;
    TimerTwosComplement	tb_min;
    TimerTwosComplement	t_out;
    TimerTwosComplement	lc_short;
    TimerTwosComplement	lc_medium;
    TimerTwosComplement	lc_long;
    TimerTwosComplement	lc_extended;
    TimerTwosComplement	t_scrub;
    TimerTwosComplement	ns_max;
    Counter		link_errors;
    Counter		viol_syms;
    Counter		mini_occur;
    int			min_idle_gap;
    double		link_error_rate;
};

void plc_init(struct plc_info *pip);
int plc_inited(struct plc_info *pip);
void pc_start(LoopbackType loopback);
void plc_sleep(void);
void plc_poll(void);
void pc_stop(void);
void pc_restart(void);
void pcm_dump_rtcodes(void);
void pcm_pseudo_code(void);
void pcm_lct_done(void);
void pcm_enabled(void);
void pcm_trace_prop(void);











