#include "soft-fp.h"
#include "quad.h"

int FABSQ(unsigned long *rd, unsigned long *rs2)
{
/*
	FP_DECL_Q(A); FP_DECL_Q(R);

	__FP_UNPACK_Q(A, rs2);
	_FP_FRAC_COPY_2(R, A);
	R_c = A_c;
	R_e = A_e;
	R_s = 0;
	__FP_PACK_Q(rd, R);
 */
	rd[0] = rs2[0] & 0x7fffffffffffffffUL;
	rd[1] = rs2[1];
	return 1;
}
