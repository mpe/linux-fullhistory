#include <linux/mm.h>
#include <linux/sched.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/desc.h>

static struct vm_area_struct init_mmap = INIT_MMAP;
static struct fs_struct init_fs = INIT_FS;
static struct file * init_fd_array[NR_OPEN] = { NULL, };
static struct files_struct init_files = INIT_FILES;
static struct signal_struct init_signals = INIT_SIGNALS;
struct mm_struct init_mm = INIT_MM;

/*
 * Initial task structure.
 *
 * We need to make sure that this is 8192-byte aligned due to the
 * way process stacks are handled. This is done by having a special
 * "init_task" linker map entry..
 */
union task_union init_task_union 
	__attribute__((__section__(".data.init_task"))) = { INIT_TASK };
 
