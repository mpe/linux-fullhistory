  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/*
 * Definitions for FDDI Station Management.
 */

/*
 * FDDI-COMMON types.
 */

typedef unsigned int Counter;	/* 32-bit event counter */

typedef enum {
    cp_isolated,
    cp_local,
    cp_secondary,
    cp_primary,
    cp_concatenated,
    cp_thru
} CurrentPath;

typedef char Flag;

typedef unsigned char LongAddressType[6];

typedef enum {
    pt_a,
    pt_b,
    pt_s,
    pt_m,
    pt_none
} PortType;

typedef unsigned short ResourceId;

typedef int Time;		/* time in 80ns units */
#define FDDI_TIME_UNIT	80e-9	/* 80 nanoseconds */
#define SECS_TO_FDDI_TIME(s)	((int)((s)/FDDI_TIME_UNIT+0.99))

typedef int TimerTwosComplement;

/*
 * FDDI-SMT types.
 */
typedef enum {
    ec_Out,
    ec_In,
    ec_Trace,
    ec_Leave,
    ec_Path_Test,
    ec_Insert,
    ec_Check,
    ec_Deinsert
} ECMState;

/*
 * FDDI-MAC types.
 */
typedef enum {
    dat_none,
    dat_pass,
    dat_fail
} DupAddressTest;

typedef unsigned short DupCondition;
#define DC_MYDUP	1
#define DC_UNADUP	2

typedef unsigned short FS_Functions;
#define FSF_FS_REPEATING	1
#define FSF_FS_SETTING		2
#define FSF_FS_CLEARING		4

typedef unsigned char NACondition;
#define NAC_UNACHANGE	1
#define NAC_DNACHANGE	2

typedef enum {
    rmt_Isolated,
    rmt_Non_Op,
    rmt_Ring_Op,
    rmt_Detect,
    rmt_Non_Op_Dup,
    rmt_Ring_Op_Dup,
    rmt_Directed,
    rmt_Trace
} RMTState;

typedef unsigned char ShortAddressType[2];

/*
 * FDDI-PATH types.
 */
typedef unsigned short TraceStatus;
#define TS_TRACEINITIATED	1
#define TS_TRACEPROPAGATED	2
#define TS_TRACETERMINATED	4
#define TS_TRACETIMEOUT		8

/*
 * FDDI-PORT types.
 */
typedef enum {
    PC_Maint,
    PC_Enable,
    PC_Disable,
    PC_Start,
    PC_Stop
} ActionType;

typedef unsigned char ConnectionPolicies;
#define PC_MAC_LCT	1
#define PC_MAC_LOOP	2

typedef enum {
    cs_disabled,
    cs_connecting,
    cs_standby,
    cs_active
} ConnectState;

typedef enum {
    ls_qls,
    ls_ils,
    ls_mls,
    ls_hls,
    ls_pdr,
    ls_lsu,
    ls_nls
} LineState;

typedef enum {
    pc_Off,
    pc_Break,
    pc_Trace,
    pc_Connect,
    pc_Next,
    pc_Signal,
    pc_Join,
    pc_Verify,
    pc_Active,
    pc_Maint
} PCMState;

typedef enum {
    pcw_none,
    pcw_mm,
    pcw_otherincompatible,
    pcw_pathnotavailable
} PC_Withhold;

typedef enum {
    pmd_multimode,
    pmd_single_mode1,
    pmd_single_mode2,
    pmd_sonet,
    pmd_low_cost_fiber,
    pmd_twisted_pair,
    pmd_unknown,
    pmd_unspecified
} PMDClass;

