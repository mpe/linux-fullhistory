/*
 *  asm-i386/acpi.h
 *
 *  Copyright (C) 2001 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2001 Patrick Mochel <mochel@osdl.org>
  *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#ifndef _ASM_ACPI_H
#define _ASM_ACPI_H

#ifdef __KERNEL__

#define COMPILER_DEPENDENT_INT64   long long
#define COMPILER_DEPENDENT_UINT64  unsigned long long

/*
 * Calling conventions:
 *
 * ACPI_SYSTEM_XFACE        - Interfaces to host OS (handlers, threads)
 * ACPI_EXTERNAL_XFACE      - External ACPI interfaces 
 * ACPI_INTERNAL_XFACE      - Internal ACPI interfaces
 * ACPI_INTERNAL_VAR_XFACE  - Internal variable-parameter list interfaces
 */
#define ACPI_SYSTEM_XFACE
#define ACPI_EXTERNAL_XFACE
#define ACPI_INTERNAL_XFACE
#define ACPI_INTERNAL_VAR_XFACE

/* Asm macros */

#define ACPI_ASM_MACROS
#define BREAKPOINT3
#define ACPI_DISABLE_IRQS() local_irq_disable()
#define ACPI_ENABLE_IRQS()  local_irq_enable()
#define ACPI_FLUSH_CPU_CACHE()	wbinvd()

/*
 * A brief explanation as GNU inline assembly is a bit hairy
 *  %0 is the output parameter in EAX ("=a")
 *  %1 and %2 are the input parameters in ECX ("c")
 *  and an immediate value ("i") respectively
 *  All actual register references are preceded with "%%" as in "%%edx"
 *  Immediate values in the assembly are preceded by "$" as in "$0x1"
 *  The final asm parameter are the operation altered non-output registers.
 */
#define ACPI_ACQUIRE_GLOBAL_LOCK(GLptr, Acq) \
    do { \
        int dummy; \
        asm("1:     movl (%1),%%eax;" \
            "movl   %%eax,%%edx;" \
            "andl   %2,%%edx;" \
            "btsl   $0x1,%%edx;" \
            "adcl   $0x0,%%edx;" \
            "lock;  cmpxchgl %%edx,(%1);" \
            "jnz    1b;" \
            "cmpb   $0x3,%%dl;" \
            "sbbl   %%eax,%%eax" \
            :"=a"(Acq),"=c"(dummy):"c"(GLptr),"i"(~1L):"dx"); \
    } while(0)

#define ACPI_RELEASE_GLOBAL_LOCK(GLptr, Acq) \
    do { \
        int dummy; \
        asm("1:     movl (%1),%%eax;" \
            "movl   %%eax,%%edx;" \
            "andl   %2,%%edx;" \
            "lock;  cmpxchgl %%edx,(%1);" \
            "jnz    1b;" \
            "andl   $0x1,%%eax" \
            :"=a"(Acq),"=c"(dummy):"c"(GLptr),"i"(~3L):"dx"); \
    } while(0)


/*
 * Math helper asm macros
 */
#define ACPI_DIV_64_BY_32(n_hi, n_lo, d32, q32, r32) \
        asm("divl %2;"        \
        :"=a"(q32), "=d"(r32) \
        :"r"(d32),            \
        "0"(n_lo), "1"(n_hi))


#define ACPI_SHIFT_RIGHT_64(n_hi, n_lo) \
    asm("shrl   $1,%2;"             \
        "rcrl   $1,%3;"             \
        :"=r"(n_hi), "=r"(n_lo)     \
        :"0"(n_hi), "1"(n_lo))


#ifdef CONFIG_ACPI_BOOT 
extern int acpi_lapic;
extern int acpi_ioapic;


/* Fixmap pages to reserve for ACPI boot-time tables (see fixmap.h) */
#define FIX_ACPI_PAGES 4

#ifdef CONFIG_X86_IO_APIC
extern int skip_ioapic_setup;

static inline void disable_ioapic_setup(void)
{
	skip_ioapic_setup = 1;
}

static inline int ioapic_setup_disabled(void)
{
	return skip_ioapic_setup;
}

#else
static inline void disable_ioapic_setup(void)
{ }

#endif

#else	/* CONFIG_ACPI_BOOT */
#  define acpi_lapic 0
#  define acpi_ioapic 0

#endif

#ifdef CONFIG_ACPI_SLEEP

/* routines for saving/restoring kernel state */
extern int acpi_save_state_mem(void);
extern int acpi_save_state_disk(void);
extern void acpi_restore_state_mem(void);

extern unsigned long acpi_wakeup_address;

/* early initialization routine */
extern void acpi_reserve_bootmem(void);

#endif /*CONFIG_ACPI_SLEEP*/

#endif /*__KERNEL__*/

#endif /*_ASM_ACPI_H*/
