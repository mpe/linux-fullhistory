#include <linux/config.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/elfcore.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/bios32.h>

#include <asm/semaphore.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include <asm/bitops.h>
#include <asm/checksum.h>
#include <asm/pgtable.h>
#include <asm/cuda.h>
#include <asm/prom.h>
#include <asm/system.h>
#include <asm/pci-bridge.h>

void transfer_to_handler();
void int_return();
void syscall_trace();
void do_IRQ();
void MachineCheckException();
void AlignmentException();
void ProgramCheckException();
void SingleStepException();
void FloatingPointCheckException();
void sys_sigreturn();
extern unsigned lost_interrupts;
extern void do_lost_interrupts(unsigned long);

EXPORT_SYMBOL(do_signal);
EXPORT_SYMBOL(syscall_trace);
EXPORT_SYMBOL(transfer_to_handler);
EXPORT_SYMBOL(int_return);
EXPORT_SYMBOL(do_IRQ);
EXPORT_SYMBOL(init_task_union);
EXPORT_SYMBOL(MachineCheckException);
EXPORT_SYMBOL(AlignmentException);
EXPORT_SYMBOL(ProgramCheckException);
EXPORT_SYMBOL(SingleStepException);
EXPORT_SYMBOL(sys_sigreturn);
EXPORT_SYMBOL(lost_interrupts);
EXPORT_SYMBOL(do_lost_interrupts);

EXPORT_SYMBOL(atomic_add);
EXPORT_SYMBOL(atomic_sub);
EXPORT_SYMBOL(atomic_inc);
EXPORT_SYMBOL(atomic_inc_return);
EXPORT_SYMBOL(atomic_dec);
EXPORT_SYMBOL(atomic_dec_return);
EXPORT_SYMBOL(atomic_dec_and_test);

EXPORT_SYMBOL(set_bit);
EXPORT_SYMBOL(clear_bit);
EXPORT_SYMBOL(change_bit);
EXPORT_SYMBOL(test_and_set_bit);
EXPORT_SYMBOL(test_and_clear_bit);
EXPORT_SYMBOL(test_and_change_bit);
#if 0
EXPORT_SYMBOL(ffz);
EXPORT_SYMBOL(find_first_zero_bit);
EXPORT_SYMBOL(find_next_zero_bit);
#endif

EXPORT_SYMBOL(strcpy);
EXPORT_SYMBOL(strncpy);
EXPORT_SYMBOL(strcat);
EXPORT_SYMBOL(strncat);
EXPORT_SYMBOL(strchr);
EXPORT_SYMBOL(strrchr);
EXPORT_SYMBOL(strpbrk);
EXPORT_SYMBOL(strtok);
EXPORT_SYMBOL(strstr);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(strnlen);
EXPORT_SYMBOL(strspn);
EXPORT_SYMBOL(strcmp);
EXPORT_SYMBOL(strncmp);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(memscan);
EXPORT_SYMBOL(memcmp);

/* EXPORT_SYMBOL(csum_partial); already in net/netsyms.c */
EXPORT_SYMBOL(csum_partial_copy_generic);
EXPORT_SYMBOL(ip_fast_csum);
EXPORT_SYMBOL(csum_tcpudp_magic);

EXPORT_SYMBOL(__copy_tofrom_user);
EXPORT_SYMBOL(__clear_user);
EXPORT_SYMBOL(__strncpy_from_user);
EXPORT_SYMBOL(strlen_user);

/*
EXPORT_SYMBOL(inb);
EXPORT_SYMBOL(inw);
EXPORT_SYMBOL(inl);
EXPORT_SYMBOL(outb);
EXPORT_SYMBOL(outw);
EXPORT_SYMBOL(outl);
EXPORT_SYMBOL(outsl);*/

EXPORT_SYMBOL(_insw);
EXPORT_SYMBOL(_outsw);
EXPORT_SYMBOL(_insl);
EXPORT_SYMBOL(_outsl);
EXPORT_SYMBOL(ioremap);

EXPORT_SYMBOL(start_thread);

EXPORT_SYMBOL(__down_interruptible);

EXPORT_SYMBOL(__cli);
EXPORT_SYMBOL(__sti);
/*EXPORT_SYMBOL(__restore_flags);*/
EXPORT_SYMBOL(_disable_interrupts);
EXPORT_SYMBOL(_enable_interrupts);
EXPORT_SYMBOL(flush_instruction_cache);
EXPORT_SYMBOL(_get_PVR);
EXPORT_SYMBOL(giveup_fpu);
EXPORT_SYMBOL(flush_icache_range);
EXPORT_SYMBOL(xchg_u32);

EXPORT_SYMBOL(cuda_request);
EXPORT_SYMBOL(cuda_send_request);
EXPORT_SYMBOL(adb_register);
EXPORT_SYMBOL(abort);
EXPORT_SYMBOL(call_prom);
EXPORT_SYMBOL(find_devices);
EXPORT_SYMBOL(find_type_devices);
EXPORT_SYMBOL(find_path_device);
EXPORT_SYMBOL(get_property);
EXPORT_SYMBOL(pci_io_base);
EXPORT_SYMBOL(pci_device_loc);
EXPORT_SYMBOL(note_scsi_host);
