#include "soft-fp.h"
#include "double.h"

int FDIVD(void *rd, void *rs2, void *rs1)
{
	FP_DECL_D(A); FP_DECL_D(B); FP_DECL_D(R);
	int ret = 0;

	__FP_UNPACK_D(A, rs1);
	__FP_UNPACK_D(B, rs2);
	if(B_c == FP_CLS_ZERO &&
	   A_c != FP_CLS_ZERO) {
		ret |= EFLAG_DIVZERO;
		if(__FPU_TRAP_P(EFLAG_DIVZERO))
			return ret;
	}
	FP_DIV_D(R, A, B);
	return (ret | __FP_PACK_D(rd, R));
}
