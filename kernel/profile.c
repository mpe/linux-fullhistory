/*
 *  linux/kernel/profile.c
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/profile.h>
#include <linux/bootmem.h>
#include <linux/notifier.h>
#include <linux/mm.h>
#include <linux/cpumask.h>
#include <linux/profile.h>
#include <asm/sections.h>

static atomic_t *prof_buffer;
static unsigned long prof_len, prof_shift;
static int prof_on;
static cpumask_t prof_cpu_mask = CPU_MASK_ALL;

static int __init profile_setup(char * str)
{
	int par;

	if (!strncmp(str, "schedule", 8)) {
		prof_on = 2;
		printk(KERN_INFO "kernel schedule profiling enabled\n");
		if (str[7] == ',')
			str += 8;
	}
	if (get_option(&str,&par)) {
		prof_shift = par;
		prof_on = 1;
		printk(KERN_INFO "kernel profiling enabled (shift: %ld)\n",
			prof_shift);
	}
	return 1;
}
__setup("profile=", profile_setup);


void __init profile_init(void)
{
	if (!prof_on) 
		return;
 
	/* only text is profiled */
	prof_len = (_etext - _stext) >> prof_shift;
	prof_buffer = alloc_bootmem(prof_len*sizeof(atomic_t));
}

/* Profile event notifications */
 
#ifdef CONFIG_PROFILING
 
static DECLARE_RWSEM(profile_rwsem);
static rwlock_t handoff_lock = RW_LOCK_UNLOCKED;
static struct notifier_block * task_exit_notifier;
static struct notifier_block * task_free_notifier;
static struct notifier_block * munmap_notifier;
 
void profile_task_exit(struct task_struct * task)
{
	down_read(&profile_rwsem);
	notifier_call_chain(&task_exit_notifier, 0, task);
	up_read(&profile_rwsem);
}
 
int profile_handoff_task(struct task_struct * task)
{
	int ret;
	read_lock(&handoff_lock);
	ret = notifier_call_chain(&task_free_notifier, 0, task);
	read_unlock(&handoff_lock);
	return (ret == NOTIFY_OK) ? 1 : 0;
}

void profile_munmap(unsigned long addr)
{
	down_read(&profile_rwsem);
	notifier_call_chain(&munmap_notifier, 0, (void *)addr);
	up_read(&profile_rwsem);
}

int task_handoff_register(struct notifier_block * n)
{
	int err = -EINVAL;

	write_lock(&handoff_lock);
	err = notifier_chain_register(&task_free_notifier, n);
	write_unlock(&handoff_lock);
	return err;
}

int task_handoff_unregister(struct notifier_block * n)
{
	int err = -EINVAL;

	write_lock(&handoff_lock);
	err = notifier_chain_unregister(&task_free_notifier, n);
	write_unlock(&handoff_lock);
	return err;
}

int profile_event_register(enum profile_type type, struct notifier_block * n)
{
	int err = -EINVAL;
 
	down_write(&profile_rwsem);
 
	switch (type) {
		case PROFILE_TASK_EXIT:
			err = notifier_chain_register(&task_exit_notifier, n);
			break;
		case PROFILE_MUNMAP:
			err = notifier_chain_register(&munmap_notifier, n);
			break;
	}
 
	up_write(&profile_rwsem);
 
	return err;
}

 
int profile_event_unregister(enum profile_type type, struct notifier_block * n)
{
	int err = -EINVAL;
 
	down_write(&profile_rwsem);
 
	switch (type) {
		case PROFILE_TASK_EXIT:
			err = notifier_chain_unregister(&task_exit_notifier, n);
			break;
		case PROFILE_MUNMAP:
			err = notifier_chain_unregister(&munmap_notifier, n);
			break;
	}

	up_write(&profile_rwsem);
	return err;
}

static struct notifier_block * profile_listeners;
static rwlock_t profile_lock = RW_LOCK_UNLOCKED;
 
int register_profile_notifier(struct notifier_block * nb)
{
	int err;
	write_lock_irq(&profile_lock);
	err = notifier_chain_register(&profile_listeners, nb);
	write_unlock_irq(&profile_lock);
	return err;
}


int unregister_profile_notifier(struct notifier_block * nb)
{
	int err;
	write_lock_irq(&profile_lock);
	err = notifier_chain_unregister(&profile_listeners, nb);
	write_unlock_irq(&profile_lock);
	return err;
}


void profile_hook(struct pt_regs * regs)
{
	read_lock(&profile_lock);
	notifier_call_chain(&profile_listeners, 0, regs);
	read_unlock(&profile_lock);
}

EXPORT_SYMBOL_GPL(register_profile_notifier);
EXPORT_SYMBOL_GPL(unregister_profile_notifier);
EXPORT_SYMBOL_GPL(task_handoff_register);
EXPORT_SYMBOL_GPL(task_handoff_unregister);

#endif /* CONFIG_PROFILING */

EXPORT_SYMBOL_GPL(profile_event_register);
EXPORT_SYMBOL_GPL(profile_event_unregister);

void profile_hit(int type, void *__pc)
{
	unsigned long pc;

	if (prof_on != type || !prof_buffer)
		return;
	pc = ((unsigned long)__pc - (unsigned long)_stext) >> prof_shift;
	atomic_inc(&prof_buffer[min(pc, prof_len - 1)]);
}

void profile_tick(int type, struct pt_regs *regs)
{
	if (type == CPU_PROFILING)
		profile_hook(regs);
	if (!user_mode(regs) && cpu_isset(smp_processor_id(), prof_cpu_mask))
		profile_hit(type, (void *)profile_pc(regs));
}

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <asm/ptrace.h>

static int prof_cpu_mask_read_proc (char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	int len = cpumask_scnprintf(page, count, *(cpumask_t *)data);
	if (count - len < 2)
		return -EINVAL;
	len += sprintf(page + len, "\n");
	return len;
}

static int prof_cpu_mask_write_proc (struct file *file, const char __user *buffer,
					unsigned long count, void *data)
{
	cpumask_t *mask = (cpumask_t *)data;
	unsigned long full_count = count, err;
	cpumask_t new_value;

	err = cpumask_parse(buffer, count, new_value);
	if (err)
		return err;

	*mask = new_value;
	return full_count;
}

void create_prof_cpu_mask(struct proc_dir_entry *root_irq_dir)
{
	struct proc_dir_entry *entry;

	/* create /proc/irq/prof_cpu_mask */
	if (!(entry = create_proc_entry("prof_cpu_mask", 0600, root_irq_dir)))
		return;
	entry->nlink = 1;
	entry->data = (void *)&prof_cpu_mask;
	entry->read_proc = prof_cpu_mask_read_proc;
	entry->write_proc = prof_cpu_mask_write_proc;
}

/*
 * This function accesses profiling information. The returned data is
 * binary: the sampling step and the actual contents of the profile
 * buffer. Use of the program readprofile is recommended in order to
 * get meaningful info out of these data.
 */
static ssize_t
read_profile(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	ssize_t read;
	char * pnt;
	unsigned int sample_step = 1 << prof_shift;

	if (p >= (prof_len+1)*sizeof(unsigned int))
		return 0;
	if (count > (prof_len+1)*sizeof(unsigned int) - p)
		count = (prof_len+1)*sizeof(unsigned int) - p;
	read = 0;

	while (p < sizeof(unsigned int) && count > 0) {
		put_user(*((char *)(&sample_step)+p),buf);
		buf++; p++; count--; read++;
	}
	pnt = (char *)prof_buffer + p - sizeof(atomic_t);
	if (copy_to_user(buf,(void *)pnt,count))
		return -EFAULT;
	read += count;
	*ppos += read;
	return read;
}

/*
 * Writing to /proc/profile resets the counters
 *
 * Writing a 'profiling multiplier' value into it also re-sets the profiling
 * interrupt frequency, on architectures that support this.
 */
static ssize_t write_profile(struct file *file, const char __user *buf,
			     size_t count, loff_t *ppos)
{
#ifdef CONFIG_SMP
	extern int setup_profiling_timer (unsigned int multiplier);

	if (count == sizeof(int)) {
		unsigned int multiplier;

		if (copy_from_user(&multiplier, buf, sizeof(int)))
			return -EFAULT;

		if (setup_profiling_timer(multiplier))
			return -EINVAL;
	}
#endif

	memset(prof_buffer, 0, prof_len * sizeof(atomic_t));
	return count;
}

static struct file_operations proc_profile_operations = {
	.read		= read_profile,
	.write		= write_profile,
};

static int __init create_proc_profile(void)
{
	struct proc_dir_entry *entry;

	if (!prof_on)
		return 0;
	if (!(entry = create_proc_entry("profile", S_IWUSR | S_IRUGO, NULL)))
		return 0;
	entry->proc_fops = &proc_profile_operations;
	entry->size = (1+prof_len) * sizeof(atomic_t);
	return 0;
}
module_init(create_proc_profile);
#endif /* CONFIG_PROC_FS */
