/* <hack>
   This is just a huge ugly hack to get things compiled.
   Hopefully will disappear quickly, once we get everything
   to compile... */
	.text
	.globl ROOT_DEV
ROOT_DEV:
	nop
	.globl ___lock_kernel
___lock_kernel:
	nop
	.globl ___unlock_kernel
___unlock_kernel:
	nop
	.globl __memcpy
__memcpy:
	nop
	.globl __strncmp
__strncmp:
	nop
	.globl _ctype
_ctype:
	nop
	.globl _stext
_stext:
	nop
	.globl bad_trap
bad_trap:
	nop
	.globl bad_trap_tl1
bad_trap_tl1:
	nop
	.globl bdflush
bdflush:
	nop
	.globl breakpoint_trap
breakpoint_trap:
	nop
	.globl buff_setup
buff_setup:
	nop
	.globl buffer_init
buffer_init:
	nop
	.globl change_root
change_root:
	nop
	.globl console_init
console_init:
	nop
	.globl console_loglevel
console_loglevel:
	nop
	.globl console_restore_palette
console_restore_palette:
	nop
	.globl current_set
current_set:
	nop
	.globl do_cee
do_cee:
	nop
	.globl do_cee_tl1
do_cee_tl1:
	nop
	.globl do_dae
do_dae:
	nop
	.globl do_dae_tl1
do_dae_tl1:
	nop
	.globl do_dax
do_dax:
	nop
	.globl do_dax_tl1
do_dax_tl1:
	nop
	.globl do_div0
do_div0:
	nop
	.globl do_div0_tl1
do_div0_tl1:
	nop
	.globl do_fpdis
do_fpdis:
	nop
	.globl do_fpdis_tl1
do_fpdis_tl1:
	nop
	.globl do_fpieee
do_fpieee:
	nop
	.globl do_fpieee_tl1
do_fpieee_tl1:
	nop
	.globl do_fpother
do_fpother:
	nop
	.globl do_fpother_tl1
do_fpother_tl1:
	nop
	.globl do_iae
do_iae:
	nop
	.globl do_iae_tl1
do_iae_tl1:
	nop
	.globl do_iax
do_iax:
	nop
	.globl do_iax_tl1
do_iax_tl1:
	nop
	.globl do_ill
do_ill:
	nop
	.globl do_ill_tl1
do_ill_tl1:
	nop
	.globl do_irq
do_irq:
	nop
	.globl do_irq_tl1
do_irq_tl1:
	nop
	.globl do_ivec
do_ivec:
	nop
	.globl do_ivec_tl1
do_ivec_tl1:
	nop
	.globl do_lddfmna
do_lddfmna:
	nop
	.globl do_lddfmna_tl1
do_lddfmna_tl1:
	nop
	.globl do_mna
do_mna:
	nop
	.globl do_mna_tl1
do_mna_tl1:
	nop
	.globl do_paw
do_paw:
	nop
	.globl do_paw_tl1
do_paw_tl1:
	nop
	.globl do_privact
do_privact:
	nop
	.globl do_privop
do_privop:
	nop
	.globl do_stdfmna
do_stdfmna:
	nop
	.globl do_stdfmna_tl1
do_stdfmna_tl1:
	nop
	.globl do_tof
do_tof:
	nop
	.globl do_tof_tl1
do_tof_tl1:
	nop
	.globl do_vaw
do_vaw:
	nop
	.globl do_vaw_tl1
do_vaw_tl1:
	nop
	.globl dquot_init
dquot_init:
	nop
	.globl errno
errno:
	nop
	.globl eth_setup
eth_setup:
	nop
	.globl file_table_init
file_table_init:
	nop
	.globl floppy_setup
floppy_setup:
	nop
	.globl getcc
getcc:
	nop
	.globl indirect_syscall
indirect_syscall:
	nop
	.globl init_IRQ
init_IRQ:
	nop
	.globl init_task
init_task:
	nop
	.globl initrd_below_start_ok
initrd_below_start_ok:
	nop
	.globl initrd_start
initrd_start:
	nop
	.globl inode_init
inode_init:
	nop
	.globl install_linux_ticker
install_linux_ticker:
	nop
	.globl install_obp_ticker
install_obp_ticker:
	nop
	.globl ipc_init
ipc_init:
	nop
	.globl jiffies
jiffies:
	nop
	.globl kernel_enter_debugger
kernel_enter_debugger:
	nop
	.globl kmalloc_init
kmalloc_init:
	nop
	.globl kmem_cache_init
kmem_cache_init:
	nop
	.globl kmem_cache_sizes_init
kmem_cache_sizes_init:
	nop
	.globl kswapd
kswapd:
	nop
	.globl linux32_syscall
linux32_syscall:
	nop
	.globl linux64_syscall
linux64_syscall:
	nop
	.globl lookup_fault
lookup_fault:
	nop
	.globl mem_init
mem_init:
	nop
	.globl memcpy
memcpy:
	nop
	.globl mount_initrd
mount_initrd:
	nop
	.globl name_cache_init
name_cache_init:
	nop
	.globl netbsd_syscall
netbsd_syscall:
	nop
	.globl no_scroll
no_scroll:
	nop
	.globl paging_init
paging_init:
	nop
	.globl panic
panic:
	nop
	.globl panic_setup
panic_setup:
	nop
	.globl printk
printk:
	nop
	.globl prof_buffer
prof_buffer:
	nop
	.globl prof_len
prof_len:
	nop
	.globl prof_shift
prof_shift:
	nop
	.globl pseudo_root
pseudo_root:
	nop
	.globl rd_doload
rd_doload:
	nop
	.globl rd_image_start
rd_image_start:
	nop
	.globl rd_prompt
rd_prompt:
	nop
	.globl rd_size
rd_size:
	nop
	.globl reboot_command
reboot_command:
	nop
	.globl reserve_setup
reserve_setup:
	nop
	.globl sbus_init
sbus_init:
	nop
	.globl sched_init
sched_init:
	nop
	.globl scsi_luns_setup
scsi_luns_setup:
	nop
	.globl serial_console
serial_console:
	nop
	.globl set_palette
set_palette:
	nop
	.globl setcc
setcc:
	nop
	.globl setup_arch
setup_arch:
	nop
	.globl simple_strtoul
simple_strtoul:
	nop
	.globl sock_init
sock_init:
	nop
	.globl solaris_syscall
solaris_syscall:
	nop
	.globl sparc64_dtlb_fault
sparc64_dtlb_fault:
	nop
	.globl sparc64_dtlb_refbit_catch
sparc64_dtlb_refbit_catch:
	nop
	.globl sparc64_itlb_refbit_catch
sparc64_itlb_refbit_catch:
	nop
	.globl sprintf
sprintf:
	nop
	.globl st_setup
st_setup:
	nop
	.globl strchr
strchr:
	nop
	.globl strcmp
strcmp:
	nop
	.globl strcpy
strcpy:
	nop
	.globl strlen
strlen:
	nop
	.globl strncpy
strncpy:
	nop
	.globl sunos_syscall
sunos_syscall:
	nop
	.globl swap_setup
swap_setup:
	nop
	.globl sysctl_init
sysctl_init:
	nop
	.globl time_init
time_init:
	nop
	.globl trap_init
trap_init:
	nop
	.globl vma_init
vma_init:
	nop
	.globl vsprintf
vsprintf:
	nop
	.globl schedule
schedule:
	nop
	.globl getname
getname:
	nop
	.globl do_execve
do_execve:
	nop
	.globl putname
putname:
	nop
