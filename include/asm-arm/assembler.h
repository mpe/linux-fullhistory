/*
 * linux/asm/assembler.h
 *
 * This file contains arm architecture specific defines
 * for the different processors.
 *
 * Do not include any C declarations in this file - it is included by
 * assembler source.
 */

/*
 * LOADREGS: multiple register load (ldm) with pc in register list
 *		(takes account of ARM6 not using ^)
 *
 * RETINSTR: return instruction: adds the 's' in at the end of the
 *		instruction if this is not an ARM6
 *
 * SAVEIRQS: save IRQ state (not required on ARM2/ARM3 - done
 *		implicitly
 *
 * RESTOREIRQS: restore IRQ state (not required on ARM2/ARM3 - done
 *		implicitly with ldm ... ^ or movs.
 *
 * These next two need thinking about - can't easily use stack... (see system.S)
 * DISABLEIRQS: disable IRQS in SVC mode
 *
 * ENABLEIRQS: enable IRQS in SVC mode
 *
 * USERMODE: switch to USER mode
 *
 * SVCMODE: switch to SVC mode
 */

#include <asm/proc/assembler.h>
