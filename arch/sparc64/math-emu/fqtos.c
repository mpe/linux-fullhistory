#include "soft-fp.h"
#include "quad.h"
#include "single.h"

int FQTOS(void *rd, void *rs2)
{
	FP_DECL_Q(A); FP_DECL_S(R);

	__FP_UNPACK_Q(A, rs2);
	FP_CONV(S,Q,1,2,R,A);
	__FP_PACK_S(rd, R);
	return 1;
}
