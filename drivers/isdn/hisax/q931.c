/* $Id: q931.c,v 1.6 1997/07/27 21:09:44 keil Exp $

 * q931.c       code to decode ITU Q.931 call control messages
 *
 * Author       Jan den Ouden
 *
 * Changelog
 *
 * Pauline Middelink    general improvements
 *
 * Beat Doebeli         cause texts, display information element
 *
 * Karsten Keil         cause texts, display information element for 1TR6
 *
 *
 * $Log: q931.c,v $
 * Revision 1.6  1997/07/27 21:09:44  keil
 * move functions to isdnl3.c
 *
 * Revision 1.5  1997/04/06 22:56:43  keil
 * Some cosmetic changes
 *
 * Revision 1.4  1997/02/09 00:29:11  keil
 * new interface handling, one interface per card
 *
 * Revision 1.3  1997/01/21 22:24:59  keil
 * cleanups
 *
 * Revision 1.2  1996/10/27 22:12:45  keil
 * reporting unknown level 3 protocol ids
 *
 * Revision 1.1  1996/10/13 20:04:56  keil
 * Initial revision
 *
 *
 */


#define __NO_VERSION__
#include "hisax.h"
#include "l3_1tr6.h"

void
iecpy(u_char * dest, u_char * iestart, int ieoffset)
{
	u_char *p;
	int l;

	p = iestart + ieoffset + 2;
	l = iestart[1] - ieoffset;
	while (l--)
		*dest++ = *p++;
	*dest++ = '\0';
}

/*
 * According to Table 4-2/Q.931
 */
static
struct MessageType {
	u_char nr;
	char *descr;
} mtlist[] = {

	{
		0x1, "ALERTING"
	},
	{
		0x2, "CALL PROCEEDING"
	},
	{
		0x7, "CONNECT"
	},
	{
		0xf, "CONNECT ACKNOWLEDGE"
	},
	{
		0x3, "PROGRESS"
	},
	{
		0x5, "SETUP"
	},
	{
		0xd, "SETUP ACKNOWLEDGE"
	},
	{
		0x26, "RESUME"
	},
	{
		0x2e, "RESUME ACKNOWLEDGE"
	},
	{
		0x22, "RESUME REJECT"
	},
	{
		0x25, "SUSPEND"
	},
	{
		0x2d, "SUSPEND ACKNOWLEDGE"
	},
	{
		0x21, "SUSPEND REJECT"
	},
	{
		0x20, "USER INFORMATION"
	},
	{
		0x45, "DISCONNECT"
	},
	{
		0x4d, "RELEASE"
	},
	{
		0x5a, "RELEASE COMPLETE"
	},
	{
		0x46, "RESTART"
	},
	{
		0x4e, "RESTART ACKNOWLEDGE"
	},
	{
		0x60, "SEGMENT"
	},
	{
		0x79, "CONGESTION CONTROL"
	},
	{
		0x7b, "INFORMATION"
	},
	{
		0x62, "FACILITY"
	},
	{
		0x6e, "NOTIFY"
	},
	{
		0x7d, "STATUS"
	},
	{
		0x75, "STATUS ENQUIRY"
	}
};

#define MTSIZE sizeof(mtlist)/sizeof(struct MessageType)

static
struct MessageType mt_n0[] =
{
	{MT_N0_REG_IND, "REGister INDication"},
	{MT_N0_CANC_IND, "CANCel INDication"},
	{MT_N0_FAC_STA, "FACility STAtus"},
	{MT_N0_STA_ACK, "STAtus ACKnowledge"},
	{MT_N0_STA_REJ, "STAtus REJect"},
	{MT_N0_FAC_INF, "FACility INFormation"},
	{MT_N0_INF_ACK, "INFormation ACKnowledge"},
	{MT_N0_INF_REJ, "INFormation REJect"},
	{MT_N0_CLOSE, "CLOSE"},
	{MT_N0_CLO_ACK, "CLOse ACKnowledge"}
};

int mt_n0_len = (sizeof(mt_n0) / sizeof(struct MessageType));

static
struct MessageType mt_n1[] =
{
	{MT_N1_ESC, "ESCape"},
	{MT_N1_ALERT, "ALERT"},
	{MT_N1_CALL_SENT, "CALL SENT"},
	{MT_N1_CONN, "CONNect"},
	{MT_N1_CONN_ACK, "CONNect ACKnowledge"},
	{MT_N1_SETUP, "SETUP"},
	{MT_N1_SETUP_ACK, "SETUP ACKnowledge"},
	{MT_N1_RES, "RESume"},
	{MT_N1_RES_ACK, "RESume ACKnowledge"},
	{MT_N1_RES_REJ, "RESume REJect"},
	{MT_N1_SUSP, "SUSPend"},
	{MT_N1_SUSP_ACK, "SUSPend ACKnowledge"},
	{MT_N1_SUSP_REJ, "SUSPend REJect"},
	{MT_N1_USER_INFO, "USER INFO"},
	{MT_N1_DET, "DETach"},
	{MT_N1_DISC, "DISConnect"},
	{MT_N1_REL, "RELease"},
	{MT_N1_REL_ACK, "RELease ACKnowledge"},
	{MT_N1_CANC_ACK, "CANCel ACKnowledge"},
	{MT_N1_CANC_REJ, "CANCel REJect"},
	{MT_N1_CON_CON, "CONgestion CONtrol"},
	{MT_N1_FAC, "FACility"},
	{MT_N1_FAC_ACK, "FACility ACKnowledge"},
	{MT_N1_FAC_CAN, "FACility CANcel"},
	{MT_N1_FAC_REG, "FACility REGister"},
	{MT_N1_FAC_REJ, "FACility REJect"},
	{MT_N1_INFO, "INFOrmation"},
	{MT_N1_REG_ACK, "REGister ACKnowledge"},
	{MT_N1_REG_REJ, "REGister REJect"},
	{MT_N1_STAT, "STATus"}
};

int mt_n1_len = (sizeof(mt_n1) / sizeof(struct MessageType));

static struct MessageType fac_1tr6[] =
{
	{FAC_Sperre, "Sperre"},
	{FAC_Forward1, "Forward 1"},
	{FAC_Forward2, "Forward 2"},
	{FAC_Konferenz, "Konferenz"},
	{FAC_GrabBchan, "Grab Bchannel"},
	{FAC_Reactivate, "Reactivate"},
	{FAC_Konferenz3, "Dreier Konferenz"},
	{FAC_Dienstwechsel1, "Einseitiger Dienstwechsel"},
	{FAC_Dienstwechsel2, "Zweiseitiger Dienstwechsel"},
	{FAC_NummernIdent, "Rufnummer-Identifizierung"},
	{FAC_GBG, "GBG"},
	{FAC_DisplayUebergeben, "Display Uebergeben"},
	{FAC_DisplayUmgeleitet, "Display Umgeleitet"},
	{FAC_Unterdruecke, "Unterdruecke Rufnummer"},
	{FAC_Deactivate, "Deactivate"},
	{FAC_Activate, "Activate"},
	{FAC_SPV, "SPV"},
	{FAC_Rueckwechsel, "Rueckwechsel"},
	{FAC_Umleitung, "Umleitung"}
};
int fac_1tr6_len = (sizeof(fac_1tr6) / sizeof(struct MessageType));



static int
prbits(char *dest, u_char b, int start, int len)
{
	char *dp = dest;

	b = b << (8 - start);
	while (len--) {
		if (b & 0x80)
			*dp++ = '1';
		else
			*dp++ = '0';
		b = b << 1;
	}
	return (dp - dest);
}

static
u_char *
skipext(u_char * p)
{
	while (!(*p++ & 0x80));
	return (p);
}

/*
 * Cause Values According to Q.850
 * edescr: English description
 * ddescr: German description used by Swissnet II (Swiss Telecom
 *         not yet written...
 */

static
struct CauseValue {
	u_char nr;
	char *edescr;
	char *ddescr;
} cvlist[] = {

	{
		0x01, "Unallocated (unassigned) number", "Nummer nicht zugeteilt"
	},
	{
		0x02, "No route to specified transit network", ""
	},
	{
		0x03, "No route to destination", ""
	},
	{
		0x04, "Send special information tone", ""
	},
	{
		0x05, "Misdialled trunk prefix", ""
	},
	{
		0x06, "Channel unacceptable", "Kanal nicht akzeptierbar"
	},
	{
		0x07, "Channel awarded and being delivered in an established channel", ""
	},
	{
		0x08, "Preemption", ""
	},
	{
		0x09, "Preemption - circuit reserved for reuse", ""
	},
	{
		0x10, "Normal call clearing", "Normale Ausloesung"
	},
	{
		0x11, "User busy", "TNB besetzt"
	},
	{
		0x12, "No user responding", ""
	},
	{
		0x13, "No answer from user (user alerted)", ""
	},
	{
		0x14, "Subscriber absent", ""
	},
	{
		0x15, "Call rejected", ""
	},
	{
		0x16, "Number changed", ""
	},
	{
		0x1a, "non-selected user clearing", ""
	},
	{
		0x1b, "Destination out of order", ""
	},
	{
		0x1c, "Invalid number format (address incomplete)", ""
	},
	{
		0x1d, "Facility rejected", ""
	},
	{
		0x1e, "Response to Status enquiry", ""
	},
	{
		0x1f, "Normal, unspecified", ""
	},
	{
		0x22, "No circuit/channel available", ""
	},
	{
		0x26, "Network out of order", ""
	},
	{
		0x27, "Permanent frame mode connection out-of-service", ""
	},
	{
		0x28, "Permanent frame mode connection operational", ""
	},
	{
		0x29, "Temporary failure", ""
	},
	{
		0x2a, "Switching equipment congestion", ""
	},
	{
		0x2b, "Access information discarded", ""
	},
	{
		0x2c, "Requested circuit/channel not available", ""
	},
	{
		0x2e, "Precedence call blocked", ""
	},
	{
		0x2f, "Resource unavailable, unspecified", ""
	},
	{
		0x31, "Quality of service unavailable", ""
	},
	{
		0x32, "Requested facility not subscribed", ""
	},
	{
		0x35, "Outgoing calls barred within CUG", ""
	},
	{
		0x37, "Incoming calls barred within CUG", ""
	},
	{
		0x39, "Bearer capability not authorized", ""
	},
	{
		0x3a, "Bearer capability not presently available", ""
	},
	{
		0x3e, "Inconsistency in designated outgoing access information and subscriber class ", " "
	},
	{
		0x3f, "Service or option not available, unspecified", ""
	},
	{
		0x41, "Bearer capability not implemented", ""
	},
	{
		0x42, "Channel type not implemented", ""
	},
	{
		0x43, "Requested facility not implemented", ""
	},
	{
		0x44, "Only restricted digital information bearer capability is available", ""
	},
	{
		0x4f, "Service or option not implemented", ""
	},
	{
		0x51, "Invalid call reference value", ""
	},
	{
		0x52, "Identified channel does not exist", ""
	},
	{
		0x53, "A suspended call exists, but this call identity does not", ""
	},
	{
		0x54, "Call identity in use", ""
	},
	{
		0x55, "No call suspended", ""
	},
	{
		0x56, "Call having the requested call identity has been cleared", ""
	},
	{
		0x57, "User not member of CUG", ""
	},
	{
		0x58, "Incompatible destination", ""
	},
	{
		0x5a, "Non-existent CUG", ""
	},
	{
		0x5b, "Invalid transit network selection", ""
	},
	{
		0x5f, "Invalid message, unspecified", ""
	},
	{
		0x60, "Mandatory information element is missing", ""
	},
	{
		0x61, "Message type non-existent or not implemented", ""
	},
	{
		0x62, "Message not compatible with call state or message type non-existent or not implemented ", " "
	},
	{
		0x63, "Information element/parameter non-existent or not implemented", ""
	},
	{
		0x64, "Invalid information element contents", ""
	},
	{
		0x65, "Message not compatible with call state", ""
	},
	{
		0x66, "Recovery on timer expiry", ""
	},
	{
		0x67, "Parameter non-existent or not implemented - passed on", ""
	},
	{
		0x6e, "Message with unrecognized parameter discarded", ""
	},
	{
		0x6f, "Protocol error, unspecified", ""
	},
	{
		0x7f, "Interworking, unspecified", ""
	},
};

#define CVSIZE sizeof(cvlist)/sizeof(struct CauseValue)

static
int
prcause(char *dest, u_char * p)
{
	u_char *end;
	char *dp = dest;
	int i, cause;

	end = p + p[1] + 1;
	p += 2;
	dp += sprintf(dp, "    coding ");
	dp += prbits(dp, *p, 7, 2);
	dp += sprintf(dp, " location ");
	dp += prbits(dp, *p, 4, 4);
	*dp++ = '\n';
	p = skipext(p);

	cause = 0x7f & *p++;

	/* locate cause value */
	for (i = 0; i < CVSIZE; i++)
		if (cvlist[i].nr == cause)
			break;

	/* display cause value if it exists */
	if (i == CVSIZE)
		dp += sprintf(dp, "Unknown cause type %x!\n", cause);
	else
		dp += sprintf(dp, "  cause value %x : %s \n", cause, cvlist[i].edescr);

	while (!0) {
		if (p > end)
			break;
		dp += sprintf(dp, "    diag attribute %d ", *p++ & 0x7f);
		dp += sprintf(dp, " rej %d ", *p & 0x7f);
		if (*p & 0x80) {
			*dp++ = '\n';
			break;
		} else
			dp += sprintf(dp, " av %d\n", (*++p) & 0x7f);
	}
	return (dp - dest);

}

static
struct MessageType cause_1tr6[] =
{
	{CAUSE_InvCRef, "Invalid Call Reference"},
	{CAUSE_BearerNotImpl, "Bearer Service Not Implemented"},
	{CAUSE_CIDunknown, "Caller Identity unknown"},
	{CAUSE_CIDinUse, "Caller Identity in Use"},
	{CAUSE_NoChans, "No Channels available"},
	{CAUSE_FacNotImpl, "Facility Not Implemented"},
	{CAUSE_FacNotSubscr, "Facility Not Subscribed"},
	{CAUSE_OutgoingBarred, "Outgoing calls barred"},
	{CAUSE_UserAccessBusy, "User Access Busy"},
	{CAUSE_NegativeGBG, "Negative GBG"},
	{CAUSE_UnknownGBG, "Unknown  GBG"},
	{CAUSE_NoSPVknown, "No SPV known"},
	{CAUSE_DestNotObtain, "Destination not obtainable"},
	{CAUSE_NumberChanged, "Number changed"},
	{CAUSE_OutOfOrder, "Out Of Order"},
	{CAUSE_NoUserResponse, "No User Response"},
	{CAUSE_UserBusy, "User Busy"},
	{CAUSE_IncomingBarred, "Incoming Barred"},
	{CAUSE_CallRejected, "Call Rejected"},
	{CAUSE_NetworkCongestion, "Network Congestion"},
	{CAUSE_RemoteUser, "Remote User initiated"},
	{CAUSE_LocalProcErr, "Local Procedure Error"},
	{CAUSE_RemoteProcErr, "Remote Procedure Error"},
	{CAUSE_RemoteUserSuspend, "Remote User Suspend"},
	{CAUSE_RemoteUserResumed, "Remote User Resumed"},
	{CAUSE_UserInfoDiscarded, "User Info Discarded"}
};

int cause_1tr6_len = (sizeof(cause_1tr6) / sizeof(struct MessageType));

static int
prcause_1tr6(char *dest, u_char * p)
{
	char *dp = dest;
	int i, cause;

	p++;
	if (0 == *p) {
		dp += sprintf(dp, "   OK (cause length=0)\n");
		return (dp - dest);
	} else if (*p > 1) {
		dp += sprintf(dp, "    coding ");
		dp += prbits(dp, p[2], 7, 2);
		dp += sprintf(dp, " location ");
		dp += prbits(dp, p[2], 4, 4);
		*dp++ = '\n';
	}
	p++;
	cause = 0x7f & *p;

	/* locate cause value */
	for (i = 0; i < cause_1tr6_len; i++)
		if (cause_1tr6[i].nr == cause)
			break;

	/* display cause value if it exists */
	if (i == cause_1tr6_len)
		dp += sprintf(dp, "Unknown cause type %x!\n", cause);
	else
		dp += sprintf(dp, "  cause value %x : %s \n", cause, cause_1tr6[i].descr);

	return (dp - dest);

}

static int
prchident(char *dest, u_char * p)
{
	char *dp = dest;

	p += 2;
	dp += sprintf(dp, "    octet 3 ");
	dp += prbits(dp, *p, 8, 8);
	*dp++ = '\n';
	return (dp - dest);
}

static int
prcalled(char *dest, u_char * p)
{
	int l;
	char *dp = dest;

	p++;
	l = *p++ - 1;
	dp += sprintf(dp, "    octet 3 ");
	dp += prbits(dp, *p++, 8, 8);
	*dp++ = '\n';
	dp += sprintf(dp, "    number digits ");
	while (l--)
		*dp++ = *p++;
	*dp++ = '\n';
	return (dp - dest);
}
static int
prcalling(char *dest, u_char * p)
{
	int l;
	char *dp = dest;

	p++;
	l = *p++ - 1;
	dp += sprintf(dp, "    octet 3 ");
	dp += prbits(dp, *p, 8, 8);
	*dp++ = '\n';
	if (!(*p & 0x80)) {
		dp += sprintf(dp, "    octet 3a ");
		dp += prbits(dp, *++p, 8, 8);
		*dp++ = '\n';
		l--;
	};
	p++;

	dp += sprintf(dp, "    number digits ");
	while (l--)
		*dp++ = *p++;
	*dp++ = '\n';
	return (dp - dest);
}

static
int
prbearer(char *dest, u_char * p)
{
	char *dp = dest, ch;

	p += 2;
	dp += sprintf(dp, "    octet 3  ");
	dp += prbits(dp, *p++, 8, 8);
	*dp++ = '\n';
	dp += sprintf(dp, "    octet 4  ");
	dp += prbits(dp, *p, 8, 8);
	*dp++ = '\n';
	if ((*p++ & 0x1f) == 0x18) {
		dp += sprintf(dp, "    octet 4.1 ");
		dp += prbits(dp, *p++, 8, 8);
		*dp++ = '\n';
	}
	/* check for user information layer 1 */
	if ((*p & 0x60) == 0x20) {
		ch = ' ';
		do {
			dp += sprintf(dp, "    octet 5%c ", ch);
			dp += prbits(dp, *p, 8, 8);
			*dp++ = '\n';
			if (ch == ' ')
				ch = 'a';
			else
				ch++;
		}
		while (!(*p++ & 0x80));
	}
	/* check for user information layer 2 */
	if ((*p & 0x60) == 0x40) {
		dp += sprintf(dp, "    octet 6  ");
		dp += prbits(dp, *p++, 8, 8);
		*dp++ = '\n';
	}
	/* check for user information layer 3 */
	if ((*p & 0x60) == 0x60) {
		dp += sprintf(dp, "    octet 7  ");
		dp += prbits(dp, *p++, 8, 8);
		*dp++ = '\n';
	}
	return (dp - dest);
}

static int
general(char *dest, u_char * p)
{
	char *dp = dest;
	char ch = ' ';
	int l, octet = 3;

	p++;
	l = *p++;
	/* Iterate over all octets in the information element */
	while (l--) {
		dp += sprintf(dp, "    octet %d%c ", octet, ch);
		dp += prbits(dp, *p++, 8, 8);
		*dp++ = '\n';

		/* last octet in group? */
		if (*p & 0x80) {
			octet++;
			ch = ' ';
		} else if (ch == ' ')
			ch = 'a';
		else
			ch++;
	}
	return (dp - dest);
}

static int
prcharge(char *dest, u_char * p)
{
	char *dp = dest;
	int l;

	p++;
	l = *p++ - 1;
	dp += sprintf(dp, "    GEA ");
	dp += prbits(dp, *p++, 8, 8);
	dp += sprintf(dp, "  Anzahl: ");
	/* Iterate over all octets in the * information element */
	while (l--)
		*dp++ = *p++;
	*dp++ = '\n';
	return (dp - dest);
}
static int
prtext(char *dest, u_char * p)
{
	char *dp = dest;
	int l;

	p++;
	l = *p++;
	dp += sprintf(dp, "    ");
	/* Iterate over all octets in the * information element */
	while (l--)
		*dp++ = *p++;
	*dp++ = '\n';
	return (dp - dest);
}
static int
display(char *dest, u_char * p)
{
	char *dp = dest;
	char ch = ' ';
	int l, octet = 3;

	p++;
	l = *p++;
	/* Iterate over all octets in the * display-information element */
	dp += sprintf(dp, "   \"");
	while (l--) {
		dp += sprintf(dp, "%c", *p++);

		/* last octet in group? */
		if (*p & 0x80) {
			octet++;
			ch = ' ';
		} else if (ch == ' ')
			ch = 'a';

		else
			ch++;
	}
	*dp++ = '\"';
	*dp++ = '\n';
	return (dp - dest);
}

int
prfacility(char *dest, u_char * p)
{
	char *dp = dest;
	int l, l2;

	p++;
	l = *p++;
	dp += sprintf(dp, "    octet 3 ");
	dp += prbits(dp, *p++, 8, 8);
	dp += sprintf(dp, "\n");
	l -= 1;

	while (l > 0) {
		dp += sprintf(dp, "   octet 4 ");
		dp += prbits(dp, *p++, 8, 8);
		dp += sprintf(dp, "\n");
		dp += sprintf(dp, "   octet 5 %d\n", l2 = *p++ & 0x7f);
		l -= 2;
		dp += sprintf(dp, "   contents ");
		while (l2--) {
			dp += sprintf(dp, "%2x ", *p++);
			l--;
		}
		dp += sprintf(dp, "\n");
	}

	return (dp - dest);
}

static
struct InformationElement {
	u_char nr;
	char *descr;
	int (*f) (char *, u_char *);
} ielist[] = {

	{
		0x00, "Segmented message", general
	},
	{
		0x04, "Bearer capability", prbearer
	},
	{
		0x08, "Cause", prcause
	},
	{
		0x10, "Call identity", general
	},
	{
		0x14, "Call state", general
	},
	{
		0x18, "Channel identification", prchident
	},
	{
		0x1c, "Facility", prfacility
	},
	{
		0x1e, "Progress indicator", general
	},
	{
		0x20, "Network-specific facilities", general
	},
	{
		0x27, "Notification indicator", general
	},
	{
		0x28, "Display", display
	},
	{
		0x29, "Date/Time", general
	},
	{
		0x2c, "Keypad facility", general
	},
	{
		0x34, "Signal", general
	},
	{
		0x40, "Information rate", general
	},
	{
		0x42, "End-to-end delay", general
	},
	{
		0x43, "Transit delay selection and indication", general
	},
	{
		0x44, "Packet layer binary parameters", general
	},
	{
		0x45, "Packet layer window size", general
	},
	{
		0x46, "Packet size", general
	},
	{
		0x47, "Closed user group", general
	},
	{
		0x4a, "Reverse charge indication", general
	},
	{
		0x6c, "Calling party number", prcalling
	},
	{
		0x6d, "Calling party subaddress", general
	},
	{
		0x70, "Called party number", prcalled
	},
	{
		0x71, "Called party subaddress", general
	},
	{
		0x74, "Redirecting number", general
	},
	{
		0x78, "Transit network selection", general
	},
	{
		0x79, "Restart indicator", general
	},
	{
		0x7c, "Low layer compatibility", general
	},
	{
		0x7d, "High layer compatibility", general
	},
	{
		0x7e, "User-user", general
	},
	{
		0x7f, "Escape for extension", general
	},
};


#define IESIZE sizeof(ielist)/sizeof(struct InformationElement)

static struct InformationElement we_0[] =
{
	{WE0_cause, "Cause", prcause_1tr6},
	{WE0_connAddr, "Connecting Address", prcalled},
	{WE0_callID, "Call IDentity", general},
	{WE0_chanID, "Channel IDentity", general},
	{WE0_netSpecFac, "Network Specific Facility", general},
	{WE0_display, "Display", general},
	{WE0_keypad, "Keypad", general},
	{WE0_origAddr, "Origination Address", prcalled},
	{WE0_destAddr, "Destination Address", prcalled},
	{WE0_userInfo, "User Info", general}
};

static int we_0_len = (sizeof(we_0) / sizeof(struct InformationElement));

static struct InformationElement we_6[] =
{
	{WE6_serviceInd, "Service Indicator", general},
	{WE6_chargingInfo, "Charging Information", prcharge},
	{WE6_date, "Date", prtext},
	{WE6_facSelect, "Facility Select", general},
	{WE6_facStatus, "Facility Status", general},
	{WE6_statusCalled, "Status Called", general},
	{WE6_addTransAttr, "Additional Transmission Attributes", general}
};
static int we_6_len = (sizeof(we_6) / sizeof(struct InformationElement));

int
QuickHex(char *txt, u_char * p, int cnt)
{
	register int i;
	register char *t = txt;
	register u_char w;

	for (i = 0; i < cnt; i++) {
		*t++ = ' ';
		w = (p[i] >> 4) & 0x0f;
		if (w < 10)
			*t++ = '0' + w;
		else
			*t++ = 'A' - 10 + w;
		w = p[i] & 0x0f;
		if (w < 10)
			*t++ = '0' + w;
		else
			*t++ = 'A' - 10 + w;
	}
	*t++ = 0;
	return (t - txt);
}

void
LogFrame(struct IsdnCardState *sp, u_char * buf, int size)
{
	char *dp;

	if (size < 1)
		return;
	dp = sp->dlogspace;
	if (size < 4096 / 3 - 10) {
		dp += sprintf(dp, "HEX:");
		dp += QuickHex(dp, buf, size);
		dp--;
		*dp++ = '\n';
		*dp = 0;
	} else
		sprintf(dp, "LogFrame: warning Frame too big (%d)\n",
			size);
	HiSax_putstatus(sp, sp->dlogspace);
}

void
dlogframe(struct IsdnCardState *sp, u_char * buf, int size, char *comment)
{
	u_char *bend = buf + size;
	char *dp;
	unsigned char pd, cr_l, cr, mt;
	int i, cs = 0, cs_old = 0, cs_fest = 0;

	if (size < 1)
		return;
	/* display header */
	dp = sp->dlogspace;
	dp += sprintf(dp, "%s\n", comment);

	if ((0xfe & buf[0]) == PROTO_DIS_N0) {	/* 1TR6 */
		/* locate message type */
		pd = *buf++;
		cr_l = *buf++;
		if (cr_l)
			cr = *buf++;
		else
			cr = 0;
		mt = *buf++;
		if (pd == PROTO_DIS_N0) {	/* N0 */
			for (i = 0; i < mt_n0_len; i++)
				if (mt_n0[i].nr == mt)
					break;
			/* display message type if it exists */
			if (i == mt_n0_len)
				dp += sprintf(dp, "callref %d %s size %d unknown message type N0 %x!\n",
					      cr & 0x7f, (cr & 0x80) ? "called" : "caller",
					      size, mt);
			else
				dp += sprintf(dp, "callref %d %s size %d message type %s\n",
					      cr & 0x7f, (cr & 0x80) ? "called" : "caller",
					      size, mt_n0[i].descr);
		} else {	/* N1 */
			for (i = 0; i < mt_n1_len; i++)
				if (mt_n1[i].nr == mt)
					break;
			/* display message type if it exists */
			if (i == mt_n1_len)
				dp += sprintf(dp, "callref %d %s size %d unknown message type N1 %x!\n",
					      cr & 0x7f, (cr & 0x80) ? "called" : "caller",
					      size, mt);
			else
				dp += sprintf(dp, "callref %d %s size %d message type %s\n",
					      cr & 0x7f, (cr & 0x80) ? "called" : "caller",
					      size, mt_n1[i].descr);
		}

		/* display each information element */
		while (buf < bend) {
			/* Is it a single octet information element? */
			if (*buf & 0x80) {
				switch ((*buf >> 4) & 7) {
					case 1:
						dp += sprintf(dp, "  Shift %x\n", *buf & 0xf);
						cs_old = cs;
						cs = *buf & 7;
						cs_fest = *buf & 8;
						break;
					case 3:
						dp += sprintf(dp, "  Congestion level %x\n", *buf & 0xf);
						break;
					case 2:
						if (*buf == 0xa0) {
							dp += sprintf(dp, "  More data\n");
							break;
						}
						if (*buf == 0xa1) {
							dp += sprintf(dp, "  Sending complete\n");
						}
						break;
						/* fall through */
					default:
						dp += sprintf(dp, "  Reserved %x\n", *buf);
						break;
				}
				buf++;
				continue;
			}
			/* No, locate it in the table */
			if (cs == 0) {
				for (i = 0; i < we_0_len; i++)
					if (*buf == we_0[i].nr)
						break;

				/* When found, give appropriate msg */
				if (i != we_0_len) {
					dp += sprintf(dp, "  %s\n", we_0[i].descr);
					dp += we_0[i].f(dp, buf);
				} else
					dp += sprintf(dp, "  Codeset %d attribute %x attribute size %d\n", cs, *buf, buf[1]);
			} else if (cs == 6) {
				for (i = 0; i < we_6_len; i++)
					if (*buf == we_6[i].nr)
						break;

				/* When found, give appropriate msg */
				if (i != we_6_len) {
					dp += sprintf(dp, "  %s\n", we_6[i].descr);
					dp += we_6[i].f(dp, buf);
				} else
					dp += sprintf(dp, "  Codeset %d attribute %x attribute size %d\n", cs, *buf, buf[1]);
			} else
				dp += sprintf(dp, "  Unknown Codeset %d attribute %x attribute size %d\n", cs, *buf, buf[1]);
			/* Skip to next element */
			if (cs_fest == 8) {
				cs = cs_old;
				cs_old = 0;
				cs_fest = 0;
			}
			buf += buf[1] + 2;
		}
	} else if (buf[0] == 8) {	/* EURO */
		/* locate message type */
		buf++;
		cr_l = *buf++;
		if (cr_l)
			cr = *buf++;
		else
			cr = 0;
		mt = *buf++;
		for (i = 0; i < MTSIZE; i++)
			if (mtlist[i].nr == mt)
				break;

		/* display message type if it exists */
		if (i == MTSIZE)
			dp += sprintf(dp, "callref %d %s size %d unknown message type %x!\n",
			    cr & 0x7f, (cr & 0x80) ? "called" : "caller",
				      size, mt);
		else
			dp += sprintf(dp, "callref %d %s size %d message type %s\n",
			    cr & 0x7f, (cr & 0x80) ? "called" : "caller",
				      size, mtlist[i].descr);

		/* display each information element */
		while (buf < bend) {
			/* Is it a single octet information element? */
			if (*buf & 0x80) {
				switch ((*buf >> 4) & 7) {
					case 1:
						dp += sprintf(dp, "  Shift %x\n", *buf & 0xf);
						break;
					case 3:
						dp += sprintf(dp, "  Congestion level %x\n", *buf & 0xf);
						break;
					case 5:
						dp += sprintf(dp, "  Repeat indicator %x\n", *buf & 0xf);
						break;
					case 2:
						if (*buf == 0xa0) {
							dp += sprintf(dp, "  More data\n");
							break;
						}
						if (*buf == 0xa1) {
							dp += sprintf(dp, "  Sending complete\n");
						}
						break;
						/* fall through */
					default:
						dp += sprintf(dp, "  Reserved %x\n", *buf);
						break;
				}
				buf++;
				continue;
			}
			/* No, locate it in the table */
			for (i = 0; i < IESIZE; i++)
				if (*buf == ielist[i].nr)
					break;

			/* When not found, give appropriate msg */
			if (i != IESIZE) {
				dp += sprintf(dp, "  %s\n", ielist[i].descr);
				dp += ielist[i].f(dp, buf);
			} else
				dp += sprintf(dp, "  attribute %x attribute size %d\n", *buf, buf[1]);

			/* Skip to next element */
			buf += buf[1] + 2;
		}
	} else {
		dp += sprintf(dp, "Unknown protocol %x!", buf[0]);
	}
	dp += sprintf(dp, "\n");
	HiSax_putstatus(sp, sp->dlogspace);
}
