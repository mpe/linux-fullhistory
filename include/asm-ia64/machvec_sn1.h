#ifndef _ASM_IA64_MACHVEC_SN1_h
#define _ASM_IA64_MACHVEC_SN1_h

extern ia64_mv_setup_t sn1_setup;
extern ia64_mv_irq_init_t sn1_irq_init;
extern ia64_mv_map_nr_t sn1_map_nr;

/*
 * This stuff has dual use!
 *
 * For a generic kernel, the macros are used to initialize the
 * platform's machvec structure.  When compiling a non-generic kernel,
 * the macros are used directly.
 */
#define platform_name		"sn1"
#define platform_setup		sn1_setup
#define platform_irq_init	sn1_irq_init
#define platform_map_nr		sn1_map_nr

#endif /* _ASM_IA64_MACHVEC_SN1_h */
