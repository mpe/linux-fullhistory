#include <asm/addrspace.h>
#include <asm/asm.h>
#include <asm/regdef.h>

/*
 * Compute kernel code checksum to check kernel code against corruption
 * (Ancient debugging trash ...)
 */
		LEAF(csum)
		LONG_L	t0,cacheflush
		move	t8,ra
		jalr	t0
		li	t0,KSEG1
		la	t1,final
		li	t2,KSEG1
		or	t0,t2
		or	t1,t2
		move	v0,zero
1:		lw	t2,(t0)
		addiu	t0,4
		bne	t0,t1,1b
		xor	v0,t2
		jr	t8
		nop
		END(csum)
