/*
 * The size of struct machine_desc
 *   (for assembler code)
 */
#define SIZEOF_MACHINE_DESC	40

#ifndef __ASSEMBLY__

struct machine_desc {
	/*
	 * Note! The first four elements are used
	 * by assembler code in head-armv.S
	 */
	unsigned int	nr;		/* architecture number	*/
	unsigned int	phys_ram;	/* start of physical ram */
	unsigned int	phys_io;	/* start of physical io	*/
	unsigned int	virt_io;	/* start of virtual io	*/

	const char	*name;		/* architecture name	*/
	unsigned int	param_offset;	/* parameter page	*/

	unsigned int	video_start;	/* start of video RAM	*/
	unsigned int	video_end;	/* end of video RAM	*/

	unsigned int	reserve_lp0 :1;	/* never has lp0	*/
	unsigned int	reserve_lp1 :1;	/* never has lp1	*/
	unsigned int	reserve_lp2 :1;	/* never has lp2	*/
	unsigned int	broken_hlt  :1;	/* hlt is broken	*/
	unsigned int	soft_reboot :1;	/* soft reboot		*/
	void		(*fixup)(struct machine_desc *,
				 struct param_struct *, char **,
				 struct meminfo *);
};

/*
 * Set of macros to define architecture features.  This is built into
 * a table by the linker.
 */
#define MACHINE_START(_type,_name)		\
const struct machine_desc __mach_desc_##_type	\
 __attribute__((__section__(".arch.info"))) = {	\
	nr:		MACH_TYPE_##_type##,	\
	name:		_name,

#define MAINTAINER(n)

#define BOOT_MEM(_pram,_pio,_vio)		\
	phys_ram:	_pram,			\
	phys_io:	_pio,			\
	virt_io:	_vio,

#define BOOT_PARAMS(_params)			\
	param_offset:	_params,

#define VIDEO(_start,_end)			\
	video_start:	_start,			\
	video_end:	_end,

#define DISABLE_PARPORT(_n)			\
	reserve_lp##_n##:	1,

#define BROKEN_HLT				\
	broken_hlt:	1,

#define SOFT_REBOOT				\
	soft_reboot:	1,

#define FIXUP(_func)				\
	fixup:		_func,

#define MACHINE_END				\
};

#endif
