/*
 * Basic four-word fraction declaration and manipulation.
 */

#define _FP_FRAC_DECL_4(X)	_FP_W_TYPE X##_f[4]
#define _FP_FRAC_COPY_4(D,S)			\
  (D##_f[0] = S##_f[0], D##_f[1] = S##_f[1],	\
   D##_f[2] = S##_f[2], D##_f[3] = S##_f[3])
#define _FP_FRAC_SET_4(X,I)	__FP_FRAC_SET_4(X, I)
#define _FP_FRAC_HIGH_4(X)	(X##_f[3])
#define _FP_FRAC_LOW_4(X)	(X##_f[0])
#define _FP_FRAC_WORD_4(X,w)	(X##_f[w])

#define _FP_FRAC_SLL_4(X,N)						\
  do {									\
    _FP_I_TYPE _up, _down, _skip, _i;					\
    _skip = (N) / _FP_W_TYPE_SIZE;					\
    _up = (N) % _FP_W_TYPE_SIZE;					\
    _down = _FP_W_TYPE_SIZE - _up;					\
    for (_i = 3; _i > _skip; --_i)					\
      X##_f[_i] = X##_f[_i-_skip] << _up | X##_f[_i-_skip-1] >> _down;	\
    X##_f[_i] <<= _up;							\
    for (--_i; _i >= 0; --_i)						\
      X##_f[_i] = 0;							\
  } while (0)

#define _FP_FRAC_SRL_4(X,N)						\
  do {									\
    _FP_I_TYPE _up, _down, _skip, _i;					\
    _skip = (N) / _FP_W_TYPE_SIZE;					\
    _down = (N) % _FP_W_TYPE_SIZE;					\
    _up = _FP_W_TYPE_SIZE - _down;					\
    for (_i = 0; _i < 4-_skip; ++_i)					\
      X##_f[_i] = X##_f[_i+_skip] >> _down | X##_f[_i+_skip+1] << _up;	\
    X##_f[_i] >>= _down;						\
    for (++_i; _i < 4; ++_i)						\
      X##_f[_i] = 0;							\
  } while (0)


/* Right shift with sticky-lsb.  */
#define _FP_FRAC_SRS_4(X,N,size)					\
  do {									\
    _FP_I_TYPE _up, _down, _skip, _i;					\
    _FP_W_TYPE _s;							\
    _skip = (N) / _FP_W_TYPE_SIZE;					\
    _down = (N) % _FP_W_TYPE_SIZE;					\
    _up = _FP_W_TYPE_SIZE - _down;					\
    for (_s = _i = 0; _i < _skip; ++_i)					\
      _s |= X##_f[_i];							\
    _s = X##_f[_i] << _up;						\
    X##_f[0] = X##_f[_skip] >> _down | X##_f[_skip+1] << _up | (_s != 0); \
    for (_i = 1; _i < 4-_skip; ++_i)					\
      X##_f[_i] = X##_f[_i+_skip] >> _down | X##_f[_i+_skip+1] << _up;	\
    X##_f[_i] >>= _down;						\
    for (++_i; _i < 4; ++_i)						\
      X##_f[_i] = 0;							\
  } while (0)

#define _FP_FRAC_ADD_4(R,X,Y)						\
  __FP_FRAC_ADD_4(R##_f[3], R##_f[2], R##_f[1], R##_f[0],		\
		  X##_f[3], X##_f[2], X##_f[1], X##_f[0],		\
		  Y##_f[3], Y##_f[2], Y##_f[1], Y##_f[0])

/*
 * Internals 
 */

#define __FP_FRAC_SET_4(X,I3,I2,I1,I0)					\
  (X##_f[3] = I3, X##_f[2] = I2, X##_f[1] = I1, X##_f[0] = I0)

#ifndef __FP_FRAC_ADD_4
#define __FP_FRAC_ADD_4(r3,r2,r1,r0,x3,x2,x1,x0,y3,y2,y1,y0)		\
  (r0 = x0 + y0,							\
   r1 = x1 + y1 + (r0 < x0),						\
   r2 = x2 + y2 + (r1 < x1),						\
   r3 = x3 + y3 + (r2 < x2))
#endif
