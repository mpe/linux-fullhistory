/*
 * These defines correspond to the dynamic rounding mode bits in the
 * Floating Point Control Register.  They also happen to correspond to
 * the instruction encodings except that 0x03 signifies dynamic
 * rounding mode in that case.
 */
#define ROUND_CHOP	0x00	/* chopped (aka round towards zero) */
#define ROUND_NINF	0x01	/* round towards negative infinity */
#define ROUND_NEAR	0x02	/* round towards nearest number */
#define ROUND_PINF	0x03	/* round towards positive infinity */
