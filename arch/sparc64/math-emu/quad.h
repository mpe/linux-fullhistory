/*
 * Definitions for IEEE Quad Precision
 */

#if _FP_W_TYPE_SIZE < 64
#error "Only stud muffins allowed, schmuck."
#endif

#define _FP_FRACTBITS_Q		(2*_FP_W_TYPE_SIZE)

#define _FP_FRACBITS_Q		113
#define _FP_FRACXBITS_Q		(_FP_FRACTBITS_Q - _FP_FRACBITS_Q)
#define _FP_WFRACBITS_Q		(_FP_WORKBITS + _FP_FRACBITS_Q)
#define _FP_WFRACXBITS_Q	(_FP_FRACTBITS_Q - _FP_WFRACBITS_Q)
#define _FP_EXPBITS_Q		15
#define _FP_EXPBIAS_Q		16383
#define _FP_EXPMAX_Q		32767

#define _FP_QNANBIT_Q		\
	((_FP_W_TYPE)1 << (_FP_FRACBITS_Q-2) % _FP_W_TYPE_SIZE)
#define _FP_IMPLBIT_Q		\
	((_FP_W_TYPE)1 << (_FP_FRACBITS_Q-1) % _FP_W_TYPE_SIZE)
#define _FP_OVERFLOW_Q		\
	((_FP_W_TYPE)1 << (_FP_WFRACBITS_Q % _FP_W_TYPE_SIZE))

union _FP_UNION_Q
{
  long double flt /* __attribute__((mode(TF))) */ ;
  struct {
#if __BYTE_ORDER == __BIG_ENDIAN
    unsigned sign  : 1;
    unsigned exp   : _FP_EXPBITS_Q;
    unsigned long frac1 : _FP_FRACBITS_Q-(_FP_IMPLBIT_Q != 0)-_FP_W_TYPE_SIZE;
    unsigned long frac0 : _FP_W_TYPE_SIZE;
#else
    unsigned long frac0 : _FP_W_TYPE_SIZE;
    unsigned long frac1 : _FP_FRACBITS_Q-(_FP_IMPLBIT_Q != 0)-_FP_W_TYPE_SIZE;
    unsigned exp   : _FP_EXPBITS_Q;
    unsigned sign  : 1;
#endif
  } bits;
};

#define FP_DECL_Q(X)		_FP_DECL(2,X)
#define FP_UNPACK_RAW_Q(X,val)	_FP_UNPACK_RAW_2(Q,X,val)
#define FP_PACK_RAW_Q(val,X)	_FP_PACK_RAW_2(Q,val,X)

#define FP_UNPACK_Q(X,val)		\
  do {					\
    _FP_UNPACK_RAW_2(Q,X,val);		\
    _FP_UNPACK_CANONICAL(Q,2,X);	\
  } while (0)

#define FP_PACK_Q(val,X)		\
  do {					\
    _FP_PACK_CANONICAL(Q,2,X);		\
    _FP_PACK_RAW_2(Q,val,X);		\
  } while (0)

#define FP_NEG_Q(R,X)		_FP_NEG(Q,2,R,X)
#define FP_ADD_Q(R,X,Y)		_FP_ADD(Q,2,R,X,Y)
#define FP_SUB_Q(R,X,Y)		_FP_SUB(Q,2,R,X,Y)
#define FP_MUL_Q(R,X,Y)		_FP_MUL(Q,2,R,X,Y)
#define FP_DIV_Q(R,X,Y)		_FP_DIV(Q,2,R,X,Y)
#define FP_SQRT_Q(R,X)		_FP_SQRT(Q,2,R,X)

#define FP_CMP_Q(r,X,Y,un)	_FP_CMP(Q,2,r,X,Y,un)
#define FP_CMP_EQ_Q(r,X,Y)	_FP_CMP_EQ(Q,2,r,X,Y)

#define FP_TO_INT_Q(r,X,rsz,rsg)  _FP_TO_INT(Q,2,r,X,rsz,rsg)
#define FP_FROM_INT_Q(X,r,rs,rt)  _FP_FROM_INT(Q,2,X,r,rs,rt)
