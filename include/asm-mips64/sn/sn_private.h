extern cnodeid_t get_compact_nodeid(void);
extern void hub_rtc_init(cnodeid_t);
extern void cpu_time_init(void);
extern void per_cpu_init(void);
extern void install_cpuintr(cpuid_t cpu);
extern void install_tlbintr(cpuid_t cpu);
