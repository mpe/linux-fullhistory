/*
 * fs/proc/omirr.c  -  online mirror support
 *
 * (C) 1997 Thomas Schoebel-Theuer
 */

#include <linux/string.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/omirr.h>
#include <asm/uaccess.h>

static int nr_omirr_open = 0;
static int cleared_flag = 0;

static char * buffer = NULL;
static int read_pos, write_pos;
static int clip_pos, max_pos;
static struct wait_queue * read_wait = NULL;
static struct wait_queue * write_wait = NULL;

static /*inline*/ int reserve_write_space(int len)
{
	int rest = max_pos - write_pos;

	if(rest < len) {
		clip_pos = write_pos;
		write_pos = 0;
		rest = max_pos;
	}
	while(read_pos > write_pos && read_pos <= write_pos+len) {
		if(!nr_omirr_open)
			return 0;
		interruptible_sleep_on(&write_wait);
	}
	return 1;
}

static /*inline*/ void write_space(int len)
{
	write_pos += len;
	wake_up_interruptible(&read_wait);
}

static /*inline*/ int reserve_read_space(int len)
{
	int rest = clip_pos - read_pos;

	if(!rest) {
		read_pos = 0;
		rest = clip_pos;
		clip_pos = max_pos;
	}
	if(len > rest)
		len = rest;
	while(read_pos == write_pos) {
		interruptible_sleep_on(&read_wait);
	}
	rest = write_pos - read_pos;
	if(rest > 0 && rest < len)
		len = rest;
	return len;
}

static /*inline*/ void read_space(int len)
{
	read_pos += len;
	if(read_pos >= clip_pos) {
		read_pos = 0;
		clip_pos = max_pos;
	}
	wake_up_interruptible(&write_wait);
}

static /*inline*/ void init_buffer(char * initxt)
{
	int len = initxt ? strlen(initxt) : 0;

	if(!buffer) {
		buffer = (char*)__get_free_page(GFP_USER);
		max_pos = clip_pos = PAGE_SIZE;
	}
	read_pos = write_pos = 0;
	memcpy(buffer, initxt, len);
	write_space(len);
}

static int omirr_open(struct inode * inode, struct file * file)
{
	if(nr_omirr_open)
		return -EAGAIN;
	nr_omirr_open++;
	if(!buffer)
		init_buffer(NULL);
	return 0;
}

static int omirr_release(struct inode * inode, struct file * file)
{
	nr_omirr_open--;
	read_space(0);
	return 0;
}

static long omirr_read(struct inode * inode, struct file * file,
		       char * buf, unsigned long count)
{
	char * tmp;
	int len;
	int error = 0;

	if(!count)
		goto done;
	error = -EINVAL;
	if(!buf || count < 0)
		goto done;
	
	error = verify_area(VERIFY_WRITE, buf, count);
	if(error)
		goto done;
	
	error = -EAGAIN;
	if((file->f_flags & O_NONBLOCK) && read_pos == write_pos)
		goto done;

	error = len = reserve_read_space(count);
	tmp = buffer + read_pos;
	while(len) {
		put_user(*tmp++, buf++);
		len--;
	}
	read_space(error);
done:
	return error;		
}

int compute_name(struct dentry * entry, char * buf)
{
	int len;

	if(IS_ROOT(entry)) {
		*buf = '/';
		return 1;
	}
	len = compute_name(entry->d_parent, buf);
	if(len > 1) {
		buf[len++] = '/';
	}
	memcpy(buf+len, entry->d_name, entry->d_len);
	return len + entry->d_len;
}

int _omirr_print(struct dentry * ent1, struct dentry * ent2, 
		struct qstr * suffix, const char * fmt,
		 va_list args1, va_list args2)
{
	int count = strlen(fmt) + 10; /* estimate */
	const char * tmp = fmt;
	char lenbuf[8];
	int res;

	if(!buffer)
		init_buffer(NULL);
	while(*tmp) {
		while(*tmp && *tmp++ != '%') ;
		if(*tmp) {
			if(*tmp == 's') {
				char * str = va_arg(args1, char*);
				count += strlen(str);
			} else {
				(void)va_arg(args1, int);
				count += 8; /* estimate */
			}
		}
	}
	if(ent1) {
		struct dentry * dent = ent1;
		while(dent && !IS_ROOT(dent)) {
			count += dent->d_len + 1;
			dent = dent->d_parent;
		}
		count++;
		if(ent2) {
			dent = ent2;
			while(dent && !IS_ROOT(dent)) {
				count += dent->d_len + 1;
				dent = dent->d_parent;
			}
		count++;
		}
		if(suffix)
			count += suffix->len + 1;
	}
	
	if((nr_omirr_open | cleared_flag) && reserve_write_space(count)) {
		cleared_flag = 0;
		res = vsprintf(buffer+write_pos+4, fmt, args2) + 4;
		if(res > count)
			printk("omirr: format estimate was wrong\n");
		if(ent1) {
			res += compute_name(ent1, buffer+write_pos+res);
			if(ent2) {
				buffer[write_pos+res++] = '\0';
				res += compute_name(ent2, buffer+write_pos+res);
			}
			if(suffix) {
				buffer[write_pos+res++] = '/';
				memcpy(buffer+write_pos+res,
				       suffix->name, suffix->len);
				res += suffix->len;
			}
			buffer[write_pos+res++] = '\0';
			buffer[write_pos+res++] = '\n';
		}
		sprintf(lenbuf, "%04d", res);
		memcpy(buffer+write_pos, lenbuf, 4);
	} else {
		if(!cleared_flag) {
			cleared_flag = 1;
			init_buffer("0007 Z\n");
		}
		res = 0;
	}
	write_space(res);
	return res;
}

int omirr_print(struct dentry * ent1, struct dentry * ent2, 
		struct qstr * suffix, const char * fmt, ...)
{
	va_list args1, args2;
	int res;

	/* I don't know whether I could make a simple copy of the va_list,
	 * so for the safe way...
	 */
	va_start(args1, fmt);
	va_start(args2, fmt);
	res = _omirr_print(ent1, ent2, suffix, fmt, args1, args2);
	va_end(args2);
	va_end(args1);
	return res;
}

int omirr_printall(struct inode * inode, const char * fmt, ...)
{
	int res = 0;
	struct dentry * tmp = inode->i_dentry;

	if(tmp) do {
		va_list args1, args2;
		va_start(args1, fmt);
		va_start(args2, fmt);
		res += _omirr_print(tmp, NULL, NULL, fmt, args1, args2);
		va_end(args2);
		va_end(args1);
		tmp = tmp->d_next;
	} while(tmp != inode->i_dentry);
	return res;
}

static struct file_operations omirr_operations = {
    NULL,	/* omirr_lseek */
    omirr_read,
    NULL,	/* omirr_write */
    NULL,	/* omirr_readdir */
    NULL,	/* omirr_select */
    NULL,	/* omirr_ioctl */
    NULL,	/* mmap */
    omirr_open,
    NULL,	/* flush */
    omirr_release,
    NULL,	/* fsync */
    NULL,       /* fasync */
    NULL,       /* check_media_change */
    NULL        /* revalidate */
};

struct inode_operations proc_omirr_inode_operations = {
    &omirr_operations,
    NULL, /* create */
    NULL, /* lookup */
    NULL, /* link */
    NULL, /* unlink */
    NULL, /* symlink */
    NULL, /* mkdir */
    NULL, /* rmdir */
    NULL, /* mknod */
    NULL, /* rename */
    NULL, /* readlink */
    NULL, /* follow_link */
    NULL, /* readpage */
    NULL, /* writepage */
    NULL, /* bmap */
    NULL, /* truncate */
    NULL, /* permission */
    NULL  /* smap */
};
