#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/kdb.h>

static struct kdb_bp_support {
	unsigned long	addr ;
	int		slot ;
} kdb_bp_info[NR_CPUS] ;


extern void kdb_bp_install (void);

/*
 * This gets invoked right before a call to ia64_fault().
 * Returns zero the normal fault handler should be invoked.
 */
long
ia64_kdb_fault_handler (unsigned long vector, unsigned long isr, unsigned long ifa,
			unsigned long iim, unsigned long itir, unsigned long arg5,
			unsigned long arg6, unsigned long arg7, unsigned long stack)
{
	struct switch_stack *sw = (struct switch_stack *) &stack;
	struct pt_regs *regs = (struct pt_regs *) (sw + 1);
	int bundle_slot;

	/*
	 * TBD
	 * If KDB is configured, enter KDB for any fault.
	 */
	if ((vector == 29) || (vector == 35) || (vector == 36)) {
		if (!user_mode(regs)) {
			bundle_slot = ia64_psr(regs)->ri;
			if (vector == 29) {
				if (bundle_slot == 0) {
			 		kdb_bp_info[0].addr = regs->cr_iip;
					kdb_bp_info[0].slot = bundle_slot;
					kdb(KDB_REASON_FLTDBG, 0, regs);
				} else {
					if ((bundle_slot < 3) &&
					    (kdb_bp_info[0].addr == regs->cr_iip))
					{
						ia64_psr(regs)->id = 1;
						ia64_psr(regs)->db = 1;
						kdb_bp_install() ;
					} else /* some error ?? */
						kdb(KDB_REASON_FLTDBG, 0, regs);
				}
			} else /* single step or taken branch */
				kdb(KDB_REASON_DEBUG, 0, regs);
			return 1;
		}
	}
	return 0;
}
