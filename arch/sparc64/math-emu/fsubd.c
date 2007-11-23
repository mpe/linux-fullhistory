#include "soft-fp.h"
#include "double.h"

int FSUBD(void *rd, void *rs2, void *rs1)
{
	FP_DECL_D(A); FP_DECL_D(B); FP_DECL_D(R);

	__FP_UNPACK_D(A, rs1);
	__FP_UNPACK_D(B, rs2);
	if (B_c != FP_CLS_NAN)
		B_s ^= 1;
	FP_ADD_D(R, A, B);
	__FP_PACK_D(rd, R);
	return 1;
}
