/*
 * Machine vector for IA-64.
 * 
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) Srinivasa Thirumalachar <sprasad@engr.sgi.com>
 * Copyright (C) Vijay Chander <vijay@engr.sgi.com>
 * Copyright (C) 1999-2000 Hewlett-Packard Co.
 * Copyright (C) 1999-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 */
#ifndef _ASM_IA64_MACHVEC_H
#define _ASM_IA64_MACHVEC_H

#include <linux/config.h>
#include <linux/types.h>

/* forward declarations: */
struct hw_interrupt_type;
struct irq_desc;
struct mm_struct;
struct pt_regs;
struct task_struct;
struct timeval;
struct vm_area_struct;
struct acpi_entry_iosapic;

typedef void ia64_mv_setup_t (char **);
typedef void ia64_mv_irq_init_t (void);
typedef void ia64_mv_pci_fixup_t (void);
typedef unsigned long ia64_mv_map_nr_t (unsigned long);
typedef void ia64_mv_mca_init_t (void);
typedef void ia64_mv_mca_handler_t (void);
typedef void ia64_mv_cmci_handler_t (int, void *, struct pt_regs *);
typedef void ia64_mv_log_print_t (void);
typedef void ia64_mv_register_iosapic_t (struct acpi_entry_iosapic *);

extern void machvec_noop (void);

# if defined (CONFIG_IA64_HP_SIM)
#  include <asm/machvec_hpsim.h>
# elif defined (CONFIG_IA64_DIG)
#  include <asm/machvec_dig.h>
# elif defined (CONFIG_IA64_SGI_SN1_SIM)
#  include <asm/machvec_sn1.h>
# elif defined (CONFIG_IA64_GENERIC)

# ifdef MACHVEC_PLATFORM_HEADER
#  include MACHVEC_PLATFORM_HEADER
# else
#  define platform_name		ia64_mv.name
#  define platform_setup	ia64_mv.setup
#  define platform_irq_init	ia64_mv.irq_init
#  define platform_map_nr	ia64_mv.map_nr
#  define platform_mca_init	ia64_mv.mca_init
#  define platform_mca_handler	ia64_mv.mca_handler
#  define platform_cmci_handler	ia64_mv.cmci_handler
#  define platform_log_print	ia64_mv.log_print
#  define platform_pci_fixup	ia64_mv.pci_fixup
#  define platform_register_iosapic	ia64_mv.register_iosapic
# endif

struct ia64_machine_vector {
	const char *name;
	ia64_mv_setup_t *setup;
	ia64_mv_irq_init_t *irq_init;
	ia64_mv_pci_fixup_t *pci_fixup;
	ia64_mv_map_nr_t *map_nr;
	ia64_mv_mca_init_t *mca_init;
	ia64_mv_mca_handler_t *mca_handler;
	ia64_mv_cmci_handler_t *cmci_handler;
	ia64_mv_log_print_t *log_print;
	ia64_mv_register_iosapic_t *register_iosapic;
};

#define MACHVEC_INIT(name)			\
{						\
	#name,					\
	platform_setup,				\
	platform_irq_init,			\
	platform_pci_fixup,			\
	platform_map_nr,			\
	platform_mca_init,			\
	platform_mca_handler,			\
	platform_cmci_handler,			\
	platform_log_print,			\
	platform_register_iosapic			\
}

extern struct ia64_machine_vector ia64_mv;
extern void machvec_init (const char *name);

# else
#  error Unknown configuration.  Update asm-ia64/machvec.h.
# endif /* CONFIG_IA64_GENERIC */

/*
 * Define default versions so we can extend machvec for new platforms without having
 * to update the machvec files for all existing platforms.
 */
#ifndef platform_setup
# define platform_setup		((ia64_mv_setup_t *) machvec_noop)
#endif
#ifndef platform_irq_init
# define platform_irq_init	((ia64_mv_irq_init_t *) machvec_noop)
#endif
#ifndef platform_mca_init
# define platform_mca_init	((ia64_mv_mca_init_t *) machvec_noop)
#endif
#ifndef platform_mca_handler
# define platform_mca_handler	((ia64_mv_mca_handler_t *) machvec_noop)
#endif
#ifndef platform_cmci_handler
# define platform_cmci_handler	((ia64_mv_cmci_handler_t *) machvec_noop)
#endif
#ifndef platform_log_print
# define platform_log_print	((ia64_mv_log_print_t *) machvec_noop)
#endif
#ifndef platform_pci_fixup
# define platform_pci_fixup	((ia64_mv_pci_fixup_t *) machvec_noop)
#endif
#ifndef platform_register_iosapic
# define platform_register_iosapic	((ia64_mv_register_iosapic_t *) machvec_noop)
#endif

#endif /* _ASM_IA64_MACHVEC_H */
