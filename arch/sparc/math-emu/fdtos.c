#include "soft-fp.h"
#include "double.h"
#include "single.h"

int FDTOS(void *rd, void *rs2)
{
	FP_DECL_D(A); FP_DECL_S(R);

	__FP_UNPACK_D(A, rs2);
	FP_CONV(S,D,1,2,R,A);
	__FP_PACK_S(rd, R);
	return 1;
}
