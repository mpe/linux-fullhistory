/*
 * Copyright (C) 2001 Lennert Buytenhek (buytenh@gnu.org)
 * Copyright (C) 2001 - 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include "linux/kernel.h"
#include "linux/slab.h"
#include "linux/init.h"
#include "linux/notifier.h"
#include "linux/reboot.h"
#include "linux/utsname.h"
#include "linux/ctype.h"
#include "linux/interrupt.h"
#include "linux/sysrq.h"
#include "linux/workqueue.h"
#include "linux/module.h"
#include "linux/file.h"
#include "linux/fs.h"
#include "linux/namei.h"
#include "linux/proc_fs.h"
#include "asm/irq.h"
#include "asm/uaccess.h"
#include "user_util.h"
#include "kern_util.h"
#include "kern.h"
#include "mconsole.h"
#include "mconsole_kern.h"
#include "irq_user.h"
#include "init.h"
#include "os.h"
#include "umid.h"
#include "irq_kern.h"

static int do_unlink_socket(struct notifier_block *notifier, 
			    unsigned long what, void *data)
{
	return(mconsole_unlink_socket());
}


static struct notifier_block reboot_notifier = {
	.notifier_call		= do_unlink_socket,
	.priority		= 0,
};

/* Safe without explicit locking for now.  Tasklets provide their own 
 * locking, and the interrupt handler is safe because it can't interrupt
 * itself and it can only happen on CPU 0.
 */

LIST_HEAD(mc_requests);

static void mc_work_proc(void *unused)
{
	struct mconsole_entry *req;
	unsigned long flags;

	while(!list_empty(&mc_requests)){
		local_save_flags(flags);
		req = list_entry(mc_requests.next, struct mconsole_entry, 
				 list);
		list_del(&req->list);
		local_irq_restore(flags);
		req->request.cmd->handler(&req->request);
		kfree(req);
	}
}

DECLARE_WORK(mconsole_work, mc_work_proc, NULL);

static irqreturn_t mconsole_interrupt(int irq, void *dev_id,
				      struct pt_regs *regs)
{
	int fd;
	struct mconsole_entry *new;
	struct mc_request req;

	fd = (int) dev_id;
	while (mconsole_get_request(fd, &req)){
		if(req.cmd->context == MCONSOLE_INTR)
			(*req.cmd->handler)(&req);
		else {
			new = kmalloc(sizeof(*new), GFP_ATOMIC);
			if(new == NULL)
				mconsole_reply(&req, "Out of memory", 1, 0);
			else {
				new->request = req;
				list_add(&new->list, &mc_requests);
			}
		}
	}
	if(!list_empty(&mc_requests))
		schedule_work(&mconsole_work);
	reactivate_fd(fd, MCONSOLE_IRQ);
	return(IRQ_HANDLED);
}

void mconsole_version(struct mc_request *req)
{
	char version[256];

	sprintf(version, "%s %s %s %s %s", system_utsname.sysname, 
		system_utsname.nodename, system_utsname.release, 
		system_utsname.version, system_utsname.machine);
	mconsole_reply(req, version, 0, 0);
}

void mconsole_log(struct mc_request *req)
{
	int len;
	char *ptr = req->request.data;

	ptr += strlen("log ");

	len = req->len - (ptr - req->request.data);
	printk("%.*s", len, ptr);
	mconsole_reply(req, "", 0, 0);
}

void mconsole_proc(struct mc_request *req)
{
	struct nameidata nd;
	struct file_system_type *proc;
	struct super_block *super;
	struct file *file;
	int n, err;
	char *ptr = req->request.data, *buf;

	ptr += strlen("proc");
	while(isspace(*ptr)) ptr++;

	proc = get_fs_type("proc");
	if(proc == NULL){
		mconsole_reply(req, "procfs not registered", 1, 0);
		goto out;
	}

	super = (*proc->get_sb)(proc, 0, NULL, NULL);
	put_filesystem(proc);
	if(super == NULL){
		mconsole_reply(req, "Failed to get procfs superblock", 1, 0);
		goto out;
	}
	up_write(&super->s_umount);

	nd.dentry = super->s_root;
	nd.mnt = NULL;
	nd.flags = O_RDONLY + 1;
	nd.last_type = LAST_ROOT;

	err = link_path_walk(ptr, &nd);
	if(err){
		mconsole_reply(req, "Failed to look up file", 1, 0);
		goto out_kill;
	}

	file = dentry_open(nd.dentry, nd.mnt, O_RDONLY);
	if(IS_ERR(file)){
		mconsole_reply(req, "Failed to open file", 1, 0);
		goto out_kill;
	}

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if(buf == NULL){
		mconsole_reply(req, "Failed to allocate buffer", 1, 0);
		goto out_fput;
	}

	if((file->f_op != NULL) && (file->f_op->read != NULL)){
		do {
			n = (*file->f_op->read)(file, buf, PAGE_SIZE - 1,
						&file->f_pos);
			if(n >= 0){
				buf[n] = '\0';
				mconsole_reply(req, buf, 0, (n > 0));
			}
			else {
				mconsole_reply(req, "Read of file failed",
					       1, 0);
				goto out_free;
			}
		} while(n > 0);
	}
	else mconsole_reply(req, "", 0, 0);

 out_free:
	kfree(buf);
 out_fput:
	fput(file);
 out_kill:
	deactivate_super(super);
 out: ;
}

#define UML_MCONSOLE_HELPTEXT \
"Commands: \n\
    version - Get kernel version \n\
    help - Print this message \n\
    halt - Halt UML \n\
    reboot - Reboot UML \n\
    config <dev>=<config> - Add a new device to UML;  \n\
	same syntax as command line \n\
    config <dev> - Query the configuration of a device \n\
    remove <dev> - Remove a device from UML \n\
    sysrq <letter> - Performs the SysRq action controlled by the letter \n\
    cad - invoke the Ctl-Alt-Del handler \n\
    stop - pause the UML; it will do nothing until it receives a 'go' \n\
    go - continue the UML after a 'stop' \n\
    log <string> - make UML enter <string> into the kernel log\n\
    proc <file> - returns the contents of the UML's /proc/<file>\n\
"

void mconsole_help(struct mc_request *req)
{
	mconsole_reply(req, UML_MCONSOLE_HELPTEXT, 0, 0);
}

void mconsole_halt(struct mc_request *req)
{
	mconsole_reply(req, "", 0, 0);
	machine_halt();
}

void mconsole_reboot(struct mc_request *req)
{
	mconsole_reply(req, "", 0, 0);
	machine_restart(NULL);
}

extern void ctrl_alt_del(void);

void mconsole_cad(struct mc_request *req)
{
	mconsole_reply(req, "", 0, 0);
	ctrl_alt_del();
}

void mconsole_go(struct mc_request *req)
{
	mconsole_reply(req, "Not stopped", 1, 0);
}

void mconsole_stop(struct mc_request *req)
{
	deactivate_fd(req->originating_fd, MCONSOLE_IRQ);
	os_set_fd_block(req->originating_fd, 1);
	mconsole_reply(req, "", 0, 0);
	while(mconsole_get_request(req->originating_fd, req)){
		if(req->cmd->handler == mconsole_go) break;
		(*req->cmd->handler)(req);
	}
	os_set_fd_block(req->originating_fd, 0);
	reactivate_fd(req->originating_fd, MCONSOLE_IRQ);
	mconsole_reply(req, "", 0, 0);
}

/* This list is populated by __initcall routines. */

LIST_HEAD(mconsole_devices);

void mconsole_register_dev(struct mc_device *new)
{
	list_add(&new->list, &mconsole_devices);
}

static struct mc_device *mconsole_find_dev(char *name)
{
	struct list_head *ele;
	struct mc_device *dev;

	list_for_each(ele, &mconsole_devices){
		dev = list_entry(ele, struct mc_device, list);
		if(!strncmp(name, dev->name, strlen(dev->name)))
			return(dev);
	}
	return(NULL);
}

#define CONFIG_BUF_SIZE 64

static void mconsole_get_config(int (*get_config)(char *, char *, int, 
						  char **),
				struct mc_request *req, char *name)
{
	char default_buf[CONFIG_BUF_SIZE], *error, *buf;
	int n, size;

	if(get_config == NULL){
		mconsole_reply(req, "No get_config routine defined", 1, 0);
		return;
	}

	error = NULL;
	size = sizeof(default_buf)/sizeof(default_buf[0]);
	buf = default_buf;

	while(1){
		n = (*get_config)(name, buf, size, &error);
		if(error != NULL){
			mconsole_reply(req, error, 1, 0);
			goto out;
		}

		if(n <= size){
			mconsole_reply(req, buf, 0, 0);
			goto out;
		}

		if(buf != default_buf)
			kfree(buf);

		size = n;
		buf = kmalloc(size, GFP_KERNEL);
		if(buf == NULL){
			mconsole_reply(req, "Failed to allocate buffer", 1, 0);
			return;
		}
	}
 out:
	if(buf != default_buf)
		kfree(buf);
	
}

void mconsole_config(struct mc_request *req)
{
	struct mc_device *dev;
	char *ptr = req->request.data, *name;
	int err;

	ptr += strlen("config");
	while(isspace(*ptr)) ptr++;
	dev = mconsole_find_dev(ptr);
	if(dev == NULL){
		mconsole_reply(req, "Bad configuration option", 1, 0);
		return;
	}

	name = &ptr[strlen(dev->name)];
	ptr = name;
	while((*ptr != '=') && (*ptr != '\0'))
		ptr++;

	if(*ptr == '='){
		err = (*dev->config)(name);
		mconsole_reply(req, "", err, 0);
	}
	else mconsole_get_config(dev->get_config, req, name);
}

void mconsole_remove(struct mc_request *req)
{
	struct mc_device *dev;	
	char *ptr = req->request.data;
	int err;

	ptr += strlen("remove");
	while(isspace(*ptr)) ptr++;
	dev = mconsole_find_dev(ptr);
	if(dev == NULL){
		mconsole_reply(req, "Bad remove option", 1, 0);
		return;
	}
	err = (*dev->remove)(&ptr[strlen(dev->name)]);
	mconsole_reply(req, "", err, 0);
}

#ifdef CONFIG_MAGIC_SYSRQ
void mconsole_sysrq(struct mc_request *req)
{
	char *ptr = req->request.data;

	ptr += strlen("sysrq");
	while(isspace(*ptr)) ptr++;

	mconsole_reply(req, "", 0, 0);
	handle_sysrq(*ptr, &current->thread.regs, NULL);
}
#else
void mconsole_sysrq(struct mc_request *req)
{
	mconsole_reply(req, "Sysrq not compiled in", 1, 0);
}
#endif

/* Changed by mconsole_setup, which is __setup, and called before SMP is
 * active.
 */
static char *notify_socket = NULL; 

int mconsole_init(void)
{
	int err, sock;
	char file[256];

	if(umid_file_name("mconsole", file, sizeof(file))) return(-1);
	snprintf(mconsole_socket_name, sizeof(file), "%s", file);

	sock = os_create_unix_socket(file, sizeof(file), 1);
	if (sock < 0){
		printk("Failed to initialize management console\n");
		return(1);
	}

	register_reboot_notifier(&reboot_notifier);

	err = um_request_irq(MCONSOLE_IRQ, sock, IRQ_READ, mconsole_interrupt,
			     SA_INTERRUPT | SA_SHIRQ | SA_SAMPLE_RANDOM,
			     "mconsole", (void *)sock);
	if (err){
		printk("Failed to get IRQ for management console\n");
		return(1);
	}

	if(notify_socket != NULL){
		notify_socket = uml_strdup(notify_socket);
		if(notify_socket != NULL)
			mconsole_notify(notify_socket, MCONSOLE_SOCKET,
					mconsole_socket_name, 
					strlen(mconsole_socket_name) + 1);
		else printk(KERN_ERR "mconsole_setup failed to strdup "
			    "string\n");
	}

	printk("mconsole (version %d) initialized on %s\n", 
	       MCONSOLE_VERSION, mconsole_socket_name);
	return(0);
}

__initcall(mconsole_init);

static int write_proc_mconsole(struct file *file, const char *buffer,
			       unsigned long count, void *data)
{
	char *buf;

	buf = kmalloc(count + 1, GFP_KERNEL);
	if(buf == NULL) 
		return(-ENOMEM);

	if(copy_from_user(buf, buffer, count)){
		count = -EFAULT;
		goto out;
	}

	buf[count] = '\0';

	mconsole_notify(notify_socket, MCONSOLE_USER_NOTIFY, buf, count);
 out:
	kfree(buf);
	return(count);
}

static int create_proc_mconsole(void)
{
	struct proc_dir_entry *ent;

	if(notify_socket == NULL) return(0);

	ent = create_proc_entry("mconsole", S_IFREG | 0200, NULL);
	if(ent == NULL){
		printk("create_proc_mconsole : create_proc_entry failed\n");
		return(0);
	}

	ent->read_proc = NULL;
	ent->write_proc = write_proc_mconsole;
	return(0);
}

static spinlock_t notify_spinlock = SPIN_LOCK_UNLOCKED;

void lock_notify(void)
{
	spin_lock(&notify_spinlock);
}

void unlock_notify(void)
{
	spin_unlock(&notify_spinlock);
}

__initcall(create_proc_mconsole);

#define NOTIFY "=notify:"

static int mconsole_setup(char *str)
{
	if(!strncmp(str, NOTIFY, strlen(NOTIFY))){
		str += strlen(NOTIFY);
		notify_socket = str;
	}
	else printk(KERN_ERR "mconsole_setup : Unknown option - '%s'\n", str);
	return(1);
}

__setup("mconsole", mconsole_setup);

__uml_help(mconsole_setup,
"mconsole=notify:<socket>\n"
"    Requests that the mconsole driver send a message to the named Unix\n"
"    socket containing the name of the mconsole socket.  This also serves\n"
"    to notify outside processes when UML has booted far enough to respond\n"
"    to mconsole requests.\n\n"
);

static int notify_panic(struct notifier_block *self, unsigned long unused1,
			void *ptr)
{
	char *message = ptr;

	if(notify_socket == NULL) return(0);

	mconsole_notify(notify_socket, MCONSOLE_PANIC, message, 
			strlen(message) + 1);
	return(0);
}

static struct notifier_block panic_exit_notifier = {
	.notifier_call 		= notify_panic,
	.next 			= NULL,
	.priority 		= 1
};

static int add_notifier(void)
{
	notifier_chain_register(&panic_notifier_list, &panic_exit_notifier);
	return(0);
}

__initcall(add_notifier);

char *mconsole_notify_socket(void)
{
	return(notify_socket);
}

EXPORT_SYMBOL(mconsole_notify_socket);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
