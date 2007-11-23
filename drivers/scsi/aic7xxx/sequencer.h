/*
 * Instruction formats for the sequencer program downloaded to
 * Aic7xxx SCSI host adapters
 *
 * Copyright (c) 1997 Justin T. Gibbs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Where this Software is combined with software released under the terms of 
 * the GNU Public License ("GPL") and the terms of the GPL would require the 
 * combined work to also be released under the terms of the GPL, the terms
 * and conditions of this License will apply in addition to those of the
 * GPL with the exception of any terms or conditions of this License that
 * conflict with, or are expressly prohibited by, the GPL.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      $Id: sequencer.h,v 1.3 1997/09/27 19:37:31 gibbs Exp $
 */

struct ins_format1 {
	unsigned char immediate;
	unsigned char source;
	unsigned char destination;
	unsigned char opcode_ret;
#define DOWNLOAD_CONST_IMMEDIATE 0x80
};

struct ins_format2 {
	unsigned char shift_control;
	unsigned char source;
	unsigned char destination;
	unsigned char opcode_ret;
#define RETURN_BIT 0x01
};

struct ins_format3 {
	unsigned char immediate;
	unsigned char source;
	unsigned char address;
	unsigned char opcode_addr;
#define ADDR_HIGH_BIT 0x01
};

#ifndef __KERNEL__
struct instruction {
	union {
		struct ins_format1 format1;
		struct ins_format2 format2;
		struct ins_format3 format3;
		unsigned char	   bytes[4];
	} format;
	u_int	srcline;
	struct symbol *patch_label;
	STAILQ_ENTRY(instruction) links;
};
#endif

#define	AIC_OP_OR	0x0
#define	AIC_OP_AND	0x1
#define AIC_OP_XOR	0x2
#define	AIC_OP_ADD	0x3
#define	AIC_OP_ADC	0x4
#define	AIC_OP_ROL	0x5

#define	AIC_OP_JMP	0x8
#define AIC_OP_JC	0x9
#define AIC_OP_JNC	0xa
#define AIC_OP_CALL	0xb
#define	AIC_OP_JNE	0xc
#define	AIC_OP_JNZ	0xd
#define	AIC_OP_JE	0xe
#define	AIC_OP_JZ	0xf

/* Pseudo Ops */
#define	AIC_OP_SHL	0x10
#define	AIC_OP_SHR	0x20
#define	AIC_OP_ROR	0x30
