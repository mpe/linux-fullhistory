/* Prototypes of functions used across modules here in this directory.  */

#define vucp	volatile unsigned char  *
#define vusp	volatile unsigned short *
#define vip	volatile int *
#define vuip	volatile unsigned int   *
#define vulp	volatile unsigned long  *

struct linux_hose_info;

/* core_apecs.c */
extern int apecs_hose_read_config_byte (u8, u8, u8, u8 *value,
					struct linux_hose_info *hose);
extern int apecs_hose_read_config_word (u8, u8, u8, u16 *value,
					struct linux_hose_info *hose);
extern int apecs_hose_read_config_dword (u8, u8, u8, u32 *value,
					 struct linux_hose_info *hose);
extern int apecs_hose_write_config_byte (u8, u8, u8, u8 value,
					 struct linux_hose_info *hose);
extern int apecs_hose_write_config_word (u8, u8, u8, u16 value,
					 struct linux_hose_info *hose);
extern int apecs_hose_write_config_dword (u8, u8, u8, u32 value,
					  struct linux_hose_info *hose);
extern void apecs_init_arch(unsigned long *, unsigned long *);

extern volatile unsigned int apecs_mcheck_expected;
extern volatile unsigned int apecs_mcheck_taken;
extern int apecs_pci_clr_err(void);
extern void apecs_machine_check(u64, u64, struct pt_regs *);

/* core_cia.c */
extern int cia_hose_read_config_byte (u8, u8, u8, u8 *value,
				      struct linux_hose_info *hose);
extern int cia_hose_read_config_word (u8, u8, u8, u16 *value,
				      struct linux_hose_info *hose);
extern int cia_hose_read_config_dword (u8, u8, u8, u32 *value,
				       struct linux_hose_info *hose);
extern int cia_hose_write_config_byte (u8, u8, u8, u8 value,
				       struct linux_hose_info *hose);
extern int cia_hose_write_config_word (u8, u8, u8, u16 value,
				       struct linux_hose_info *hose);
extern int cia_hose_write_config_dword (u8, u8, u8, u32 value,
					struct linux_hose_info *hose);
extern void cia_init_arch(unsigned long *, unsigned long *);
extern void cia_machine_check(u64, u64, struct pt_regs *);

/* core_lca.c */
extern int lca_hose_read_config_byte (u8, u8, u8, u8 *value,
				      struct linux_hose_info *hose);
extern int lca_hose_read_config_word (u8, u8, u8, u16 *value,
				      struct linux_hose_info *hose);
extern int lca_hose_read_config_dword (u8, u8, u8, u32 *value,
				       struct linux_hose_info *hose);
extern int lca_hose_write_config_byte (u8, u8, u8, u8 value,
				       struct linux_hose_info *hose);
extern int lca_hose_write_config_word (u8, u8, u8, u16 value,
				       struct linux_hose_info *hose);
extern int lca_hose_write_config_dword (u8, u8, u8, u32 value,
					struct linux_hose_info *hose);
extern void lca_init_arch(unsigned long *, unsigned long *);
extern void lca_machine_check(u64, u64, struct pt_regs *);

/* core_mcpcia.c */
extern int mcpcia_hose_read_config_byte (u8, u8, u8, u8 *value,
					 struct linux_hose_info *hose);
extern int mcpcia_hose_read_config_word (u8, u8, u8, u16 *value,
					 struct linux_hose_info *hose);
extern int mcpcia_hose_read_config_dword (u8, u8, u8, u32 *value,
					  struct linux_hose_info *hose);
extern int mcpcia_hose_write_config_byte (u8, u8, u8, u8 value,
					  struct linux_hose_info *hose);
extern int mcpcia_hose_write_config_word (u8, u8, u8, u16 value,
					  struct linux_hose_info *hose);
extern int mcpcia_hose_write_config_dword (u8, u8, u8, u32 value,
					   struct linux_hose_info *hose);
extern void mcpcia_init_arch(unsigned long *, unsigned long *);
extern void mcpcia_machine_check(u64, u64, struct pt_regs *);

/* core_polaris.c */
extern int polaris_hose_read_config_byte (u8, u8, u8, u8 *value,
					struct linux_hose_info *hose);
extern int polaris_hose_read_config_word (u8, u8, u8, u16 *value,
					struct linux_hose_info *hose);
extern int polaris_hose_read_config_dword (u8, u8, u8, u32 *value,
					 struct linux_hose_info *hose);
extern int polaris_hose_write_config_byte (u8, u8, u8, u8 value,
					 struct linux_hose_info *hose);
extern int polaris_hose_write_config_word (u8, u8, u8, u16 value,
					 struct linux_hose_info *hose);
extern int polaris_hose_write_config_dword (u8, u8, u8, u32 value,
					  struct linux_hose_info *hose);
extern void polaris_init_arch(unsigned long *, unsigned long *);
extern void polaris_machine_check(u64, u64, struct pt_regs *);

/* core_pyxis.c */
extern int pyxis_hose_read_config_byte (u8, u8, u8, u8 *value,
					struct linux_hose_info *hose);
extern int pyxis_hose_read_config_word (u8, u8, u8, u16 *value,
					struct linux_hose_info *hose);
extern int pyxis_hose_read_config_dword (u8, u8, u8, u32 *value,
					 struct linux_hose_info *hose);
extern int pyxis_hose_write_config_byte (u8, u8, u8, u8 value,
					 struct linux_hose_info *hose);
extern int pyxis_hose_write_config_word (u8, u8, u8, u16 value,
					 struct linux_hose_info *hose);
extern int pyxis_hose_write_config_dword (u8, u8, u8, u32 value,
					  struct linux_hose_info *hose);
extern void pyxis_enable_errors (void);
extern int pyxis_srm_window_setup (void);
extern void pyxis_native_window_setup(void);
extern void pyxis_finish_init_arch(void);
extern void pyxis_init_arch(unsigned long *, unsigned long *);
extern void pyxis_machine_check(u64, u64, struct pt_regs *);

/* core_t2.c */
extern int t2_hose_read_config_byte (u8, u8, u8, u8 *value,
				     struct linux_hose_info *hose);
extern int t2_hose_read_config_word (u8, u8, u8, u16 *value,
				     struct linux_hose_info *hose);
extern int t2_hose_read_config_dword (u8, u8, u8, u32 *value,
				      struct linux_hose_info *hose);
extern int t2_hose_write_config_byte (u8, u8, u8, u8 value,
				      struct linux_hose_info *hose);
extern int t2_hose_write_config_word (u8, u8, u8, u16 value,
				      struct linux_hose_info *hose);
extern int t2_hose_write_config_dword (u8, u8, u8, u32 value,
				       struct linux_hose_info *hose);
extern void t2_init_arch(unsigned long *, unsigned long *);
extern void t2_machine_check(u64, u64, struct pt_regs *);

/* core_tsunami.c */
extern int tsunami_hose_read_config_byte (u8, u8, u8, u8 *value,
					  struct linux_hose_info *hose);
extern int tsunami_hose_read_config_word (u8, u8, u8, u16 *value,
					  struct linux_hose_info *hose);
extern int tsunami_hose_read_config_dword (u8, u8, u8, u32 *value,
					   struct linux_hose_info *hose);
extern int tsunami_hose_write_config_byte (u8, u8, u8, u8 value,
					   struct linux_hose_info *hose);
extern int tsunami_hose_write_config_word (u8, u8, u8, u16 value,
					   struct linux_hose_info *hose);
extern int tsunami_hose_write_config_dword (u8, u8, u8, u32 value,
					    struct linux_hose_info *hose);
extern void tsunami_init_arch(unsigned long *, unsigned long *);
extern void tsunami_machine_check(u64, u64, struct pt_regs *);

/* setup.c */
extern unsigned long srm_hae;

/* smp.c */
extern void setup_smp(void);
extern int smp_info(char *buffer);
extern void handle_ipi(struct pt_regs *);

/* bios32.c */
extern void reset_for_srm(void);

/* time.c */
extern void timer_interrupt(int irq, void *dev, struct pt_regs * regs);
extern void rtc_init_pit(void);
extern void generic_init_pit(void);
extern unsigned long est_cycle_freq;

/* smc37c93x.c */
extern void SMC93x_Init(void);

/* smc37c669.c */
extern void SMC669_Init(void);

/* es1888.c */
extern void es1888_init(void);

/* fpregs.c */
extern void alpha_write_fp_reg (unsigned long reg, unsigned long val);
extern unsigned long alpha_read_fp_reg (unsigned long reg);

/* head.S */
extern void wrmces(unsigned long mces);
extern void cserve_ena(unsigned long);
extern void cserve_dis(unsigned long);
extern void __start_cpu(unsigned long);

/* entry.S */
extern void entArith(void);
extern void entIF(void);
extern void entInt(void);
extern void entMM(void);
extern void entSys(void);
extern void entUna(void);

/* process.c */
extern void generic_kill_arch (int mode, char *reboot_cmd);
extern void cpu_idle(void *) __attribute__((noreturn));

/* ptrace.c */
extern int ptrace_set_bpt (struct task_struct *child);
extern int ptrace_cancel_bpt (struct task_struct *child);

/* ../mm/init.c */
void srm_paging_stop(void);
