/* $Id: l3_1TR6.h,v 1.1 1996/04/13 10:25:42 fritz Exp $
 *
 * $Log: l3_1TR6.h,v $
 * Revision 1.1  1996/04/13 10:25:42  fritz
 * Initial revision
 *
 *
 */
#ifndef l3_1TR6
#define l3_1TR6

#define PROTO_DIS_N0 0x40
#define PROTO_DIS_N1 0x41

/*
 * MsgType N0
 */
#define MT_N0_REG_IND 61
#define MT_N0_CANC_IND 62
#define MT_N0_FAC_STA 63
#define MT_N0_STA_ACK 64
#define MT_N0_STA_REJ 65
#define MT_N0_FAC_INF 66
#define MT_N0_INF_ACK 67
#define MT_N0_INF_REJ 68
#define MT_N0_CLOSE 75
#define MT_N0_CLO_ACK 77


/*
 * MsgType N1
 */

#define MT_N1_ESC 0x00
#define MT_N1_ALERT 0x01
#define MT_N1_CALL_SENT 0x02
#define MT_N1_CONN 0x07
#define MT_N1_CONN_ACK 0x0F
#define MT_N1_SETUP 0x05
#define MT_N1_SETUP_ACK 0x0D
#define MT_N1_RES 0x26
#define MT_N1_RES_ACK 0x2E
#define MT_N1_RES_REJ 0x22
#define MT_N1_SUSP 0x25
#define MT_N1_SUSP_ACK 0x2D
#define MT_N1_SUSP_REJ 0x21
#define MT_N1_USER_INFO 0x20
#define MT_N1_DET 0x40
#define MT_N1_DISC 0x45
#define MT_N1_REL 0x4D
#define MT_N1_REL_ACK 0x5A
#define MT_N1_CANC_ACK 0x6E
#define MT_N1_CANC_REJ 0x67
#define MT_N1_CON_CON 0x69
#define MT_N1_FAC 0x60
#define MT_N1_FAC_ACK 0x68
#define MT_N1_FAC_CAN 0x66
#define MT_N1_FAC_REG 0x64
#define MT_N1_FAC_REJ 0x65
#define MT_N1_INFO 0x6D
#define MT_N1_REG_ACK 0x6C
#define MT_N1_REG_REJ 0x6F
#define MT_N1_STAT 0x63


struct MTypeDesc {
	byte            mt;
	char           *descr;
};

static struct MTypeDesc mtdesc_n0[] =
{
	{MT_N0_REG_IND, "MT_N0_REG_IND"},
	{MT_N0_CANC_IND, "MT_N0_CANC_IND"},
	{MT_N0_FAC_STA, "MT_N0_FAC_STA"},
	{MT_N0_STA_ACK, "MT_N0_STA_ACK"},
	{MT_N0_STA_REJ, "MT_N0_STA_REJ"},
	{MT_N0_FAC_INF, "MT_N0_FAC_INF"},
	{MT_N0_INF_ACK, "MT_N0_INF_ACK"},
	{MT_N0_INF_REJ, "MT_N0_INF_REJ"},
	{MT_N0_CLOSE, "MT_N0_CLOSE"},
	{MT_N0_CLO_ACK, "MT_N0_CLO_ACK"}
};

static struct MTypeDesc mtdesc_n1[] =
{
	{MT_N1_ESC, "MT_N1_ESC"},
	{MT_N1_ALERT, "MT_N1_ALERT"},
	{MT_N1_CALL_SENT, "MT_N1_CALL_SENT"},
	{MT_N1_CONN, "MT_N1_CONN"},
	{MT_N1_CONN_ACK, "MT_N1_CONN_ACK"},
	{MT_N1_SETUP, "MT_N1_SETUP"},
	{MT_N1_SETUP_ACK, "MT_N1_SETUP_ACK"},
	{MT_N1_RES, "MT_N1_RES"},
	{MT_N1_RES_ACK, "MT_N1_RES_ACK"},
	{MT_N1_RES_REJ, "MT_N1_RES_REJ"},
	{MT_N1_SUSP, "MT_N1_SUSP"},
	{MT_N1_SUSP_ACK, "MT_N1_SUSP_ACK"},
	{MT_N1_SUSP_REJ, "MT_N1_SUSP_REJ"},
	{MT_N1_USER_INFO, "MT_N1_USER_INFO"},
	{MT_N1_DET, "MT_N1_DET"},
	{MT_N1_DISC, "MT_N1_DISC"},
	{MT_N1_REL, "MT_N1_REL"},
	{MT_N1_REL_ACK, "MT_N1_REL_ACK"},
	{MT_N1_CANC_ACK, "MT_N1_CANC_ACK"},
	{MT_N1_CANC_REJ, "MT_N1_CANC_REJ"},
	{MT_N1_CON_CON, "MT_N1_CON_CON"},
	{MT_N1_FAC, "MT_N1_FAC"},
	{MT_N1_FAC_ACK, "MT_N1_FAC_ACK"},
	{MT_N1_FAC_CAN, "MT_N1_FAC_CAN"},
	{MT_N1_FAC_REG, "MT_N1_FAC_REG"},
	{MT_N1_FAC_REJ, "MT_N1_FAC_REJ"},
	{MT_N1_INFO, "MT_N1_INFO"},
	{MT_N1_REG_ACK, "MT_N1_REG_ACK"},
	{MT_N1_REG_REJ, "MT_N1_REG_REJ"},
	{MT_N1_STAT, "MT_N1_STAT"}
};


/*
 * W Elemente
 */

#define WE_Shift_F0 0x90
#define WE_Shift_F6 0x96
#define WE_Shift_OF0 0x98
#define WE_Shift_OF6 0x9E

#define WE0_cause 0x08
#define WE0_connAddr 0x0C
#define WE0_callID 0x10
#define WE0_chanID 0x18
#define WE0_netSpecFac 0x20
#define WE0_display 0x28
#define WE0_keypad 0x2C
#define WE0_origAddr 0x6C
#define WE0_destAddr 0x70
#define WE0_userInfo 0x7E

#define WE0_moreData 0xA0
#define WE0_congestLevel 0xB0

#define WE6_serviceInd 0x01
#define WE6_chargingInfo 0x02
#define WE6_date 0x03
#define WE6_facSelect 0x05
#define WE6_facStatus 0x06
#define WE6_statusCalled 0x07
#define WE6_addTransAttr 0x08

/*
 * FacCodes
 */
#define FAC_Sperre 0x01
#define FAC_Sperre_All 0x02
#define FAC_Sperre_Fern 0x03
#define FAC_Sperre_Intl 0x04
#define FAC_Sperre_Interk 0x05

#define FAC_Forward1 0x02
#define FAC_Forward2 0x03
#define FAC_Konferenz 0x06
#define FAC_GrabBchan 0x0F
#define FAC_Reactivate 0x10
#define FAC_Konferenz3 0x11
#define FAC_Dienstwechsel1 0x12
#define FAC_Dienstwechsel2 0x13
#define FAC_NummernIdent 0x14
#define FAC_GBG 0x15
#define FAC_DisplayUebergeben 0x17
#define FAC_DisplayUmgeleitet 0x1A
#define FAC_Unterdruecke 0x1B
#define FAC_Deactivate 0x1E
#define FAC_Activate 0x1D
#define FAC_SVC 0x1F
#define FAC_Rueckwechsel 0x23
#define FAC_Umleitung 0x24

/*
 * Cause codes
 */
#define CAUSE_InvCRef 0x01
#define CAUSE_BearerNotImpl 0x03
#define CAUSE_CIDunknown 0x07
#define CAUSE_CIDinUse 0x08
#define CAUSE_NoChans 0x0A
#define CAUSE_FacNotImpl 0x10
#define CAUSE_FacNotSubscr 0x11
#define CAUSE_OutgoingBarred 0x20
#define CAUSE_UserAssessBusy 0x21
#define CAUSE_NegativeGBG 0x22
#define CAUSE_UnknownGBG 0x23
#define CAUSE_NoSPVknown 0x25
#define CAUSE_DestNotObtain 0x35
#define CAUSE_NumberChanged 0x38
#define CAUSE_OutOfOrder 0x39
#define CAUSE_NoUserResponse 0x3A
#define CAUSE_UserBusy 0x3B
#define CAUSE_IncomingBarred 0x3D
#define CAUSE_CallRejected 0x3E
#define CAUSE_NetworkCongestion 0x59
#define CAUSE_RemoteUser 0x5A
#define CAUSE_LocalProcErr 0x70
#define CAUSE_RemoteProcErr 0x71
#define CAUSE_RemoteUserSuspend 0x72
#define CAUSE_RemoteUserResumed 0x73
#define CAUSE_UserInfoDiscarded 0x7F


#endif
