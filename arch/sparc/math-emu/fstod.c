#include "soft-fp.h"
#include "double.h"
#include "single.h"

int FSTOD(void *rd, void *rs2)
{
	FP_DECL_S(A); FP_DECL_D(R);

	__FP_UNPACK_S(A, rs2);
	FP_CONV(D,S,2,1,R,A);
	__FP_PACK_D(rd, R);
	return 1;
}
