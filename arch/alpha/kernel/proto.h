/* Prototypes of functions used across modules here in this directory.  */

#define vucp	volatile unsigned char  *
#define vusp	volatile unsigned short *
#define vip	volatile int *
#define vuip	volatile unsigned int   *
#define vulp	volatile unsigned long  *

struct pt_regs;
struct task_struct;
struct pci_dev;

/* core_apecs.c */
extern struct pci_ops apecs_pci_ops;
extern void apecs_init_arch(void);
extern void apecs_pci_clr_err(void);
extern void apecs_machine_check(u64, u64, struct pt_regs *);

/* core_cia.c */
extern struct pci_ops cia_pci_ops;
extern void cia_init_arch(void);
extern void cia_machine_check(u64, u64, struct pt_regs *);

/* core_lca.c */
extern struct pci_ops lca_pci_ops;
extern void lca_init_arch(void);
extern void lca_machine_check(u64, u64, struct pt_regs *);

/* core_mcpcia.c */
extern struct pci_ops mcpcia_pci_ops;
extern void mcpcia_init_arch(void);
extern void mcpcia_machine_check(u64, u64, struct pt_regs *);

/* core_polaris.c */
extern struct pci_ops polaris_pci_ops;
extern void polaris_init_arch(void);
extern void polaris_machine_check(u64, u64, struct pt_regs *);

/* core_pyxis.c */
extern struct pci_ops pyxis_pci_ops;
extern void pyxis_init_arch(void);
extern void pyxis_machine_check(u64, u64, struct pt_regs *);

/* core_t2.c */
extern struct pci_ops t2_pci_ops;
extern void t2_init_arch(void);
extern void t2_machine_check(u64, u64, struct pt_regs *);

/* core_tsunami.c */
extern struct pci_ops tsunami_pci_ops;
extern void tsunami_init_arch(void);
extern void tsunami_machine_check(u64, u64, struct pt_regs *);

/* setup.c */
extern unsigned long srm_hae;

/* smp.c */
extern void setup_smp(void);
extern int smp_info(char *buffer);
extern void handle_ipi(struct pt_regs *);
extern void smp_percpu_timer_interrupt(struct pt_regs *);
extern int smp_boot_cpuid;

/* bios32.c */
/* extern void reset_for_srm(void); */

/* time.c */
extern void timer_interrupt(int irq, void *dev, struct pt_regs * regs);
extern void rtc_init_pit(void);
extern void common_init_pit(void);
extern unsigned long est_cycle_freq;

/* smc37c93x.c */
extern void SMC93x_Init(void);

/* smc37c669.c */
extern void SMC669_Init(int);

/* es1888.c */
extern void es1888_init(void);

/* ns87312.c */
extern void ns87312_enable_ide(long ide_base);

/* fpregs.c */
extern void alpha_write_fp_reg (unsigned long reg, unsigned long val);
extern unsigned long alpha_read_fp_reg (unsigned long reg);

/* head.S */
extern void wrmces(unsigned long mces);
extern void cserve_ena(unsigned long);
extern void cserve_dis(unsigned long);
extern void __smp_callin(unsigned long);

/* entry.S */
extern void entArith(void);
extern void entIF(void);
extern void entInt(void);
extern void entMM(void);
extern void entSys(void);
extern void entUna(void);
extern void entDbg(void);

/* process.c */
extern void common_kill_arch (int mode, char *reboot_cmd);
extern void cpu_idle(void) __attribute__((noreturn));

/* ptrace.c */
extern int ptrace_set_bpt (struct task_struct *child);
extern int ptrace_cancel_bpt (struct task_struct *child);

/* traps.c */
extern void dik_show_regs(struct pt_regs *regs, unsigned long *r9_15);
extern void die_if_kernel(char *, struct pt_regs *, long, unsigned long *);

/* ../mm/init.c */
void srm_paging_stop(void);

/* irq.h */

#ifdef __SMP__
#define mcheck_expected(cpu)	(cpu_data[cpu].mcheck_expected)
#define mcheck_taken(cpu)	(cpu_data[cpu].mcheck_taken)
#define mcheck_extra(cpu)	(cpu_data[cpu].mcheck_extra)
#else
extern struct mcheck_info
{
	unsigned char expected __attribute__((aligned(8)));
	unsigned char taken;
	unsigned char extra;
} __mcheck_info;

#define mcheck_expected(cpu)	(__mcheck_info.expected)
#define mcheck_taken(cpu)	(__mcheck_info.taken)
#define mcheck_extra(cpu)	(__mcheck_info.extra)
#endif

#define DEBUG_MCHECK 0          /* 0 = minimal, 1 = debug, 2 = debug+dump.  */

extern void process_mcheck_info(unsigned long vector, unsigned long la_ptr,
				struct pt_regs *regs, const char *machine,
				int expected);
