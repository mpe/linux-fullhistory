#include "soft-fp.h"
#include "quad.h"

int FADDQ(void *rd, void *rs2, void *rs1)
{
	FP_DECL_Q(A); FP_DECL_Q(B); FP_DECL_Q(R);

	__FP_UNPACK_Q(A, rs1);
	__FP_UNPACK_Q(B, rs2);
	FP_ADD_Q(R, A, B);
	__FP_PACK_Q(rd, R);
	return 1;
}
