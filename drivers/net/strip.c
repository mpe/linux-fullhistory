/*
 * Copyright 1996 The Board of Trustees of The Leland Stanford
 * Junior University. All Rights Reserved.
 *
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  Stanford University
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 *
 * strip.c	This module implements Starmode Radio IP (STRIP)
 *		for kernel-based devices like TTY.  It interfaces between a
 *		raw TTY, and the kernel's INET protocol layers (via DDI).
 *
 * Version:	@(#)strip.c	0.9.8	June 1996
 *
 * Author:	Stuart Cheshire <cheshire@cs.stanford.edu>
 *
 * Fixes:	v0.9 12th Feb 1996.
 *		New byte stuffing (2+6 run-length encoding)
 *		New watchdog timer task
 *		New Protocol key (SIP0)
 *		
 *		v0.9.1 3rd March 1996
 *		Changed to dynamic device allocation -- no more compile
 *		time (or boot time) limit on the number of STRIP devices.
 *		
 *		v0.9.2 13th March 1996
 *		Uses arp cache lookups (but doesn't send arp packets yet)
 *		
 *		v0.9.3 17th April 1996
 *		Fixed bug where STR_ERROR flag was getting set unneccessarily
 *		
 *		v0.9.4 27th April 1996
 *		First attempt at using "&COMMAND" Starmode AT commands
 *		
 *		v0.9.5 29th May 1996
 *		First attempt at sending (unicast) ARP packets
 *		
 *		v0.9.6 5th June 1996
 *		Elliot put "message level" tags in every "printk" statement
 *		
 *		v0.9.7 13th June 1996
 *		Added support for the /proc fs (laik)
 *
 *              v0.9.8 July 1996
 *              Added packet logging (Mema)
 */

/*
 * Undefine this symbol if you don't have PROC_NET_STRIP_STATUS
 * defined in include/linux/proc_fs.h
 */

#define DO_PROC_NET_STRIP_STATUS 1

/*
 * Define this symbol if you want to enable STRIP packet tracing.
 */

#define DO_PROC_NET_STRIP_TRACE 0


/************************************************************************/
/* Header files								*/

#include <linux/config.h>

#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#endif

#include <stdlib.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>

/*
 * isdigit() and isspace() use the ctype[] array, which is not available
 * to kernel modules.  If compiling as a module,  use  a local definition
 * of isdigit() and isspace() until  _ctype is added to ksyms.
 */
#ifdef MODULE
# define isdigit(c) ('0' <= (c) && (c)  <= '9')
# define isspace(c) ((c) == ' ' || (c)  == '\t')
#else
# include <linux/ctype.h>
#endif

#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/if_strip.h>
#include <linux/proc_fs.h>
#include <net/arp.h>

#ifdef CONFIG_INET
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/time.h>
#endif


/************************************************************************/
/* Useful structures and definitions					*/

/*
 * A MetricomKey identifies the protocol being carried inside a Metricom
 * Starmode packet.
 */

typedef union
{
    __u8 c[4];
    __u32 l;
} MetricomKey;

/*
 * An IP address can be viewed as four bytes in memory (which is what it is) or as
 * a single 32-bit long (which is convenient for assignment, equality testing etc.)
 */

typedef union
{
    __u8 b[4];
    __u32 l;
} IPaddr;

/*
 * A MetricomAddressString is used to hold a printable representation of
 * a Metricom address.
 */

typedef struct
{
    __u8 c[24];
} MetricomAddressString;

/*
 * Note: A Metricom packet looks like this: *<address>*<key><payload><CR>
 * eg. *0000-1234*SIP0<payload><CR>
 * A STRIP_Header is never really sent over the radio, but making a dummy header
 * for internal use within the kernel that looks like an Ethernet header makes
 * certain other software happier. For example, tcpdump already understands
 * Ethernet headers.
 */

typedef struct
{
    MetricomAddress dst_addr;		/* Destination address, e.g. "0000-1234"   */
    MetricomAddress src_addr;		/* Source address, e.g. "0000-5678"        */
    unsigned short  protocol;		/* The protocol type, using Ethernet codes */
} STRIP_Header;

typedef struct GeographicLocation
{
    char s[18];
} GeographicLocation;

typedef enum {
    NodeValid = 0x1,
    NodeHasWAN = 0x2,
    NodeIsRouter = 0x4
} NodeType;

typedef struct MetricomNode
{
    NodeType type;                  /* Some flags about the type of node */
    GeographicLocation gl;     /* The location of the node. */
    MetricomAddress addr;      /* The metricom address of this node */
    int poll_latency;          /* The latency to poll that node ? */
    int rssi;                  /* The Receiver Signal Strength Indicator */
    struct MetricomNode *next; /* The next node */
} MetricomNode;

enum { FALSE = 0, TRUE = 1 };

/*
 * Holds the packet signature for an IP packet.
 */
typedef struct
{
    IPaddr src;
    /* Data is stored in the following field in network byte order. */
    __u16 id;
} IPSignature;

/*
 * Holds the packet signature for an ARP packet.
 */
typedef struct
{
    IPaddr src;
    /* Data is stored in the following field in network byte order. */
    __u16 op;
} ARPSignature;

/*
 * Holds the signature of a packet.
 */
typedef union
{
    IPSignature ip_sig;
    ARPSignature arp_sig;
    __u8 print_sig[6];
} PacketSignature;

typedef enum {
    EntrySend = 0,
    EntryReceive = 1
} LogEntry;

/* Structure for Packet Logging */
typedef struct stripLog
{
    LogEntry entry_type;
    u_long seqNum;
    int packet_type;
    PacketSignature sig;
    MetricomAddress src;
    MetricomAddress dest;
    struct timeval timeStamp;
    u_long rawSize;
    u_long stripSize;
    u_long slipSize;
    u_long valid;
} StripLog;

#define ENTRY_TYPE_TO_STRING(X) ((X) ? "r" : "s")

#define BOOLEAN_TO_STRING(X) ((X) ? "true" : "false")

/*
 * Holds the radio's firmware version.
 */
typedef struct
{
    char c[50];
} MetricomFirmwareVersion;

/*
 * Holds the radio's serial number.
 */
typedef struct
{
    char c[18];
} MetricomSerialNumber;

/*
 * Holds the radio's battery voltage.
 */
typedef struct
{
    char c[11];
} MetricomBatteryVoltage;

struct strip
{
    int magic;
    /*
     * These are pointers to the malloc()ed frame buffers.
     */

    unsigned char     *rx_buff;			/* buffer for received IP packet*/
    unsigned char     *sx_buff;			/* buffer for received serial data*/
    int                sx_count;		/* received serial data counter */
    int                sx_size;			/* Serial buffer size		*/
    unsigned char     *tx_buff;			/* transmitter buffer           */
    unsigned char     *tx_head;			/* pointer to next byte to XMIT */
    int                tx_left;			/* bytes left in XMIT queue     */
    int                tx_size;			/* Serial buffer size		*/

    /*
     * STRIP interface statistics.
     */

    unsigned long      rx_packets;		/* inbound frames counter	*/
    unsigned long      tx_packets;		/* outbound frames counter	*/
    unsigned long      rx_errors;		/* Parity, etc. errors		*/
    unsigned long      tx_errors;		/* Planned stuff		*/
    unsigned long      rx_dropped;		/* No memory for skb		*/
    unsigned long      tx_dropped;		/* When MTU change		*/
    unsigned long      rx_over_errors;		/* Frame bigger then STRIP buf. */

    /*
     * Internal variables.
     */

    struct strip      *next;			/* The next struct in the list	*/
    struct strip     **referrer;		/* The pointer that points to us*/
    int                discard;			/* Set if serial error		*/
    int                working;			/* Is radio working correctly?	*/
    int                structured_messages;	/* Parsable AT response msgs?	*/
    int                mtu;			/* Our mtu (to spot changes!)	*/
    long               watchdog_doprobe;	/* Next time to test the radio	*/
    long               watchdog_doreset;	/* Time to do next reset	*/
    long               gratuitous_arp;		/* Time to send next ARP refresh*/
    long               arp_interval;		/* Next ARP interval		*/
    struct timer_list  idle_timer;		/* For periodic wakeup calls	*/
    MetricomNode      *neighbor_list;		/* The list of neighbor nodes   */
    int                neighbor_list_locked;    /* Indicates the list is locked */
    MetricomFirmwareVersion firmware_version;	/* The radio's firmware version */
    MetricomSerialNumber serial_number;		/* The radio's serial number    */
    MetricomBatteryVoltage battery_voltage;     /* The radio's battery voltage  */

    /*
     * Other useful structures.
     */

    struct tty_struct *tty;			/* ptr to TTY structure		*/
    char               if_name[8];		/* Dynamically generated name	*/
    struct device      dev;			/* Our device structure		*/

    /*
     * Packet Logging Structures.
     */

    u_long             num_sent;
    u_long             num_received;

    int next_entry;                            	/* The index of the oldest packet; */
                                                /* Also the next to be logged. */
    StripLog packetLog[610];
};


/************************************************************************/
/* Constants								*/

#ifdef MODULE
static const char StripVersion[] = "0.9.8-STUART.CHESHIRE-MODULAR";
#else
static const char StripVersion[] = "0.9.8-STUART.CHESHIRE";
#endif

static const char TickleString1[] = "***&COMMAND*ATS305?\r";
static const char TickleString2[] = "***&COMMAND*ATS305?\r\r"
       "*&COMMAND*ATS300?\r\r*&COMMAND*ATS325?\r\r*&COMMAND*AT~I2 nn\r\r";

static const char            hextable[16]      = "0123456789ABCDEF";

static const MetricomAddress zero_address;
static const MetricomAddress broadcast_address = { { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF } };

static const MetricomKey     SIP0Key           = { { "SIP0" } };
static const MetricomKey     ARP0Key           = { { "ARP0" } };
static const MetricomKey     ERR_Key           = { { "ERR_" } };
static const MetricomKey     ATR_Key           = { { "ATR " } };

static const long            MaxARPInterval    = 60 * HZ;          /* One minute */

/*
 * Maximum Starmode packet length (including starmode address) is 1183 bytes.
 * Allowing 32 bytes for header, and 65/64 expansion for STRIP encoding,
 * that translates to a maximum payload MTU of 1132.
 */
static const unsigned short  MAX_STRIP_MTU          = 1132;
static const unsigned short  DEFAULT_STRIP_MTU      = 1024;
static const int             STRIP_MAGIC            = 0x5303;
static const long            LongTime               = 0x7FFFFFFF;

static const int             STRIP_NODE_LEN         = 64;
static const char            STRIP_PORTABLE_CHAR    = 'P';
static const char            STRIP_ROUTER_CHAR      = 'r';
static const int             STRIP_PROC_BUFFER_SIZE = 4096;
static const int             STRIP_LOG_INT_SIZE     = 10;

/************************************************************************/
/* Global variables							*/

static struct strip *struct_strip_list = NULL;


/************************************************************************/
/* Macros								*/

#define READHEX(X) ((X)>='0' && (X)<='9' ? (X)-'0' :      \
                    (X)>='a' && (X)<='f' ? (X)-'a'+10 :   \
                    (X)>='A' && (X)<='F' ? (X)-'A'+10 : 0 )

#define READDEC(X) ((X)>='0' && (X)<='9' ? (X)-'0' : 0)

#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))
#define ELEMENTS_OF(X) (sizeof(X) / sizeof((X)[0]))
#define ARRAY_END(X) (&((X)[ELEMENTS_OF(X)]))

/* Encapsulation can expand packet of size x to 65/64x + 1              */
/* Sent packet looks like "*<address>*<key><encaps payload><CR>"        */
/*                         1   1-18  1  4         ?         1           */
/* We allow 31 bytes for the stars, the key, the address and the <CR>   */
#define STRIP_ENCAP_SIZE(X) (32 + (X)*65L/64L)

#define IS_RADIO_ADDRESS(p) (                                                   \
    isdigit((p)[0]) && isdigit((p)[1]) && isdigit((p)[2]) && isdigit((p)[3]) && \
    (p)[4] == '-' &&                                                            \
    isdigit((p)[5]) && isdigit((p)[6]) && isdigit((p)[7]) && isdigit((p)[8])    )

#define JIFFIE_TO_SEC(X) ((X) / HZ)


/************************************************************************/
/* Utility routines							*/

typedef unsigned long InterruptStatus;

extern __inline__ InterruptStatus DisableInterrupts(void)
{
    InterruptStatus x;
    save_flags(x);
    cli();
    return(x);
}

extern __inline__ void RestoreInterrupts(InterruptStatus x)
{
    restore_flags(x);
}

static void DumpData(char *msg, struct strip *strip_info, __u8 *ptr, __u8 *end)
{
    static const int MAX_DumpData = 80;
    __u8 pkt_text[MAX_DumpData], *p = pkt_text;

    *p++ = '\"';

    while (ptr<end && p < &pkt_text[MAX_DumpData-4])
    {
        if (*ptr == '\\')
        {
            *p++ = '\\';
            *p++ = '\\';
        }
        else
        {
            if (*ptr >= 32 && *ptr <= 126)
            {
                *p++ = *ptr;
            }
            else
            {
                sprintf(p, "\\%02X", *ptr);
                p+= 3;
            }
        }
        ptr++;
    }

    if (ptr == end)
    {
        *p++ = '\"';
    }

    *p++ = 0;

    printk(KERN_INFO "%s: %-13s%s\n", strip_info->dev.name, msg, pkt_text);
}

#if 0
static void HexDump(char *msg, struct strip *strip_info, __u8 *start, __u8 *end)
{
    __u8 *ptr = start;
    printk(KERN_INFO "%s: %s: %d bytes\n", strip_info->dev.name, msg, end-ptr);

    while (ptr < end)
    {
        long offset = ptr - start;
        __u8 text[80], *p = text;
        while (ptr < end && p < &text[16*3])
        {
            *p++ = hextable[*ptr >> 4];
            *p++ = hextable[*ptr++ & 0xF];
            *p++ = ' ';
        }
        p[-1] = 0;
        printk(KERN_INFO "%s: %4lX %s\n", strip_info->dev.name, offset, text);
    }
}
#endif


/************************************************************************/
/* Byte stuffing/unstuffing routines					*/

/* Stuffing scheme:
 * 00    Unused (reserved character)
 * 01-3F Run of 2-64 different characters
 * 40-7F Run of 1-64 different characters plus a single zero at the end
 * 80-BF Run of 1-64 of the same character
 * C0-FF Run of 1-64 zeroes (ASCII 0)
 */

typedef enum
{
    Stuff_Diff      = 0x00,
    Stuff_DiffZero  = 0x40,
    Stuff_Same      = 0x80,
    Stuff_Zero      = 0xC0,
    Stuff_NoCode    = 0xFF,	/* Special code, meaning no code selected */

    Stuff_CodeMask  = 0xC0,
    Stuff_CountMask = 0x3F,
    Stuff_MaxCount  = 0x3F,
    Stuff_Magic     = 0x0D	/* The value we are eliminating */
} StuffingCode;

/* StuffData encodes the data starting at "src" for "length" bytes.
 * It writes it to the buffer pointed to by "dst" (which must be at least
 * as long as 1 + 65/64 of the input length). The output may be up to 1.6%
 * larger than the input for pathological input, but will usually be smaller.
 * StuffData returns the new value of the dst pointer as its result.
 * "code_ptr_ptr" points to a "__u8 *" which is used to hold encoding state
 * between calls, allowing an encoded packet to be incrementally built up
 * from small parts. On the first call, the "__u8 *" pointed to should be
 * initialized to NULL; between subsequent calls the calling routine should
 * leave the value alone and simply pass it back unchanged so that the
 * encoder can recover its current state.
 */

#define StuffData_FinishBlock(X) \
(*code_ptr = (X) ^ Stuff_Magic, code = Stuff_NoCode)

static __u8 *StuffData(__u8 *src, __u32 length, __u8 *dst, __u8 **code_ptr_ptr)
{
    __u8 *end = src + length;
    __u8 *code_ptr = *code_ptr_ptr;
     __u8 code = Stuff_NoCode, count = 0;

    if (!length)
        return(dst);

    if (code_ptr)
    {
        /*
         * Recover state from last call, if applicable
         */
        code  = (*code_ptr ^ Stuff_Magic) & Stuff_CodeMask;
        count = (*code_ptr ^ Stuff_Magic) & Stuff_CountMask;
    }

    while (src < end)
    {
        switch (code)
        {
            /* Stuff_NoCode: If no current code, select one */
            case Stuff_NoCode:
                /* Record where we're going to put this code */
                code_ptr = dst++;
                count = 0;    /* Reset the count (zero means one instance) */
                /* Tentatively start a new block */
                if (*src == 0)
                {
                    code = Stuff_Zero;
                    src++;
                }
                else
                {
                    code = Stuff_Same;
                    *dst++ = *src++ ^ Stuff_Magic;
                }
                /* Note: We optimistically assume run of same -- */
                /* which will be fixed later in Stuff_Same */
                /* if it turns out not to be true. */
                break;

            /* Stuff_Zero: We already have at least one zero encoded */
            case Stuff_Zero:
                /* If another zero, count it, else finish this code block */
                if (*src == 0)
                {
                    count++;
                    src++;
                }
                else
                {
                    StuffData_FinishBlock(Stuff_Zero + count);
                }
                break;

            /* Stuff_Same: We already have at least one byte encoded */
            case Stuff_Same:
                /* If another one the same, count it */
                if ((*src ^ Stuff_Magic) == code_ptr[1])
                {
                    count++;
                    src++;
                    break;
                }
                /* else, this byte does not match this block. */
                /* If we already have two or more bytes encoded, finish this code block */
                if (count)
                {
                    StuffData_FinishBlock(Stuff_Same + count);
                    break;
                }
                /* else, we only have one so far, so switch to Stuff_Diff code */
                code = Stuff_Diff;
                /* and fall through to Stuff_Diff case below */

            /* Stuff_Diff: We have at least two *different* bytes encoded */
            case Stuff_Diff:
                /* If this is a zero, must encode a Stuff_DiffZero, and begin a new block */
                if (*src == 0)
                {
                    StuffData_FinishBlock(Stuff_DiffZero + count);
                }
                /* else, if we have three in a row, it is worth starting a Stuff_Same block */
                else if ((*src ^ Stuff_Magic)==dst[-1] && dst[-1]==dst[-2])
                {
                    /* Back off the last two characters we encoded */
                    code += count-2;
                    /* Note: "Stuff_Diff + 0" is an illegal code */
                    if (code == Stuff_Diff + 0)
                    {
                        code = Stuff_Same + 0;
                    }
                    StuffData_FinishBlock(code);
                    code_ptr = dst-2;
                    /* dst[-1] already holds the correct value */
                    count = 2;        /* 2 means three bytes encoded */
                    code = Stuff_Same;
                }
                /* else, another different byte, so add it to the block */
                else
                {
                    *dst++ = *src ^ Stuff_Magic;
                    count++;
                }
                src++;    /* Consume the byte */
                break;
        }
    if (count == Stuff_MaxCount)
    {
        StuffData_FinishBlock(code + count);
    }
    }
    if (code == Stuff_NoCode)
    {
        *code_ptr_ptr = NULL;
    }
    else
    {
        *code_ptr_ptr = code_ptr;
        StuffData_FinishBlock(code + count);
    }
    return(dst);
}

/*
 * UnStuffData decodes the data at "src", up to (but not including) "end".
 * It writes the decoded data into the buffer pointed to by "dst", up to a
 * maximum of "dst_length", and returns the new value of "src" so that a
 * follow-on call can read more data, continuing from where the first left off.
 * 
 * There are three types of results:
 * 1. The source data runs out before extracting "dst_length" bytes:
 *    UnStuffData returns NULL to indicate failure.
 * 2. The source data produces exactly "dst_length" bytes:
 *    UnStuffData returns new_src = end to indicate that all bytes were consumed.
 * 3. "dst_length" bytes are extracted, with more remaining.
 *    UnStuffData returns new_src < end to indicate that there are more bytes
 *    to be read.
 * 
 * Note: The decoding may be destructive, in that it may alter the source
 * data in the process of decoding it (this is necessary to allow a follow-on
 * call to resume correctly).
 */

static __u8 *UnStuffData(__u8 *src, __u8 *end, __u8 *dst, __u32 dst_length)
{
    __u8 *dst_end = dst + dst_length;
    /* Sanity check */
    if (!src || !end || !dst || !dst_length)
        return(NULL);
    while (src < end && dst < dst_end)
    {
        int count = (*src ^ Stuff_Magic) & Stuff_CountMask;
        switch ((*src ^ Stuff_Magic) & Stuff_CodeMask)
        {
            case Stuff_Diff:
                if (src+1+count >= end)
                    return(NULL);
                do
                {
                    *dst++ = *++src ^ Stuff_Magic;
                }
                while(--count >= 0 && dst < dst_end);
                if (count < 0)
                    src += 1;
                else
                {
                    if (count == 0)
                        *src = Stuff_Same ^ Stuff_Magic;
                    else
                        *src = (Stuff_Diff + count) ^ Stuff_Magic;
                }
                break;
            case Stuff_DiffZero:
                if (src+1+count >= end)
                    return(NULL);
                do
                {
                    *dst++ = *++src ^ Stuff_Magic;
                }
                while(--count >= 0 && dst < dst_end);
                if (count < 0)
                    *src = Stuff_Zero ^ Stuff_Magic;
                else
                    *src = (Stuff_DiffZero + count) ^ Stuff_Magic;
                break;
            case Stuff_Same:
                if (src+1 >= end)
                    return(NULL);
                do
                {
                    *dst++ = src[1] ^ Stuff_Magic;
                }
                while(--count >= 0 && dst < dst_end);
                if (count < 0)
                    src += 2;
                else
                    *src = (Stuff_Same + count) ^ Stuff_Magic;
                break;
            case Stuff_Zero:
                do
                {
                    *dst++ = 0;
                }
                while(--count >= 0 && dst < dst_end);
                if (count < 0)
                    src += 1;
                else
                    *src = (Stuff_Zero + count) ^ Stuff_Magic;
                break;
        }
    }
    if (dst < dst_end)
        return(NULL);
    else
        return(src);
}


/************************************************************************/
/* General routines for STRIP						*/

/*
 * Convert a string to a Metricom Address.
 */

static void string_to_radio_address(MetricomAddress *addr, __u8 *p)
{
    addr->c[0] = 0;
    addr->c[1] = 0;
    addr->c[2] = READHEX(p[0]) << 4 | READHEX(p[1]);
    addr->c[3] = READHEX(p[2]) << 4 | READHEX(p[3]);
    addr->c[4] = READHEX(p[5]) << 4 | READHEX(p[6]);
    addr->c[5] = READHEX(p[7]) << 4 | READHEX(p[8]);
}

/*
 * Convert a Metricom Address to a string.
 */

static __u8 *radio_address_to_string(const MetricomAddress *addr, MetricomAddressString *p)
{
    sprintf(p->c, "%02X%02X-%02X%02X", addr->c[2], addr->c[3], addr->c[4], addr->c[5]);
    return(p->c);
}

/*
 * Note: Must make sure sx_size is big enough to receive a stuffed
 * MAX_STRIP_MTU packet. Additionally, we also want to ensure that it's
 * big enough to receive a large radio neighbour list (currently 4K).
 */

static int allocate_buffers(struct strip *strip_info)
{
    struct device *dev = &strip_info->dev;
    int stuffedlen = STRIP_ENCAP_SIZE(dev->mtu);
    int sx_size    = MAX(stuffedlen, 4096);
    int tx_size    = stuffedlen + sizeof(TickleString2);
    __u8 *r = kmalloc(MAX_STRIP_MTU, GFP_ATOMIC);
    __u8 *s = kmalloc(sx_size,       GFP_ATOMIC);
    __u8 *t = kmalloc(tx_size,       GFP_ATOMIC);
    if (r && s && t)
    {
        strip_info->rx_buff = r;
        strip_info->sx_buff = s;
        strip_info->tx_buff = t;
        strip_info->sx_size = sx_size;
        strip_info->tx_size = tx_size;
        strip_info->mtu     = dev->mtu;
        return(1);
    }
    if (r) kfree(r);
    if (s) kfree(s);
    if (t) kfree(t);
    return(0);
}

/*
 * MTU has been changed by the IP layer. Unfortunately we are not told
 * about this, but we spot it ourselves and fix things up. We could be in
 * an upcall from the tty driver, or in an ip packet queue.
 */

static void strip_changedmtu(struct strip *strip_info)
{
    int old_mtu           = strip_info->mtu;
    struct device *dev    = &strip_info->dev;
    unsigned char *orbuff = strip_info->rx_buff;
    unsigned char *osbuff = strip_info->sx_buff;
    unsigned char *otbuff = strip_info->tx_buff;
    InterruptStatus intstat;

    if (dev->mtu > MAX_STRIP_MTU)
    {
        printk(KERN_ERR "%s: MTU exceeds maximum allowable (%d), MTU change cancelled.\n",
            strip_info->dev.name, MAX_STRIP_MTU);
        dev->mtu = old_mtu;
        return;
    }

    /*
     * Have to disable interrupts here because we're reallocating and resizing
     * the serial buffers, and we can't have data arriving in them while we're
     * moving them around in memory. This may cause data to be lost on the serial
     * port, but hopefully people won't change MTU that often.
     * Also note, this may not work on a symmetric multi-processor system.
     */
    intstat = DisableInterrupts();

    if (!allocate_buffers(strip_info))
    {
        RestoreInterrupts(intstat);
        printk(KERN_ERR "%s: unable to grow strip buffers, MTU change cancelled.\n",
            strip_info->dev.name);
        dev->mtu = old_mtu;
        return;
    }

    if (strip_info->sx_count)
    {
        if (strip_info->sx_count <= strip_info->sx_size)
            memcpy(strip_info->sx_buff, osbuff, strip_info->sx_count);
        else
        {
            strip_info->sx_count = 0;
            strip_info->rx_over_errors++;
            strip_info->discard = 1;
        }
    }

    if (strip_info->tx_left)
    {
        if (strip_info->tx_left <= strip_info->tx_size)
            memcpy(strip_info->tx_buff, strip_info->tx_head, strip_info->tx_left);
        else
        {
            strip_info->tx_left = 0;
            strip_info->tx_dropped++;
        }
    }
    strip_info->tx_head = strip_info->tx_buff;

    RestoreInterrupts(intstat);

    printk(KERN_NOTICE "%s: strip MTU changed fom %d to %d.\n",
        strip_info->dev.name, old_mtu, strip_info->mtu);

    if (orbuff) kfree(orbuff);
    if (osbuff) kfree(osbuff);
    if (otbuff) kfree(otbuff);
}

static void strip_unlock(struct strip *strip_info)
{
    /*
     * Set the time to go off in one second.
     */
    strip_info->idle_timer.expires  = jiffies + HZ;
    add_timer(&strip_info->idle_timer);
    if (!clear_bit(0, (void *)&strip_info->dev.tbusy))
        printk(KERN_ERR "%s: trying to unlock already unlocked device!\n",
            strip_info->dev.name);
}


/************************************************************************/
/* Callback routines for exporting information through /proc		*/

#if DO_PROC_NET_STRIP_STATUS | DO_PROC_NET_STRIP_TRACE

/*
 * This function updates the total amount of data printed so far. It then
 * determines if the amount of data printed into a buffer  has reached the
 * offset requested. If it hasn't, then the buffer is shifted over so that
 * the next bit of data can be printed over the old bit. If the total
 * amount printed so far exceeds the total amount requested, then this
 * function returns 1, otherwise 0.
 */
static int 
shift_buffer(char *buffer, int requested_offset, int requested_len,
	     int *total, int *slop, char **buf)
{
    int printed;

    /* printk(KERN_DEBUG "shift: buffer: %d o: %d l: %d t: %d buf: %d\n",
	   (int) buffer, requested_offset, requested_len, *total,
	   (int) *buf); */
    printed = *buf - buffer;
    if (*total + printed <= requested_offset) {
	*total += printed;
	*buf = buffer;
    }
    else {
	if (*total < requested_offset) {
	    *slop = requested_offset - *total;
	}
	*total = requested_offset + printed - *slop;
    }
    if (*total > requested_offset + requested_len) {
	return 1;
    }
    else {
	return 0;
    }
}

/*
 * This function calculates the actual start of the requested data
 * in the buffer. It also calculates actual length of data returned,
 * which could be less that the amount of data requested.
 */
static int
calc_start_len(char *buffer, char **start, int requested_offset,
	       int requested_len, int total, char *buf)
{
    int return_len, buffer_len;

    buffer_len = buf - buffer;
    if (buffer_len >= STRIP_PROC_BUFFER_SIZE - 1) {
 	printk(KERN_ERR "STRIP: exceeded /proc buffer size\n");
    }

    /*
     * There may be bytes before and after the
     * chunk that was actually requested.
     */
    return_len = total - requested_offset;
    if (return_len < 0) {
	return_len = 0;
    }
    *start = buf - return_len;
    if (return_len > requested_len) {
	return_len = requested_len;
    }
    /* printk(KERN_DEBUG "return_len: %d\n", return_len); */
    return return_len;
}

#endif DO_PROC_NET_STRIP_STATUS | DO_PROC_NET_STRIP_TRACE

#if DO_PROC_NET_STRIP_STATUS

/*
 * If the time is in the near future, time_delta prints the number of
 * seconds to go into the buffer and returns the address of the buffer.
 * If the time is not in the near future, it returns the address of the
 * string "Not scheduled" The buffer must be long enough to contain the
 * ascii representation of the number plus 9 charactes for the " seconds"
 * and the null character.
 */
static char *time_delta(char buffer[], long time)
{
    time -= jiffies;
    if (time > LongTime / 2) return("Not scheduled");
    if(time < 0) time = 0;  /* Don't print negative times */
    sprintf(buffer, "%ld seconds", time / HZ);
    return(buffer);
}

/*
 * This function prints radio status information into the specified
 * buffer.
 */
static int
sprintf_status_info(char *buffer, struct strip *strip_info)
{
    char temp_buffer[32];
    MetricomAddressString addr_string;
    char *buf;

    buf = buffer;
    buf += sprintf(buf, "Interface name\t\t%s\n", strip_info->if_name);
    buf += sprintf(buf, " Radio working:\t\t%s\n",
		   strip_info->working &&
                   (long)jiffies - strip_info->watchdog_doreset < 0 ? "Yes" : "No");
    (void) radio_address_to_string((MetricomAddress *)
				   &strip_info->dev.dev_addr,
				   &addr_string);
    buf += sprintf(buf, " Device address:\t%s\n", addr_string.c);
    buf += sprintf(buf, " Firmware version:\t%s\n",
		   !strip_info->working             ? "Unknown" :
		   !strip_info->structured_messages ? "Should be upgraded" :
		   strip_info->firmware_version.c);
    buf += sprintf(buf, " Serial number:\t\t%s\n", strip_info->serial_number.c);
    buf += sprintf(buf, " Battery voltage:\t%s\n", strip_info->battery_voltage.c);
    buf += sprintf(buf, " Transmit queue (bytes):%d\n", strip_info->tx_left);
    buf += sprintf(buf, " Next watchdog probe:\t%s\n",
		   time_delta(temp_buffer, strip_info->watchdog_doprobe));
    buf += sprintf(buf, " Next watchdog reset:\t%s\n",
		   time_delta(temp_buffer, strip_info->watchdog_doreset));
    buf += sprintf(buf, " Next gratuitous ARP:\t%s\n",
		   time_delta(temp_buffer, strip_info->gratuitous_arp));
    buf += sprintf(buf, " Next ARP interval:\t%ld seconds\n",
		   JIFFIE_TO_SEC(strip_info->arp_interval));
    return buf - buffer;
}

static int
sprintf_portables(char *buffer, struct strip *strip_info)
{

    MetricomAddressString addr_string;
    MetricomNode          *node;
    char *buf;

    buf = buffer;
    buf += sprintf(buf, " portables: name\t\tpoll_latency\tsignal strength\n");
    for (node = strip_info->neighbor_list; node != NULL;
	 node = node->next) {
	if (!(node->type & NodeValid)) {
	    break;
	}
	if (node->type & NodeHasWAN) {
	    continue;
	}
	(void) radio_address_to_string(&node->addr, &addr_string);
	buf += sprintf(buf, "  %s\t\t\t\t%d\t\t%d\n",
		       addr_string.c, node->poll_latency, node->rssi);
    }
    return buf - buffer;
}

static int
sprintf_poletops(char *buffer, struct strip *strip_info)
{
    MetricomNode    *node;
    char *buf;

    buf = buffer;
    buf += sprintf(buf, " poletops: GPS\t\t\tpoll_latency\tsignal strength\n");
    for (node = strip_info->neighbor_list;
	 node != NULL; node = node->next) {
	if (!(node->type & NodeValid)) {
	    break;
	}
	if (!(node->type & NodeHasWAN)) {
	    continue;
	}
	buf += sprintf(buf, "  %s\t\t\t%d\t\t%d\n",
		       node->gl.s, node->poll_latency, node->rssi);
    }
    return buf - buffer;
}

/*
 * This function is exports status information from the STRIP driver through
 * the /proc file system. /proc filesystem should be fixed:
 *    1) slow (sprintfs here, a memory copy in the proc that calls this one)
 *    2) length of buffer not passed
 *    3) dummy isn't client data set when the callback was registered
 *    4) poorly documented (this function is called until the requested amount
 *       of data is returned, buffer is only 4K long, dummy is the permissions
 *       of the file (?), the proc_dir_entry passed to proc_net_register must
 *       be kmalloc-ed)
 */

static int
strip_get_status_info(char *buffer, char **start, off_t requested_offset,
		      int requested_len, int dummy)
{
    char            *buf;
    int             total = 0, slop = 0, len_exceeded;
    InterruptStatus i_status;
    struct strip    *strip_info;

    buf = buffer;
    buf += sprintf(buf, "strip_version: %s\n", StripVersion);

    i_status = DisableInterrupts();
    strip_info = struct_strip_list;
    RestoreInterrupts(i_status);

    while (strip_info != NULL) {
	i_status = DisableInterrupts();
	buf += sprintf_status_info(buf, strip_info);
	RestoreInterrupts(i_status);
	len_exceeded = shift_buffer(buffer, requested_offset, requested_len,
				    &total, &slop, &buf);
	if (len_exceeded) {
	    goto done;
	}
	strip_info->neighbor_list_locked = TRUE;
	buf += sprintf_portables(buf, strip_info);
	strip_info->neighbor_list_locked = FALSE;
	len_exceeded = shift_buffer(buffer, requested_offset, requested_len,
				    &total, &slop, &buf);
	if (len_exceeded) {
	    goto done;
	}
	strip_info->neighbor_list_locked = TRUE;
	buf += sprintf_poletops(buf, strip_info);
	strip_info->neighbor_list_locked = FALSE;
	len_exceeded = shift_buffer(buffer, requested_offset, requested_len,
				    &total, &slop, &buf);
	if (len_exceeded) {
	    goto done;
	}
	strip_info = strip_info->next;
    }
done:
    return calc_start_len(buffer, start, requested_offset, requested_len,
				total, buf);
}

#endif DO_PROC_NET_STRIP_STATUS

#if DO_PROC_NET_STRIP_TRACE

/*
 * Convert an Ethernet protocol to a string
 * Returns the number of characters printed.
 */

static int protocol_to_string(int protocol, __u8 *p)
{
    int printed;

    switch (protocol) {
    case ETH_P_IP:
	printed = sprintf(p, "IP");
	break;
    case ETH_P_ARP:
	printed = sprintf(p, "ARP");
	break;
    default: 
	printed = sprintf(p, "%d", protocol);
    }
    return printed;
}

static int
sprintf_log_entry(char *buffer, struct strip *strip_info, int packet_index)
{
    StripLog *entry;
    MetricomAddressString addr_string;
    __u8     sig_buf[24], *s;
    char     *buf, proto_buf[10];

    entry = &strip_info->packetLog[packet_index];
    if (!entry->valid) {
	return 0;
    }
    buf = buffer;
    buf += sprintf(buf, "%-4s %s   %7lu ", strip_info->if_name,
		   ENTRY_TYPE_TO_STRING(entry->entry_type), entry->seqNum);
    (void) protocol_to_string(entry->packet_type, proto_buf);
    buf += sprintf(buf, "%-4s", proto_buf);
    s = entry->sig.print_sig;
    sprintf(sig_buf, "%d.%d.%d.%d.%d.%d", s[0], s[1], s[2], s[3], s[4], s[5]);
    buf += sprintf(buf, "%-24s", sig_buf);
    (void) radio_address_to_string((MetricomAddress *) &entry->src,
				   &addr_string);
    buf += sprintf(buf, "%-10s", addr_string.c);
    (void) radio_address_to_string((MetricomAddress *) &entry->dest,
				   &addr_string);
    buf += sprintf(buf, "%-10s", addr_string.c);
    buf += sprintf(buf, "%8d %6d %5lu %6lu %5lu\n", entry->timeStamp.tv_sec,
		   entry->timeStamp.tv_usec, entry->rawSize,
		   entry->stripSize, entry->slipSize);
    return buf - buffer;
}

/*
 * This function exports trace information from the STRIP driver through the
 * /proc file system.
 */

static int
strip_get_trace_info(char *buffer, char **start, off_t requested_offset,
		     int requested_len, int dummy)
{
    char            *buf;
    int             len_exceeded, total = 0, slop = 0, packet_index, oldest;
    InterruptStatus i_status;
    struct strip    *strip_info;

    buf = buffer;
    buf += sprintf(buf, "if   s/r seqnum  t   signature               ");
    buf += sprintf(buf,
		   "src       dest      sec      usec   raw   strip  slip\n");

    i_status = DisableInterrupts();
    strip_info = struct_strip_list;
    oldest = strip_info->next_entry;
    RestoreInterrupts(i_status);

    /*
     * If we disable interrupts for this entire loop,
     * characters from the serial port could be lost,
     * so we only disable interrupts when accessing
     * a log entry. If more than STRIP_LOG_INT_SIZE
     * packets are logged before the first entry is
     * printed, then some of the entries could be
     * printed out of order.
     */
    while (strip_info != NULL) {
	for (packet_index = oldest + STRIP_LOG_INT_SIZE;
	     packet_index != oldest;
	     packet_index = (packet_index + 1) %
		 ELEMENTS_OF(strip_info->packetLog)) {
	    i_status = DisableInterrupts();
	    buf += sprintf_log_entry(buf, strip_info, packet_index);
	    RestoreInterrupts(i_status);
	    len_exceeded = shift_buffer(buffer, requested_offset,
					requested_len, &total, &slop, &buf);
	    if (len_exceeded) {
		goto done;
	    }
	}
	strip_info = strip_info->next;
    }
done:
    return calc_start_len(buffer, start, requested_offset, requested_len,
			  total, buf);
}

static int slip_len(unsigned char *data, int len)
{
    static const unsigned char SLIP_END=0300;	/* indicates end of SLIP frame	*/
    static const unsigned char SLIP_ESC=0333;	/* indicates SLIP byte stuffing	*/
    int count = len;
    while (--len >= 0)
    {
	if (*data == SLIP_END || *data == SLIP_ESC) count++;
	data++;
    }
    return(count);
}

/* Copied from kernel/sched.c */
static void jiffiestotimeval(unsigned long jiffies, struct timeval *value)
{
    value->tv_usec = (jiffies % HZ) * (1000000.0 / HZ);
    value->tv_sec = jiffies / HZ;
    return;
}

/*
 * This function logs a packet.
 * A pointer to the packet itself is passed so that some of the data can be
 * used to compute a signature. The pointer should point the the
 * part of the packet following the STRIP_header.
 */

static void packet_log(struct strip *strip_info, __u8 *packet,
		       LogEntry entry_type, STRIP_Header *hdr, 
		       int raw_size, int strip_size, int slip_size)
{
    StripLog *entry;
    struct iphdr *iphdr;
    struct arphdr *arphdr;

    entry = &strip_info->packetLog[strip_info->next_entry];
    if (entry_type == EntrySend) {
	entry->seqNum = strip_info->num_sent++;
    }
    else {
	entry->seqNum = strip_info->num_received++;
    }
    entry->entry_type = entry_type;
    entry->packet_type = ntohs(hdr->protocol);
    switch (entry->packet_type) {
    case ETH_P_IP:
	/*
	 * The signature for IP is the sender's ip address and
	 * the identification field.
	 */
	iphdr = (struct iphdr *) packet;
	entry->sig.ip_sig.id = iphdr->id;
	entry->sig.ip_sig.src.l = iphdr->saddr;
	break;
    case ETH_P_ARP:
	/*
	 * The signature for ARP is the sender's ip address and
	 * the operation.
	 */
	arphdr = (struct arphdr *) packet;
	entry->sig.arp_sig.op = arphdr->ar_op;
        memcpy(&entry->sig.arp_sig.src.l, packet + 8 + arphdr->ar_hln,
	       sizeof(entry->sig.arp_sig.src.l));
        entry->sig.arp_sig.src.l = entry->sig.arp_sig.src.l;
	break;
    default:
	printk(KERN_DEBUG "STRIP: packet_log: unknown packet type: %d\n",
	       entry->packet_type);
	break;
    }
    memcpy(&entry->src, &hdr->src_addr, sizeof(MetricomAddress));
    memcpy(&entry->dest, &hdr->dst_addr, sizeof(MetricomAddress));

    jiffiestotimeval(jiffies, &(entry->timeStamp));
    entry->rawSize = raw_size;
    entry->stripSize = strip_size;
    entry->slipSize = slip_size;
    entry->valid = 1;

    strip_info->next_entry = (strip_info->next_entry + 1) %
	ELEMENTS_OF(strip_info->packetLog);
}

#endif DO_PROC_NET_STRIP_TRACE

/*
 * This function parses the response to the ATS300? command,
 * extracting the radio version and serial number.
 */
static void get_radio_version(struct strip *strip_info, __u8 *ptr, __u8 *end)
{
    __u8 *p, *value_begin, *value_end;
    int len;
    
    /* Determine the beginning of the second line of the payload */
    p = ptr;
    while (p < end && *p != 10) p++;
    if (p >= end) return;
    p++;
    value_begin = p;
    
    /* Determine the end of line */
    while (p < end && *p != 10) p++;
    if (p >= end) return;
    value_end = p;
    p++;
     
    len = value_end - value_begin;
    len = MIN(len, sizeof(MetricomFirmwareVersion) - 1);
    sprintf(strip_info->firmware_version.c, "%.*s", len, value_begin);
    
    /* Look for the first colon */
    while (p < end && *p != ':') p++;
    if (p >= end) return;
    /* Skip over the space */
    p += 2;
    len = sizeof(MetricomSerialNumber) - 1;
    if (p + len <= end) {
	sprintf(strip_info->serial_number.c, "%.*s", len, p);
    }
    else {
     	printk(KERN_ERR "STRIP: radio serial number shorter (%d) than expected (%d)\n",
     	       end - p, len);
    }
}

/*
 * This function parses the response to the ATS325? command,
 * extracting the radio battery voltage.
 */
static void get_radio_voltage(struct strip *strip_info, __u8 *ptr, __u8 *end)
{
    int len;

    len = sizeof(MetricomBatteryVoltage) - 1;
    if (ptr + len <= end) {
	sprintf(strip_info->battery_voltage.c, "%.*s", len, ptr);
    }
    else {
 	printk(KERN_ERR "STRIP: radio voltage string shorter (%d) than expected (%d)\n",
 	       end - ptr, len);
    }
}

/*
 * This function parses the response to the AT~I2 command,
 * which gives the names of the radio's nearest neighbors.
 * It relies on the format of the response.
 */
static void get_radio_neighbors(struct strip *strip_info, __u8 *ptr, __u8 *end)
{
    __u8 *p, *line_begin;
    int num_nodes_reported, num_nodes_counted;
    MetricomNode *node, *last;

    /* Check if someone is reading the list */
    if (strip_info->neighbor_list_locked) {
	return;
    }

    /* Determine the number of Nodes */
    p = ptr;
    num_nodes_reported = simple_strtoul(p, NULL, 10);
    /* printk(KERN_DEBUG "num_nodes: %d\n", num_nodes_reported); */

    /* Determine the beginning of the next line */
    while (p < end && *p != 10) p++;
    if (p >= end) return;
    p++;

    /*
     * The node list should never be empty because we allocate one empty
     * node when the strip_info is allocated. The nodes which were allocated
     * when the number of neighbors was high but are no longer needed because
     * there aren't as many neighbors any more are marked invalid. Invalid nodes
     * are kept at the end of the list.
     */
    node = strip_info->neighbor_list;
    last = node;
    if (node == NULL) {
	DumpData("Neighbor list is NULL:", strip_info, p, end);
	return;
    }	
    line_begin = p;
    num_nodes_counted = 0;
    while (line_begin < end) {
 	/* Check to see if the format is what we expect. */
 	if ((line_begin + STRIP_NODE_LEN) > end) {
 	    printk(KERN_ERR "STRIP: radio neighbor node string shorter (%d) than expected (%d)\n",
 	       end - line_begin, STRIP_NODE_LEN);
	    break;
 	}

	/* Get a node */
	if (node == NULL) {
	    node = kmalloc(sizeof(MetricomNode), GFP_ATOMIC);
	    node->next = NULL;
	}
	node->type = NodeValid;

	/* Fill the node in */

	/* Determine if it has a GPS location and fill it in if it does. */
	p = line_begin;
	/* printk(KERN_DEBUG "node: %64s\n", p); */
	if (p[0] != STRIP_PORTABLE_CHAR) {
	    node->type |= NodeHasWAN;
	    sprintf(node->gl.s, "%.*s", (int) sizeof(GeographicLocation) - 1, p);
	}
	
	/* Determine if it is a router */
	p = line_begin + 18;
	if (p[0] == STRIP_ROUTER_CHAR) {
	    node->type |= NodeIsRouter;
	}

	/* Could be a radio address or some weird poletop address. */
	p = line_begin + 20;
	/* printk(KERN_DEBUG "before addr: %6s\n", p); */
	string_to_radio_address(&node->addr, p);
	/* radio_address_to_string(&node->addr, addr_string);
	printk(KERN_DEBUG "after addr: %s\n", addr_string);  */
	
	if (IS_RADIO_ADDRESS(p)) {
	    string_to_radio_address(&node->addr, p);
	}
	else {
	    memset(&node->addr, 0, sizeof(MetricomAddress));
	}
	
	/* Get the poll latency. %$#!@ simple_strtoul can't skip white space */
	p = line_begin + 41;
	while (isspace(*p) && (p < end)) {
	    p++;
	}
	node->poll_latency = simple_strtoul(p, NULL, 10);
	
	/* Get the signal strength. simple_strtoul doesn't do minus signs */
	p = line_begin + 60;
	node->rssi = -simple_strtoul(p, NULL, 10);
	
	if (last != node) {
	    last->next = node;
	    last = node;
	}
	node = node->next;
	line_begin += STRIP_NODE_LEN;
	num_nodes_counted++;
    }

    /* invalidate all remaining nodes */
    for (;node != NULL; node = node->next) {
	node->type &= ~NodeValid;
    }

    /*
     * If the number of nodes reported is different
     * from the number counted, might need to up the number
     * requested.
     */
    if (num_nodes_reported != num_nodes_counted) {
	printk(KERN_DEBUG "nodes reported: %d \tnodes counted: %d\n",
	       num_nodes_reported, num_nodes_counted);
    }	
}


/************************************************************************/
/* Sending routines							*/

static void ResetRadio(struct strip *strip_info)
{	
    static const char InitString[] = "\rat\r\rate0q1dt**starmode\r\r**";

    /* If the radio isn't working anymore, we should clear the old status information. */
    if (strip_info->working)
    {
        printk(KERN_INFO "%s: No response: Resetting radio.\n", strip_info->dev.name);
        strip_info->firmware_version.c[0] = '\0';
        strip_info->serial_number.c[0] = '\0';
        strip_info->battery_voltage.c[0] = '\0';
    }
    /* Mark radio address as unknown */
    *(MetricomAddress*)&strip_info->dev.dev_addr = zero_address;
    strip_info->working = FALSE;
    strip_info->structured_messages = FALSE;
    strip_info->watchdog_doprobe = jiffies + 10 * HZ;
    strip_info->watchdog_doreset = jiffies + 1 * HZ;
    strip_info->tty->driver.write(strip_info->tty, 0, (char *)InitString, sizeof(InitString)-1);
}

/*
 * Called by the driver when there's room for more data.  If we have
 * more packets to send, we send them here.
 */

static void strip_write_some_more(struct tty_struct *tty)
{
    struct strip *strip_info = (struct strip *) tty->disc_data;

    /* First make sure we're connected. */
    if (!strip_info || strip_info->magic != STRIP_MAGIC || !strip_info->dev.start)
        return;

    if (strip_info->tx_left > 0)
    {
        /*
         * If some data left, send it
         * Note: There's a kernel design bug here. The write_wakeup routine has to
         * know how many bytes were written in the previous call, but the number of
         * bytes written is returned as the result of the tty->driver.write call,
         * and there's no guarantee that the tty->driver.write routine will have
         * returned before the write_wakeup routine is invoked. If the PC has fast
         * Serial DMA hardware, then it's quite possible that the write could complete
         * almost instantaneously, meaning that my write_wakeup routine could be
         * called immediately, before tty->driver.write has had a chance to return
         * the number of bytes that it wrote. In an attempt to guard against this,
         * I disable interrupts around the call to tty->driver.write, although even
         * this might not work on a symmetric multi-processor system.
         */
        InterruptStatus intstat = DisableInterrupts();
        int num_written = tty->driver.write(tty, 0, strip_info->tx_head, strip_info->tx_left);
        strip_info->tx_left -= num_written;
        strip_info->tx_head += num_written;
        RestoreInterrupts(intstat);
    }
    else            /* Else start transmission of another packet */
    {
        tty->flags &= ~(1 << TTY_DO_WRITE_WAKEUP);
        strip_unlock(strip_info);
        mark_bh(NET_BH);
    }
}

static unsigned char *strip_make_packet(unsigned char *ptr, struct strip *strip_info, struct sk_buff *skb)
{
#if DO_PROC_NET_STRIP_TRACE
    unsigned char *start_ptr;
#endif DO_PROC_NET_STRIP_TRACE

    __u8           *stuffstate = NULL;
    STRIP_Header   *header     = (STRIP_Header *)skb->data;
    MetricomAddress haddr      = header->dst_addr;
    int             len        = skb->len - sizeof(STRIP_Header);
    MetricomKey     key;

    /*HexDump("strip_make_packet", strip_info, skb->data, skb->data + skb->len);*/

    if      (header->protocol == htons(ETH_P_IP))  key = SIP0Key;
    else if (header->protocol == htons(ETH_P_ARP)) key = ARP0Key;
    else
    {
        printk(KERN_ERR "%s: strip_make_packet: Unknown packet type 0x%04X\n",
            strip_info->dev.name, ntohs(header->protocol));
        strip_info->tx_dropped++;
        return(NULL);
    }

    if (len > strip_info->mtu)
    {
        printk(KERN_ERR "%s: Dropping oversized transmit packet: %d bytes\n",
            strip_info->dev.name, len);
        strip_info->tx_dropped++;
        return(NULL);
    }

    /*
     * If this is a broadcast packet, send it to our designated Metricom
     * 'broadcast hub' radio (First byte of address being 0xFF means broadcast)
     */
    if (haddr.c[0] == 0xFF)
    {
	    memcpy(haddr.c, strip_info->dev.broadcast, sizeof(haddr));
	    if (haddr.c[0] == 0xFF)
	    {
		    strip_info->tx_dropped++;
		    return(NULL);
	    }
    }

    *ptr++ = '*';
    *ptr++ = hextable[haddr.c[2] >> 4];
    *ptr++ = hextable[haddr.c[2] & 0xF];
    *ptr++ = hextable[haddr.c[3] >> 4];
    *ptr++ = hextable[haddr.c[3] & 0xF];
    *ptr++ = '-';
    *ptr++ = hextable[haddr.c[4] >> 4];
    *ptr++ = hextable[haddr.c[4] & 0xF];
    *ptr++ = hextable[haddr.c[5] >> 4];
    *ptr++ = hextable[haddr.c[5] & 0xF];
    *ptr++ = '*';
    *ptr++ = key.c[0];
    *ptr++ = key.c[1];
    *ptr++ = key.c[2];
    *ptr++ = key.c[3];

#if DO_PROC_NET_STRIP_TRACE
    start_ptr = ptr;
#endif DO_PROC_NET_STRIP_TRACE

    ptr = StuffData(skb->data + sizeof(STRIP_Header), len, ptr, &stuffstate);

#if DO_PROC_NET_STRIP_TRACE
    packet_log(strip_info, skb->data + sizeof(STRIP_Header), EntrySend,
	       header, len, ptr-start_ptr,
	       slip_len(skb->data + sizeof(STRIP_Header), len)); 
#endif DO_PROC_NET_STRIP_TRACE

    *ptr++ = 0x0D;
    return(ptr);
}

static void strip_send(struct strip *strip_info, struct sk_buff *skb)
{
    unsigned char *ptr = strip_info->tx_buff;

    /* If we have a packet, encapsulate it and put it in the buffer */
    if (skb)
    {
        ptr = strip_make_packet(ptr, strip_info, skb);
        /* If error, unlock and return */
        if (!ptr) { strip_unlock(strip_info); return; }
        strip_info->tx_packets++;        /* Count another successful packet */
        /*DumpData("Sending:", strip_info, strip_info->tx_buff, ptr);*/
        /*HexDump("Sending", strip_info, strip_info->tx_buff, ptr);*/
    }

    /* Set up the strip_info ready to send the data */
    strip_info->tx_head =       strip_info->tx_buff;
    strip_info->tx_left = ptr - strip_info->tx_buff;
    strip_info->tty->flags |= (1 << TTY_DO_WRITE_WAKEUP);

    /* If watchdog has expired, reset the radio */
    if ((long)jiffies - strip_info->watchdog_doreset >= 0)
    {
        ResetRadio(strip_info);
        return;
        /* Note: if there's a packet to send, strip_write_some_more
                 will do it after the reset has finished */
    }

    /* No reset.
     * If it is time for another tickle, tack it on the end of the packet
     */
    if ((long)jiffies - strip_info->watchdog_doprobe >= 0)
    {
        /* Send tickle to make radio protest */
        /*printk(KERN_INFO "%s: Routine radio test.\n", strip_info->dev.name);*/
        const char *TickleString = TickleString1;
        int length = sizeof(TickleString1)-1;
        if (strip_info->structured_messages)
        {
            TickleString = TickleString2;
            length = sizeof(TickleString2)-1;
        }
        memcpy(ptr, TickleString, length);
        strip_info->tx_left += length;
        strip_info->watchdog_doprobe = jiffies + 10 * HZ;
        strip_info->watchdog_doreset = jiffies + 1 * HZ;
    }

    /*
     * If it is time for a periodic ARP, queue one up to be sent
     */
    if (strip_info->working && (long)jiffies - strip_info->gratuitous_arp >= 0 &&
        memcmp(strip_info->dev.dev_addr, zero_address.c, sizeof(zero_address)))
    {
        /*printk(KERN_INFO "%s: Sending gratuitous ARP with interval %ld\n",
            strip_info->dev.name, strip_info->arp_interval / HZ);*/
        strip_info->gratuitous_arp = jiffies + strip_info->arp_interval;
        strip_info->arp_interval *= 2;
        if (strip_info->arp_interval > MaxARPInterval)
            strip_info->arp_interval = MaxARPInterval;
        arp_send(ARPOP_REPLY, ETH_P_ARP, strip_info->dev.pa_addr,
                        &strip_info->dev, strip_info->dev.pa_addr,
                        NULL, strip_info->dev.dev_addr, NULL);
    }

    if (strip_info->tx_size - strip_info->tx_left < 20)
        printk(KERN_ERR "%s: Sending%5d bytes;%5d bytes free.\n", strip_info->dev.name,
            strip_info->tx_left, strip_info->tx_size - strip_info->tx_left);

    /* All ready. Start the transmission */
    strip_write_some_more(strip_info->tty);
}

/* Encapsulate a datagram and kick it into a TTY queue. */
static int strip_xmit(struct sk_buff *skb, struct device *dev)
{
    struct strip *strip_info = (struct strip *)(dev->priv);

    if (!dev->start)
    {
        printk(KERN_ERR "%s: xmit call when iface is down\n", dev->name);
        return(1);
    }
    if (set_bit(0, (void *) &strip_info->dev.tbusy)) return(1);
    del_timer(&strip_info->idle_timer);

    /* See if someone has been ifconfigging */
    if (strip_info->mtu != strip_info->dev.mtu)
        strip_changedmtu(strip_info);

    strip_send(strip_info, skb);

    if (skb) dev_kfree_skb(skb, FREE_WRITE);
    return(0);
}

/*
 * Create the MAC header for an arbitrary protocol layer
 *
 * saddr!=NULL        means use this specific address (n/a for Metricom)
 * saddr==NULL        means use default device source address
 * daddr!=NULL        means use this destination address
 * daddr==NULL        means leave destination address alone
 *                 (e.g. unresolved arp -- kernel will call
 *                 rebuild_header later to fill in the address)
 */

static int strip_header(struct sk_buff *skb, struct device *dev,
        unsigned short type, void *daddr, void *saddr, unsigned len)
{
    STRIP_Header *header = (STRIP_Header *)skb_push(skb, sizeof(STRIP_Header));

    /*printk(KERN_INFO "%s: strip_header 0x%04X %s\n", dev->name, type,
        type == ETH_P_IP ? "IP" : type == ETH_P_ARP ? "ARP" : "");*/

    memcpy(header->src_addr.c, dev->dev_addr, dev->addr_len);
    header->protocol = htons(type);

    /*HexDump("strip_header", (struct strip *)(dev->priv), skb->data, skb->data + skb->len);*/

    if (!daddr) return(-dev->hard_header_len);

    memcpy(header->dst_addr.c, daddr, dev->addr_len);
    return(dev->hard_header_len);
}

/*
 * Rebuild the MAC header. This is called after an ARP
 * (or in future other address resolution) has completed on this
 * sk_buff. We now let ARP fill in the other fields.
 * I think this should return zero if packet is ready to send,
 * or non-zero if it needs more time to do an address lookup
 */

static int strip_rebuild_header(struct sk_buff *skb)
{
    STRIP_Header *header = (STRIP_Header *)skb->data;

    /*printk(KERN_INFO "%s: strip_rebuild_header\n", skb->dev->name);*/

#ifdef CONFIG_INET
    /* Arp find returns zero if if knows the address, */
    /* or if it doesn't know the address it sends an ARP packet and returns non-zero */
    return arp_find(header->dst_addr.c, skb)? 1 : 0;
#else
    return 0;
#endif
}

/*
 * IdleTask periodically calls strip_xmit, so even when we have no IP packets
 * to send for an extended period of time, the watchdog processing still gets
 * done to ensure that the radio stays in Starmode
 */

static void strip_IdleTask(unsigned long parameter)
{
    strip_xmit(NULL, (struct device *)parameter);
}


/************************************************************************/
/* Receiving routines							*/

static int strip_receive_room(struct tty_struct *tty)
{
    return 0x10000;  /* We can handle an infinite amount of data. :-) */
}

static void get_radio_address(struct strip *strip_info, __u8 *p)
{
    MetricomAddress addr;

    string_to_radio_address(&addr, p);

    /* See if our radio address has changed */
    if (memcmp(strip_info->dev.dev_addr, addr.c, sizeof(addr)))
    {
        MetricomAddressString addr_string;
        radio_address_to_string(&addr, &addr_string);
        printk(KERN_INFO "%s: My radio address = %s\n", strip_info->dev.name, addr_string.c);
        memcpy(strip_info->dev.dev_addr, addr.c, sizeof(addr));
        /* Give the radio a few seconds to get its head straight, then send an arp */
        strip_info->gratuitous_arp = jiffies + 6 * HZ;
        strip_info->arp_interval = 1 * HZ;
    }
}

static void RecvErr(char *msg, struct strip *strip_info)
{
    __u8 *ptr = strip_info->sx_buff;
    __u8 *end = strip_info->sx_buff + strip_info->sx_count;
    DumpData(msg, strip_info, ptr, end);
    strip_info->rx_errors++;
}

static void RecvErr_Message(struct strip *strip_info, __u8 *sendername, const __u8 *msg)
{
    static const char ERR_001[] = "001"; /* Not in StarMode! */
    static const char ERR_002[] = "002"; /* Remap handle */
    static const char ERR_003[] = "003"; /* Can't resolve name */
    static const char ERR_004[] = "004"; /* Name too small or missing */
    static const char ERR_005[] = "005"; /* Bad count specification */
    static const char ERR_006[] = "006"; /* Header too big */
    static const char ERR_007[] = "007"; /* Body too big */
    static const char ERR_008[] = "008"; /* Bad character in name */
    static const char ERR_009[] = "009"; /* No count or line terminator */

    if (!strncmp(msg, ERR_001, sizeof(ERR_001)-1))
    {
        RecvErr("Error Msg:", strip_info);
        printk(KERN_INFO "%s: Radio %s is not in StarMode\n",
            strip_info->dev.name, sendername);
    }
    else if (!strncmp(msg, ERR_002, sizeof(ERR_002)-1))
    {
        RecvErr("Error Msg:", strip_info);
#ifdef notyet        /*Kernel doesn't have scanf!*/
        int handle;
        __u8 newname[64];
        sscanf(msg, "ERR_002 Remap handle &%d to name %s", &handle, newname);
        printk(KERN_INFO "%s: Radio name %s is handle %d\n",
            strip_info->dev.name, newname, handle);
#endif
    }
    else if (!strncmp(msg, ERR_003, sizeof(ERR_003)-1))
    {
        RecvErr("Error Msg:", strip_info);
        printk(KERN_INFO "%s: Destination radio name is unknown\n",
            strip_info->dev.name);
    }
    else if (!strncmp(msg, ERR_004, sizeof(ERR_004)-1))
    {
        strip_info->watchdog_doreset = jiffies + LongTime;
        if (!strip_info->working)
        {
            strip_info->working = TRUE;
            printk(KERN_INFO "%s: Radio now in starmode\n",
                strip_info->dev.name);
            /*
             * If the radio has just entered a working state, we should do our first
             * probe ASAP, so that we find out our radio address etc. without delay.
             */
            strip_info->watchdog_doprobe = jiffies;
        }
        if (!strip_info->structured_messages && sendername)
        {
            strip_info->structured_messages = TRUE;
            printk(KERN_INFO "%s: Radio provides structured messages\n",
                strip_info->dev.name);
        }
    }
    else if (!strncmp(msg, ERR_005, sizeof(ERR_005)-1))
        RecvErr("Error Msg:", strip_info);
    else if (!strncmp(msg, ERR_006, sizeof(ERR_006)-1))
        RecvErr("Error Msg:", strip_info);
    else if (!strncmp(msg, ERR_007, sizeof(ERR_007)-1))
    {
        /*
         * Note: This error knocks the radio back into
         * command mode.
         */
        RecvErr("Error Msg:", strip_info);
        printk(KERN_ERR "%s: Error! Packet size too big for radio.",
            strip_info->dev.name);
        strip_info->watchdog_doreset = jiffies;        /* Do reset ASAP */
    }
    else if (!strncmp(msg, ERR_008, sizeof(ERR_008)-1))
    {
        RecvErr("Error Msg:", strip_info);
        printk(KERN_ERR "%s: Radio name contains illegal character\n",
            strip_info->dev.name);
    }
    else if (!strncmp(msg, ERR_009, sizeof(ERR_009)-1))
        RecvErr("Error Msg:", strip_info);
    else
        RecvErr("Error Msg:", strip_info);
}

static void process_AT_response(struct strip *strip_info, __u8 *ptr, __u8 *end)
{
    static const char ATS305[] = "ATS305?";
    static const char ATS300[] = "ATS300?";
    static const char ATS325[] = "ATS325?";
    static const char ATI2[] = "AT~I2 nn";

    /* Skip to the first newline character */
    __u8 *p = ptr;
    while (p < end && *p != 10) p++;
    if (p >= end) return;
    p++;

    if (!strncmp(ptr, ATS305, sizeof(ATS305)-1))
    {
        if (IS_RADIO_ADDRESS(p)) get_radio_address(strip_info, p);
    }
    else if (!strncmp(ptr, ATS300, sizeof(ATS300)-1)) {
        get_radio_version(strip_info, p, end);
    }
    else if (!strncmp(ptr, ATS325, sizeof(ATS325)-1)) {
        get_radio_voltage(strip_info, p, end);
    }
    else if (!strncmp(ptr, ATI2, sizeof(ATI2)-1)) {
        get_radio_neighbors(strip_info, p, end);
    }
    else RecvErr("Unknown AT Response:", strip_info);
}

/*
 * Send one completely decapsulated datagram to the next layer.
 */

static void deliver_packet(struct strip *strip_info, STRIP_Header *header, __u16 packetlen)
{
    struct sk_buff *skb = dev_alloc_skb(sizeof(STRIP_Header) + packetlen);
    if (!skb)
    {
        printk(KERN_INFO "%s: memory squeeze, dropping packet.\n", strip_info->dev.name);
        strip_info->rx_dropped++;
    }
    else
    {
        memcpy(skb_put(skb, sizeof(STRIP_Header)), header, sizeof(STRIP_Header));
        memcpy(skb_put(skb, packetlen), strip_info->rx_buff, packetlen);
        skb->dev      = &strip_info->dev;
        skb->protocol = header->protocol;
        skb->mac.raw  = skb->data;

        /* Having put a fake header on the front of the sk_buff for the */
        /* benefit of tools like tcpdump, skb_pull now 'consumes' that  */
        /* fake header before we hand the packet up to the next layer.  */
        skb_pull(skb, sizeof(STRIP_Header));

        /* Finally, hand the packet up to the next layer (e.g. IP or ARP, etc.) */
        strip_info->rx_packets++;
        netif_rx(skb);
    }
}

static void process_IP_packet(struct strip *strip_info, STRIP_Header *header, __u8 *ptr, __u8 *end)
{
    __u16 packetlen;

#if DO_PROC_NET_STRIP_TRACE
    __u8 *start_ptr = ptr;
#endif DO_PROC_NET_STRIP_TRACE

    /* Decode start of the IP packet header */
    ptr = UnStuffData(ptr, end, strip_info->rx_buff, 4);
    if (!ptr)
    {
        RecvErr("IP Packet too short", strip_info);
        return;
    }

    packetlen = ((__u16)strip_info->rx_buff[2] << 8) | strip_info->rx_buff[3];

    if (packetlen > MAX_STRIP_MTU)
    {
        printk(KERN_ERR "%s: Dropping oversized receive packet: %d bytes\n",
            strip_info->dev.name, packetlen);
        strip_info->rx_dropped++;
        return;
    }

    /*printk(KERN_INFO "%s: Got %d byte IP packet\n", strip_info->dev.name, packetlen);*/

    /* Decode remainder of the IP packet */
    ptr = UnStuffData(ptr, end, strip_info->rx_buff+4, packetlen-4);
    if (!ptr)
    {
        RecvErr("IP Packet too short", strip_info);
        return;
    }

    if (ptr < end)
    {
        RecvErr("IP Packet too long", strip_info);
        return;
    }

    header->protocol = htons(ETH_P_IP);

#if DO_PROC_NET_STRIP_TRACE
    packet_log(strip_info, strip_info->rx_buff, EntryReceive, header,
	       packetlen, end-start_ptr, slip_len(strip_info->rx_buff, packetlen)); 
#endif DO_PROC_NET_STRIP_TRACE

    deliver_packet(strip_info, header, packetlen);
}

static void process_ARP_packet(struct strip *strip_info, STRIP_Header *header, __u8 *ptr, __u8 *end)
{
    __u16 packetlen;
    struct arphdr *arphdr = (struct arphdr *)strip_info->rx_buff;

#if DO_PROC_NET_STRIP_TRACE
    __u8 *start_ptr = ptr;
#endif DO_PROC_NET_STRIP_TRACE

    /* Decode start of the ARP packet */
    ptr = UnStuffData(ptr, end, strip_info->rx_buff, 8);
    if (!ptr)
    {
        RecvErr("ARP Packet too short", strip_info);
        return;
    }

    packetlen = 8 + (arphdr->ar_hln + arphdr->ar_pln) * 2;

    if (packetlen > MAX_STRIP_MTU)
    {
        printk(KERN_ERR "%s: Dropping oversized receive packet: %d bytes\n",
            strip_info->dev.name, packetlen);
        strip_info->rx_dropped++;
        return;
    }

    /*printk(KERN_INFO "%s: Got %d byte ARP %s\n",
        strip_info->dev.name, packetlen,
        ntohs(arphdr->ar_op) == ARPOP_REQUEST ? "request" : "reply");*/

    /* Decode remainder of the ARP packet */
    ptr = UnStuffData(ptr, end, strip_info->rx_buff+8, packetlen-8);
    if (!ptr)
    {
        RecvErr("ARP Packet too short", strip_info);
        return;
    }

    if (ptr < end)
    {
        RecvErr("ARP Packet too long", strip_info);
        return;
    }

    header->protocol = htons(ETH_P_ARP);

#if DO_PROC_NET_STRIP_TRACE
    packet_log(strip_info, strip_info->rx_buff, EntryReceive, header,
	       packetlen, end-start_ptr, slip_len(strip_info->rx_buff, packetlen)); 
#endif DO_PROC_NET_STRIP_TRACE

    deliver_packet(strip_info, header, packetlen);
}

static void process_packet(struct strip *strip_info)
{
    STRIP_Header header = { zero_address, zero_address, 0 };
    __u8 *ptr = strip_info->sx_buff;
    __u8 *end = strip_info->sx_buff + strip_info->sx_count;
    __u8 sendername[32], *sptr = sendername;
    MetricomKey key;

    /* Ignore 'OK' responses from prior commands */
    if (strip_info->sx_count == 2 && ptr[0] == 'O' && ptr[1] == 'K') return;

    /* Check for anything that looks like it might be our radio name: dddd-dddd */
    /* (This is here for backwards compatibility with old firmware)             */
    if (strip_info->sx_count == 9 && IS_RADIO_ADDRESS(ptr))
    {
        get_radio_address(strip_info, ptr);
        return;
    }

    /*HexDump("Receiving", strip_info, ptr, end);*/

    /* Check for start of address marker, and then skip over it */
    if (*ptr != '*')
    {
        /* Catch other error messages */
        if (ptr[0] == 'E' && ptr[1] == 'R' && ptr[2] == 'R' && ptr[3] == '_')
            RecvErr_Message(strip_info, NULL, &ptr[4]);
        else RecvErr("No initial *", strip_info);
        return;
    }
    ptr++; /* Skip the initial '*' */

    /* Copy out the return address */
    while (ptr < end && *ptr != '*' && sptr < ARRAY_END(sendername)-1) *sptr++ = *ptr++;
    *sptr = 0;                /* Null terminate the sender name */

    /* Check for end of address marker, and skip over it */
    if (ptr >= end || *ptr != '*')
    {
        RecvErr("No second *", strip_info);
        return;
    }
    ptr++; /* Skip the second '*' */

    /* If the sender name is "&COMMAND", ignore this 'packet'       */
    /* (This is here for backwards compatibility with old firmware) */
    if (!strcmp(sendername, "&COMMAND"))
    {
        strip_info->structured_messages = FALSE;
        return;
    }

    if (ptr+4 >= end)
    {
        RecvErr("No proto key", strip_info);
        return;
    }

    /*printk(KERN_INFO "%s: Got packet from \"%s\".\n", strip_info->dev.name, sendername);*/

    /*
     * Fill in (pseudo) source and destination addresses in the packet.
     * We assume that the destination address was our address (the radio does not
     * tell us this). If the radio supplies a source address, then we use it.
     */
    memcpy(&header.dst_addr, strip_info->dev.dev_addr, sizeof(MetricomAddress));
    if (IS_RADIO_ADDRESS(sendername)) string_to_radio_address(&header.src_addr, sendername);

    /* Get the protocol key out of the buffer */
    key.c[0] = *ptr++;
    key.c[1] = *ptr++;
    key.c[2] = *ptr++;
    key.c[3] = *ptr++;

    if      (key.l == SIP0Key.l) process_IP_packet(strip_info, &header, ptr, end);
    else if (key.l == ARP0Key.l) process_ARP_packet(strip_info, &header, ptr, end);
    else if (key.l == ATR_Key.l) process_AT_response(strip_info, ptr, end);
    else if (key.l == ERR_Key.l) RecvErr_Message(strip_info, sendername, ptr);
    else /* RecvErr("Unrecognized protocol key", strip_info); */

    /* Note, this "else" block is temporary, until Metricom fix their */
    /* packet corruption bug */
    {
        RecvErr("Unrecognized protocol key (retrying)", strip_info);
        ptr -= 3; /* Back up and try again */
        key.c[0] = *ptr++;
        key.c[1] = *ptr++;
        key.c[2] = *ptr++;
        key.c[3] = *ptr++;
        if      (key.l == SIP0Key.l) process_IP_packet(strip_info, &header, ptr, end);
        else if (key.l == ARP0Key.l) process_ARP_packet(strip_info, &header, ptr, end);
        else if (key.l == ATR_Key.l) process_AT_response(strip_info, ptr, end);
        else if (key.l == ERR_Key.l) RecvErr_Message(strip_info, sendername, ptr);
        else RecvErr("Unrecognized protocol key", strip_info);
    }
}

/*
 * Handle the 'receiver data ready' interrupt.
 * This function is called by the 'tty_io' module in the kernel when
 * a block of STRIP data has been received, which can now be decapsulated
 * and sent on to some IP layer for further processing.
 */

static void
strip_receive_buf(struct tty_struct *tty, const unsigned char *cp, char *fp, int count)
{
    struct strip *strip_info = (struct strip *) tty->disc_data;
    const unsigned char *end = cp + count;

    if (!strip_info || strip_info->magic != STRIP_MAGIC || !strip_info->dev.start)
        return;

    /* Argh! mtu change time! - costs us the packet part received at the change */
    if (strip_info->mtu != strip_info->dev.mtu)
        strip_changedmtu(strip_info);

#if 0
    {
    struct timeval tv;
    do_gettimeofday(&tv);
    printk(KERN_INFO "**** strip_receive_buf: %3d bytes at %d.%06d\n",
        count, tv.tv_sec % 100, tv.tv_usec);
    }
#endif

    /* Read the characters out of the buffer */
    while (cp < end)
    {
        if (fp && *fp++ && !strip_info->discard) /* If there's a serial error, record it */
        {
            strip_info->discard = 1;
            strip_info->rx_errors++;
        }

        /* Leading control characters (CR, NL, Tab, etc.) are ignored */
        if (strip_info->sx_count > 0 || *cp >= ' ')
        {
            if (*cp == 0x0D)                /* If end of packet, decide what to do with it */
            {
                if (strip_info->sx_count > 3000)
                    printk(KERN_INFO "Cut a %d byte packet (%d bytes remaining)%s\n",
                        strip_info->sx_count, end-cp-1,
                        strip_info->discard ? " (discarded)" : "");
                if (strip_info->sx_count > strip_info->sx_size)
                {
                    strip_info->discard = 1;
                    strip_info->rx_over_errors++;
                    printk(KERN_INFO "%s: sx_buff overflow (%d bytes total)\n",
                           strip_info->dev.name, strip_info->sx_count);
                }
                if (!strip_info->discard) process_packet(strip_info);
                strip_info->discard = 0;
                strip_info->sx_count = 0;
            }
            else if (!strip_info->discard) /* If we're not discarding, store the character */
            {
                /* Make sure we have space in the buffer */
                if (strip_info->sx_count < strip_info->sx_size)
                    strip_info->sx_buff[strip_info->sx_count] = *cp;
                strip_info->sx_count++;
            }
        }
        cp++;
    }
}


/************************************************************************/
/* General control routines						*/

static int strip_set_dev_mac_address(struct device *dev, void *addr)
{
    return -1;        /* You cannot override a Metricom radio's address */
}

static struct net_device_stats *strip_get_stats(struct device *dev)
{
    static struct net_device_stats stats;
    struct strip *strip_info = (struct strip *)(dev->priv);

    memset(&stats, 0, sizeof(struct net_device_stats));

    stats.rx_packets     = strip_info->rx_packets;
    stats.tx_packets     = strip_info->tx_packets;
    stats.rx_dropped     = strip_info->rx_dropped;
    stats.tx_dropped     = strip_info->tx_dropped;
    stats.tx_errors      = strip_info->tx_errors;
    stats.rx_errors      = strip_info->rx_errors;
    stats.rx_over_errors = strip_info->rx_over_errors;
    return(&stats);
}


/************************************************************************/
/* Opening and closing							*/

/*
 * Here's the order things happen:
 * When the user runs "slattach -p strip ..."
 *  1. The TTY module calls strip_open
 *  2. strip_open calls strip_alloc
 *  3.                  strip_alloc calls register_netdev
 *  4.                  register_netdev calls strip_dev_init
 *  5. then strip_open finishes setting up the strip_info
 *
 * When the user runs "ifconfig st<x> up address netmask ..."
 *  6. strip_open_low gets called
 *
 * When the user runs "ifconfig st<x> down"
 *  7. strip_close_low gets called
 *
 * When the user kills the slattach process
 *  8. strip_close gets called
 *  9. strip_close calls dev_close
 * 10. if the device is still up, then dev_close calls strip_close_low
 * 11. strip_close calls strip_free
 */

/* Open the low-level part of the STRIP channel. Easy! */

static int strip_open_low(struct device *dev)
{
    struct strip *strip_info = (struct strip *)(dev->priv);

    if (strip_info->tty == NULL)
        return(-ENODEV);

    if (!allocate_buffers(strip_info))
        return(-ENOMEM);

    strip_info->discard  = 0;
    strip_info->working  = FALSE;
    strip_info->structured_messages = FALSE;
    strip_info->sx_count = 0;
    strip_info->tx_left  = 0;

    dev->tbusy  = 0;
    dev->start  = 1;

    printk(KERN_INFO "%s: Initializing Radio.\n", strip_info->dev.name);
    ResetRadio(strip_info);
    strip_info->idle_timer.expires  = jiffies + 2 * HZ;
    add_timer(&strip_info->idle_timer);
    return(0);
}


/*
 * Close the low-level part of the STRIP channel. Easy!
 */

static int strip_close_low(struct device *dev)
{
    struct strip *strip_info = (struct strip *)(dev->priv);

    if (strip_info->tty == NULL)
        return -EBUSY;
    strip_info->tty->flags &= ~(1 << TTY_DO_WRITE_WAKEUP);
    dev->tbusy = 1;
    dev->start = 0;

    /*
     * Free all STRIP frame buffers.
     */
    if (strip_info->rx_buff)
    {
        kfree(strip_info->rx_buff);
        strip_info->rx_buff = NULL;
    }
    if (strip_info->sx_buff)
    {
        kfree(strip_info->sx_buff);
        strip_info->sx_buff = NULL;
    }
    if (strip_info->tx_buff)
    {
        kfree(strip_info->tx_buff);
        strip_info->tx_buff = NULL;
    }
    del_timer(&strip_info->idle_timer);
    return 0;
}

/*
 * This routine is called by DDI when the
 * (dynamically assigned) device is registered
 */

static int strip_dev_init(struct device *dev)
{
    /*
     * Finish setting up the DEVICE info.
     */

    dev->trans_start        = 0;
    dev->last_rx            = 0;
    dev->tx_queue_len       = 30;         /* Drop after 30 frames queued */

    dev->flags              = 0;
    dev->family             = AF_INET;
    dev->metric             = 0;
    dev->mtu                = DEFAULT_STRIP_MTU;
    dev->type               = ARPHRD_METRICOM;        /* dtang */
    dev->hard_header_len    = sizeof(STRIP_Header);
    /*
     *  dev->priv             Already holds a pointer to our struct strip
     */

    *(MetricomAddress*)&dev->broadcast = broadcast_address;
    dev->dev_addr[0]        = 0;
    dev->addr_len           = sizeof(MetricomAddress);
    dev->pa_addr            = 0;
    dev->pa_brdaddr         = 0;
    dev->pa_mask            = 0;
    dev->pa_alen            = sizeof(unsigned long);

    /*
     * Pointer to the interface buffers.
     */

   dev_init_buffers(dev);

    /*
     * Pointers to interface service routines.
     */

    dev->open               = strip_open_low;
    dev->stop               = strip_close_low;
    dev->hard_start_xmit    = strip_xmit;
    dev->hard_header        = strip_header;
    dev->rebuild_header     = strip_rebuild_header;
    /*  dev->type_trans            unused */
    /*  dev->set_multicast_list   unused */
    dev->set_mac_address    = strip_set_dev_mac_address;
    /*  dev->do_ioctl             unused */
    /*  dev->set_config           unused */
    dev->get_stats          = strip_get_stats;
    return 0;
}

/*
 * Free a STRIP channel.
 */

static void strip_free(struct strip *strip_info)
{
    MetricomNode *node, *free;

    *(strip_info->referrer) = strip_info->next;
    if (strip_info->next)
        strip_info->next->referrer = strip_info->referrer;
    strip_info->magic = 0;

    for (node = strip_info->neighbor_list; node != NULL; )
    {
        free = node;
        node = node->next;
        kfree(free);
    }
    kfree(strip_info);
}

/*
 * Allocate a new free STRIP channel
 */

static struct strip *strip_alloc(void)
{
    int channel_id = 0;
    struct strip **s = &struct_strip_list;
    struct strip *strip_info = (struct strip *)
        kmalloc(sizeof(struct strip), GFP_KERNEL);

    if (!strip_info)
        return(NULL);        /* If no more memory, return */

    /*
     * Clear the allocated memory
     */

    memset(strip_info, 0, sizeof(struct strip));

    /*
     * Search the list to find where to put our new entry
     * (and in the process decide what channel number it is
     * going to be)
     */

    while (*s && (*s)->dev.base_addr == channel_id)
    {
        channel_id++;
        s = &(*s)->next;
    }

    /*
     * Fill in the link pointers
     */

    strip_info->next = *s;
    if (*s)
        (*s)->referrer = &strip_info->next;
    strip_info->referrer = s;
    *s = strip_info;

    strip_info->magic = STRIP_MAGIC;
    strip_info->tty   = NULL;

    strip_info->gratuitous_arp   = jiffies + LongTime;
    strip_info->arp_interval     = 0;
    init_timer(&strip_info->idle_timer);
    strip_info->idle_timer.data     = (long)&strip_info->dev;
    strip_info->idle_timer.function = strip_IdleTask;

    strip_info->neighbor_list = kmalloc(sizeof(MetricomNode), GFP_KERNEL);
    strip_info->neighbor_list->type = 0;
    strip_info->neighbor_list->next = NULL;

    /* Note: strip_info->if_name is currently 8 characters long */
    sprintf(strip_info->if_name, "st%d", channel_id);
    strip_info->dev.name         = strip_info->if_name;
    strip_info->dev.base_addr    = channel_id;
    strip_info->dev.priv         = (void*)strip_info;
    strip_info->dev.next         = NULL;
    strip_info->dev.init         = strip_dev_init;

    return(strip_info);
}

/*
 * Open the high-level part of the STRIP channel.
 * This function is called by the TTY module when the
 * STRIP line discipline is called for.  Because we are
 * sure the tty line exists, we only have to link it to
 * a free STRIP channel...
 */

static int strip_open(struct tty_struct *tty)
{
    struct strip *strip_info = (struct strip *) tty->disc_data;

    /*
     * First make sure we're not already connected.
     */

    if (strip_info && strip_info->magic == STRIP_MAGIC)
        return -EEXIST;

    /*
     * OK.  Find a free STRIP channel to use.
     */
    if ((strip_info = strip_alloc()) == NULL)
        return -ENFILE;

    /*
     * Register our newly created device so it can be ifconfig'd
     * strip_dev_init() will be called as a side-effect
     */

    if (register_netdev(&strip_info->dev) != 0)
    {
        printk(KERN_ERR "strip: register_netdev() failed.\n");
        strip_free(strip_info);
        return -ENFILE;
    }

    strip_info->tty = tty;
    tty->disc_data = strip_info;
    if (tty->driver.flush_buffer)
        tty->driver.flush_buffer(tty);
    if (tty->ldisc.flush_buffer)
        tty->ldisc.flush_buffer(tty);

    /*
     * Restore default settings
     */

    strip_info->dev.type = ARPHRD_METRICOM;    /* dtang */

    /*
     * Set tty options
     */

    tty->termios->c_iflag |= IGNBRK |IGNPAR;/* Ignore breaks and parity errors. */
    tty->termios->c_cflag |= CLOCAL;    /* Ignore modem control signals. */
    tty->termios->c_cflag &= ~HUPCL;    /* Don't close on hup */

#ifdef MODULE
    MOD_INC_USE_COUNT;
#endif
    /*
     * Done.  We have linked the TTY line to a channel.
     */
    return(strip_info->dev.base_addr);
}

/*
 * Close down a STRIP channel.
 * This means flushing out any pending queues, and then restoring the
 * TTY line discipline to what it was before it got hooked to STRIP
 * (which usually is TTY again).
 */

static void strip_close(struct tty_struct *tty)
{
    struct strip *strip_info = (struct strip *) tty->disc_data;

    /*
     * First make sure we're connected.
     */

    if (!strip_info || strip_info->magic != STRIP_MAGIC)
        return;

    dev_close(&strip_info->dev);
    unregister_netdev(&strip_info->dev);

    tty->disc_data = 0;
    strip_info->tty = NULL;
    strip_free(strip_info);
    tty->disc_data = NULL;
#ifdef MODULE
    MOD_DEC_USE_COUNT;
#endif
}


/************************************************************************/
/* Perform I/O control calls on an active STRIP channel.		*/

static int strip_ioctl(struct tty_struct *tty, struct file *file,
    unsigned int cmd, unsigned long arg)
{
    struct strip *strip_info = (struct strip *) tty->disc_data;
    int err;

    /*
     * First make sure we're connected.
     */

    if (!strip_info || strip_info->magic != STRIP_MAGIC)
        return -EINVAL;

    switch(cmd)
    {
        case SIOCGIFNAME:
            err = verify_area(VERIFY_WRITE, (void*)arg, 16);
            if (err)
                return -err;
            copy_to_user((void*)arg, strip_info->dev.name,
                strlen(strip_info->dev.name) + 1);
            return 0;

        case SIOCSIFHWADDR:
            return -EINVAL;

        /*
         * Allow stty to read, but not set, the serial port
         */

        case TCGETS:
        case TCGETA:
            return n_tty_ioctl(tty, (struct file *) file, cmd,
                (unsigned long) arg);

        default:
            return -ENOIOCTLCMD;
    }
}


/************************************************************************/
/* Initialization							*/

/*
 *      Registers with the /proc file system to create different /proc/net files.
 */

static int strip_proc_net_register(unsigned short type, char *file_name,
                                   int (*get_info)(char *, char **, off_t, int, int))
{
    struct proc_dir_entry *strip_entry;

    strip_entry = kmalloc(sizeof(struct proc_dir_entry), GFP_ATOMIC);

    memset(strip_entry, 0, sizeof(struct proc_dir_entry));
    strip_entry->low_ino = type;
    strip_entry->namelen = strlen(file_name);
    strip_entry->name = file_name;
    strip_entry->mode = S_IFREG | S_IRUGO;
    strip_entry->nlink = 1;
    strip_entry->uid = 0;
    strip_entry->gid = 0;
    strip_entry->size = 0;
    strip_entry->ops = &proc_net_inode_operations;
    strip_entry->get_info = get_info;

    return proc_net_register(strip_entry);
}

/*
 * Initialize the STRIP driver.
 * This routine is called at boot time, to bootstrap the multi-channel
 * STRIP driver
 */

#ifdef MODULE
static
#endif
int strip_init_ctrl_dev(struct device *dummy)
{
    static struct tty_ldisc strip_ldisc;
    int status;

    printk("STRIP: version %s (unlimited channels)\n", StripVersion);

    /*
     * Fill in our line protocol discipline, and register it
     */

    memset(&strip_ldisc, 0, sizeof(strip_ldisc));
    strip_ldisc.magic        = TTY_LDISC_MAGIC;
    strip_ldisc.flags        = 0;
    strip_ldisc.open         = strip_open;
    strip_ldisc.close        = strip_close;
    strip_ldisc.read         = NULL;
    strip_ldisc.write        = NULL;
    strip_ldisc.ioctl        = strip_ioctl;
    strip_ldisc.poll         = NULL;
    strip_ldisc.receive_buf  = strip_receive_buf;
    strip_ldisc.receive_room = strip_receive_room;
    strip_ldisc.write_wakeup = strip_write_some_more;
    status = tty_register_ldisc(N_STRIP, &strip_ldisc);
    if (status != 0)
    {
        printk(KERN_ERR "STRIP: can't register line discipline (err = %d)\n", status);
    }

    /*
     * Register the status and trace files with /proc
     */

#if DO_PROC_NET_STRIP_STATUS
    if (strip_proc_net_register(PROC_NET_STRIP_STATUS, "strip_status",
                                               &strip_get_status_info) != 0)
    {
        printk(KERN_ERR "strip: status strip_proc_net_register() failed.\n");
    }
#endif

#if DO_PROC_NET_STRIP_TRACE
    if (strip_proc_net_register(PROC_NET_STRIP_TRACE, "strip_trace",
                    &strip_get_trace_info) != 0)
    {
        printk(KERN_ERR "strip: trace strip_proc_net_register() failed.\n");
    }
#endif

#ifdef MODULE
     return status;
#else

    /* Return "not found", so that dev_init() will unlink
     * the placeholder device entry for us.
     */
    return ENODEV;
#endif
}


/************************************************************************/
/* From here down is only used when compiled as an external module	*/

#ifdef MODULE

int init_module(void)
{
    return strip_init_ctrl_dev(0);
}

void cleanup_module(void)
{
    int i;
    while (struct_strip_list)
        strip_free(struct_strip_list);

    /* Unregister with the /proc/net files here. */

#if DO_PROC_NET_STRIP_TRACE
    proc_net_unregister(PROC_NET_STRIP_TRACE);
#endif
#if DO_PROC_NET_STRIP_STATUS
    proc_net_unregister(PROC_NET_STRIP_STATUS);
#endif

    if ((i = tty_register_ldisc(N_STRIP, NULL)))
        printk(KERN_ERR "STRIP: can't unregister line discipline (err = %d)\n", i);
}
#endif /* MODULE */
