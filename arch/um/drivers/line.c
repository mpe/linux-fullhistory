/* 
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/sched.h"
#include "linux/slab.h"
#include "linux/list.h"
#include "linux/kd.h"
#include "linux/interrupt.h"
#include "linux/devfs_fs_kernel.h"
#include "asm/uaccess.h"
#include "chan_kern.h"
#include "irq_user.h"
#include "line.h"
#include "kern.h"
#include "user_util.h"
#include "kern_util.h"
#include "os.h"
#include "irq_kern.h"

#define LINE_BUFSIZE 4096

static irqreturn_t line_interrupt(int irq, void *data, struct pt_regs *unused)
{
	struct tty_struct *tty = data;
	struct line *line = tty->driver_data;

	if (line)
		chan_interrupt(&line->chan_list, &line->task, tty, irq);
	return IRQ_HANDLED;
}

static void line_timer_cb(void *arg)
{
	struct tty_struct *tty = arg;
	struct line *line = tty->driver_data;

	line_interrupt(line->driver->read_irq, arg, NULL);
}

static int write_room(struct line *dev)
{
	int n;

	if (dev->buffer == NULL)
		return (LINE_BUFSIZE - 1);

	n = dev->head - dev->tail;
	if (n <= 0)
		n = LINE_BUFSIZE + n;
	return (n - 1);
}

static int buffer_data(struct line *line, const char *buf, int len)
{
	int end, room;

	if(line->buffer == NULL){
		line->buffer = kmalloc(LINE_BUFSIZE, GFP_ATOMIC);
		if (line->buffer == NULL) {
			printk("buffer_data - atomic allocation failed\n");
			return(0);
		}
		line->head = line->buffer;
		line->tail = line->buffer;
	}

	room = write_room(line);
	len = (len > room) ? room : len;

	end = line->buffer + LINE_BUFSIZE - line->tail;
	if(len < end){
		memcpy(line->tail, buf, len);
		line->tail += len;
	}
	else {
		memcpy(line->tail, buf, end);
		buf += end;
		memcpy(line->buffer, buf, len - end);
		line->tail = line->buffer + len - end;
	}

	return(len);
}

static int flush_buffer(struct line *line)
{
	int n, count;

	if ((line->buffer == NULL) || (line->head == line->tail))
		return(1);

	if (line->tail < line->head) {
		count = line->buffer + LINE_BUFSIZE - line->head;
		n = write_chan(&line->chan_list, line->head, count,
			       line->driver->write_irq);
		if (n < 0)
			return(n);
		if (n == count)
			line->head = line->buffer;
		else {
			line->head += n;
			return(0);
		}
	}

	count = line->tail - line->head;
	n = write_chan(&line->chan_list, line->head, count, 
		       line->driver->write_irq);
	if(n < 0) return(n);

	line->head += n;
	return(line->head == line->tail);
}

int line_write(struct tty_struct *tty, const unsigned char *buf, int len)
{
	struct line *line = tty->driver_data;
	unsigned long flags;
	int n, err, ret = 0;

	if(tty->stopped) return 0;

	down(&line->sem);
	if(line->head != line->tail){
		local_irq_save(flags);
		ret = buffer_data(line, buf, len);
		err = flush_buffer(line);
		local_irq_restore(flags);
		if(err <= 0)
			ret = err;
	}
	else {
		n = write_chan(&line->chan_list, buf, len, 
			       line->driver->write_irq);
		if(n < 0){
			ret = n;
			goto out_up;
		}

		len -= n;
		ret += n;
		if(len > 0)
			ret += buffer_data(line, buf + n, len);
	}
 out_up:
	up(&line->sem);
	return(ret);
}

void line_put_char(struct tty_struct *tty, unsigned char ch)
{
	line_write(tty, &ch, sizeof(ch));
}

void line_set_termios(struct tty_struct *tty, struct termios * old)
{
	/* nothing */
}

int line_chars_in_buffer(struct tty_struct *tty)
{
	return 0;
}

static struct {
	int  cmd;
	char *level;
	char *name;
} tty_ioctls[] = {
	/* don't print these, they flood the log ... */
	{ TCGETS,      NULL,       "TCGETS"      },
        { TCSETS,      NULL,       "TCSETS"      },
        { TCSETSW,     NULL,       "TCSETSW"     },
        { TCFLSH,      NULL,       "TCFLSH"      },
        { TCSBRK,      NULL,       "TCSBRK"      },

	/* general tty stuff */
        { TCSETSF,     KERN_DEBUG, "TCSETSF"     },
        { TCGETA,      KERN_DEBUG, "TCGETA"      },
        { TIOCMGET,    KERN_DEBUG, "TIOCMGET"    },
        { TCSBRKP,     KERN_DEBUG, "TCSBRKP"     },
        { TIOCMSET,    KERN_DEBUG, "TIOCMSET"    },

	/* linux-specific ones */
	{ TIOCLINUX,   KERN_INFO,  "TIOCLINUX"   },
	{ KDGKBMODE,   KERN_INFO,  "KDGKBMODE"   },
	{ KDGKBTYPE,   KERN_INFO,  "KDGKBTYPE"   },
	{ KDSIGACCEPT, KERN_INFO,  "KDSIGACCEPT" },
};

int line_ioctl(struct tty_struct *tty, struct file * file,
	       unsigned int cmd, unsigned long arg)
{
	int ret;
	int i;

	ret = 0;
	switch(cmd) {
#ifdef TIOCGETP
	case TIOCGETP:
	case TIOCSETP:
	case TIOCSETN:
#endif
#ifdef TIOCGETC
	case TIOCGETC:
	case TIOCSETC:
#endif
#ifdef TIOCGLTC
	case TIOCGLTC:
	case TIOCSLTC:
#endif
	case TCGETS:
	case TCSETSF:
	case TCSETSW:
	case TCSETS:
	case TCGETA:
	case TCSETAF:
	case TCSETAW:
	case TCSETA:
	case TCXONC:
	case TCFLSH:
	case TIOCOUTQ:
	case TIOCINQ:
	case TIOCGLCKTRMIOS:
	case TIOCSLCKTRMIOS:
	case TIOCPKT:
	case TIOCGSOFTCAR:
	case TIOCSSOFTCAR:
		return -ENOIOCTLCMD;
#if 0
	case TCwhatever:
		/* do something */
		break;
#endif
	default:
		for (i = 0; i < ARRAY_SIZE(tty_ioctls); i++)
			if (cmd == tty_ioctls[i].cmd)
				break;
		if (i < ARRAY_SIZE(tty_ioctls)) {
			if (NULL != tty_ioctls[i].level)
				printk("%s%s: %s: ioctl %s called\n",
				       tty_ioctls[i].level, __FUNCTION__,
				       tty->name, tty_ioctls[i].name);
		} else {
			printk(KERN_ERR "%s: %s: unknown ioctl: 0x%x\n",
			       __FUNCTION__, tty->name, cmd);
		}
		ret = -ENOIOCTLCMD;
		break;
	}
	return(ret);
}

static irqreturn_t line_write_interrupt(int irq, void *data,
					struct pt_regs *unused)
{
	struct tty_struct *tty = data;
	struct line *line = tty->driver_data;
	int err;

	err = flush_buffer(line);
	if(err == 0)
		return(IRQ_NONE);
	else if(err < 0){
		line->head = line->buffer;
		line->tail = line->buffer;
	}

	if(tty == NULL)
		return(IRQ_NONE);

	if(test_bit(TTY_DO_WRITE_WAKEUP, &tty->flags) &&
	   (tty->ldisc.write_wakeup != NULL))
		(tty->ldisc.write_wakeup)(tty);
	
	/* BLOCKING mode
	 * In blocking mode, everything sleeps on tty->write_wait.
	 * Sleeping in the console driver would break non-blocking
	 * writes.
	 */

	if(waitqueue_active(&tty->write_wait))
		wake_up_interruptible(&tty->write_wait);
	return(IRQ_HANDLED);
}

int line_setup_irq(int fd, int input, int output, struct tty_struct *tty)
{
	struct line *line = tty->driver_data;
	struct line_driver *driver = line->driver;
	int err = 0, flags = SA_INTERRUPT | SA_SHIRQ | SA_SAMPLE_RANDOM;

	if(input) err = um_request_irq(driver->read_irq, fd, IRQ_READ, 
				       line_interrupt, flags, 
				       driver->read_irq_name, tty);
	if(err) return(err);
	if(output) err = um_request_irq(driver->write_irq, fd, IRQ_WRITE, 
					line_write_interrupt, flags, 
					driver->write_irq_name, tty);
	line->have_irq = 1;
	return(err);
}

void line_disable(struct tty_struct *tty, int current_irq)
{
	struct line *line = tty->driver_data;

	if(!line->have_irq)
		return;

	if(line->driver->read_irq == current_irq)
		free_irq_later(line->driver->read_irq, tty);
	else {
		free_irq_by_irq_and_dev(line->driver->read_irq, tty);
		free_irq(line->driver->read_irq, tty);
	}

	if(line->driver->write_irq == current_irq)
		free_irq_later(line->driver->write_irq, tty);
	else {
		free_irq_by_irq_and_dev(line->driver->write_irq, tty);
		free_irq(line->driver->write_irq, tty);
	}

	line->have_irq = 0;
}

int line_open(struct line *lines, struct tty_struct *tty,
	      struct chan_opts *opts)
{
	struct line *line;
	int err = 0;

	line = &lines[tty->index];
	tty->driver_data = line;

	down(&line->sem);
	if (tty->count == 1) {
		if (!line->valid) {
			err = -ENODEV;
			goto out;
		}
		if (list_empty(&line->chan_list)) {
			err = parse_chan_pair(line->init_str, &line->chan_list,
					      line->init_pri, tty->index, opts);
			if(err) goto out;
			err = open_chan(&line->chan_list);
			if(err) goto out;
		}
		enable_chan(&line->chan_list, tty);
		INIT_WORK(&line->task, line_timer_cb, tty);
	}

	if(!line->sigio){
		chan_enable_winch(&line->chan_list, tty);
		line->sigio = 1;
	}
	chan_window_size(&line->chan_list, &tty->winsize.ws_row,
			 &tty->winsize.ws_col);
	line->count++;

out:
	up(&line->sem);
	return(err);
}

void line_close(struct tty_struct *tty, struct file * filp)
{
	struct line *line = tty->driver_data;

	down(&line->sem);
	line->count--;
	if (tty->count == 1) {
		line_disable(tty, -1);
		tty->driver_data = NULL;
	}
	up(&line->sem);
}

void close_lines(struct line *lines, int nlines)
{
	int i;

	for(i = 0; i < nlines; i++)
		close_chan(&lines[i].chan_list);
}

int line_setup(struct line *lines, int num, char *init, int all_allowed)
{
	int i, n;
	char *end;

	if(*init == '=') n = -1;
	else {
		n = simple_strtoul(init, &end, 0);
		if(*end != '='){
			printk(KERN_ERR "line_setup failed to parse \"%s\"\n", 
			       init);
			return(0);
		}
		init = end;
	}
	init++;
	if((n >= 0) && (n >= num)){
		printk("line_setup - %d out of range ((0 ... %d) allowed)\n",
		       n, num - 1);
		return(0);
	}
	else if (n >= 0){
		if (lines[n].count > 0) {
			printk("line_setup - device %d is open\n", n);
			return(0);
		}
		if (lines[n].init_pri <= INIT_ONE){
			lines[n].init_pri = INIT_ONE;
			if (!strcmp(init, "none"))
				lines[n].valid = 0;
			else {
				lines[n].init_str = init;
				lines[n].valid = 1;
			}	
		}
	}
	else if(!all_allowed){
		printk("line_setup - can't configure all devices from "
		       "mconsole\n");
		return(0);
	}
	else {
		for(i = 0; i < num; i++){
			if(lines[i].init_pri <= INIT_ALL){
				lines[i].init_pri = INIT_ALL;
				if(!strcmp(init, "none")) lines[i].valid = 0;
				else {
					lines[i].init_str = init;
					lines[i].valid = 1;
				}
			}
		}
	}
	return(1);
}

int line_config(struct line *lines, int num, char *str)
{
	char *new = uml_strdup(str);

	if(new == NULL){
		printk("line_config - uml_strdup failed\n");
		return(-ENOMEM);
	}
	return(!line_setup(lines, num, new, 0));
}

int line_get_config(char *name, struct line *lines, int num, char *str, 
		    int size, char **error_out)
{
	struct line *line;
	char *end;
	int dev, n = 0;

	dev = simple_strtoul(name, &end, 0);
	if((*end != '\0') || (end == name)){
		*error_out = "line_get_config failed to parse device number";
		return(0);
	}

	if((dev < 0) || (dev >= num)){
		*error_out = "device number of of range";
		return(0);
	}

	line = &lines[dev];

	down(&line->sem);
	if(!line->valid)
		CONFIG_CHUNK(str, size, n, "none", 1);
	else if(line->count == 0)
		CONFIG_CHUNK(str, size, n, line->init_str, 1);
	else n = chan_config_string(&line->chan_list, str, size, error_out);
	up(&line->sem);

	return(n);
}

int line_remove(struct line *lines, int num, char *str)
{
	char config[sizeof("conxxxx=none\0")];

	sprintf(config, "%s=none", str);
	return(!line_setup(lines, num, config, 0));
}

int line_write_room(struct tty_struct *tty)
{
	struct line *dev = tty->driver_data;
	int room;

	if (tty->stopped)
		return 0;
	room = write_room(dev);
	if (0 == room)
		printk(KERN_DEBUG "%s: %s: no room left in buffer\n",
		       __FUNCTION__,tty->name);
	return room;
}

struct tty_driver *line_register_devfs(struct lines *set,
			 struct line_driver *line_driver, 
			 struct tty_operations *ops, struct line *lines,
			 int nlines)
{
	int i;
	struct tty_driver *driver = alloc_tty_driver(nlines);

	if (!driver)
		return NULL;

	driver->driver_name = line_driver->name;
	driver->name = line_driver->device_name;
	driver->devfs_name = line_driver->devfs_name;
	driver->major = line_driver->major;
	driver->minor_start = line_driver->minor_start;
	driver->type = line_driver->type;
	driver->subtype = line_driver->subtype;
	driver->flags = TTY_DRIVER_REAL_RAW;
	driver->init_termios = tty_std_termios;
	tty_set_operations(driver, ops);

	if (tty_register_driver(driver)) {
		printk("%s: can't register %s driver\n",
		       __FUNCTION__,line_driver->name);
		put_tty_driver(driver);
		return NULL;
	}

	for(i = 0; i < nlines; i++){
		if(!lines[i].valid) 
			tty_unregister_device(driver, i);
	}

	mconsole_register_dev(&line_driver->mc);
	return driver;
}

void lines_init(struct line *lines, int nlines)
{
	struct line *line;
	int i;

	for(i = 0; i < nlines; i++){
		line = &lines[i];
		INIT_LIST_HEAD(&line->chan_list);
		sema_init(&line->sem, 1);
		if(line->init_str != NULL){
			line->init_str = uml_strdup(line->init_str);
			if(line->init_str == NULL)
				printk("lines_init - uml_strdup returned "
				       "NULL\n");
		}
	}
}

struct winch {
	struct list_head list;
	int fd;
	int tty_fd;
	int pid;
	struct tty_struct *tty;
};

irqreturn_t winch_interrupt(int irq, void *data, struct pt_regs *unused)
{
	struct winch *winch = data;
	struct tty_struct *tty;
	struct line *line;
	int err;
	char c;

	if(winch->fd != -1){
		err = generic_read(winch->fd, &c, NULL);
		if(err < 0){
			if(err != -EAGAIN){
				printk("winch_interrupt : read failed, "
				       "errno = %d\n", -err);
				printk("fd %d is losing SIGWINCH support\n",
				       winch->tty_fd);
				return(IRQ_HANDLED);
			}
			goto out;
		}
	}
	tty  = winch->tty;
	if (tty != NULL) {
		line = tty->driver_data;
		chan_window_size(&line->chan_list,
				 &tty->winsize.ws_row, 
				 &tty->winsize.ws_col);
		kill_pg(tty->pgrp, SIGWINCH, 1);
	}
 out:
	if(winch->fd != -1)
		reactivate_fd(winch->fd, WINCH_IRQ);
	return(IRQ_HANDLED);
}

DECLARE_MUTEX(winch_handler_sem);
LIST_HEAD(winch_handlers);

void register_winch_irq(int fd, int tty_fd, int pid, struct tty_struct *tty)
{
	struct winch *winch;

	down(&winch_handler_sem);
	winch = kmalloc(sizeof(*winch), GFP_KERNEL);
	if (winch == NULL) {
		printk("register_winch_irq - kmalloc failed\n");
		goto out;
	}
	*winch = ((struct winch) { .list  	= LIST_HEAD_INIT(winch->list),
				   .fd  	= fd,
				   .tty_fd 	= tty_fd,
				   .pid  	= pid,
				   .tty 	= tty });
	list_add(&winch->list, &winch_handlers);
	if(um_request_irq(WINCH_IRQ, fd, IRQ_READ, winch_interrupt, 
			  SA_INTERRUPT | SA_SHIRQ | SA_SAMPLE_RANDOM, 
			  "winch", winch) < 0)
		printk("register_winch_irq - failed to register IRQ\n");
 out:
	up(&winch_handler_sem);
}

static void winch_cleanup(void)
{
	struct list_head *ele;
	struct winch *winch;

	list_for_each(ele, &winch_handlers){
		winch = list_entry(ele, struct winch, list);
		if(winch->fd != -1){
			deactivate_fd(winch->fd, WINCH_IRQ);
			os_close_file(winch->fd);
		}
		if(winch->pid != -1) 
			os_kill_process(winch->pid, 1);
	}
}
__uml_exitcall(winch_cleanup);

char *add_xterm_umid(char *base)
{
	char *umid, *title;
	int len;

	umid = get_umid(1);
	if(umid == NULL) return(base);
	
	len = strlen(base) + strlen(" ()") + strlen(umid) + 1;
	title = kmalloc(len, GFP_KERNEL);
	if(title == NULL){
		printk("Failed to allocate buffer for xterm title\n");
		return(base);
	}

	snprintf(title, len, "%s (%s)", base, umid);
	return(title);
}

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
