/* $Id: sys_sunos.c,v 1.71 1996/12/29 20:46:02 davem Exp $
 * sys_sunos.c: SunOS specific syscall compatibility support.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1995 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *
 * Based upon preliminary work which is:
 *
 * Copyright (C) 1995 Adrian M. Rodriguez (adrian@remus.rutgers.edu)
 *
 * The sunos_poll routine is based on iBCS2's poll routine, this
 * is the copyright message for that file:
 *
 * This file contains the procedures for the handling of poll.
 *
 * Copyright (C) 1994 Eric Youngdale
 *
 * Created for Linux based loosely upon linux select code, which
 * in turn is loosely based upon Mathius Lattner's minix
 * patches by Peter MacDonald. Heavily edited by Linus.
 *
 * Poll is used by SVr4 instead of select, and it has considerably
 * more functionality.  Parts of it are related to STREAMS, and since
 * we do not have streams, we fake it.  In fact, select() still exists
 * under SVr4, but libc turns it into a poll() call instead.  We attempt
 * to do the inverse mapping.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/fs.h>
#include <linux/resource.h>
#include <linux/ipc.h>
#include <linux/shm.h>
#include <linux/sem.h>
#include <linux/signal.h>
#include <linux/uio.h>
#include <linux/utsname.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/stat.h>
#include <linux/malloc.h>
#include <linux/pagemap.h>
#include <linux/errno.h>

#include <asm/uaccess.h>
#ifndef KERNEL_DS
#include <linux/segment.h>
#endif

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/pconf.h>
#include <asm/idprom.h> /* for gethostid() */
#include <asm/unistd.h>
#include <asm/system.h>

/* For the nfs mount emulation */
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/nfs.h>
#include <linux/nfs_mount.h>

/* for sunos_select */
#include <linux/time.h>
#include <linux/personality.h>

extern unsigned long get_unmapped_area(unsigned long addr, unsigned long len);

/* We use the SunOS mmap() semantics. */
asmlinkage unsigned long sunos_mmap(unsigned long addr, unsigned long len,
				    unsigned long prot, unsigned long flags,
				    unsigned long fd, unsigned long off)
{
	struct file * file = NULL;
	unsigned long retval, ret_type;

	current->personality |= PER_BSD;
	if(flags & MAP_NORESERVE) {
		printk("%s: unimplemented SunOS MAP_NORESERVE mmap() flag\n",
		       current->comm);
		flags &= ~MAP_NORESERVE;
	}
	if(!(flags & MAP_ANONYMOUS))
		if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
			return -EBADF;
	if(!(flags & MAP_FIXED) && !addr) {
		addr = get_unmapped_area(addr, len);
		if(!addr)
			return -ENOMEM;
	}
	/* If this is ld.so or a shared library doing an mmap
	 * of /dev/zero, transform it into an anonymous mapping.
	 * SunOS is so stupid some times... hmph!
	 */
	if(MAJOR(file->f_inode->i_rdev) == MEM_MAJOR &&
	   MINOR(file->f_inode->i_rdev) == 5) {
		flags |= MAP_ANONYMOUS;
		file = 0;
	}
	if(!(flags & MAP_FIXED))
		addr = 0;
	ret_type = flags & _MAP_NEW;
	flags &= ~_MAP_NEW;

	/* See asm-sparc/uaccess.h */
	if((len > (TASK_SIZE - PAGE_SIZE)) || (addr > (TASK_SIZE-len-PAGE_SIZE)))
		return -EINVAL;

	if(sparc_cpu_model == sun4c) {
		if(((addr >= 0x20000000) && (addr < 0xe0000000)))
			return current->mm->brk;
	}

	retval = do_mmap(file, addr, len, prot, flags, off);
	if(ret_type)
		return retval;
	else
		return ((retval < PAGE_OFFSET) ? 0 : retval);
}

/* lmbench calls this, just say "yeah, ok" */
asmlinkage int sunos_mctl(unsigned long addr, unsigned long len, int function, char *arg)
{
	return 0;
}

/* SunOS is completely broken... it returns 0 on success, otherwise
 * ENOMEM.  For sys_sbrk() it wants the old brk value as a return
 * on success and ENOMEM as before on failure.
 */
asmlinkage int sunos_brk(unsigned long brk)
{
	int freepages;
	unsigned long rlim;
	unsigned long newbrk, oldbrk;

	if(sparc_cpu_model == sun4c) {
		if(brk >= 0x20000000 && brk < 0xe0000000)
			return -ENOMEM;
	}

	if (brk < current->mm->end_code)
		return -ENOMEM;

	newbrk = PAGE_ALIGN(brk);
	oldbrk = PAGE_ALIGN(current->mm->brk);
	if (oldbrk == newbrk) {
		current->mm->brk = brk;
		return 0;
	}

	/*
	 * Always allow shrinking brk
	 */
	if (brk <= current->mm->brk) {
		current->mm->brk = brk;
		do_munmap(newbrk, oldbrk-newbrk);
		return 0;
	}
	/*
	 * Check against rlimit and stack..
	 */
	rlim = current->rlim[RLIMIT_DATA].rlim_cur;
	if (rlim >= RLIM_INFINITY)
		rlim = ~0;
	if (brk - current->mm->end_code > rlim)
		return -ENOMEM;

	/*
	 * Check against existing mmap mappings.
	 */
	if (find_vma_intersection(current->mm, oldbrk, newbrk+PAGE_SIZE))
		return -ENOMEM;

	/*
	 * stupid algorithm to decide if we have enough memory: while
	 * simple, it hopefully works in most obvious cases.. Easy to
	 * fool it, but this should catch most mistakes.
	 */
	freepages = buffermem >> PAGE_SHIFT;
        freepages += page_cache_size;
	freepages >>= 1;
	freepages += nr_free_pages;
	freepages += nr_swap_pages;
	freepages -= num_physpages >> 4;
	freepages -= (newbrk-oldbrk) >> PAGE_SHIFT;
	if (freepages < 0)
		return -ENOMEM;
	/*
	 * Ok, we have probably got enough memory - let it rip.
	 */
	current->mm->brk = brk;
	do_mmap(NULL, oldbrk, newbrk-oldbrk,
		PROT_READ|PROT_WRITE|PROT_EXEC,
		MAP_FIXED|MAP_PRIVATE, 0);
	return 0;
}

asmlinkage unsigned long sunos_sbrk(int increment)
{
	int error;
	unsigned long oldbrk = current->mm->brk;

	/* This should do it hopefully... */
	error = sunos_brk(((int) current->mm->brk) + increment);
	if(error)
		return error;
	else
		return oldbrk;
}

/* XXX Completely undocumented, and completely magic...
 * XXX I believe it is to increase the size of the stack by
 * XXX argument 'increment' and return the new end of stack
 * XXX area.  Wheee...
 */
asmlinkage unsigned long sunos_sstk(int increment)
{
	printk("%s: Call to sunos_sstk(increment<%d>) is unsupported\n",
	       current->comm, increment);
	return -1;
}

/* Give hints to the kernel as to what paging strategy to use...
 * Completely bogus, don't remind me.
 */
#define VA_NORMAL     0 /* Normal vm usage expected */
#define VA_ABNORMAL   1 /* Abnormal/random vm usage probable */
#define VA_SEQUENTIAL 2 /* Accesses will be of a sequential nature */
#define VA_INVALIDATE 3 /* Page table entries should be flushed ??? */
static char *vstrings[] = {
	"VA_NORMAL",
	"VA_ABNORMAL",
	"VA_SEQUENTIAL",
	"VA_INVALIDATE",
};

asmlinkage void sunos_vadvise(unsigned long strategy)
{
	/* I wanna see who uses this... */
	printk("%s: Advises us to use %s paging strategy\n",
	       current->comm,
	       strategy <= 3 ? vstrings[strategy] : "BOGUS");
	return; /* We don't do diddly... */
}

/* Same as vadvise, and just as bogus, but for a range of virtual
 * process address space.
 */
#define MADV_NORMAL      0 /* Nothing special... */
#define MADV_RANDOM      1 /* I am emacs... */
#define MADV_SEQUENTIAL  2 /* I am researcher code... */
#define MADV_WILLNEED    3 /* Pages in this range will be needed */
#define MADV_DONTNEED    4 /* Pages in this range won't be needed */

static char *mstrings[] = {
	"MADV_NORMAL",
	"MADV_RANDOM",
	"MADV_SEQUENTIAL",
	"MADV_WILLNEED",
	"MADV_DONTNEED",
};

asmlinkage void sunos_madvise(unsigned long address, unsigned long len,
			      unsigned long strategy)
{
	/* I wanna see who uses this... */
	printk("%s: Advises us to use %s paging strategy for addr<%08lx> len<%08lx>\n",
	       current->comm,
	       strategy <= 4 ? mstrings[strategy] : "BOGUS",
	       address, len);
	return; /* We don't do diddly... */
}

/* Places into character array, the status of all the pages in the passed
 * range from 'addr' to 'addr + len'.  -1 on failure, 0 on success...
 * The encoding in each character is:
 * low-bit is zero == Page is not in physical ram right now
 * low-bit is one  == Page is currently residing in core
 * All other bits are undefined within the character so there...
 * Also, if you try to get stats on an area outside of the user vm area
 * *or* the passed base address is not aligned on a page boundary you
 * get an error.
 */
asmlinkage int sunos_mincore(unsigned long addr, unsigned long len, char *array)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	unsigned long limit;
	int num_pages, pnum;

	if(addr & ~(PAGE_MASK))
		return -EINVAL;

	num_pages = (len / PAGE_SIZE);
	if(verify_area(VERIFY_WRITE, array, num_pages))
		return -EFAULT;
	if((addr >= PAGE_OFFSET) || ((addr + len) > PAGE_OFFSET))
		return -ENOMEM; /* I'm sure you're curious about kernel mappings.. */

	/* Wheee, go through pte's */
	pnum = 0;
	for(limit = addr + len; addr < limit; addr += PAGE_SIZE, pnum++) {
		pgdp = pgd_offset(current->mm, addr);
		if(pgd_none(*pgdp))
			return -ENOMEM; /* As per SunOS manpage */
		pmdp = pmd_offset(pgdp, addr);
		if(pmd_none(*pmdp))
			return -ENOMEM; /* As per SunOS manpage */
		ptep = pte_offset(pmdp, addr);
		if(pte_none(*ptep))
			return -ENOMEM; /* As per SunOS manpage */
		/* Page in core or Swapped page? */
		__put_user((pte_present(*ptep) ? 1 : 0), &array[pnum]);
	}
	return 0; /* Success... I think... */
}

/* This just wants the soft limit (ie. rlim_cur element) of the RLIMIT_NOFILE
 * resource limit and is for backwards compatibility with older sunos
 * revs.
 */
asmlinkage long sunos_getdtablesize(void)
{
	return NR_OPEN;
}
#define _S(nr) (1<<((nr)-1))

#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

asmlinkage unsigned long sunos_sigblock(unsigned long blk_mask)
{
	unsigned long flags;
	unsigned long old;

	save_and_cli(flags);
	old = current->blocked;
	current->blocked |= (blk_mask & _BLOCKABLE);
	restore_flags(flags);
	return old;
}

asmlinkage unsigned long sunos_sigsetmask(unsigned long newmask)
{
	unsigned long flags;
	unsigned long retval;

	save_and_cli(flags);
	retval = current->blocked;
	current->blocked = newmask & _BLOCKABLE;
	restore_flags(flags);
	return retval;
}

/* SunOS getdents is very similar to the newer Linux (iBCS2 compliant)    */
/* getdents system call, the format of the structure just has a different */
/* layout (d_off+d_ino instead of d_ino+d_off) */
struct sunos_dirent {
    long           d_off;
    unsigned long  d_ino;
    unsigned short d_reclen;
    unsigned short d_namlen;
    char           d_name[1];
};

struct sunos_dirent_callback {
    struct sunos_dirent *curr;
    struct sunos_dirent *previous;
    int count;
    int error;
};

#define NAME_OFFSET(de) ((int) ((de)->d_name - (char *) (de)))
#define ROUND_UP(x) (((x)+sizeof(long)-1) & ~(sizeof(long)-1))

static int sunos_filldir(void * __buf, const char * name, int namlen,
			 off_t offset, ino_t ino)
{
	struct sunos_dirent * dirent;
	struct sunos_dirent_callback * buf = (struct sunos_dirent_callback *) __buf;
	int reclen = ROUND_UP(NAME_OFFSET(dirent) + namlen + 1);

	buf->error = -EINVAL;	/* only used if we fail.. */
	if (reclen > buf->count)
		return -EINVAL;
	dirent = buf->previous;
	if (dirent)
		put_user(offset, &dirent->d_off);
	dirent = buf->curr;
	buf->previous = dirent;
	put_user(ino, &dirent->d_ino);
	put_user(namlen, &dirent->d_namlen);
	put_user(reclen, &dirent->d_reclen);
	copy_to_user(dirent->d_name, name, namlen);
	put_user(0, dirent->d_name + namlen);
	((char *) dirent) += reclen;
	buf->curr = dirent;
	buf->count -= reclen;
	return 0;
}

asmlinkage int sunos_getdents(unsigned int fd, void * dirent, int cnt)
{
	struct file * file;
	struct sunos_dirent * lastdirent;
	struct sunos_dirent_callback buf;
	int error;

	if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
		return -EBADF;
	if (!file->f_op || !file->f_op->readdir)
		return -ENOTDIR;
	if(cnt < (sizeof(struct sunos_dirent) + 255))
		return -EINVAL;

	buf.curr = (struct sunos_dirent *) dirent;
	buf.previous = NULL;
	buf.count = cnt;
	buf.error = 0;
	error = file->f_op->readdir(file->f_inode, file, &buf, sunos_filldir);
	if (error < 0)
		return error;
	lastdirent = buf.previous;
	if (!lastdirent)
		return buf.error;
	put_user(file->f_pos, &lastdirent->d_off);
	return cnt - buf.count;
}

/* Old sunos getdirentries, severely broken compatibility stuff here. */
struct sunos_direntry {
    unsigned long  d_ino;
    unsigned short d_reclen;
    unsigned short d_namlen;
    char           d_name[1];
};

struct sunos_direntry_callback {
    struct sunos_direntry *curr;
    struct sunos_direntry *previous;
    int count;
    int error;
};

static int sunos_filldirentry(void * __buf, const char * name, int namlen,
			      off_t offset, ino_t ino)
{
	struct sunos_direntry * dirent;
	struct sunos_direntry_callback * buf = (struct sunos_direntry_callback *) __buf;
	int reclen = ROUND_UP(NAME_OFFSET(dirent) + namlen + 1);

	buf->error = -EINVAL;	/* only used if we fail.. */
	if (reclen > buf->count)
		return -EINVAL;
	dirent = buf->previous;
	dirent = buf->curr;
	buf->previous = dirent;
	put_user(ino, &dirent->d_ino);
	put_user(namlen, &dirent->d_namlen);
	put_user(reclen, &dirent->d_reclen);
	copy_to_user(dirent->d_name, name, namlen);
	put_user(0, dirent->d_name + namlen);
	((char *) dirent) += reclen;
	buf->curr = dirent;
	buf->count -= reclen;
	return 0;
}

asmlinkage int sunos_getdirentries(unsigned int fd, void * dirent, int cnt, unsigned int *basep)
{
	struct file * file;
	struct sunos_direntry * lastdirent;
	struct sunos_direntry_callback buf;
	int error;

	if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
		return -EBADF;
	if (!file->f_op || !file->f_op->readdir)
		return -ENOTDIR;
	if(cnt < (sizeof(struct sunos_direntry) + 255))
		return -EINVAL;

	buf.curr = (struct sunos_direntry *) dirent;
	buf.previous = NULL;
	buf.count = cnt;
	buf.error = 0;
	error = file->f_op->readdir(file->f_inode, file, &buf, sunos_filldirentry);
	if (error < 0)
		return error;
	lastdirent = buf.previous;
	if (!lastdirent)
		return buf.error;
	put_user(file->f_pos, basep);
	return cnt - buf.count;
}

asmlinkage int sunos_getdomainname(char *name, int len)
{
        int nlen = strlen(system_utsname.domainname);

        if (nlen < len)
                len = nlen;

	if(len > __NEW_UTS_LEN)
		return -EFAULT;
	if(copy_to_user(name, system_utsname.domainname, len))
		return -EFAULT;
	return 0;
}

struct sunos_utsname {
	char sname[9];
	char nname[9];
	char nnext[56];
	char rel[9];
	char ver[9];
	char mach[9];
};

asmlinkage int sunos_uname(struct sunos_utsname *name)
{
	if(!name)
		return -EFAULT;
	if(copy_to_user(&name->sname[0], &system_utsname.sysname[0], sizeof(name->sname) - 1))
		return -EFAULT;
	copy_to_user(&name->nname[0], &system_utsname.nodename[0], sizeof(name->nname) - 1);
	put_user('\0', &name->nname[8]);
	copy_to_user(&name->rel[0], &system_utsname.release[0], sizeof(name->rel) - 1);
	copy_to_user(&name->ver[0], &system_utsname.version[0], sizeof(name->ver) - 1);
	copy_to_user(&name->mach[0], &system_utsname.machine[0], sizeof(name->mach) - 1);
	return 0;
}

asmlinkage int sunos_nosys(void)
{
	struct pt_regs *regs;

	regs = current->tss.kregs;
	current->tss.sig_address = regs->pc;
	current->tss.sig_desc = regs->u_regs[UREG_G1];
	send_sig(SIGSYS, current, 1);
	printk("Process makes ni_syscall number %d, register dump:\n",
	       (int) regs->u_regs[UREG_G1]);
	show_regs(regs);
	return -ENOSYS;
}

/* This is not a real and complete implementation yet, just to keep
 * the easy SunOS binaries happy.
 */
asmlinkage int sunos_fpathconf(int fd, int name)
{
	switch(name) {
	case _PCONF_LINK:
		return LINK_MAX;
	case _PCONF_CANON:
		return MAX_CANON;
	case _PCONF_INPUT:
		return MAX_INPUT;
	case _PCONF_NAME:
		return NAME_MAX;
	case _PCONF_PATH:
		return PATH_MAX;
	case _PCONF_PIPE:
		return PIPE_BUF;
	case _PCONF_CHRESTRICT:
		return 1; /* XXX Investigate XXX */
	case _PCONF_NOTRUNC:
		return 0; /* XXX Investigate XXX */
	case _PCONF_VDISABLE:
		return 0;
	default:
		return -EINVAL;
	}
}

asmlinkage int sunos_pathconf(char *path, int name)
{
	return sunos_fpathconf(0, name); /* XXX cheese XXX */
}

/* SunOS mount system call emulation */
extern asmlinkage int
sys_select(int n, fd_set *inp, fd_set *outp, fd_set *exp, struct timeval *tvp);

asmlinkage int sunos_select(int width, fd_set *inp, fd_set *outp, fd_set *exp, struct timeval *tvp)
{
	/* SunOS binaries expect that select won't change the tvp contents */
	current->personality |= STICKY_TIMEOUTS;
	return sys_select (width, inp, outp, exp, tvp);
}

asmlinkage void sunos_nop(void)
{
	return;
}

/* SunOS mount/umount. */
#define SMNT_RDONLY       1
#define SMNT_NOSUID       2
#define SMNT_NEWTYPE      4
#define SMNT_GRPID        8
#define SMNT_REMOUNT      16
#define SMNT_NOSUB        32
#define SMNT_MULTI        64
#define SMNT_SYS5         128

struct sunos_fh_t {
	char fh_data [NFS_FHSIZE];
};

struct sunos_nfs_mount_args {
	struct sockaddr_in  *addr; /* file server address */
	struct nfs_fh *fh;     /* File handle to be mounted */
	int        flags;      /* flags */
	int        wsize;      /* write size in bytes */
	int        rsize;      /* read size in bytes */
	int        timeo;      /* initial timeout in .1 secs */
	int        retrans;    /* times to retry send */
	char       *hostname;  /* server's hostname */
	int        acregmin;   /* attr cache file min secs */
	int        acregmax;   /* attr cache file max secs */
	int        acdirmin;   /* attr cache dir min secs */
	int        acdirmax;   /* attr cache dir max secs */
	char       *netname;   /* server's netname */
};


extern int do_mount(kdev_t, const char *, const char *, char *, int, void *);
extern dev_t get_unnamed_dev(void);
extern void put_unnamed_dev(dev_t);
extern asmlinkage int sys_mount(char *, char *, char *, unsigned long, void *);
extern asmlinkage int sys_connect(int fd, struct sockaddr *uservaddr, int addrlen);
extern asmlinkage int sys_socket(int family, int type, int protocol);
extern asmlinkage int sys_bind(int fd, struct sockaddr *umyaddr, int addrlen);


/* Bind the socket on a local reserved port and connect it to the
 * remote server.  This on Linux/i386 is done by the mount program,
 * not by the kernel. 
 */
static int
sunos_nfs_get_server_fd (int fd, struct sockaddr_in *addr)
{
	struct sockaddr_in local;
	struct sockaddr_in server;
	int    try_port;
	int    ret;
	struct socket *socket;
	struct inode  *inode;
	struct file   *file;

	file = current->files->fd [fd];
	inode = file->f_inode;
	if (!inode || !inode->i_sock)
		return 0;

	socket = &inode->u.socket_i;
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = INADDR_ANY;

	/* IPPORT_RESERVED = 1024, can't find the definition in the kernel */
	try_port = 1024;
	do {
		local.sin_port = htons (--try_port);
		ret = socket->ops->bind(socket, (struct sockaddr*)&local,
					sizeof(local));
	} while (ret && try_port > (1024 / 2));

	if (ret)
		return 0;

	server.sin_family = AF_INET;
	server.sin_addr = addr->sin_addr;
	server.sin_port = NFS_PORT;

	/* Call sys_connect */
	ret = socket->ops->connect (socket, (struct sockaddr *) &server,
				    sizeof (server), file->f_flags);
	if (ret < 0)
		return 0;
	return 1;
}

static int get_default (int value, int def_value)
{
    if (value)
	return value;
    else
	return def_value;
}

asmlinkage int sunos_nfs_mount(char *dir_name, int linux_flags, void *data)
{
	int  ret = -ENODEV;
	int  server_fd;
	char *the_name;
	struct nfs_mount_data linux_nfs_mount;
	struct sunos_nfs_mount_args *sunos_mount = data;
	dev_t dev;

	/* Ok, here comes the fun part: Linux's nfs mount needs a
	 * socket connection to the server, but SunOS mount does not
	 * require this, so we use the information on the destination
	 * address to create a socket and bind it to a reserved
	 * port on this system
	 */
	server_fd = sys_socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (server_fd < 0)
		return -ENXIO;

	if (!sunos_nfs_get_server_fd (server_fd, sunos_mount->addr)){
		sys_close (server_fd);
		return -ENXIO;
	}

	/* Now, bind it to a locally reserved port */
	linux_nfs_mount.version  = NFS_MOUNT_VERSION;
	linux_nfs_mount.flags    = sunos_mount->flags;
	linux_nfs_mount.addr     = *sunos_mount->addr;
	linux_nfs_mount.root     = *sunos_mount->fh;
	linux_nfs_mount.fd       = server_fd;
	
	linux_nfs_mount.rsize    = get_default (sunos_mount->rsize, 8192);
	linux_nfs_mount.wsize    = get_default (sunos_mount->wsize, 8192);
	linux_nfs_mount.timeo    = get_default (sunos_mount->timeo, 10);
	linux_nfs_mount.retrans  = sunos_mount->retrans;
	
	linux_nfs_mount.acregmin = sunos_mount->acregmin;
	linux_nfs_mount.acregmax = sunos_mount->acregmax;
	linux_nfs_mount.acdirmin = sunos_mount->acdirmin;
	linux_nfs_mount.acdirmax = sunos_mount->acdirmax;

	if (getname (sunos_mount->hostname, &the_name))
		return -EFAULT;

	strncpy (linux_nfs_mount.hostname, the_name, 254);
	linux_nfs_mount.hostname [255] = 0;
	putname (the_name);

	dev = get_unnamed_dev ();
	
	ret = do_mount (dev, "", dir_name, "nfs", linux_flags, &linux_nfs_mount);
	if (ret)
	    put_unnamed_dev(dev);

	return ret;
}

asmlinkage int
sunos_mount(char *type, char *dir, int flags, void *data)
{
	int linux_flags = MS_MGC_MSK; /* new semantics */
	char *dev_fname = 0;

	/* We don't handle the integer fs type */
	if ((flags & SMNT_NEWTYPE) == 0)
		return -EINVAL;

	/* Do not allow for those flags we don't support */
	if (flags & (SMNT_GRPID|SMNT_NOSUB|SMNT_MULTI|SMNT_SYS5))
		return -EINVAL;

	if(flags & SMNT_REMOUNT)
		linux_flags |= MS_REMOUNT;
	if(flags & SMNT_RDONLY)
		linux_flags |= MS_RDONLY;
	if(flags & SMNT_NOSUID)
		linux_flags |= MS_NOSUID;
	if(strcmp(type, "ext2") == 0) {
		dev_fname = (char *) data;
	} else if(strcmp(type, "iso9660") == 0) {
		dev_fname = (char *) data;
	} else if(strcmp(type, "minix") == 0) {
		dev_fname = (char *) data;
	} else if(strcmp(type, "ext") == 0) {
		dev_fname = (char *) data;
	} else if(strcmp(type, "xiafs") == 0) {
		dev_fname = (char *) data;
	} else if(strcmp(type, "nfs") == 0) {
		return sunos_nfs_mount (dir, flags, data);
        } else if(strcmp(type, "ufs") == 0) {
		printk("Warning: UFS filesystem mounts unsupported.\n");
		return -ENODEV;
	} else if(strcmp(type, "proc")) {
		return -ENODEV;
	}
	return sys_mount(dev_fname, dir, type, linux_flags, NULL);
}

extern asmlinkage int sys_setsid(void);
extern asmlinkage int sys_setpgid(pid_t, pid_t);

asmlinkage int sunos_setpgrp(pid_t pid, pid_t pgid)
{
	/* So stupid... */
	if((!pid || pid == current->pid) &&
	   !pgid) {
		sys_setsid();
		return 0;
	} else {
		return sys_setpgid(pid, pgid);
	}
}

/* So stupid... */
extern asmlinkage int sys_wait4(pid_t, unsigned int *, int, struct rusage *);
asmlinkage int sunos_wait4(pid_t pid, unsigned int *stat_addr, int options, struct rusage *ru)
{
	return sys_wait4((pid ? pid : -1), stat_addr, options, ru);
}

extern int kill_pg(int, int, int);
asmlinkage int sunos_killpg(int pgrp, int sig)
{
	return kill_pg(pgrp, sig, 0);
}

asmlinkage int sunos_audit(void)
{
	printk ("sys_audit\n");
	return -1;
}

extern asmlinkage unsigned long sunos_gethostid(void)
{
	return ((unsigned long)idprom->id_machtype << 24) |
	       (unsigned long)idprom->id_sernum;
}

extern asmlinkage long sunos_sysconf (int name)
{
	switch (name){
	case _SC_ARG_MAX:
		return ARG_MAX;
	case _SC_CHILD_MAX:
		return CHILD_MAX;
	case _SC_CLK_TCK:
		return HZ;
	case _SC_NGROUPS_MAX:
		return NGROUPS_MAX;
	case _SC_OPEN_MAX:
		return OPEN_MAX;
	case _SC_JOB_CONTROL:
		return 1;	/* yes, we do support job control */
	case _SC_SAVED_IDS:
		return 1;	/* yes, we do support saved uids  */
	case _SC_VERSION:
		/* mhm, POSIX_VERSION is in /usr/include/unistd.h
		 * should it go on /usr/include/linux?
		 */
		return  199009L; 
	}
	return -1;
}

#define POLL_ROUND_UP(x,y) (((x)+(y)-1)/(y))

#define POLLIN 1
#define POLLPRI 2
#define POLLOUT 4
#define POLLERR 8
#define POLLHUP 16
#define POLLNVAL 32
#define POLLRDNORM 64
#define POLLWRNORM POLLOUT
#define POLLRDBAND 128
#define POLLWRBAND 256

#define LINUX_POLLIN (POLLRDNORM | POLLRDBAND | POLLIN)
#define LINUX_POLLOUT (POLLWRBAND | POLLWRNORM | POLLOUT)
#define LINUX_POLLERR (POLLERR)

static inline void free_wait(select_table * p)
{
	struct select_table_entry * entry = p->entry + p->nr;

	while (p->nr > 0) {
		p->nr--;
		entry--;
		remove_wait_queue(entry->wait_address,&entry->wait);
	}
}


/* Copied directly from fs/select.c */
static int check(int flag, select_table * wait, struct file * file)
{
	struct inode * inode;
	struct file_operations *fops;
	int (*select) (struct inode *, struct file *, int, select_table *);

	inode = file->f_inode;
	if ((fops = file->f_op) && (select = fops->select))
		return select(inode, file, flag, wait)
		    || (wait && select(inode, file, flag, NULL));
	if (S_ISREG(inode->i_mode))
		return 1;
	return 0;
}

struct poll {
	int fd;
	short events;
	short revents;
};

int sunos_poll(struct poll * ufds, size_t nfds, int timeout)
{
        int i,j, count, fdcount, retflag;
	struct poll * fdpnt;
	struct poll * fds, *fds1;
	select_table wait_table, *wait;
	struct select_table_entry *entry;

	if (nfds > NR_OPEN)
		return -EINVAL;

	if (!(entry = (struct select_table_entry*)__get_free_page(GFP_KERNEL))
	|| !(fds = (struct poll *)kmalloc(nfds*sizeof(struct poll), GFP_KERNEL)))
		return -ENOMEM;

	if(copy_from_user(fds, ufds, nfds*sizeof(struct poll))) {
		free_page((unsigned long)entry);
		kfree(fds);
		return -EFAULT;
	}

	if (timeout < 0)
		current->timeout = 0x7fffffff;
	else {
		current->timeout = jiffies + POLL_ROUND_UP(timeout, (1000/HZ));
		if (current->timeout <= jiffies)
			current->timeout = 0;
	}

	count = 0;
	wait_table.nr = 0;
	wait_table.entry = entry;
	wait = &wait_table;

	for(fdpnt = fds, j = 0; j < (int)nfds; j++, fdpnt++) {
		i = fdpnt->fd;
		fdpnt->revents = 0;
		if (!current->files->fd[i] || !current->files->fd[i]->f_inode)
			fdpnt->revents = POLLNVAL;
	}
repeat:
	current->state = TASK_INTERRUPTIBLE;
	for(fdpnt = fds, j = 0; j < (int)nfds; j++, fdpnt++) {
		i = fdpnt->fd;

		if(i < 0) continue;
		if (!current->files->fd[i] || !current->files->fd[i]->f_inode) continue;

		if ((fdpnt->events & LINUX_POLLIN)
		&& check(SEL_IN, wait, current->files->fd[i])) {
			retflag = 0;
			if (fdpnt->events & POLLIN)
				retflag = POLLIN;
			if (fdpnt->events & POLLRDNORM)
				retflag = POLLRDNORM;
			fdpnt->revents |= retflag; 
			count++;
			wait = NULL;
		}

		if ((fdpnt->events & LINUX_POLLOUT) &&
		check(SEL_OUT, wait, current->files->fd[i])) {
			fdpnt->revents |= (LINUX_POLLOUT & fdpnt->events);
			count++;
			wait = NULL;
		}

		if (check(SEL_EX, wait, current->files->fd[i])) {
			fdpnt->revents |= POLLHUP;
			count++;
			wait = NULL;
		}
	}

	if ((current->signal & (~current->blocked)))
		return -EINTR;

	wait = NULL;
	if (!count && current->timeout > jiffies) {
		schedule();
		goto repeat;
	}

	free_wait(&wait_table);
	free_page((unsigned long) entry);

	/* OK, now copy the revents fields back to user space. */
	fds1 = fds;
	fdcount = 0;
	for(i=0; i < (int)nfds; i++, ufds++, fds++) {
		if (fds->revents) {
			fdcount++;
		}
		__put_user(fds->revents, &ufds->revents);
	}
	kfree(fds1);
	current->timeout = 0;
	current->state = TASK_RUNNING;
	return fdcount;
}

extern asmlinkage int sys_semctl (int semid, int semnum, int cmd, union semun arg);
extern asmlinkage int sys_semget (key_t key, int nsems, int semflg);
extern asmlinkage int sys_semop  (int semid, struct sembuf *tsops, unsigned nsops);

asmlinkage int sunos_semsys(int op, unsigned long arg1, unsigned long arg2,
			    unsigned long arg3, void *ptr)
{
	union semun arg4;

	switch (op) {
	case 0:
		/* Most arguments match on a 1:1 basis but cmd doesn't */
		switch(arg3) {
		case 4:
			arg3=GETPID; break;
		case 5:
			arg3=GETVAL; break;
		case 6:
			arg3=GETALL; break;
		case 3:
			arg3=GETNCNT; break;
		case 7:
			arg3=GETZCNT; break;
		case 8:
			arg3=SETVAL; break;
		case 9:
			arg3=SETALL; break;
		}
		/* sys_semctl(): */
		arg4.__pad=ptr; /* value to modify semaphore to */
		return sys_semctl((int)arg1, (int)arg2, (int)arg3, arg4 );
	case 1:
		/* sys_semget(): */
		return sys_semget((key_t)arg1, (int)arg2, (int)arg3);
	case 2:
		/* sys_semop(): */
		return sys_semop((int)arg1, (struct sembuf *)arg2, (unsigned)arg3);
	default:
		return -EINVAL;
	}
}
    
extern asmlinkage int sys_shmat (int shmid, char *shmaddr, int shmflg, ulong *raddr);
extern asmlinkage int sys_shmctl (int shmid, int cmd, struct shmid_ds *buf);
extern asmlinkage int sys_shmdt (char *shmaddr);
extern asmlinkage int sys_shmget (key_t key, int size, int shmflg);

asmlinkage int sunos_shmsys(int op, unsigned long arg1, unsigned long arg2,
			    unsigned long arg3)
{
	unsigned long raddr;
	int rval;

	switch(op) {
	case 0:
		/* sys_shmat(): attach a shared memory area */
		rval = sys_shmat((int)arg1,(char *)arg2,(int)arg3,&raddr);
		if(rval != 0)
			return rval;
		return (int) raddr;
	case 1:
		/* sys_shmctl(): modify shared memory area attr. */
		rval = sys_shmctl((int)arg1,(int)arg2,(struct shmid_ds *)arg3);
		return (rval);
	case 2:
		/* sys_shmdt(): detach a shared memory area */
		return sys_shmdt((char *)arg1);
	case 3:
		/* sys_shmget(): get a shared memory area */
		return sys_shmget((key_t)arg1,(int)arg2,(int)arg3);
	default:
		return -EINVAL;
	}
}

asmlinkage int sunos_open(const char *filename, int flags, int mode)
{
	current->personality |= PER_BSD;
	return sys_open (filename, flags, mode);
}


#define SUNOS_EWOULDBLOCK 35

/* see the sunos man page read(2v) for an explanation
   of this garbage. We use O_NDELAY to mark
   file descriptors that have been set non-blocking 
   using 4.2BSD style calls. (tridge) */

static inline int check_nonblock(int ret,int fd)
{
	if (ret == -EAGAIN && (current->files->fd[fd]->f_flags & O_NDELAY))
		return -SUNOS_EWOULDBLOCK;
	return ret;
}

extern asmlinkage int sys_read(unsigned int fd,char *buf,int count);
extern asmlinkage int sys_write(unsigned int fd,char *buf,int count);
extern asmlinkage int sys_recv(int fd, void * ubuf, int size, unsigned flags);
extern asmlinkage int sys_send(int fd, void * buff, int len, unsigned flags);
extern asmlinkage int sys_accept(int fd, struct sockaddr *sa, int *addrlen);
extern asmlinkage int sys_readv(unsigned long fd, const struct iovec * vector, long count);
extern asmlinkage int sys_writev(unsigned long fd, const struct iovec * vector, long count);


asmlinkage int sunos_read(unsigned int fd,char *buf,int count)
{
	return check_nonblock(sys_read(fd,buf,count),fd);
}

asmlinkage int sunos_readv(unsigned long fd, const struct iovec * vector, long count)
{
	return check_nonblock(sys_readv(fd,vector,count),fd);
}

asmlinkage int sunos_write(unsigned int fd,char *buf,int count)
{
	return check_nonblock(sys_write(fd,buf,count),fd);
}

asmlinkage int sunos_writev(unsigned long fd, const struct iovec * vector, long count)
{
	return check_nonblock(sys_writev(fd,vector,count),fd);
}

asmlinkage int sunos_recv(int fd, void * ubuf, int size, unsigned flags)
{
	return check_nonblock(sys_recv(fd,ubuf,size,flags),fd);
}

asmlinkage int sunos_send(int fd, void * buff, int len, unsigned flags)
{
	return check_nonblock(sys_send(fd,buff,len,flags),fd);
}

asmlinkage int sunos_accept(int fd, struct sockaddr *sa, int *addrlen)
{
	return check_nonblock(sys_accept(fd,sa,addrlen),fd);	
}

#define SUNOS_SV_INTERRUPT 2

extern void check_pending(int signum);

asmlinkage int sunos_sigaction(int signum, const struct sigaction *action,
	struct sigaction *oldaction)
{
	struct sigaction new_sa, *p;
	const  int sigaction_size = sizeof (struct sigaction) - sizeof (void *);
	int err;

	current->personality |= PER_BSD;

	if (signum<1 || signum>32)
		return -EINVAL;

	p = signum - 1 + current->sig->action;
	if (action) {
		if(copy_from_user(&new_sa, action, sigaction_size))
			return -EFAULT;
		if (signum==SIGKILL || signum==SIGSTOP)
			return -EINVAL;
		memset(&new_sa, 0, sizeof(struct sigaction));
		if(copy_from_user(&new_sa, action, sigaction_size))
			return -EFAULT;	
		if (new_sa.sa_handler != SIG_DFL && new_sa.sa_handler != SIG_IGN) {
			err = verify_area(VERIFY_READ, new_sa.sa_handler, 1);
			if (err)
				return err;
		}
		new_sa.sa_flags ^= SUNOS_SV_INTERRUPT;
	}

	if (oldaction) {
		if (copy_to_user(oldaction, p, sigaction_size))
			return -EFAULT;	
		if (oldaction->sa_flags & SA_RESTART)
			oldaction->sa_flags &= ~SA_RESTART;
		else
			oldaction->sa_flags |= SUNOS_SV_INTERRUPT;
	}

	if (action) {
		*p = new_sa;
		check_pending(signum);
	}

	return 0;
}


extern asmlinkage int sys_setsockopt(int fd, int level, int optname, char *optval, int optlen);
extern asmlinkage int sys_getsockopt(int fd, int level, int optname, char *optval, int *optlen);

asmlinkage int sunos_setsockopt(int fd, int level, int optname, char *optval,
				int optlen)
{
	int tr_opt = optname;

	if (level == SOL_IP)
	{
		/*
		 *	Multicast socketopts (ttl, membership)
		 */
		if (tr_opt >=2 && tr_opt <= 6)
		{
			tr_opt += 30;
		}
	}
	return sys_setsockopt(fd, level, tr_opt, optval, optlen);
}

asmlinkage int sunos_getsockopt(int fd, int level, int optname, char *optval,
				int *optlen)
{
	int tr_opt = optname;

	if (level == SOL_IP)
	{
		/*
		 *	Multicast socketopts (ttl, membership)
		 */
		if (tr_opt >=2 && tr_opt <= 6)
		{
			tr_opt += 30;
		}
	}
	return sys_getsockopt(fd, level, tr_opt, optval, optlen);
}
