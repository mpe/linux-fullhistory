/*
 * q931.c               code to decode ITU Q.931 call control messages
 * 
 * Author               Jan den Ouden
 * 
 * Changelog
 * 
 * Pauline Middelink    general improvements
 * 
 * Beat Doebeli         cause texts, display information element
 * 
 */


#define __NO_VERSION__
#include "teles.h"

byte           *
findie(byte * p, int size, byte ie, int wanted_set)
{
	int             l, codeset, maincodeset;
	byte           *pend = p + size;

	/* skip protocol discriminator, callref and message type */
	p++;
	l = (*p++) & 0xf;
	p += l;
	p++;
	codeset = 0;
	maincodeset = 0;
	/* while there are bytes left... */
	while (p < pend) {
		if ((*p & 0xf0) == 0x90) {
			codeset = *p & 0x07;
			if (!(*p & 0x08))
				maincodeset = codeset;
		}
		if (*p & 0x80)
			p++;
		else {
			if (codeset == wanted_set) {
				if (*p == ie)
					return (p);
				if (*p > ie)
					return (NULL);
			}
			p++;
			l = *p++;
			p += l;
			codeset = maincodeset;
		}
	}
	return (NULL);
}

void
iecpy(byte * dest, byte * iestart, int ieoffset)
{
	byte           *p;
	int             l;

	p = iestart + ieoffset + 2;
	l = iestart[1] - ieoffset;
	while (l--)
		*dest++ = *p++;
	*dest++ = '\0';
}

int
getcallref(byte * p)
{
	p++;			/* prot discr */
	p++;			/* callref length */
	return (*p);		/* assuming one-byte callref */
}

/*
 * According to Table 4-2/Q.931
 */
static
struct MessageType {
	byte            nr;
	char           *descr;
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
int
prbits(char *dest, byte b, int start, int len)
{
	char           *dp = dest;

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
byte           *
skipext(byte * p)
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
	byte            nr;
	char           *edescr;
	char           *ddescr;
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
		0x1e, "Response to Status enuiry", ""
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
		0x39, "Bearer capability not auhorized", ""
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
prcause(char *dest, byte * p)
{
	byte           *end;
	char           *dp = dest;
	int             i, cause;

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


#if 0
	dp += sprintf(dp,"    cause value ");
        dp += prbits(dp,*p++,7,7);
        *dp++ = '\n';
#endif
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
int
prchident(char *dest, byte * p)
{
	char           *dp = dest;

	p += 2;
	dp += sprintf(dp, "    octet 3 ");
	dp += prbits(dp, *p, 8, 8);
	*dp++ = '\n';
	return (dp - dest);
}
static
int
prcalled(char *dest, byte * p)
{
	int             l;
	char           *dp = dest;

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
static
int
prcalling(char *dest, byte * p)
{
	int             l;
	char           *dp = dest;

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
prbearer(char *dest, byte * p)
{
	char           *dp = dest, ch;

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

static
int
general(char *dest, byte * p)
{
	char           *dp = dest;
	char            ch = ' ';
	int             l, octet = 3;

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

static
int
display(char *dest, byte * p)
{
	char           *dp = dest;
	char            ch = ' ';
	int             l, octet = 3;

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
prfacility(char *dest, byte * p)
{
	char           *dp = dest;
	int             l, l2;

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
	byte            nr;
	char           *descr;
	int             (*f) (char *, byte *);
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

#ifdef FRITZDEBUG
void
hexdump(byte * buf, int len, char *comment)
{
	static char     dbuf[1024];
	char           *p = dbuf;

	p += sprintf(p, "%s: ", comment);
	while (len) {
		p += sprintf(p, "%02x ", *p++);
		len--;
	}
	p += sprintf(p, "\n");

	teles_putstatus(dbuf);
}
#endif

void
dlogframe(struct IsdnCardState *sp, byte * buf, int size, char *comment)
{
	byte           *bend = buf + size;
	char           *dp;
	int             i;

	/* display header */
	dp = sp->dlogspace;
	dp += sprintf(dp, "%s\n", comment);

	{
		byte           *p = buf;

		dp += sprintf(dp, "hex: ");
		while (p < bend)
			dp += sprintf(dp, "%02x ", *p++);
		dp += sprintf(dp, "\n");
		teles_putstatus(sp->dlogspace);
		dp = sp->dlogspace;
	}
	/* locate message type */
	for (i = 0; i < MTSIZE; i++)
		if (mtlist[i].nr == buf[3])
			break;

	/* display message type iff it exists */
	if (i == MTSIZE)
		dp += sprintf(dp, "Unknown message type %x!\n", buf[3]);
	else
		dp += sprintf(dp, "call reference %d size %d message type %s\n",
			      buf[2], size, mtlist[i].descr);

	/* display each information element */
	buf += 4;
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

		/* When not found, give apropriate msg */
		if (i != IESIZE) {
			dp += sprintf(dp, "  %s\n", ielist[i].descr);
			dp += ielist[i].f(dp, buf);
		} else
			dp += sprintf(dp, "  attribute %x attribute size %d\n", *buf, buf[1]);

		/* Skip to next element */
		buf += buf[1] + 2;
	}

	dp += sprintf(dp, "\n");
	teles_putstatus(sp->dlogspace);
}
