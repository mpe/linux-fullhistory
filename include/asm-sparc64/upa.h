/* $Id: upa.h,v 1.2 1997/04/04 00:50:30 davem Exp $ */
#ifndef _SPARC64_UPA_H
#define _SPARC64_UPA_H

/* UPA level registers and defines. */

/* UPA Config Register */
#define UPA_CONFIG_RESV		0xffffffffc0000000 /* Reserved.                    */
#define UPA_CONFIG_PCON		0x000000003fc00000 /* Depth of various sys queues. */
#define UPA_CONFIG_MID		0x00000000003e0000 /* Module ID.                   */
#define UPA_CONFIG_PCAP		0x000000000001ffff /* Port Capabilities.           */

/* UPA Port ID Register */
#define UPA_PORTID_FNP		0xff00000000000000 /* Hardcoded to 0xfc on ultra.  */
#define UPA_PORTID_RESV		0x00fffff800000000 /* Reserved.                    */
#define UPA_PORTID_ECCVALID     0x0000000400000000 /* Zero if mod can generate ECC */
#define UPA_PORTID_ONEREAD      0x0000000200000000 /* Set if mod generates P_RASB  */
#define UPA_PORTID_PINTRDQ      0x0000000180000000 /* # outstanding P_INT_REQ's    */
#define UPA_PORTID_PREQDQ       0x000000007e000000 /* slave-wr's to mod supported  */
#define UPA_PORTID_PREQRD       0x0000000001e00000 /* # incoming P_REQ's supported */
#define UPA_PORTID_UPACAP       0x00000000001f0000 /* UPA capabilities of mod      */
#define UPA_PORTID_ID           0x000000000000ffff /* Module Indentification bits  */

#endif /* !(_SPARC64_UPA_H) */
