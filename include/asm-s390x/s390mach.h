/*
 *  arch/s390/kernel/s390mach.h
 *   S/390 data definitions for machine check processing
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 */

#ifndef __s390mach_h
#define __s390mach_h

#include <asm/types.h>

typedef struct _mci {
	__u32	to_be_defined_1 :  9;
	__u32 cp              :  1; /* channel-report pending */
	__u32	to_be_defined_2 : 22;
	__u32	to_be_defined_3;
	} mci_t;

//
// machine-check-interruption code
//
typedef struct _mcic {
   union _mcc {
      __u64 mcl;	/* machine check int. code - long info */	
      mci_t mcd;  /* machine check int. code - details   */
   } mcc;
} __attribute__ ((packed)) mcic_t;

//
// Channel Report Word
//
typedef struct _crw {
	__u32 res1    :  1;   /* reserved zero */
	__u32 slct    :  1;   /* solicited */
	__u32 oflw    :  1;   /* overflow */
	__u32 chn     :  1;   /* chained */
	__u32 rsc     :  4;   /* reporting source code */
	__u32 anc     :  1;   /* ancillary report */
	__u32 res2    :  1;   /* reserved zero */
	__u32 erc     :  6;   /* error-recovery code */
	__u32 rsid    : 16;   /* reporting-source ID */
} __attribute__ ((packed)) crw_t;

#define CRW_RSC_MONITOR  0x2  /* monitoring facility */
#define CRW_RSC_SCH      0x3  /* subchannel */
#define CRW_RSC_CPATH    0x4  /* channel path */
#define CRW_RSC_CONFIG   0x9  /* configuration-alert facility */
#define CRW_RSC_CSS      0xB  /* channel subsystem */

#define CRW_ERC_EVENT    0x00 /* event information pending */
#define CRW_ERC_AVAIL    0x01 /* available */
#define CRW_ERC_INIT     0x02 /* initialized */
#define CRW_ERC_TERROR   0x03 /* temporary error */
#define CRW_ERC_IPARM    0x04 /* installed parm initialized */
#define CRW_ERC_TERM     0x05 /* terminal */
#define CRW_ERC_PERRN    0x06 /* perm. error, fac. not init */
#define CRW_ERC_PERRI    0x07 /* perm. error, facility init */
#define CRW_ERC_PMOD     0x08 /* installed parameters modified */

#define MAX_CRW_PENDING  1024
#define MAX_MACH_PENDING 1024

//
// CRW Entry
//
typedef struct _crwe {
	crw_t   crw;
	struct _crwe *crwe_next;
} __attribute__ ((packed)) crwe_t;

typedef struct _mache {
	spinlock_t     lock;
	unsigned int   status;
	mcic_t         mcic;
	union _mc {
	   crwe_t     *crwe;		/* CRW if applicable */
   } mc;
	struct _mache *next;
	struct _mache *prev;
} mache_t;

#define MCHCHK_STATUS_TO_PROCESS    0x00000001
#define MCHCHK_STATUS_IN_PROGRESS   0x00000002
#define MCHCHK_STATUS_WAITING       0x00000004

void s390_init_machine_check( void );
void s390_do_machine_check  ( void );
void s390_do_crw_pending    ( crwe_t *pcrwe );

extern __inline__ int stcrw( __u32 *pcrw )
{
        int ccode;

        __asm__ __volatile__(
                "STCRW 0(%1)\n\t"
                "IPM %0\n\t"
                "SRL %0,28\n\t"
                : "=d" (ccode) : "a" (pcrw)
                : "cc", "1" );
        return ccode;
}

#endif /* __s390mach */
