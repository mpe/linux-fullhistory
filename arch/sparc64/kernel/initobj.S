#include <asm/errno.h>

	.section	".text.init",#alloc,#execinstr
	.globl	text_init_begin
text_init_begin:
	.section	".data.init",#alloc,#write
	.globl	data_init_begin
data_init_begin:

	.section	".fixup",#alloc,#execinstr
	.globl	__ret_efault
__ret_efault:
	ret
	 restore %g0, -EFAULT, %o0

