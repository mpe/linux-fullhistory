#include "soft-fp.h"
#include "quad.h"

int FNEGQ(unsigned long *rd, unsigned long *rs2)
{
/*
	FP_DECL_Q(A); FP_DECL_Q(R);

	__FP_UNPACK_Q(A, rs2);
	FP_NEG_Q(R, A);
	__FP_PACK_Q(rd, R);
 */
	rd[0] = rs2[0] ^ 0x8000000000000000UL;
	rd[1] = rs2[1];
	return 1;
}

                
