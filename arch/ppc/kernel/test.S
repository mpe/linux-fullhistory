#include "ppc_asm.tmpl"
#include "ppc_defs.h"

#define SYNC() \
	isync; \
	sync

#define BEEF ori r0,r0,0; ori r0,r0,0 ;.word 0 ; .word 0xBEEF
_BEG:	
	lis	r7,0xF000		/* To mask upper 4 bits */
	

/* Initialize BAT registers */
	lis	r3,OFF_BAT@h
	ori	r3,r3,OFF_BAT@l
	andc	r3,r3,r7	/* make unmapped address */
	lwz	r0,0(r3)

	mtspr	IBAT0U,r0
	mtspr	DBAT0U,r0
	lwz	r0,4(r3)
	mtspr	IBAT0L,r0
	mtspr	DBAT0L,r0
	
	lis	r3,OFF_BAT@h
	ori	r3,r3,OFF_BAT@l
	andc	r3,r3,r7	/* make unmapped address */
	lwz	r0,0(r3)
	mtspr	IBAT1U,r0
	mtspr	DBAT1U,r0
	lwz	r0,4(r3)
	mtspr	IBAT1L,r0
	mtspr	DBAT1L,r0
	
	lis	r3,ZERO_BAT@h
	ori	r3,r3,ZERO_BAT@l
	andc	r3,r3,r7	/* make unmapped address */
	lwz	r0,0(r3)
	mtspr	IBAT2U,r0
	mtspr	DBAT2U,r0
	lwz	r0,4(r3)
	mtspr	IBAT2L,r0
	mtspr	DBAT2L,r0


	lis	r3,TMP_BAT2@h
	ori	r3,r3,TMP_BAT2@l
	andc	r3,r3,r7	/* make unmapped address */
	lwz	r0,0(r3)
	mtspr	IBAT3U,r0
	mtspr	DBAT3U,r0
	lwz	r0,4(r3)
	mtspr	IBAT3L,r0
	mtspr	DBAT3L,r0
	

/* Now we can turn on the MMU */
/* set srr1 for rfi */
	mfmsr	r3
	ori	r3,r3,MSR_DR|MSR_IR|MSR_

	mtspr	SRR1,r3

/* set srr0 for rfi */
	subf	r2, r2, r2
/*	lis	r2,10f@h*/
	lis	r2,0
	ori	r2,r2,10f@l
	mtspr	SRR0,r2

	SYNC()
	rfi

	ori r0, r0, 0
	ori r0, r0, 0
10:	ori r0, r0, 0
	ori r0, r0, 0
	BEEF
	
#if 0	
/* check bptr to see if it actually has the correct values */

	subf	r2,r2,r2
	lis	r2,bptr@h
	ori	r2,r2,bptr@l
	lwz	r0,0(r2)

/* rfi to the beef */
	mtspr	SRR1,r3
	mtspr	SRR0,r2

	SYNC()
	rfi	
		
bptr:	ori r0, r0, 0
	BEEF
	BEEF
	BEEF
#endif
_END:	