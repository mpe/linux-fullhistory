/* $Id: indy_hpc.c,v 1.1 1997/06/06 09:36:18 ralf Exp $
 * indy_hpc.c: Routines for generic manipulation of the HPC controllers.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */

#include <asm/addrspace.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/sgihpc.h>
#include <asm/sgint23.h>
#include <asm/sgialib.h>

/* #define DEBUG_SGIHPC */

struct hpc3_regs *hpc3c0, *hpc3c1;
struct hpc3_miscregs *hpc3mregs;

/* We need software copies of these because they are write only. */
static unsigned long write1, write2;

/* Machine specific identifier knobs. */
int sgi_has_ioc2 = 0;
int sgi_guiness = 0;
int sgi_boardid;

void sgihpc_write1_modify(int set, int clear)
{
	write1 |= set;
	write1 &= ~clear;
	hpc3mregs->write1 = write1;
}

void sgihpc_write2_modify(int set, int clear)
{
	write2 |= set;
	write2 &= ~clear;
	hpc3mregs->write2 = write2;
}

void sgihpc_init(void)
{
	unsigned long sid, crev, brev;

	hpc3c0 = (struct hpc3_regs *) (KSEG1 + HPC3_CHIP0_PBASE);
	hpc3c1 = (struct hpc3_regs *) (KSEG1 + HPC3_CHIP1_PBASE);
	hpc3mregs = (struct hpc3_miscregs *) (KSEG1 + HPC3_MREGS_PBASE);
	sid = hpc3mregs->sysid;

	sid &= 0xff;
	crev = (sid & 0xe0) >> 5;
	brev = (sid & 0x1e) >> 1;

#ifdef DEBUG_SGIHPC
	prom_printf("sgihpc_init: crev<%2x> brev<%2x>\n", crev, brev);
	prom_printf("sgihpc_init: ");
#endif

	if(sid & 1) {
#ifdef DEBUG_SGIHPC
		prom_printf("GUINESS ");
#endif
		sgi_guiness = 1;
	} else {
#ifdef DEBUG_SGIHPC
		prom_printf("FULLHOUSE ");
#endif
		sgi_guiness = 0;
	}
	sgi_boardid = brev;

#ifdef DEBUG_SGIHPC
	prom_printf("sgi_boardid<%d> ", sgi_boardid);
#endif

	if(crev == 1) {
		if((sid & 1) || (brev >= 2)) {
#ifdef DEBUG_SGIHPC
			prom_printf("IOC2 ");
#endif
			sgi_has_ioc2 = 1;
		} else {
#ifdef DEBUG_SGIHPC
			prom_printf("IOC1 revision 1 ");
#endif
		}
	} else {
#ifdef DEBUG_SGIHPC
		prom_printf("IOC1 revision 0 ");
#endif
	}
#ifdef DEBUG_SGIHPC
	prom_printf("\n");
#endif

	write1 = (HPC3_WRITE1_PRESET |
		  HPC3_WRITE1_KMRESET |
		  HPC3_WRITE1_ERESET |
		  HPC3_WRITE1_LC0OFF);

	write2 = (HPC3_WRITE2_EASEL |
		  HPC3_WRITE2_NTHRESH |
		  HPC3_WRITE2_TPSPEED |
		  HPC3_WRITE2_EPSEL |
		  HPC3_WRITE2_U0AMODE |
		  HPC3_WRITE2_U1AMODE);

	if(!sgi_guiness)
		write1 |= HPC3_WRITE1_GRESET;
	hpc3mregs->write1 = write1;
	hpc3mregs->write2 = write2;

	hpc3c0->pbus_piocfgs[0][6] |= HPC3_PIOPCFG_HW;
}
