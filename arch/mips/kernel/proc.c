/*
 *  linux/arch/mips/kernel/proc.c
 *
 *  Copyright (C) 1995, 1996  Ralf Baechle
 */
#include <linux/delay.h>
#include <linux/kernel.h>
#include <asm/bootinfo.h>
#include <asm/mipsregs.h>

unsigned long dflushes = 0;
unsigned long iflushes = 0;
unsigned long unaligned_instructions;

/*
 * BUFFER is PAGE_SIZE bytes long.
 *
 * Currently /proc/cpuinfo is being abused to print data about the
 * number of date/instruction cacheflushes.
 */
int get_cpuinfo(char *buffer)
{
	const char *cpu_name[] = CPU_NAMES;
	const char *mach_group_names[] = GROUP_NAMES;
	const char *mach_unknown_names[] = GROUP_UNKNOWN_NAMES;
	const char *mach_jazz_names[] = GROUP_JAZZ_NAMES;
	const char *mach_dec_names[] = GROUP_DEC_NAMES;
	const char *mach_arc_names[] = GROUP_ARC_NAMES;
	const char *mach_sni_rm_names[] = GROUP_SNI_RM_NAMES;
	const char **mach_group_to_name[] = { mach_unknown_names, mach_jazz_names,
					    mach_dec_names, mach_arc_names, mach_sni_rm_names};
	unsigned int version = read_32bit_cp0_register(CP0_PRID);
	int len;

	len = sprintf(buffer, "cpu\t\t\t: MIPS\n");
	len += sprintf(buffer + len, "cpu model\t\t: %s V%d.%d\n",
	               cpu_name[mips_cputype <= CPU_LAST ?
	                        mips_cputype :
	                        CPU_UNKNOWN],
	               (version >> 4) & 0x0f,
	               version & 0x0f);
	len += sprintf(buffer + len, "system type\t\t: %s %s\n",
		       mach_group_names[mips_machgroup],
		       mach_group_to_name[mips_machgroup][mips_machtype]);
	len += sprintf(buffer + len, "BogoMIPS\t\t: %lu.%02lu\n",
		       (loops_per_sec + 2500) / 500000,
	               ((loops_per_sec + 2500) / 5000) % 100);
#if defined (__MIPSEB__)
	len += sprintf(buffer + len, "byteorder\t\t: big endian\n");
#endif
#if defined (__MIPSEL__)
	len += sprintf(buffer + len, "byteorder\t\t: little endian\n");
#endif
	len += sprintf(buffer + len, "D-cache flushes\t\t: %lu\n",
		       dflushes);
	len += sprintf(buffer + len, "I-cache flushes\t\t: %lu\n",
		       iflushes);
	len += sprintf(buffer + len, "unaligned accesses\t: %lu\n",
		       unaligned_instructions);

	return len;
}
