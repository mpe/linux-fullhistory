#include <linux/config.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/elfcore.h>
#include <linux/sched.h>

#include <asm/semaphore.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/io.h>

void transfer_to_handler();
void int_return();
void syscall_trace();
void handle_IRQ();
void MachineCheckException();
void AlignmentException();
void ProgramCheckException();
void SingleStepException();
void FloatingPointCheckException();
void sys_sigreturn();
unsigned long sys_call_table[];

extern struct task_struct *current_set[1];

/* platform dependent support */
EXPORT_SYMBOL(current_set);
EXPORT_SYMBOL(do_signal);
EXPORT_SYMBOL(syscall_trace);
EXPORT_SYMBOL(transfer_to_handler);
EXPORT_SYMBOL(int_return);
EXPORT_SYMBOL(handle_IRQ);
EXPORT_SYMBOL(init_task_union);
EXPORT_SYMBOL(MachineCheckException);
EXPORT_SYMBOL(AlignmentException);
EXPORT_SYMBOL(ProgramCheckException);
EXPORT_SYMBOL(SingleStepException);
EXPORT_SYMBOL(FloatingPointCheckException);
EXPORT_SYMBOL(sys_sigreturn);
EXPORT_SYMBOL(sys_call_table);
