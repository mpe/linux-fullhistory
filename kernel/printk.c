/*
 *  linux/kernel/printk.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 * Modified to make sys_syslog() more flexible: added commands to
 * return the last 4k of kernel messages, regardless of whether
 * they've been read or not.  Added option to suppress kernel printk's
 * to the console.  Added hook for sending the console messages
 * elsewhere, in preparation for a serial line console (someday).
 * Ted Ts'o, 2/11/93.
 * Modified for sysctl support, 1/8/97, Chris Horn.
 */

#include <linux/mm.h>
#include <linux/tty_driver.h>
#include <linux/smp_lock.h>
#include <linux/console.h>
#include <linux/init.h>

#include <asm/uaccess.h>

#define LOG_BUF_LEN	(16384)

static char buf[1024];

/* printk's without a loglevel use this.. */
#define DEFAULT_MESSAGE_LOGLEVEL 4 /* KERN_WARNING */

/* We show everything that is MORE important than this.. */
#define MINIMUM_CONSOLE_LOGLEVEL 1 /* Minimum loglevel we let people use */
#define DEFAULT_CONSOLE_LOGLEVEL 7 /* anything MORE serious than KERN_DEBUG */

unsigned long log_size = 0;
struct wait_queue * log_wait = NULL;

/* Keep together for sysctl support */
int console_loglevel = DEFAULT_CONSOLE_LOGLEVEL;
int default_message_loglevel = DEFAULT_MESSAGE_LOGLEVEL;
int minimum_console_loglevel = MINIMUM_CONSOLE_LOGLEVEL;
int default_console_loglevel = DEFAULT_CONSOLE_LOGLEVEL;

struct console *console_drivers = NULL;
static char log_buf[LOG_BUF_LEN];
static unsigned long log_start = 0;
static unsigned long logged_chars = 0;
struct console_cmdline console_cmdline[MAX_CMDLINECONSOLES];
static int preferred_console = -1;

/*
 *	Setup a list of consoles. Called from init/main.c
 */
void __init console_setup(char *str, int *ints)
{
	struct console_cmdline *c;
	char name[sizeof(c->name)];
	char *s, *options;
	int i, idx;

	/*
	 *	Decode str into name, index, options.
	 */
	if (str[0] >= '0' && str[0] <= '9') {
		strcpy(name, "ttyS");
		strncpy(name + 4, str, sizeof(name) - 5);
	} else
		strncpy(name, str, sizeof(name) - 1);
	name[sizeof(name) - 1] = 0;
	if ((options = strchr(str, ',')) != NULL)
		*(options++) = 0;
#ifdef __sparc__
	if (!strcmp(str, "ttya"))
		strcpy(name, "ttyS0");
	if (!strcmp(str, "ttyb"))
		strcpy(name, "ttyS1");
#endif
	for(s = name; *s; s++)
		if (*s >= '0' && *s <= '9')
			break;
	idx = simple_strtoul(s, NULL, 10);
	*s = 0;

	/*
	 *	See if this tty is not yet registered, and
	 *	if we have a slot free.
	 */
	for(i = 0; i < MAX_CMDLINECONSOLES && console_cmdline[i].name[0]; i++)
		if (strcmp(console_cmdline[i].name, name) == 0 &&
			  console_cmdline[i].index == idx) {
				preferred_console = i;
				return;
		}
	if (i == MAX_CMDLINECONSOLES)
		return;
	preferred_console = i;
	c = &console_cmdline[i];
	memcpy(c->name, name, sizeof(c->name));
	c->options = options;
	c->index = idx;
}


/*
 * Commands to do_syslog:
 *
 * 	0 -- Close the log.  Currently a NOP.
 * 	1 -- Open the log. Currently a NOP.
 * 	2 -- Read from the log.
 * 	3 -- Read up to the last 4k of messages in the ring buffer.
 * 	4 -- Read and clear last 4k of messages in the ring buffer
 * 	5 -- Clear ring buffer.
 * 	6 -- Disable printk's to console
 * 	7 -- Enable printk's to console
 *	8 -- Set level of messages printed to console
 */
int do_syslog(int type, char * buf, int len)
{
	unsigned long i, j, count, flags;
	int do_clear = 0;
	char c;
	int error = -EPERM;

	lock_kernel();
	error = 0;
	switch (type) {
	case 0:		/* Close log */
		break;
	case 1:		/* Open log */
		break;
	case 2:		/* Read from log */
		error = -EINVAL;
		if (!buf || len < 0)
			goto out;
		error = 0;
		if (!len)
			goto out;
		error = verify_area(VERIFY_WRITE,buf,len);
		if (error)
			goto out;
		cli();
		error = -ERESTARTSYS;
		while (!log_size) {
			if (signal_pending(current)) {
				sti();
				goto out;
			}
			interruptible_sleep_on(&log_wait);
		}
		i = 0;
		while (log_size && i < len) {
			c = *((char *) log_buf+log_start);
			log_start++;
			log_size--;
			log_start &= LOG_BUF_LEN-1;
			sti();
			__put_user(c,buf);
			buf++;
			i++;
			cli();
		}
		sti();
		error = i;
		break;
	case 4:		/* Read/clear last kernel messages */
		do_clear = 1; 
		/* FALL THRU */
	case 3:		/* Read last kernel messages */
		error = -EINVAL;
		if (!buf || len < 0)
			goto out;
		error = 0;
		if (!len)
			goto out;
		error = verify_area(VERIFY_WRITE,buf,len);
		if (error)
			goto out;
		/*
		 * The logged_chars, log_start, and log_size values may
		 * change from an interrupt, so we disable interrupts.
		 */
		__save_flags(flags);
		__cli();
		count = len;
		if (count > LOG_BUF_LEN)
			count = LOG_BUF_LEN;
		if (count > logged_chars)
			count = logged_chars;
		j = log_start + log_size - count;
		__restore_flags(flags);
		for (i = 0; i < count; i++) {
			c = *((char *) log_buf+(j++ & (LOG_BUF_LEN-1)));
			__put_user(c, buf++);
		}
		if (do_clear)
			logged_chars = 0;
		error = i;
		break;
	case 5:		/* Clear ring buffer */
		logged_chars = 0;
		break;
	case 6:		/* Disable logging to console */
		console_loglevel = minimum_console_loglevel;
		break;
	case 7:		/* Enable logging to console */
		console_loglevel = default_console_loglevel;
		break;
	case 8:
		error = -EINVAL;
		if (len < 1 || len > 8)
			goto out;
		if (len < minimum_console_loglevel)
			len = minimum_console_loglevel;
		console_loglevel = len;
		error = 0;
		break;
	default:
		error = -EINVAL;
		break;
	}
out:
	unlock_kernel();
	return error;
}

asmlinkage int sys_syslog(int type, char * buf, int len)
{
	if ((type != 3) && !capable(CAP_SYS_ADMIN))
		return -EPERM;
	return do_syslog(type, buf, len);
}


spinlock_t console_lock;

asmlinkage int printk(const char *fmt, ...)
{
	va_list args;
	int i;
	char *msg, *p, *buf_end;
	int line_feed;
	static signed char msg_level = -1;
	long flags;

	spin_lock_irqsave(&console_lock, flags);
	va_start(args, fmt);
	i = vsprintf(buf + 3, fmt, args); /* hopefully i < sizeof(buf)-4 */
	buf_end = buf + 3 + i;
	va_end(args);
	for (p = buf + 3; p < buf_end; p++) {
		msg = p;
		if (msg_level < 0) {
			if (
				p[0] != '<' ||
				p[1] < '0' || 
				p[1] > '7' ||
				p[2] != '>'
			) {
				p -= 3;
				p[0] = '<';
				p[1] = default_message_loglevel + '0';
				p[2] = '>';
			} else
				msg += 3;
			msg_level = p[1] - '0';
		}
		line_feed = 0;
		for (; p < buf_end; p++) {
			log_buf[(log_start+log_size) & (LOG_BUF_LEN-1)] = *p;
			if (log_size < LOG_BUF_LEN)
				log_size++;
			else {
				log_start++;
				log_start &= LOG_BUF_LEN-1;
			}
			logged_chars++;
			if (*p == '\n') {
				line_feed = 1;
				break;
			}
		}
		if (msg_level < console_loglevel && console_drivers) {
			struct console *c = console_drivers;
			while(c) {
				if ((c->flags & CON_ENABLED) && c->write)
					c->write(c, msg, p - msg + line_feed);
				c = c->next;
			}
		}
		if (line_feed)
			msg_level = -1;
	}
	spin_unlock_irqrestore(&console_lock, flags);
	wake_up_interruptible(&log_wait);
	return i;
}

void console_print(const char *s)
{
	struct console *c = console_drivers;
	int len = strlen(s);

	while(c) {
		if ((c->flags & CON_ENABLED) && c->write)
			c->write(c, s, len);
		c = c->next;
	}
}

void unblank_console(void)
{
	struct console *c = console_drivers;
	while(c) {
		if ((c->flags & CON_ENABLED) && c->unblank)
			c->unblank();
		c = c->next;
	}
}

/*
 * The console driver calls this routine during kernel initialization
 * to register the console printing procedure with printk() and to
 * print any messages that were printed by the kernel before the
 * console driver was initialized.
 */
void register_console(struct console * console)
{
	int	i,j,len;
	int	p = log_start;
	char	buf[16];
	signed char msg_level = -1;
	char	*q;

	/*
	 *	See if we want to use this console driver. If we
	 *	didn't select a console we take the first one
	 *	that registers here.
	 */
	if (preferred_console < 0) {
		if (console->index < 0)
			console->index = 0;
		if (console->setup == NULL ||
		    console->setup(console, NULL) == 0) {
			console->flags |= CON_ENABLED | CON_CONSDEV;
			preferred_console = 0;
		}
	}

	/*
	 *	See if this console matches one we selected on
	 *	the command line.
	 */
	for(i = 0; i < MAX_CMDLINECONSOLES && console_cmdline[i].name[0]; i++) {
		if (strcmp(console_cmdline[i].name, console->name) != 0)
			continue;
		if (console->index >= 0 &&
		    console->index != console_cmdline[i].index)
			continue;
		if (console->index < 0)
			console->index = console_cmdline[i].index;
		if (console->setup &&
		    console->setup(console, console_cmdline[i].options) != 0)
			break;
		console->flags |= CON_ENABLED;
		console->index = console_cmdline[i].index;
		if (i == preferred_console)
			console->flags |= CON_CONSDEV;
		break;
	}

	if (!(console->flags & CON_ENABLED))
		return;

	/*
	 *	Put this console in the list - keep the
	 *	preferred driver at the head of the list.
	 */
	if ((console->flags & CON_CONSDEV) || console_drivers == NULL) {
		console->next = console_drivers;
		console_drivers = console;
	} else {
		console->next = console_drivers->next;
		console_drivers->next = console;
	}
	if ((console->flags & CON_PRINTBUFFER) == 0) return;

	/*
	 *	Print out buffered log messages.
	 */
	for (i=0,j=0; i < log_size; i++) {
		buf[j++] = log_buf[p];
		p++; p &= LOG_BUF_LEN-1;
		if (buf[j-1] != '\n' && i < log_size - 1 && j < sizeof(buf)-1)
			continue;
		buf[j] = 0;
		q = buf;
		len = j;
		if (msg_level < 0) {
			msg_level = buf[1] - '0';
			q = buf + 3;
			len -= 3;
		}
		if (msg_level < console_loglevel)
			console->write(console, q, len);
		if (buf[j-1] == '\n')
			msg_level = -1;
		j = 0;
	}
}


int unregister_console(struct console * console)
{
        struct console *a,*b;
	
	if (console_drivers == console) {
		console_drivers=console->next;
		return (0);
	}
	for (a=console_drivers->next, b=console_drivers ;
	     a; b=a, a=b->next) {
		if (a == console) {
			b->next = a->next;
			return 0;
		}  
	}
	
	return (1);
}
	
/*
 * Write a message to a certain tty, not just the console. This is used for
 * messages that need to be redirected to a specific tty.
 * We don't put it into the syslog queue right now maybe in the future if
 * really needed.
 */
void tty_write_message(struct tty_struct *tty, char *msg)
{
	if (tty && tty->driver.write)
		tty->driver.write(tty, 0, msg, strlen(msg));
	return;
}
