/*
 *	fs/proc/kcore.c kernel ELF/AOUT core dumper
 *
 *	Modelled on fs/exec.c:aout_core_dump()
 *	Jeremy Fitzhardinge <jeremy@sw.oz.au>
 *	Implemented by David Howells <David.Howells@nexor.co.uk>
 *	Modified and incorporated into 2.3.x by Tigran Aivazian <tigran@sco.com>
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/elf.h>
#include <linux/elfcore.h>
#include <asm/uaccess.h>

#ifdef CONFIG_KCORE_AOUT
ssize_t read_kcore(struct file * file, char * buf,
			 size_t count, loff_t *ppos)
{
	unsigned long p = *ppos, memsize;
	ssize_t read;
	ssize_t count1;
	char * pnt;
	struct user dump;
#if defined (__i386__) || defined (__mc68000__)
#	define FIRST_MAPPED	PAGE_SIZE	/* we don't have page 0 mapped on x86.. */
#else
#	define FIRST_MAPPED	0
#endif

	memset(&dump, 0, sizeof(struct user));
	dump.magic = CMAGIC;
	dump.u_dsize = max_mapnr;
#if defined (__i386__)
	dump.start_code = PAGE_OFFSET;
#endif
#ifdef __alpha__
	dump.start_data = PAGE_OFFSET;
#endif

	memsize = (max_mapnr + 1) << PAGE_SHIFT;
	if (p >= memsize)
		return 0;
	if (count > memsize - p)
		count = memsize - p;
	read = 0;

	if (p < sizeof(struct user) && count > 0) {
		count1 = count;
		if (p + count1 > sizeof(struct user))
			count1 = sizeof(struct user)-p;
		pnt = (char *) &dump + p;
		copy_to_user(buf,(void *) pnt, count1);
		buf += count1;
		p += count1;
		count -= count1;
		read += count1;
	}

	if (count > 0 && p < PAGE_SIZE + FIRST_MAPPED) {
		count1 = PAGE_SIZE + FIRST_MAPPED - p;
		if (count1 > count)
			count1 = count;
		clear_user(buf, count1);
		buf += count1;
		p += count1;
		count -= count1;
		read += count1;
	}
	if (count > 0) {
		copy_to_user(buf, (void *) (PAGE_OFFSET+p-PAGE_SIZE), count);
		read += count;
	}
	*ppos += read;
	return read;
}
#else


#define roundup(x, y)  ((((x)+((y)-1))/(y))*(y))

/* An ELF note in memory */
struct memelfnote
{
	const char *name;
	int type;
	unsigned int datasz;
	void *data;
};

extern char saved_command_line[];

/*****************************************************************************/
/*
 * determine size of ELF note
 */
static int notesize(struct memelfnote *en)
{
	int sz;

	sz = sizeof(struct elf_note);
	sz += roundup(strlen(en->name), 4);
	sz += roundup(en->datasz, 4);

	return sz;
} /* end notesize() */

/*****************************************************************************/
/*
 * store a note in the header buffer
 */
static char *storenote(struct memelfnote *men, char *bufp)
{
	struct elf_note en;

#define DUMP_WRITE(addr,nr) do { memcpy(bufp,addr,nr); bufp += nr; } while(0)

	en.n_namesz = strlen(men->name);
	en.n_descsz = men->datasz;
	en.n_type = men->type;

	DUMP_WRITE(&en, sizeof(en));
	DUMP_WRITE(men->name, en.n_namesz);

	/* XXX - cast from long long to long to avoid need for libgcc.a */
	bufp = (char*) roundup((unsigned long)bufp,4);
	DUMP_WRITE(men->data, men->datasz);
	bufp = (char*) roundup((unsigned long)bufp,4);

#undef DUMP_WRITE

	return bufp;
} /* end storenote() */

/*****************************************************************************/
/*
 * store an ELF coredump header in the supplied buffer
 * - assume the memory image is the size specified
 */
static void elf_kcore_store_hdr(char *bufp, size_t size, off_t dataoff)
{
	struct elf_prstatus prstatus;	/* NT_PRSTATUS */
	struct elf_prpsinfo psinfo;	/* NT_PRPSINFO */
	struct elf_phdr *nhdr, *dhdr;
	struct elfhdr *elf;
	struct memelfnote notes[3];
	off_t offset = 0;

	/* acquire an ELF header block from the buffer */
	elf = (struct elfhdr *) bufp;
	bufp += sizeof(*elf);
	offset += sizeof(*elf);

	/* set up header */
	memcpy(elf->e_ident,ELFMAG,SELFMAG);
	elf->e_ident[EI_CLASS]	= ELF_CLASS;
	elf->e_ident[EI_DATA]	= ELF_DATA;
	elf->e_ident[EI_VERSION]= EV_CURRENT;
	memset(elf->e_ident+EI_PAD,0,EI_NIDENT-EI_PAD);

	elf->e_type	= ET_CORE;
	elf->e_machine	= ELF_ARCH;
	elf->e_version	= EV_CURRENT;
	elf->e_entry	= 0;
	elf->e_phoff	= sizeof(*elf);
	elf->e_shoff	= 0;
	elf->e_flags	= 0;
	elf->e_ehsize	= sizeof(*elf);
	elf->e_phentsize= sizeof(struct elf_phdr);
	elf->e_phnum	= 2;			/* no. of segments */
	elf->e_shentsize= 0;
	elf->e_shnum	= 0;
	elf->e_shstrndx	= 0;

	/* acquire an ELF program header blocks from the buffer for notes */
	nhdr = (struct elf_phdr *) bufp;
	bufp += sizeof(*nhdr);
	offset += sizeof(*nhdr);

	/* store program headers for notes dump */
	nhdr->p_type	= PT_NOTE;
	nhdr->p_offset	= 0;
	nhdr->p_vaddr	= 0;
	nhdr->p_paddr	= 0;
	nhdr->p_memsz	= 0;
	nhdr->p_flags	= 0;
	nhdr->p_align	= 0;

	/* acquire an ELF program header blocks from the buffer for data */
	dhdr = (struct elf_phdr *) bufp;
	bufp += sizeof(*dhdr);
	offset += sizeof(*dhdr);

	/* store program headers for data dump */
	dhdr->p_type	= PT_LOAD;
	dhdr->p_flags	= PF_R|PF_W|PF_X;
	dhdr->p_offset	= dataoff;
	dhdr->p_vaddr	= PAGE_OFFSET;
	dhdr->p_paddr	= __pa(PAGE_OFFSET);
	dhdr->p_filesz	= size;
	dhdr->p_memsz	= size;
	dhdr->p_align	= PAGE_SIZE;

	/*
	 * Set up the notes in similar form to SVR4 core dumps made
	 * with info from their /proc.
	 */
	nhdr->p_offset	= offset;
	nhdr->p_filesz	= 0;

	/* set up the process status */
	notes[0].name = "CORE";
	notes[0].type = NT_PRSTATUS;
	notes[0].datasz = sizeof(prstatus);
	notes[0].data = &prstatus;

	memset(&prstatus,0,sizeof(prstatus));

	nhdr->p_filesz	= notesize(&notes[0]);
	bufp = storenote(&notes[0],bufp);

	/* set up the process info */
	notes[1].name	= "CORE";
	notes[1].type	= NT_PRPSINFO;
	notes[1].datasz	= sizeof(psinfo);
	notes[1].data	= &psinfo;

	memset(&psinfo,0,sizeof(psinfo));
	psinfo.pr_state	= 0;
	psinfo.pr_sname	= 'R';
	psinfo.pr_zomb	= 0;

	strcpy(psinfo.pr_fname,"vmlinux");
	strncpy(psinfo.pr_psargs,saved_command_line,ELF_PRARGSZ);

	nhdr->p_filesz	= notesize(&notes[1]);
	bufp = storenote(&notes[1],bufp);

	/* set up the task structure */
	notes[2].name	= "CORE";
	notes[2].type	= NT_TASKSTRUCT;
	notes[2].datasz	= sizeof(*current);
	notes[2].data	= current;

	nhdr->p_filesz	= notesize(&notes[2]);
	bufp = storenote(&notes[2],bufp);

} /* end elf_kcore_store_hdr() */

/*****************************************************************************/
/*
 * read from the ELF header and then kernel memory
 */
ssize_t read_kcore(struct file *file, char *buffer, size_t buflen,
			      loff_t *fpos)
{
	ssize_t acc;
	size_t size, tsz;
	char *page;

	/* work out how much file we allow to be read */
	size = ((size_t)high_memory - PAGE_OFFSET) + PAGE_SIZE;
	acc = 0;

	/* see if file pointer already beyond EOF */
	if (buflen==0 || *fpos>=size)
		return 0;

	/* trim buflen to not go beyond EOF */
	if (buflen > size-*fpos)
		buflen = size - *fpos;

	/* construct an ELF core header if we'll need some of it */
	if (*fpos<PAGE_SIZE) {
		/* get a buffer */
		page = (char*) __get_free_page(GFP_KERNEL);
		if (!page)
			return -ENOMEM;

		tsz = PAGE_SIZE-*fpos;
		if (buflen < tsz)
			tsz = buflen;

		/* create a header */
		memset(page,0,PAGE_SIZE);
		elf_kcore_store_hdr(page,size-PAGE_SIZE,PAGE_SIZE);

		/* copy data to the users buffer */
		copy_to_user(buffer,page,tsz);
		buflen -= tsz;
		*fpos += tsz;
		buffer += tsz;
		acc += tsz;

		free_page((unsigned long) page);

		/* leave now if filled buffer already */
		if (buflen==0)
			return tsz;
	}

	/* where page 0 not mapped, write zeros into buffer */
#if defined (__i386__) || defined (__mc68000__)
	if (*fpos < PAGE_SIZE*2) {
		/* work out how much to clear */
		tsz = PAGE_SIZE*2 - *fpos;
		if (buflen < tsz)
			tsz = buflen;

		/* write zeros to buffer */
		clear_user(buffer,tsz);
		buflen -= tsz;
		*fpos += tsz;
		buffer += tsz;
		acc += tsz;

		/* leave now if filled buffer already */
		if (buflen==0)
			return tsz;
	}
#endif

	/* fill the remainder of the buffer from kernel VM space */
#if defined (__i386__) || defined (__mc68000__)
	copy_to_user(buffer,__va(*fpos-PAGE_SIZE),buflen);
#else
	copy_to_user(buffer,__va(*fpos),buflen);
#endif
	acc += buflen;
	*fpos += buflen;

	return acc;

} /* end read_kcore() */
#endif
