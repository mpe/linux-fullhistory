/*
 *  linux/arch/ppc/amiga/bootinfo.c
 *
 *  Extracted from arch/m68k/kernel/setup.c.
 *  Should be properly generalized and put somewhere else.
 *                              Jesper
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>

#include <asm/setup.h>
#include <asm/bootinfo.h>

extern char cmd_line[CL_SIZE];

int num_memory = 0;
struct mem_info memory[NUM_MEMINFO];
struct mem_info ramdisk;

extern int amiga_parse_bootinfo(const struct bi_record *);
extern int atari_parse_bootinfo(const struct bi_record *);
extern int mac_parse_bootinfo(const struct bi_record *);

__initfunc(void parse_bootinfo(const struct bi_record *record))
{
    while (record->tag != BI_LAST) {
	int unknown = 0;
	const u_long *data = record->data;
	switch (record->tag) {
	    case BI_MACHTYPE:
	    case BI_CPUTYPE:
	    case BI_FPUTYPE:
	    case BI_MMUTYPE:
		/* Already set up by head.S */
		break;

	    case BI_MEMCHUNK:
		if (num_memory < NUM_MEMINFO) {
		    memory[num_memory].addr = data[0];
		    memory[num_memory].size = data[1];
		    num_memory++;
		} else
		    printk("parse_bootinfo: too many memory chunks\n");
		break;

	    case BI_RAMDISK:
		ramdisk.addr = data[0];
		ramdisk.size = data[1];
		break;

	    case BI_COMMAND_LINE:
		strncpy(cmd_line, (const char *)data, CL_SIZE);
		cmd_line[CL_SIZE-1] = '\0';
		break;

	    default:
		if (MACH_IS_AMIGA)
		    unknown = amiga_parse_bootinfo(record);
		else if (MACH_IS_ATARI)
		    unknown = atari_parse_bootinfo(record);
		else if (MACH_IS_MAC)
		    unknown = mac_parse_bootinfo(record);
		else
		    unknown = 1;
	}
	if (unknown)
	    printk("parse_bootinfo: unknown tag 0x%04x ignored\n",
		   record->tag);
	record = (struct bi_record *)((u_long)record+record->size);
    }
}
