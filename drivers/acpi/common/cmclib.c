/******************************************************************************
 *
 * Module Name: cmclib - Local implementation of C library functions
 * $Revision: 24 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 R. Byron Moore
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include "acpi.h"
#include "acevents.h"
#include "achware.h"
#include "acnamesp.h"
#include "acinterp.h"
#include "amlcode.h"

/*
 * These implementations of standard C Library routines can optionally be
 * used if a C library is not available.  In general, they are less efficient
 * than an inline or assembly implementation
 */

#define _COMPONENT          MISCELLANEOUS
	 MODULE_NAME         ("cmclib")


#ifdef _MSC_VER                 /* disable some level-4 warnings for VC++ */
#pragma warning(disable:4706)   /* warning C4706: assignment within conditional expression */
#endif

#ifndef ACPI_USE_SYSTEM_CLIBRARY

/*******************************************************************************
 *
 * FUNCTION:    strlen
 *
 * PARAMETERS:  String              - Null terminated string
 *
 * RETURN:      Length
 *
 * DESCRIPTION: Returns the length of the input string
 *
 ******************************************************************************/


NATIVE_UINT
acpi_cm_strlen (
	const NATIVE_CHAR       *string)
{
	NATIVE_UINT             length = 0;


	/* Count the string until a null is encountered */

	while (*string) {
		length++;
		string++;
	}

	return (length);
}


/*******************************************************************************
 *
 * FUNCTION:    strcpy
 *
 * PARAMETERS:  Dst_string      - Target of the copy
 *              Src_string      - The source string to copy
 *
 * RETURN:      Dst_string
 *
 * DESCRIPTION: Copy a null terminated string
 *
 ******************************************************************************/

NATIVE_CHAR *
acpi_cm_strcpy (
	NATIVE_CHAR             *dst_string,
	const NATIVE_CHAR       *src_string)
{
	NATIVE_CHAR             *string = dst_string;


	/* Move bytes brute force */

	while (*src_string) {
		*string = *src_string;

		string++;
		src_string++;
	}

	/* Null terminate */

	*string = 0;

	return (dst_string);
}


/*******************************************************************************
 *
 * FUNCTION:    strncpy
 *
 * PARAMETERS:  Dst_string      - Target of the copy
 *              Src_string      - The source string to copy
 *              Count           - Maximum # of bytes to copy
 *
 * RETURN:      Dst_string
 *
 * DESCRIPTION: Copy a null terminated string, with a maximum length
 *
 ******************************************************************************/

NATIVE_CHAR *
acpi_cm_strncpy (
	NATIVE_CHAR             *dst_string,
	const NATIVE_CHAR       *src_string,
	NATIVE_UINT             count)
{
	NATIVE_CHAR             *string = dst_string;


	/* Copy the string */

	for (string = dst_string;
		count && (count--, (*string++ = *src_string++)); )
	{;}

	/* Pad with nulls if necessary */

	while (count--) {
		*string = 0;
		string++;
	}

	/* Return original pointer */

	return (dst_string);
}


/*******************************************************************************
 *
 * FUNCTION:    strcmp
 *
 * PARAMETERS:  String1         - First string
 *              String2         - Second string
 *
 * RETURN:      Index where strings mismatched, or 0 if strings matched
 *
 * DESCRIPTION: Compare two null terminated strings
 *
 ******************************************************************************/

u32
acpi_cm_strcmp (
	const NATIVE_CHAR       *string1,
	const NATIVE_CHAR       *string2)
{


	for ( ; (*string1 == *string2); string2++) {
		if (!*string1++) {
			return (0);
		}
	}


	return ((unsigned char) *string1 - (unsigned char) *string2);
}


/*******************************************************************************
 *
 * FUNCTION:    strncmp
 *
 * PARAMETERS:  String1         - First string
 *              String2         - Second string
 *              Count           - Maximum # of bytes to compare
 *
 * RETURN:      Index where strings mismatched, or 0 if strings matched
 *
 * DESCRIPTION: Compare two null terminated strings, with a maximum length
 *
 ******************************************************************************/

u32
acpi_cm_strncmp (
	const NATIVE_CHAR       *string1,
	const NATIVE_CHAR       *string2,
	NATIVE_UINT             count)
{


	for ( ; count-- && (*string1 == *string2); string2++) {
		if (!*string1++) {
			return (0);
		}
	}

	return ((count == -1) ? 0 : ((unsigned char) *string1 -
		(unsigned char) *string2));
}


/*******************************************************************************
 *
 * FUNCTION:    Strcat
 *
 * PARAMETERS:  Dst_string      - Target of the copy
 *              Src_string      - The source string to copy
 *
 * RETURN:      Dst_string
 *
 * DESCRIPTION: Append a null terminated string to a null terminated string
 *
 ******************************************************************************/

NATIVE_CHAR *
acpi_cm_strcat (
	NATIVE_CHAR             *dst_string,
	const NATIVE_CHAR       *src_string)
{
	NATIVE_CHAR             *string;


	/* Find end of the destination string */

	for (string = dst_string; *string++; ) { ; }

	/* Concatinate the string */

	for (--string; (*string++ = *src_string++); ) { ; }

	return (dst_string);
}


/*******************************************************************************
 *
 * FUNCTION:    strncat
 *
 * PARAMETERS:  Dst_string      - Target of the copy
 *              Src_string      - The source string to copy
 *              Count           - Maximum # of bytes to copy
 *
 * RETURN:      Dst_string
 *
 * DESCRIPTION: Append a null terminated string to a null terminated string,
 *              with a maximum count.
 *
 ******************************************************************************/

NATIVE_CHAR *
acpi_cm_strncat (
	NATIVE_CHAR             *dst_string,
	const NATIVE_CHAR       *src_string,
	NATIVE_UINT             count)
{
	NATIVE_CHAR             *string;


	if (count) {
		/* Find end of the destination string */

		for (string = dst_string; *string++; ) { ; }

		/* Concatinate the string */

		for (--string; (*string++ = *src_string++) && --count; ) { ; }

		/* Null terminate if necessary */

		if (!count) {
			*string = 0;
		}
	}

	return (dst_string);
}


/*******************************************************************************
 *
 * FUNCTION:    memcpy
 *
 * PARAMETERS:  Dest        - Target of the copy
 *              Src         - Source buffer to copy
 *              Count       - Number of bytes to copy
 *
 * RETURN:      Dest
 *
 * DESCRIPTION: Copy arbitrary bytes of memory
 *
 ******************************************************************************/

void *
acpi_cm_memcpy (
	void                    *dest,
	const void              *src,
	NATIVE_UINT             count)
{
	NATIVE_CHAR             *new = (NATIVE_CHAR *) dest;
	NATIVE_CHAR             *old = (NATIVE_CHAR *) src;


	while (count) {
		*new = *old;
		new++;
		old++;
		count--;
	}

	return (dest);
}


/*******************************************************************************
 *
 * FUNCTION:    memset
 *
 * PARAMETERS:  Dest        - Buffer to set
 *              Value       - Value to set each byte of memory
 *              Count       - Number of bytes to set
 *
 * RETURN:      Dest
 *
 * DESCRIPTION: Initialize a buffer to a known value.
 *
 ******************************************************************************/

void *
acpi_cm_memset (
	void                    *dest,
	u32                     value,
	NATIVE_UINT             count)
{
	NATIVE_CHAR             *new = (NATIVE_CHAR *) dest;


	while (count) {
		*new = (char) value;
		new++;
		count--;
	}

	return (dest);
}


#define NEGATIVE    1
#define POSITIVE    0


#define _XA     0x00    /* extra alphabetic - not supported */
#define _XS     0x40    /* extra space */
#define _BB     0x00    /* BEL, BS, etc. - not supported */
#define _CN     0x20    /* CR, FF, HT, NL, VT */
#define _DI     0x04    /* '0'-'9' */
#define _LO     0x02    /* 'a'-'z' */
#define _PU     0x10    /* punctuation */
#define _SP     0x08    /* space */
#define _UP     0x01    /* 'A'-'Z' */
#define _XD     0x80    /* '0'-'9', 'A'-'F', 'a'-'f' */

const u8 _ctype[257] = {
	_CN,            /* 0x0      0.     */
	_CN,            /* 0x1      1.     */
	_CN,            /* 0x2      2.     */
	_CN,            /* 0x3      3.     */
	_CN,            /* 0x4      4.     */
	_CN,            /* 0x5      5.     */
	_CN,            /* 0x6      6.     */
	_CN,            /* 0x7      7.     */
	_CN,            /* 0x8      8.     */
	_CN|_SP,        /* 0x9      9.     */
	_CN|_SP,        /* 0xA     10.     */
	_CN|_SP,        /* 0xB     11.     */
	_CN|_SP,        /* 0xC     12.     */
	_CN|_SP,        /* 0xD     13.     */
	_CN,            /* 0xE     14.     */
	_CN,            /* 0xF     15.     */
	_CN,            /* 0x10    16.     */
	_CN,            /* 0x11    17.     */
	_CN,            /* 0x12    18.     */
	_CN,            /* 0x13    19.     */
	_CN,            /* 0x14    20.     */
	_CN,            /* 0x15    21.     */
	_CN,            /* 0x16    22.     */
	_CN,            /* 0x17    23.     */
	_CN,            /* 0x18    24.     */
	_CN,            /* 0x19    25.     */
	_CN,            /* 0x1A    26.     */
	_CN,            /* 0x1B    27.     */
	_CN,            /* 0x1C    28.     */
	_CN,            /* 0x1D    29.     */
	_CN,            /* 0x1E    30.     */
	_CN,            /* 0x1F    31.     */
	_XS|_SP,        /* 0x20    32. ' ' */
	_PU,            /* 0x21    33. '!' */
	_PU,            /* 0x22    34. '"' */
	_PU,            /* 0x23    35. '#' */
	_PU,            /* 0x24    36. '$' */
	_PU,            /* 0x25    37. '%' */
	_PU,            /* 0x26    38. '&' */
	_PU,            /* 0x27    39. ''' */
	_PU,            /* 0x28    40. '(' */
	_PU,            /* 0x29    41. ')' */
	_PU,            /* 0x2A    42. '*' */
	_PU,            /* 0x2B    43. '+' */
	_PU,            /* 0x2C    44. ',' */
	_PU,            /* 0x2D    45. '-' */
	_PU,            /* 0x2E    46. '.' */
	_PU,            /* 0x2F    47. '/' */
	_XD|_DI,        /* 0x30    48. '0' */
	_XD|_DI,        /* 0x31    49. '1' */
	_XD|_DI,        /* 0x32    50. '2' */
	_XD|_DI,        /* 0x33    51. '3' */
	_XD|_DI,        /* 0x34    52. '4' */
	_XD|_DI,        /* 0x35    53. '5' */
	_XD|_DI,        /* 0x36    54. '6' */
	_XD|_DI,        /* 0x37    55. '7' */
	_XD|_DI,        /* 0x38    56. '8' */
	_XD|_DI,        /* 0x39    57. '9' */
	_PU,            /* 0x3A    58. ':' */
	_PU,            /* 0x3B    59. ';' */
	_PU,            /* 0x3C    60. '<' */
	_PU,            /* 0x3D    61. '=' */
	_PU,            /* 0x3E    62. '>' */
	_PU,            /* 0x3F    63. '?' */
	_PU,            /* 0x40    64. '@' */
	_XD|_UP,        /* 0x41    65. 'A' */
	_XD|_UP,        /* 0x42    66. 'B' */
	_XD|_UP,        /* 0x43    67. 'C' */
	_XD|_UP,        /* 0x44    68. 'D' */
	_XD|_UP,        /* 0x45    69. 'E' */
	_XD|_UP,        /* 0x46    70. 'F' */
	_UP,            /* 0x47    71. 'G' */
	_UP,            /* 0x48    72. 'H' */
	_UP,            /* 0x49    73. 'I' */
	_UP,            /* 0x4A    74. 'J' */
	_UP,            /* 0x4B    75. 'K' */
	_UP,            /* 0x4C    76. 'L' */
	_UP,            /* 0x4D    77. 'M' */
	_UP,            /* 0x4E    78. 'N' */
	_UP,            /* 0x4F    79. 'O' */
	_UP,            /* 0x50    80. 'P' */
	_UP,            /* 0x51    81. 'Q' */
	_UP,            /* 0x52    82. 'R' */
	_UP,            /* 0x53    83. 'S' */
	_UP,            /* 0x54    84. 'T' */
	_UP,            /* 0x55    85. 'U' */
	_UP,            /* 0x56    86. 'V' */
	_UP,            /* 0x57    87. 'W' */
	_UP,            /* 0x58    88. 'X' */
	_UP,            /* 0x59    89. 'Y' */
	_UP,            /* 0x5A    90. 'Z' */
	_PU,            /* 0x5B    91. '[' */
	_PU,            /* 0x5C    92. '\' */
	_PU,            /* 0x5D    93. ']' */
	_PU,            /* 0x5E    94. '^' */
	_PU,            /* 0x5F    95. '_' */
	_PU,            /* 0x60    96. '`' */
	_XD|_LO,        /* 0x61    97. 'a' */
	_XD|_LO,        /* 0x62    98. 'b' */
	_XD|_LO,        /* 0x63    99. 'c' */
	_XD|_LO,        /* 0x64   100. 'd' */
	_XD|_LO,        /* 0x65   101. 'e' */
	_XD|_LO,        /* 0x66   102. 'f' */
	_LO,            /* 0x67   103. 'g' */
	_LO,            /* 0x68   104. 'h' */
	_LO,            /* 0x69   105. 'i' */
	_LO,            /* 0x6A   106. 'j' */
	_LO,            /* 0x6B   107. 'k' */
	_LO,            /* 0x6C   108. 'l' */
	_LO,            /* 0x6D   109. 'm' */
	_LO,            /* 0x6E   110. 'n' */
	_LO,            /* 0x6F   111. 'o' */
	_LO,            /* 0x70   112. 'p' */
	_LO,            /* 0x71   113. 'q' */
	_LO,            /* 0x72   114. 'r' */
	_LO,            /* 0x73   115. 's' */
	_LO,            /* 0x74   116. 't' */
	_LO,            /* 0x75   117. 'u' */
	_LO,            /* 0x76   118. 'v' */
	_LO,            /* 0x77   119. 'w' */
	_LO,            /* 0x78   120. 'x' */
	_LO,            /* 0x79   121. 'y' */
	_LO,            /* 0x7A   122. 'z' */
	_PU,            /* 0x7B   123. '{' */
	_PU,            /* 0x7C   124. '|' */
	_PU,            /* 0x7D   125. '}' */
	_PU,            /* 0x7E   126. '~' */
	_CN,            /* 0x7F   127.     */

	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  /* 0x80 to 0x8F    */
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  /* 0x90 to 0x9F    */
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  /* 0xA0 to 0xAF    */
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  /* 0xB0 to 0xBF    */
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  /* 0xC0 to 0xCF    */
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  /* 0xD0 to 0xDF    */
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  /* 0xE0 to 0xEF    */
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 /* 0xF0 to 0x100   */
};

#define IS_UPPER(c)  (_ctype[(unsigned char)(c)] & (_UP))
#define IS_LOWER(c)  (_ctype[(unsigned char)(c)] & (_LO))
#define IS_DIGIT(c)  (_ctype[(unsigned char)(c)] & (_DI))
#define IS_SPACE(c)  (_ctype[(unsigned char)(c)] & (_SP))


/*******************************************************************************
 *
 * FUNCTION:    Acpi_cm_to_upper
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION: Convert character to uppercase
 *
 ******************************************************************************/

u32
acpi_cm_to_upper (
	u32                     c)
{

	return (IS_LOWER(c) ? ((c)-0x20) : (c));
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_cm_to_lower
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION: Convert character to lowercase
 *
 ******************************************************************************/

u32
acpi_cm_to_lower (
	u32                     c)
{

	return (IS_UPPER(c) ? ((c)+0x20) : (c));
}


/*******************************************************************************
 *
 * FUNCTION:    strupr
 *
 * PARAMETERS:  Src_string      - The source string to convert to
 *
 * RETURN:      Src_string
 *
 * DESCRIPTION: Convert string to uppercase
 *
 ******************************************************************************/

NATIVE_CHAR *
acpi_cm_strupr (
	NATIVE_CHAR             *src_string)
{
	NATIVE_CHAR             *string;


	/* Walk entire string, uppercasing the letters */

	for (string = src_string; *string; ) {
		*string = (char) acpi_cm_to_upper (*string);
		string++;
	}


	return (src_string);
}


/*******************************************************************************
 *
 * FUNCTION:    strstr
 *
 * PARAMETERS:  String1       -
 *              String2
 *
 * RETURN:
 *
 * DESCRIPTION: Checks if String2 occurs in String1. This is not really a
 *              full implementation of strstr, only sufficient for command
 *              matching
 *
 ******************************************************************************/

NATIVE_CHAR *
acpi_cm_strstr (
	NATIVE_CHAR             *string1,
	NATIVE_CHAR             *string2)
{
	NATIVE_CHAR             *string;


	if (acpi_cm_strlen (string2) > acpi_cm_strlen (string1)) {
		return (NULL);
	}

	/* Walk entire string, uppercasing the letters */

	for (string = string1; *string2; ) {
		if (*string2 != *string) {
			return (NULL);
		}

		string2++;
		string++;
	}


	return (string1);
}


/*******************************************************************************
 *
 * FUNCTION:    strtoul
 *
 * PARAMETERS:  String          - Null terminated string
 *              Terminater      - Where a pointer to the terminating byte is returned
 *              Base            - Radix of the string
 *
 * RETURN:      Converted value
 *
 * DESCRIPTION: Convert a string into an unsigned value.
 *
 ******************************************************************************/

u32
acpi_cm_strtoul (
	const NATIVE_CHAR       *string,
	NATIVE_CHAR             **terminator,
	u32                     base)
{
	u32                     converted = 0;
	u32                     index;
	u32                     sign;
	const NATIVE_CHAR       *string_start;
	u32                     return_value = 0;
	ACPI_STATUS             status = AE_OK;


	/*
	 * Save the value of the pointer to the buffer's first
	 * character, save the current errno value, and then
	 * skip over any white space in the buffer:
	 */
	string_start = string;
	while (IS_SPACE (*string) || *string == '\t') {
		++string;
	}

	/*
	 * The buffer may contain an optional plus or minus sign.
	 * If it does, then skip over it but remember what is was:
	 */
	if (*string == '-') {
		sign = NEGATIVE;
		++string;
	}

	else if (*string == '+') {
		++string;
		sign = POSITIVE;
	}

	else {
		sign = POSITIVE;
	}

	/*
	 * If the input parameter Base is zero, then we need to
	 * determine if it is octal, decimal, or hexadecimal:
	 */
	if (base == 0) {
		if (*string == '0') {
			if (acpi_cm_to_lower (*(++string)) == 'x') {
				base = 16;
				++string;
			}

			else {
				base = 8;
			}
		}

		else {
			base = 10;
		}
	}

	else if (base < 2 || base > 36) {
		/*
		 * The specified Base parameter is not in the domain of
		 * this function:
		 */
		goto done;
	}

	/*
	 * For octal and hexadecimal bases, skip over the leading
	 * 0 or 0x, if they are present.
	 */
	if (base == 8 && *string == '0') {
		string++;
	}

	if (base == 16 &&
		*string == '0' &&
		acpi_cm_to_lower (*(++string)) == 'x')
	{
		string++;
	}


	/*
	 * Main loop: convert the string to an unsigned long:
	 */
	while (*string) {
		if (IS_DIGIT (*string)) {
			index = *string - '0';
		}

		else {
			index = acpi_cm_to_upper (*string);
			if (IS_UPPER (index)) {
				index = index - 'A' + 10;
			}

			else {
				goto done;
			}
		}

		if (index >= base) {
			goto done;
		}

		/*
		 * Check to see if value is out of range:
		 */

		if (return_value > ((ACPI_UINT32_MAX - (u32) index) /
				   (u32) base))
		{
			status = AE_ERROR;
			return_value = 0L;          /* reset */
		}

		else {
			return_value *= base;
			return_value += index;
			converted = 1;
		}

		++string;
	}

done:
	/*
	 * If appropriate, update the caller's pointer to the next
	 * unconverted character in the buffer.
	 */
	if (terminator) {
		if (converted == 0 && return_value == 0L && string != NULL) {
			*terminator = (NATIVE_CHAR *) string_start;
		}

		else {
			*terminator = (NATIVE_CHAR *) string;
		}
	}

	if (status == AE_ERROR) {
		return_value = ACPI_UINT32_MAX;
	}

	/*
	 * If a minus sign was present, then "the conversion is negated":
	 */
	if (sign == NEGATIVE) {
		return_value = (ACPI_UINT32_MAX - return_value) + 1;
	}

	return (return_value);
}

#endif /* ACPI_USE_SYSTEM_CLIBRARY */

