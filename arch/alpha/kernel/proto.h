/* Prototypes of functions used across modules here in this directory.  */

#define vucp	volatile unsigned char  *
#define vusp	volatile unsigned short *
#define vip	volatile int *
#define vuip	volatile unsigned int   *
#define vulp	volatile unsigned long  *

/* core_apecs.c */
extern int apecs_pcibios_read_config_byte (u8, u8, u8, u8 *value);
extern int apecs_pcibios_read_config_word (u8, u8, u8, u16 *value);
extern int apecs_pcibios_read_config_dword (u8, u8, u8, u32 *value);
extern int apecs_pcibios_write_config_byte (u8, u8, u8, u8 value);
extern int apecs_pcibios_write_config_word (u8, u8, u8, u16 value);
extern int apecs_pcibios_write_config_dword (u8, u8, u8, u32 value);
extern void apecs_init_arch(unsigned long *, unsigned long *);

extern volatile unsigned int apecs_mcheck_expected;
extern volatile unsigned int apecs_mcheck_taken;
extern int apecs_pci_clr_err(void);
extern void apecs_machine_check(u64, u64, struct pt_regs *);

/* core_cia.c */
extern int cia_pcibios_read_config_byte (u8, u8, u8, u8 *value);
extern int cia_pcibios_read_config_word (u8, u8, u8, u16 *value);
extern int cia_pcibios_read_config_dword (u8, u8, u8, u32 *value);
extern int cia_pcibios_write_config_byte (u8, u8, u8, u8 value);
extern int cia_pcibios_write_config_word (u8, u8, u8, u16 value);
extern int cia_pcibios_write_config_dword (u8, u8, u8, u32 value);
extern void cia_init_arch(unsigned long *, unsigned long *);
extern void cia_machine_check(u64, u64, struct pt_regs *);

/* core_lca.c */
extern int lca_pcibios_read_config_byte (u8, u8, u8, u8 *value);
extern int lca_pcibios_read_config_word (u8, u8, u8, u16 *value);
extern int lca_pcibios_read_config_dword (u8, u8, u8, u32 *value);
extern int lca_pcibios_write_config_byte (u8, u8, u8, u8 value);
extern int lca_pcibios_write_config_word (u8, u8, u8, u16 value);
extern int lca_pcibios_write_config_dword (u8, u8, u8, u32 value);
extern void lca_init_arch(unsigned long *, unsigned long *);
extern void lca_machine_check(u64, u64, struct pt_regs *);

/* core_mcpcia.c */
extern int mcpcia_pcibios_read_config_byte (u8, u8, u8, u8 *value);
extern int mcpcia_pcibios_read_config_word (u8, u8, u8, u16 *value);
extern int mcpcia_pcibios_read_config_dword (u8, u8, u8, u32 *value);
extern int mcpcia_pcibios_write_config_byte (u8, u8, u8, u8 value);
extern int mcpcia_pcibios_write_config_word (u8, u8, u8, u16 value);
extern int mcpcia_pcibios_write_config_dword (u8, u8, u8, u32 value);
extern void mcpcia_init_arch(unsigned long *, unsigned long *);
extern void mcpcia_machine_check(u64, u64, struct pt_regs *);
extern void mcpcia_pci_fixup(void);

/* core_pyxis.c */
extern int pyxis_pcibios_read_config_byte (u8, u8, u8, u8 *value);
extern int pyxis_pcibios_read_config_word (u8, u8, u8, u16 *value);
extern int pyxis_pcibios_read_config_dword (u8, u8, u8, u32 *value);
extern int pyxis_pcibios_write_config_byte (u8, u8, u8, u8 value);
extern int pyxis_pcibios_write_config_word (u8, u8, u8, u16 value);
extern int pyxis_pcibios_write_config_dword (u8, u8, u8, u32 value);
extern void pyxis_enable_errors (void);
extern int pyxis_srm_window_setup (void);
extern void pyxis_native_window_setup(void);
extern void pyxis_finish_init_arch(void);
extern void pyxis_init_arch(unsigned long *, unsigned long *);
extern void pyxis_machine_check(u64, u64, struct pt_regs *);

/* core_t2.c */
extern int t2_pcibios_read_config_byte (u8, u8, u8, u8 *value);
extern int t2_pcibios_read_config_word (u8, u8, u8, u16 *value);
extern int t2_pcibios_read_config_dword (u8, u8, u8, u32 *value);
extern int t2_pcibios_write_config_byte (u8, u8, u8, u8 value);
extern int t2_pcibios_write_config_word (u8, u8, u8, u16 value);
extern int t2_pcibios_write_config_dword (u8, u8, u8, u32 value);
extern void t2_init_arch(unsigned long *, unsigned long *);
extern void t2_machine_check(u64, u64, struct pt_regs *);

/* core_tsunami.c */
extern int tsunami_pcibios_read_config_byte (u8, u8, u8, u8 *value);
extern int tsunami_pcibios_read_config_word (u8, u8, u8, u16 *value);
extern int tsunami_pcibios_read_config_dword (u8, u8, u8, u32 *value);
extern int tsunami_pcibios_write_config_byte (u8, u8, u8, u8 value);
extern int tsunami_pcibios_write_config_word (u8, u8, u8, u16 value);
extern int tsunami_pcibios_write_config_dword (u8, u8, u8, u32 value);
extern void tsunami_init_arch(unsigned long *, unsigned long *);
extern void tsunami_machine_check(u64, u64, struct pt_regs *);

/* setup.c */
extern void init_pit_rest(void);
extern void generic_init_pit (void);
extern unsigned long srm_hae;

/* smp.c */
extern void setup_smp(void);
extern char *smp_info(void);
extern void handle_ipi(struct pt_regs *);

/* bios32.c */
extern void reset_for_srm(void);

/* time.c */
extern void timer_interrupt(struct pt_regs * regs);

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

/* entry.S */
extern void entArith(void);
extern void entIF(void);
extern void entInt(void);
extern void entMM(void);
extern void entSys(void);
extern void entUna(void);

/* process.c */
void generic_kill_arch (int mode, char *reboot_cmd);
