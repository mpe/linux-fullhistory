/* 
 * Copyright (C) 2002 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <linux/unistd.h>
#include <sys/ptrace.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include "os.h"
#include "user.h"
#include "user_util.h"

#define ARBITRARY_ADDR -1
#define FAILURE_PID    -1

#define STAT_PATH_LEN sizeof("/proc/#######/stat\0")
#define COMM_SCANF "%*[^)])"

unsigned long os_process_pc(int pid)
{
	char proc_stat[STAT_PATH_LEN], buf[256];
	unsigned long pc;
	int fd, err;

	sprintf(proc_stat, "/proc/%d/stat", pid);
	fd = os_open_file(proc_stat, of_read(OPENFLAGS()), 0);
	if(fd < 0){
		printk("os_process_pc - couldn't open '%s', err = %d\n",
		       proc_stat, -fd);
		return(ARBITRARY_ADDR);
	}
	err = os_read_file(fd, buf, sizeof(buf));
	if(err < 0){
		printk("os_process_pc - couldn't read '%s', err = %d\n",
		       proc_stat, -err);
		os_close_file(fd);
		return(ARBITRARY_ADDR);
	}
	os_close_file(fd);
	pc = ARBITRARY_ADDR;
	if(sscanf(buf, "%*d " COMM_SCANF " %*c %*d %*d %*d %*d %*d %*d %*d "
		  "%*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d "
		  "%*d %*d %*d %*d %*d %lu", &pc) != 1){
		printk("os_process_pc - couldn't find pc in '%s'\n", buf);
	}
	return(pc);
}

int os_process_parent(int pid)
{
	char stat[STAT_PATH_LEN];
	char data[256];
	int parent, n, fd;

	if(pid == -1) return(-1);

	snprintf(stat, sizeof(stat), "/proc/%d/stat", pid);
	fd = os_open_file(stat, of_read(OPENFLAGS()), 0);
	if(fd < 0){
		printk("Couldn't open '%s', err = %d\n", stat, -fd);
		return(FAILURE_PID);
	}

	n = os_read_file(fd, data, sizeof(data));
	os_close_file(fd);

	if(n < 0){
		printk("Couldn't read '%s', err = %d\n", stat, -n);
		return(FAILURE_PID);
	}

	parent = FAILURE_PID;
	n = sscanf(data, "%*d " COMM_SCANF " %*c %d", &parent);
	if(n != 1)
		printk("Failed to scan '%s'\n", data);

	return(parent);
}

void os_stop_process(int pid)
{
	kill(pid, SIGSTOP);
}

void os_kill_process(int pid, int reap_child)
{
	kill(pid, SIGKILL);
	if(reap_child)
		CATCH_EINTR(waitpid(pid, NULL, 0));
		
}

/* Kill off a ptraced child by all means available.  kill it normally first,
 * then PTRACE_KILL it, then PTRACE_CONT it in case it's in a run state from
 * which it can't exit directly.
 */

void os_kill_ptraced_process(int pid, int reap_child)
{
	kill(pid, SIGKILL);
	ptrace(PTRACE_KILL, pid);
	ptrace(PTRACE_CONT, pid);
	if(reap_child)
		CATCH_EINTR(waitpid(pid, NULL, 0));
}

void os_usr1_process(int pid)
{
	kill(pid, SIGUSR1);
}

/*Don't use the glibc version, which caches the result in TLS. It misses some
 * syscalls, and also breaks with clone(), which does not unshare the TLS.*/
inline _syscall0(pid_t, getpid)

int os_getpid(void)
{
	return(getpid());
}

int os_map_memory(void *virt, int fd, unsigned long long off, unsigned long len,
		  int r, int w, int x)
{
	void *loc;
	int prot;

	prot = (r ? PROT_READ : 0) | (w ? PROT_WRITE : 0) | 
		(x ? PROT_EXEC : 0);

	loc = mmap64((void *) virt, len, prot, MAP_SHARED | MAP_FIXED,
		     fd, off);
	if(loc == MAP_FAILED)
		return(-errno);
	return(0);
}

int os_protect_memory(void *addr, unsigned long len, int r, int w, int x)
{
        int prot = ((r ? PROT_READ : 0) | (w ? PROT_WRITE : 0) | 
		    (x ? PROT_EXEC : 0));

        if(mprotect(addr, len, prot) < 0)
		return(-errno);
        return(0);
}

int os_unmap_memory(void *addr, int len)
{
        int err;

        err = munmap(addr, len);
	if(err < 0)
		return(-errno);
        return(0);
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
