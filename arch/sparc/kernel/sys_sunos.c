/* $Id: sys_sunos.c,v 1.20 1995/11/25 00:58:37 davem Exp $
 * sys_sunos.c: SunOS specific syscall compatability support.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1995 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *
 * Based upon preliminary work which is:
 *
 * Copyright (C) 1995 Adrian M. Rodriguez (adrian@remus.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/mman.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/resource.h>
#include <linux/signal.h>
#include <linux/uio.h>
#include <linux/utsname.h>
#include <linux/fs.h>
#include <linux/major.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/pconf.h>

/* For the nfs mount emulation */
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/nfs.h>
#include <linux/nfs_mount.h>

/* for sunos_select */
#include <linux/time.h>
#include <linux/personality.h>

static unsigned long get_sparc_unmapped_area(unsigned long len)
{
	unsigned long addr;
	struct vm_area_struct * vmm;

	if (len > TASK_SIZE)
		return 0;
	addr = 0xE8000000UL;    /* To make it work on a sun4c. */
	for (vmm = current->mm->mmap; ; vmm = vmm->vm_next) {
		if (TASK_SIZE - len < addr)
			return 0;
		if (!vmm)
			return addr;
		if (addr > vmm->vm_end)
			continue;
		if (addr + len > vmm->vm_start) {
			addr = vmm->vm_end;
			continue;
		}
		return addr;
	}
}

/* We use the SunOS mmap() semantics. */
asmlinkage unsigned long sunos_mmap(unsigned long addr, unsigned long len,
				    unsigned long prot, unsigned long flags,
				    unsigned long fd, unsigned long off)
{
	struct file * file = NULL;
	unsigned long retval, ret_type;

	if(flags & MAP_NORESERVE) {
		printk("%s: unimplemented SunOS MAP_NORESERVE mmap() flag\n",
		       current->comm);
		flags &= ~MAP_NORESERVE;
	}
	if(!(flags & MAP_ANONYMOUS))
		if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
			return -EBADF;
	if(!(flags & MAP_FIXED) && !addr) {
		addr = get_sparc_unmapped_area(len);
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
	ret_type = flags & _MAP_NEW;
	flags &= ~_MAP_NEW;
	retval = do_mmap(file, addr, len, prot, flags, off);
	if(ret_type)
		return retval;
	else
		return ((retval < KERNBASE) ? 0 : retval);
}

/* Weird SunOS mm control function.. */
asmlinkage int sunos_mctl(unsigned long addr, unsigned long len, int function, char *arg)
{
	printk("%s: Call to sunos_mctl(addr<%08lx>, len<%08lx>, function<%d>, arg<%p>) "
	       "is unsupported\n", current->comm, addr, len, function, arg);
	return -EAGAIN;
}

/* XXX This won't be necessary when I sync up to never kernel in 1.3.x series
 * XXX which has the sys_msync() system call implemented properly.
 */
asmlinkage int sunos_msync(unsigned long addr, unsigned long len, unsigned long flags)
{
	printk("%s: Call to sunos_msync(addr<%08lx>, len<%08lx>, flags<%08lx>) "
	       "is unsupported\n", current->comm, addr, len, flags);
	return -EINVAL;
}

/* SunOS is completely broken... it returns 0 on success, otherwise
 * ENOMEM.  For sys_sbrk() it wants the new brk value as a return
 * on success and ENOMEM as before on failure.
 */
asmlinkage int sunos_brk(unsigned long brk)
{
	int freepages;
	unsigned long rlim;
	unsigned long newbrk, oldbrk;

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
	if (find_vma_intersection(current, oldbrk, newbrk+PAGE_SIZE))
		return -ENOMEM;

	/*
	 * stupid algorithm to decide if we have enough memory: while
	 * simple, it hopefully works in most obvious cases.. Easy to
	 * fool it, but this should catch most mistakes.
	 */
	freepages = buffermem >> 12;
	freepages += nr_free_pages;
	freepages += nr_swap_pages;
	freepages -= MAP_NR(high_memory) >> 4;
	freepages -= (newbrk-oldbrk) >> 12;
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

	/* This should do it hopefully... */
	error = sunos_brk(((int) current->mm->brk) + increment);
	if(error == 0)
		return current->mm->brk;
	else
		return error;
}

/* XXX Completely undocumented, and completely magic...
 * XXX I belive it is to increase the size of the stack by
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
 * *or* the passed base address is not aligned on a page boundry you
 * get an error.
 */
asmlinkage int sunos_mincore(unsigned long addr, unsigned long len, char *array)
{
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	unsigned long limit;
	int num_pages, pnum;

	if(addr & (PAGE_SIZE - 1))
		return -EINVAL;

	num_pages = (len / PAGE_SIZE);
	if(verify_area(VERIFY_WRITE, array, num_pages))
		return -EFAULT; /* bum array, you lose... */
	if((addr >= KERNBASE) || ((addr + len) > KERNBASE))
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
		array[pnum] = pte_present(*ptep) ? 1 : 0;
	}
	return 0; /* Success... I think... */
}

/* This just wants the soft limit (ie. rlim_cur element) of the RLIMIT_NOFILE
 * resource limit and is for backwards compatability with older sunos
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
	unsigned long old = current->blocked;

	current->blocked |= (blk_mask & _BLOCKABLE);
	return old;
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
	put_user((strlen(name)), &dirent->d_namlen);
	put_user(reclen, &dirent->d_reclen);
	memcpy_tofs(dirent->d_name, name, namlen);
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
	if(verify_area(VERIFY_WRITE, dirent, cnt))
		return -EFAULT;
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

asmlinkage int sunos_getdomainname(char *name, int len)
{
	int error;

	if(len > __NEW_UTS_LEN)
		return -EFAULT;
	error = verify_area(VERIFY_WRITE, name, len);
	if(error)
		return -EFAULT;
	memcpy_tofs(name, system_utsname.domainname, len);
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
	int error;
	if(!name)
		return -EFAULT;
	error = verify_area(VERIFY_WRITE, name, sizeof *name);
	if(error)
		return error;
	memcpy_tofs(&name->sname[0], &system_utsname.sysname[0],
		    sizeof(name->sname));
	memcpy_tofs(&name->nname[0], &system_utsname.nodename[0],
		    sizeof(name->nname));
	name->nname[8] = '\0';
	memcpy_tofs(&name->nnext[0], &system_utsname.nodename[9],
		    sizeof(name->nnext));
	memcpy_tofs(&name->rel[0], &system_utsname.release[0],
		    sizeof(name->rel));
	memcpy_tofs(&name->ver[0], &system_utsname.version[0],
		    sizeof(name->ver));
	memcpy_tofs(&name->mach[0], &system_utsname.machine[0],
		    sizeof(name->mach));
	return 0;
}

asmlinkage int sunos_nosys(void)
{
	struct pt_regs *regs;

	regs = (struct pt_regs *) (((current->tss.ksp) & PAGE_MASK) +
				   (PAGE_SIZE - TRACEREG_SZ));
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
		return 30; /* XXX Investigate XXX */
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

struct ufs_mntargs {
	char *dev_name;
};

struct lo_mntargs {
	char *dev_name;
};

struct ext2_mntargs {
	char *dev_name;
};

struct iso9660_mntargs {
	char *dev_name;
};

struct minix_mntargs {
	char *dev_name;
};

struct ext_mntargs {
	char *dev_name;
};

struct msdos_mntargs {
	char *dev_name;
};

struct xiafs_mntargs {
	char *dev_name;
};

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

extern asmlinkage int sys_mount(char *, char *, char *, unsigned long, void *);

extern int do_mount(dev_t, const char *, char *, int, void *);
extern dev_t get_unnamed_dev(void);
extern void put_unnamed_dev(dev_t);

extern sys_mount (char * dev_name, char * dir_name, char * type,
		  unsigned long new_flags, void * data);

extern asmlinkage int
sys_connect(int fd, struct sockaddr *uservaddr, int addrlen);

extern asmlinkage int
sys_socket(int family, int type, int protocol);

asmlinkage int
sys_bind(int fd, struct sockaddr *umyaddr, int addrlen);


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
	int  ret = -ENODEV, error;
	int  server_fd;
	char *the_name;
	struct nfs_mount_data linux_nfs_mount;
	struct sunos_nfs_mount_args *sunos_mount = data;
	dev_t dev;
	struct pt_regs *regs;

	error = verify_area(VERIFY_READ, data, sizeof (struct sunos_nfs_mount_args));
	if (error)
		return error;
	/* Ok, here comes the fun part: Linux's nfs mount needs a
	 * socket connection to the server, but SunOS mount does not
	 * requiere this, so we use the information on the destination
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
	
	ret = do_mount (dev, dir_name, "nfs", linux_flags, &linux_nfs_mount);
	if (ret)
	    put_unnamed_dev(dev);

#if 0
	/* Kill the process: the nfs directory is already mounted */
	regs = (struct pt_regs *) (((current->tss.ksp) & PAGE_MASK) +
				   (PAGE_SIZE - TRACEREG_SZ));
	current->tss.sig_address = regs->pc;
	current->tss.sig_desc = regs->u_regs[UREG_G1];
	send_sig(SIGSYS, current, 1);
#endif
	return ret;
}

asmlinkage int
sunos_mount(char *type, char *dir, int flags, void *data)
{
	int linux_flags = MS_MGC_MSK; /* new semantics */
	int error;
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
	error = verify_area(VERIFY_READ, type, 16);
	if(error)
		return error;
	if(strcmp(type, "ext2") == 0) {
		dev_fname = (char *) data;;
	} else if(strcmp(type, "iso9660") == 0) {
		dev_fname = (char *) data;
	} else if(strcmp(type, "minix") == 0) {
		dev_fname = (char *) data;
	} else if(strcmp(type, "ext") == 0) {
		dev_fname = (char *) data;
	} else if(strcmp(type, "xiafs") == 0) {
		dev_fname = (char *) data;
	} else if(strcmp(type, "nfs") == 0) {
		error = sunos_nfs_mount (dir, flags, data);
		return error;
        } else if(strcmp(type, "ufs") == 0) {
		printk("Warning: UFS filesystem mounts unsupported.\n");
		return -ENODEV;
	} else if(strcmp(type, "proc")) {
		return -ENODEV;
	}
	if(error)
		return error;
	error = sys_mount(dev_fname, dir, type, linux_flags, NULL);
	printk("sys_mount(type<%s>, device<%s>) returns %d\n",
	       type, dev_fname, error);
	return error;
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

extern asmlinkage sunos_audit ()
{
	printk ("sys_audit\n");
	return -1;
}
