#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/seq_file.h>
#include <asm/elf.h>
#include <asm/uaccess.h>

char *task_mem(struct mm_struct *mm, char *buffer)
{
	unsigned long data, text, lib;

	data = mm->total_vm - mm->shared_vm - mm->stack_vm;
	text = (mm->end_code - mm->start_code) >> 10;
	lib = (mm->exec_vm << (PAGE_SHIFT-10)) - text;
	buffer += sprintf(buffer,
		"VmSize:\t%8lu kB\n"
		"VmLck:\t%8lu kB\n"
		"VmRSS:\t%8lu kB\n"
		"VmData:\t%8lu kB\n"
		"VmStk:\t%8lu kB\n"
		"VmExe:\t%8lu kB\n"
		"VmLib:\t%8lu kB\n",
		(mm->total_vm - mm->reserved_vm) << (PAGE_SHIFT-10),
		mm->locked_vm << (PAGE_SHIFT-10),
		mm->rss << (PAGE_SHIFT-10),
		data << (PAGE_SHIFT-10),
		mm->stack_vm << (PAGE_SHIFT-10), text, lib);
	return buffer;
}

unsigned long task_vsize(struct mm_struct *mm)
{
	return PAGE_SIZE * mm->total_vm;
}

int task_statm(struct mm_struct *mm, int *shared, int *text,
	       int *data, int *resident)
{
	*shared = mm->shared_vm;
	*text = (mm->end_code - mm->start_code) >> PAGE_SHIFT;
	*data = mm->total_vm - mm->shared_vm - *text;
	*resident = mm->rss;
	return mm->total_vm;
}

static int show_map(struct seq_file *m, void *v)
{
	struct vm_area_struct *map = v;
	struct file *file = map->vm_file;
	int flags = map->vm_flags;
	unsigned long ino = 0;
	dev_t dev = 0;
	int len;

	if (file) {
		struct inode *inode = map->vm_file->f_dentry->d_inode;
		dev = inode->i_sb->s_dev;
		ino = inode->i_ino;
	}

	seq_printf(m, "%08lx-%08lx %c%c%c%c %08lx %02x:%02x %lu %n",
			map->vm_start,
			map->vm_end,
			flags & VM_READ ? 'r' : '-',
			flags & VM_WRITE ? 'w' : '-',
			flags & VM_EXEC ? 'x' : '-',
			flags & VM_MAYSHARE ? 's' : 'p',
			map->vm_pgoff << PAGE_SHIFT,
			MAJOR(dev), MINOR(dev), ino, &len);

	if (map->vm_file) {
		len = 25 + sizeof(void*) * 6 - len;
		if (len < 1)
			len = 1;
		seq_printf(m, "%*c", len, ' ');
		seq_path(m, file->f_vfsmnt, file->f_dentry, "");
	}
	seq_putc(m, '\n');
	return 0;
}

static void *m_start(struct seq_file *m, loff_t *pos)
{
	struct task_struct *task = m->private;
	struct mm_struct *mm = get_task_mm(task);
	struct vm_area_struct * map;
	loff_t l = *pos;

	if (!mm)
		return NULL;

	down_read(&mm->mmap_sem);
	map = mm->mmap;
	while (l-- && map)
		map = map->vm_next;
	if (!map) {
		up_read(&mm->mmap_sem);
		mmput(mm);
		if (l == -1)
			map = get_gate_vma(task);
	}
	return map;
}

static void m_stop(struct seq_file *m, void *v)
{
	struct task_struct *task = m->private;
	struct vm_area_struct *map = v;
	if (map && map != get_gate_vma(task)) {
		struct mm_struct *mm = map->vm_mm;
		up_read(&mm->mmap_sem);
		mmput(mm);
	}
}

static void *m_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct task_struct *task = m->private;
	struct vm_area_struct *map = v;
	(*pos)++;
	if (map->vm_next)
		return map->vm_next;
	m_stop(m, v);
	if (map != get_gate_vma(task))
		return get_gate_vma(task);
	return NULL;
}

struct seq_operations proc_pid_maps_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_map
};
