/******************************************************************************
 *
 * Name: acenv.h - Generation environment specific items
 *       $Revision: 53 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 R. Byron Moore
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
 */

#ifndef __ACENV_H__
#define __ACENV_H__


/*
 * Environment configuration.  The purpose of this file is to interface to the
 * local generation environment.
 *
 * 1) ACPI_USE_SYSTEM_CLIBRARY - Define this if linking to an actual C library.
 *      Otherwise, local versions of string/memory functions will be used.
 * 2) ACPI_USE_STANDARD_HEADERS - Define this if linking to a C library and
 *      the standard header files may be used.
 *
 * The ACPI subsystem only uses low level C library functions that do not call
 * operating system services and may therefore be inlined in the code.
 *
 * It may be necessary to tailor these include files to the target
 * generation environment.
 *
 *
 * Functions and constants used from each header:
 *
 * string.h:    memcpy
 *              memset
 *              strcat
 *              strcmp
 *              strcpy
 *              strlen
 *              strncmp
 *              strncat
 *              strncpy
 *
 * stdlib.h:    strtoul
 *
 * stdarg.h:    va_list
 *              va_arg
 *              va_start
 *              va_end
 *
 */


/*
 * Environment-specific configuration
 */

#ifdef _LINUX

#include <linux/config.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <asm/system.h>
#include <asm/atomic.h>

/* Use native Linux string library */

#define ACPI_USE_SYSTEM_CLIBRARY

/* Special functions */

#define strtoul             simple_strtoul

/* Linux clib doesn't to strupr, but we do. */
char *
strupr(char *str);

#else

#ifdef _AED_EFI

#include <efi.h>
#include <efistdarg.h>
#include <efilib.h>

#else


/* All other environments */

#define ACPI_USE_STANDARD_HEADERS

#endif
#endif


/******************************************************************************
 *
 * C library configuration
 *
 *****************************************************************************/

#ifdef ACPI_USE_SYSTEM_CLIBRARY
/*
 * Use the standard C library headers.
 * We want to keep these to a minimum.
 *
 */

#ifdef ACPI_USE_STANDARD_HEADERS
/*
 * Use the standard headers from the standard locations
 */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#endif /* ACPI_USE_STANDARD_HEADERS */

/*
 * We will be linking to the standard Clib functions
 */

#define STRSTR(s1,s2)   strstr((s1), (s2))
#define STRUPR(s)       strupr((s))
#define STRLEN(s)       strlen((s))
#define STRCPY(d,s)     strcpy((d), (s))
#define STRNCPY(d,s,n)  strncpy((d), (s), (n))
#define STRNCMP(d,s,n)  strncmp((d), (s), (n))
#define STRCMP(d,s)     strcmp((d), (s))
#define STRCAT(d,s)     strcat((d), (s))
#define STRNCAT(d,s,n)  strncat((d), (s), (n))
#define STRTOUL(d,s,n)  strtoul((d), (s), (n))
#define MEMCPY(d,s,n)   memcpy((d), (s), (n))
#define MEMSET(d,s,n)   memset((d), (s), (n))
#define TOUPPER         toupper
#define TOLOWER         tolower


/******************************************************************************
 *
 * Not using native C library, use local implementations
 *
 *****************************************************************************/
#else

/*
 * Use local definitions of C library macros and functions
 * NOTE: The function implementations may not be as efficient
 * as an inline or assembly code implementation provided by a
 * native C library.
 */

#ifndef va_arg

#ifndef _VALIST
#define _VALIST
typedef char *va_list;
#endif /* _VALIST */

/*
 * Storage alignment properties
 */

#define  _AUPBND         (sizeof(int) - 1)
#define  _ADNBND         (sizeof(int) - 1)

/*
 * Variable argument list macro definitions
 */

#define _bnd(X, bnd)    (((sizeof(X)) + (bnd)) & (~(bnd)))
#define va_arg(ap, T)  (*(T *)(((ap)+=((_bnd(T, _AUPBND)))-(_bnd(T,_ADNBND)))))
#define va_end(ap)      (void)0
#define va_start(ap, A) (void)((ap)=(((char*)&(A))+(_bnd(A,_AUPBND))))

#endif /* va_arg */


#define STRSTR(s1,s2)    acpi_cm_strstr ((s1), (s2))
#define STRUPR(s)        acpi_cm_strupr ((s))
#define STRLEN(s)        acpi_cm_strlen ((s))
#define STRCPY(d,s)      acpi_cm_strcpy ((d), (s))
#define STRNCPY(d,s,n)   acpi_cm_strncpy ((d), (s), (n))
#define STRNCMP(d,s,n)   acpi_cm_strncmp ((d), (s), (n))
#define STRCMP(d,s)      acpi_cm_strcmp ((d), (s))
#define STRCAT(d,s)      acpi_cm_strcat ((d), (s))
#define STRNCAT(d,s,n)   acpi_cm_strncat ((d), (s), (n))
#define STRTOUL(d,s,n)   acpi_cm_strtoul ((d), (s),(n))
#define MEMCPY(d,s,n)    acpi_cm_memcpy ((d), (s), (n))
#define MEMSET(d,v,n)    acpi_cm_memset ((d), (v), (n))
#define TOUPPER          acpi_cm_to_upper
#define TOLOWER          acpi_cm_to_lower

#endif /* ACPI_USE_SYSTEM_CLIBRARY */


/******************************************************************************
 *
 * Assembly code macros
 *
 *****************************************************************************/

/*
 * Handle platform- and compiler-specific assembly language differences.
 *
 * Notes:
 * 1) Interrupt 3 is used to break into a debugger
 * 2) Interrupts are turned off during ACPI register setup
 */


#ifdef __GNUC__


#ifdef __ia64__

/* Single threaded */
#define ACPI_APPLICATION

#define ACPI_ASM_MACROS
#define causeinterrupt(level)
#define BREAKPOINT3
#define disable() __cli()
#define enable()  __sti()
#define wbinvd()

/*! [Begin] no source code translation */
#include <asm/pal.h>

/* PAL_HALT[_LIGHT] */
#define halt() ia64_pal_halt_light()

/* PAL_HALT */
#define safe_halt() ia64_pal_halt(1)

#define ACPI_ACQUIRE_GLOBAL_LOCK(GLptr, Acq) \
	do { \
	__asm__ volatile ("1:  ld4      r29=%1\n"  \
		";;\n"                  \
		"mov    ar.ccv=r29\n"   \
		"mov    r2=r29\n"       \
		"shr.u  r30=r29,1\n"    \
		"and    r29=-4,r29\n"   \
		";;\n"                  \
		"add    r29=2,r29\n"    \
		"and    r30=1,r30\n"    \
		";;\n"                  \
		"add    r29=r29,r30\n"  \
		";;\n"                  \
		"cmpxchg4.acq   r30=%1,r29,ar.ccv\n" \
		";;\n"                  \
		"cmp.eq p6,p7=r2,r30\n" \
		"(p7) br.dpnt.few 1b\n" \
		"cmp.gt p8,p9=3,r29\n"  \
		";;\n"                  \
		"(p8) mov %0=-1\n"      \
		"(p9) mov %0=r0\n"      \
		:"=r"(Acq):"m" __atomic_fool_gcc((GLptr)):"r2","r29","r30","memory"); \
	} while (0)

#define ACPI_RELEASE_GLOBAL_LOCK(GLptr, Acq) \
	do { \
	__asm__ volatile ("1:  ld4      r29=%1\n" \
		";;\n"                  \
		"mov    ar.ccv=r29\n"   \
		"mov    r2=r29\n"       \
		"and    r29=-4,r29\n"   \
		";;\n"                  \
		"cmpxchg4.acq   r30=%1,r29,ar.ccv\n" \
		";;\n"                  \
		"cmp.eq p6,p7=r2,r30\n" \
		"(p7) br.dpnt.few 1b\n" \
		"and    %0=1,r2\n"      \
		";;\n"                  \
		:"=r"(Acq):"m" __atomic_fool_gcc((GLptr)):"r2","r29","r30","memory"); \
	} while (0)
/*! [End] no source code translation !*/

#else /* DO IA32 */

#define ACPI_ASM_MACROS
#define causeinterrupt(level)
#define BREAKPOINT3
#define disable() __cli()
#define enable()  __sti()
#define halt()    __asm__ __volatile__ ("sti; hlt":::"memory")
#define wbinvd()

/*! [Begin] no source code translation
 *
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
/*! [End] no source code translation !*/

#endif /* IA 32 */
#endif /* __GNUC__ */


/* Unrecognized compiler, use defaults */
#ifndef ACPI_ASM_MACROS

#define ACPI_ASM_MACROS
#define causeinterrupt(level)
#define BREAKPOINT3
#define disable()
#define enable()
#define halt()
#define ACPI_ACQUIRE_GLOBAL_LOCK(Glptr, acq)
#define ACPI_RELEASE_GLOBAL_LOCK(Glptr, acq)

#endif /* ACPI_ASM_MACROS */


#ifdef ACPI_APPLICATION

/* Don't want software interrupts within a ring3 application */

#undef causeinterrupt
#undef BREAKPOINT3
#define causeinterrupt(level)
#define BREAKPOINT3
#endif


#endif /* __ACENV_H__ */
