/* $Id: mp.c,v 1.4 1995/11/25 01:00:06 davem Exp $
 * mp.c:  OpenBoot Prom Multiprocessor support routines.  Don't call
 *        these on a UP or else you will halt and catch fire. ;)
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/openprom.h>
#include <asm/oplib.h>

/* Start cpu with prom-tree node 'cpunode' using context described
 * by 'ctable_reg' in context 'ctx' at program counter 'pc'.
 *
 * XXX Have to look into what the return values mean. XXX
 */
int
prom_startcpu(int cpunode, struct linux_prom_registers *ctable_reg, int ctx, char *pc)
{
	switch(prom_vers) {
	case PROM_V0:
	case PROM_V2:
		break;
	case PROM_V3:
	case PROM_P1275:
		return (*(romvec->v3_cpustart))(cpunode, (int) ctable_reg, ctx, pc);
		break;
	};

	return -1;
}

/* Stop CPU with device prom-tree node 'cpunode'.
 * XXX Again, what does the return value really mean? XXX
 */
int
prom_stopcpu(int cpunode)
{
	switch(prom_vers) {
	case PROM_V0:
	case PROM_V2:
		break;
	case PROM_V3:
	case PROM_P1275:
		return (*(romvec->v3_cpustop))(cpunode);
		break;
	};

	return -1;
}

/* Make CPU with device prom-tree node 'cpunode' idle.
 * XXX Return value, anyone? XXX
 */
int
prom_idlecpu(int cpunode)
{
	switch(prom_vers) {
	case PROM_V0:
	case PROM_V2:
		break;
	case PROM_V3:
	case PROM_P1275:
		return (*(romvec->v3_cpuidle))(cpunode);
		break;
	};

	return -1;
}

/* Resume the execution of CPU with nodeid 'cpunode'.
 * XXX Come on, somebody has to know... XXX
 */
int
prom_restartcpu(int cpunode)
{
	switch(prom_vers) {
	case PROM_V0:
	case PROM_V2:
		break;
	case PROM_V3:
	case PROM_P1275:
		return (*(romvec->v3_cpuresume))(cpunode);
		break;
	};

	return -1;
}
