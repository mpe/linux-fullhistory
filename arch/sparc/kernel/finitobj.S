#if defined (__svr4__) || defined (__ELF__)

	.section	".text.init",#alloc,#execinstr
	.globl	text_init_end
text_init_end:
	.section	".data.init",#alloc,#write
	.globl	data_init_end
data_init_end:
#endif
