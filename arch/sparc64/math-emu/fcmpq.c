#include "soft-fp.h"
#include "quad.h"

int FCMPQ(void *rd, void *rs2, void *rs1)
{
	FP_DECL_Q(A); FP_DECL_Q(B);
	long ret;
	int fccno = ((long)rd) & 3;
	unsigned long fsr;
	
	rd = (void *)(((long)rd)&~3);
	__FP_UNPACK_Q(A, rs1);
	__FP_UNPACK_Q(B, rs2);
	FP_CMP_Q(ret, B, A, 3);
	if (ret == -1) ret = 2;
	fsr = *(unsigned long *)rd;
	switch (fccno) {
	case 0: fsr &= ~0xc00; fsr |= (ret << 10); break;
	case 1: fsr &= ~0x300000000UL; fsr |= (ret << 32); break;
	case 2: fsr &= ~0xc00000000UL; fsr |= (ret << 34); break;
	case 3: fsr &= ~0x3000000000UL; fsr |= (ret << 36); break;
	}
	*(unsigned long *)rd = fsr;
	return 1;
}
