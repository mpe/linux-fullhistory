#include "soft-fp.h"
#include "quad.h"

int FCMPQ(void *rd, void *rs2, void *rs1)
{
	FP_DECL_Q(A); FP_DECL_Q(B);
	long ret;
	unsigned long fsr;
	
	__FP_UNPACK_Q(A, rs1);
	__FP_UNPACK_Q(B, rs2);
	FP_CMP_Q(ret, B, A, 3);
	if (ret == -1) ret = 2;
	fsr = *(unsigned long *)rd;
	fsr &= ~0xc00; fsr |= (ret << 10);
	*(unsigned long *)rd = fsr;
	return 1;
}
