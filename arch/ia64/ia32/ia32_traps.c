#include <linux/kernel.h>
#include <linux/sched.h>

#include <asm/ia32.h>
#include <asm/ptrace.h>

int
ia32_exception (struct pt_regs *regs, unsigned long isr)
{
	struct siginfo siginfo;

	switch ((isr >> 16) & 0xff) {
	      case 1:
	      case 2:
		if (isr == 0)
			siginfo.si_code = TRAP_TRACE;
		else if (isr & 0x4)
			siginfo.si_code = TRAP_BRANCH;
		else
			siginfo.si_code = TRAP_BRKPT;
		break;

	      case 3:
		siginfo.si_code = TRAP_BRKPT;
		break;

	      case 0:	/* Divide fault */
	      case 4:	/* Overflow */
	      case 5:	/* Bounds fault */
	      case 6:	/* Invalid Op-code */
	      case 7:	/* FP DNA */
	      case 8:	/* Double Fault */
	      case 9:	/* Invalid TSS */
	      case 11:	/* Segment not present */
	      case 12:	/* Stack fault */
	      case 13:	/* General Protection Fault */
	      case 16:	/* Pending FP error */
	      case 17:	/* Alignment check */
	      case 19:	/* SSE Numeric error */
	      default:
		return -1;
	}
	siginfo.si_signo = SIGTRAP;
	siginfo.si_errno = 0;
	send_sig_info(SIGTRAP, &siginfo, current);
	return 0;
}
