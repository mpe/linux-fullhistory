%{
/*
 * Parser for the Aic7xxx SCSI Host adapter sequencer assembler.
 *
 * Copyright (c) 1997, 1998, 2000 Justin T. Gibbs.
 * Copyright (c) 2001 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id: //depot/aic7xxx/aic7xxx/aicasm/aicasm_gram.y#9 $
 *
 * $FreeBSD: src/sys/dev/aic7xxx/aicasm/aicasm_gram.y,v 1.12 2000/10/31 18:44:32 gibbs Exp $
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include <sys/types.h>
#ifdef __linux__
#include "../queue.h"
#else
#include <sys/queue.h>
#endif

#include "aicasm.h"
#include "aicasm_symbol.h"
#include "aicasm_insformat.h"

int yylineno;
char *yyfilename;
char *versions;
static symbol_t *cur_symbol;
static symtype cur_symtype;
static symbol_t *accumulator;
static symbol_ref_t allones;
static symbol_ref_t allzeros;
static symbol_ref_t none;
static symbol_ref_t sindex;
static int instruction_ptr;
static int sram_or_scb_offset;
static int download_constant_count;
static int in_critical_section;

static void process_bitmask(int mask_type, symbol_t *sym, int mask);
static void initialize_symbol(symbol_t *symbol);
static void process_register(symbol_t **p_symbol);
static void format_1_instr(int opcode, symbol_ref_t *dest,
			   expression_t *immed, symbol_ref_t *src, int ret);
static void format_2_instr(int opcode, symbol_ref_t *dest,
			   expression_t *places, symbol_ref_t *src, int ret);
static void format_3_instr(int opcode, symbol_ref_t *src,
			   expression_t *immed, symbol_ref_t *address);
static void test_readable_symbol(symbol_t *symbol);
static void test_writable_symbol(symbol_t *symbol);
static void type_check(symbol_t *symbol, expression_t *expression, int and_op);
static void make_expression(expression_t *immed, int value);
static void add_conditional(symbol_t *symbol);
static void add_version(const char *verstring);
static int  is_download_const(expression_t *immed);

#define YYDEBUG 1
#define SRAM_SYMNAME "SRAM_BASE"
#define SCB_SYMNAME "SCB_BASE"
%}

%union {
	int		value;
	char		*str;
	symbol_t	*sym;
	symbol_ref_t	sym_ref;
	expression_t	expression;
}

%token T_REGISTER

%token <value> T_CONST

%token T_DOWNLOAD

%token T_SCB

%token T_SRAM

%token T_ALIAS

%token T_SIZE

%token <value> T_ADDRESS

%token T_ACCESS_MODE

%token <value> T_MODE

%token T_BEGIN_CS

%token T_END_CS

%token T_BIT

%token T_MASK

%token <value> T_NUMBER

%token <str> T_PATH T_STRING

%token <sym> T_CEXPR

%token T_EOF T_INCLUDE T_VERSION

%token <value> T_SHR T_SHL T_ROR T_ROL

%token <value> T_MVI T_MOV T_CLR T_BMOV

%token <value> T_JMP T_JC T_JNC T_JE T_JNE T_JNZ T_JZ T_CALL

%token <value> T_ADD T_ADC

%token <value> T_INC T_DEC

%token <value> T_STC T_CLC

%token <value> T_CMP T_NOT T_XOR

%token <value> T_TEST T_AND

%token <value> T_OR

%token T_RET

%token T_NOP

%token T_ACCUM T_ALLONES T_ALLZEROS T_NONE T_SINDEX

%token T_A

%token <sym> T_SYMBOL

%token T_NL

%token T_IF T_ELSE T_ELSE_IF T_ENDIF

%type <sym_ref> reg_symbol address destination source opt_source

%type <expression> expression immediate immediate_or_a

%type <value> ret f1_opcode f2_opcode jmp_jc_jnc_call jz_jnz je_jne

%type <value> numerical_value

%left '|'
%left '&'
%left '+' '-'
%right '~'
%nonassoc UMINUS
%%

program:
	include
|	program include
|	version
|	program version
|	register
|	program register
|	constant
|	program constant
|	scratch_ram
|	program scratch_ram
|	scb
|	program scb
|	label
|	program label
|	critical_section_start
|	program critical_section_start
|	critical_section_end
|	program critical_section_end
|	conditional
|	program conditional
|	code
|	program code
;

include:
	T_INCLUDE '<' T_PATH '>'
	{
		include_file($3, BRACKETED_INCLUDE);
	}
|	T_INCLUDE '"' T_PATH '"'
	{
		include_file($3, QUOTED_INCLUDE);
	}
;

version:
	T_VERSION '=' T_STRING
	{ add_version($3); }
;

register:
	T_REGISTER { cur_symtype = REGISTER; } reg_definition
;

reg_definition:
	T_SYMBOL '{'
		{
			if ($1->type != UNINITIALIZED) {
				stop("Register multiply defined", EX_DATAERR);
				/* NOTREACHED */
			}
			cur_symbol = $1; 
			cur_symbol->type = cur_symtype;
			initialize_symbol(cur_symbol);
		}
		reg_attribute_list
	'}'
		{                    
			/*
			 * Default to allowing everything in for registers
			 * with no bit or mask definitions.
			 */
			if (cur_symbol->info.rinfo->valid_bitmask == 0)
				cur_symbol->info.rinfo->valid_bitmask = 0xFF;

			if (cur_symbol->info.rinfo->size == 0)
				cur_symbol->info.rinfo->size = 1;

			/*
			 * This might be useful for registers too.
			 */
			if (cur_symbol->type != REGISTER) {
				if (cur_symbol->info.rinfo->address == 0)
					cur_symbol->info.rinfo->address =
					    sram_or_scb_offset;
				sram_or_scb_offset +=
				    cur_symbol->info.rinfo->size;
			}
			cur_symbol = NULL;
		}
;

reg_attribute_list:
	reg_attribute
|	reg_attribute_list reg_attribute
;

reg_attribute:		
	reg_address
|	size
|	access_mode
|	bit_defn
|	mask_defn
|	alias
|	accumulator
|	allones
|	allzeros
|	none
|	sindex
;

reg_address:
	T_ADDRESS T_NUMBER
	{
		cur_symbol->info.rinfo->address = $2;
	}
;

size:
	T_SIZE T_NUMBER
	{
		cur_symbol->info.rinfo->size = $2;
	}
;

access_mode:
	T_ACCESS_MODE T_MODE
	{
		cur_symbol->info.rinfo->mode = $2;
	}
;

bit_defn:
	T_BIT T_SYMBOL T_NUMBER
	{
		process_bitmask(BIT, $2, $3);
	}
;

mask_defn:
	T_MASK T_SYMBOL expression
	{
		process_bitmask(MASK, $2, $3.value);
	}
;

alias:
	T_ALIAS	T_SYMBOL
	{
		if ($2->type != UNINITIALIZED) {
			stop("Re-definition of register alias",
			     EX_DATAERR);
			/* NOTREACHED */
		}
		$2->type = ALIAS;
		initialize_symbol($2);
		$2->info.ainfo->parent = cur_symbol;
	}
;

accumulator:
	T_ACCUM
	{
		if (accumulator != NULL) {
			stop("Only one accumulator definition allowed",
			     EX_DATAERR);
			/* NOTREACHED */
		}
		accumulator = cur_symbol;
	}
;

allones:
	T_ALLONES
	{
		if (allones.symbol != NULL) {
			stop("Only one definition of allones allowed",
			     EX_DATAERR);
			/* NOTREACHED */
		}
		allones.symbol = cur_symbol;
	}
;

allzeros:
	T_ALLZEROS
	{
		if (allzeros.symbol != NULL) {
			stop("Only one definition of allzeros allowed",
			     EX_DATAERR);
			/* NOTREACHED */
		}
		allzeros.symbol = cur_symbol;
	}
;

none:
	T_NONE
	{
		if (none.symbol != NULL) {
			stop("Only one definition of none allowed",
			     EX_DATAERR);
			/* NOTREACHED */
		}
		none.symbol = cur_symbol;
	}
;

sindex:
	T_SINDEX
	{
		if (sindex.symbol != NULL) {
			stop("Only one definition of sindex allowed",
			     EX_DATAERR);
			/* NOTREACHED */
		}
		sindex.symbol = cur_symbol;
	}
;

expression:
	expression '|' expression
	{
		 $$.value = $1.value | $3.value;
		 symlist_merge(&$$.referenced_syms,
			       &$1.referenced_syms,
			       &$3.referenced_syms);
	}
|	expression '&' expression
	{
		$$.value = $1.value & $3.value;
		symlist_merge(&$$.referenced_syms,
			       &$1.referenced_syms,
			       &$3.referenced_syms);
	}
|	expression '+' expression
	{
		$$.value = $1.value + $3.value;
		symlist_merge(&$$.referenced_syms,
			       &$1.referenced_syms,
			       &$3.referenced_syms);
	}
|	expression '-' expression
	{
		$$.value = $1.value - $3.value;
		symlist_merge(&($$.referenced_syms),
			       &($1.referenced_syms),
			       &($3.referenced_syms));
	}
|	'(' expression ')'
	{
		$$ = $2;
	}
|	'~' expression
	{
		$$ = $2;
		$$.value = (~$$.value) & 0xFF;
	}
|	'-' expression %prec UMINUS
	{
		$$ = $2;
		$$.value = -$$.value;
	}
|	T_NUMBER
	{
		$$.value = $1;
		SLIST_INIT(&$$.referenced_syms);
	}
|	T_SYMBOL
	{
		symbol_t *symbol;

		symbol = $1;
		switch (symbol->type) {
		case ALIAS:
			symbol = $1->info.ainfo->parent;
		case REGISTER:
		case SCBLOC:
		case SRAMLOC:
			$$.value = symbol->info.rinfo->address;
			break;
		case MASK:
		case BIT:
			$$.value = symbol->info.minfo->mask;
			break;
		case DOWNLOAD_CONST:
		case CONST:
			$$.value = symbol->info.cinfo->value;
			break;
		case UNINITIALIZED:
		default:
		{
			char buf[255];

			snprintf(buf, sizeof(buf),
				 "Undefined symbol %s referenced",
				 symbol->name);
			stop(buf, EX_DATAERR);
			/* NOTREACHED */
			break;
		}
		}
		SLIST_INIT(&$$.referenced_syms);
		symlist_add(&$$.referenced_syms, symbol, SYMLIST_INSERT_HEAD);
	}
;

constant:
	T_CONST T_SYMBOL numerical_value
	{
		if ($2->type != UNINITIALIZED) {
			stop("Re-definition of symbol as a constant",
			     EX_DATAERR);
			/* NOTREACHED */
		}
		$2->type = CONST;
		initialize_symbol($2);
		$2->info.cinfo->value = $3;
		$2->info.cinfo->define = $1;
	}
|	T_CONST T_SYMBOL T_DOWNLOAD
	{
		if ($1) {
			stop("Invalid downloaded constant declaration",
			     EX_DATAERR);
			/* NOTREACHED */
		}
		if ($2->type != UNINITIALIZED) {
			stop("Re-definition of symbol as a downloaded constant",
			     EX_DATAERR);
			/* NOTREACHED */
		}
		$2->type = DOWNLOAD_CONST;
		initialize_symbol($2);
		$2->info.cinfo->value = download_constant_count++;
		$2->info.cinfo->define = FALSE;
	}
;

numerical_value:
	T_NUMBER
	{
		$$ = $1;
	}
|	'-' T_NUMBER
	{
		$$ = -$2;
	}
;

scratch_ram:
	T_SRAM '{'
		{
			cur_symbol = symtable_get(SRAM_SYMNAME);
			cur_symtype = SRAMLOC;
			if (cur_symbol->type != UNINITIALIZED) {
				stop("Only one SRAM definition allowed",
				     EX_DATAERR);
				/* NOTREACHED */
			}
			cur_symbol->type = SRAMLOC;
			initialize_symbol(cur_symbol);
		}
		reg_address
		{
			sram_or_scb_offset = cur_symbol->info.rinfo->address;
		}
		scb_or_sram_reg_list
	'}'
		{
			cur_symbol = NULL;
		}
;

scb:
	T_SCB '{'
		{
			cur_symbol = symtable_get(SCB_SYMNAME);
			cur_symtype = SCBLOC;
			if (cur_symbol->type != UNINITIALIZED) {
				stop("Only one SRAM definition allowed",
				     EX_SOFTWARE);
				/* NOTREACHED */
			}
			cur_symbol->type = SCBLOC;
			initialize_symbol(cur_symbol);
			/* 64 bytes of SCB space */
			cur_symbol->info.rinfo->size = 64;
		}
		reg_address
		{
			sram_or_scb_offset = cur_symbol->info.rinfo->address;
		}
		scb_or_sram_reg_list
	'}'
		{
			cur_symbol = NULL;
		}
;

scb_or_sram_reg_list:
	reg_definition
|	scb_or_sram_reg_list reg_definition
;

reg_symbol:
	T_SYMBOL
	{
		process_register(&$1);
		$$.symbol = $1;
		$$.offset = 0;
	}
|	T_SYMBOL '[' T_SYMBOL ']'
	{
		process_register(&$1);
		if ($3->type != CONST) {
			stop("register offset must be a constant", EX_DATAERR);
			/* NOTREACHED */
		}
		if (($3->info.cinfo->value + 1) > $1->info.rinfo->size) {
			stop("Accessing offset beyond range of register",
			     EX_DATAERR);
			/* NOTREACHED */
		}
		$$.symbol = $1;
		$$.offset = $3->info.cinfo->value;
	}
|	T_SYMBOL '[' T_NUMBER ']'
	{
		process_register(&$1);
		if (($3 + 1) > $1->info.rinfo->size) {
			stop("Accessing offset beyond range of register",
			     EX_DATAERR);
			/* NOTREACHED */
		}
		$$.symbol = $1;
		$$.offset = $3;
	}
|	T_A
	{
		if (accumulator == NULL) {
			stop("No accumulator has been defined", EX_DATAERR);
			/* NOTREACHED */
		}
		$$.symbol = accumulator;
		$$.offset = 0;
	}
;

destination:
	reg_symbol
	{
		test_writable_symbol($1.symbol);
		$$ = $1;
	}
;

immediate:
	expression
	{ $$ = $1; }
;

immediate_or_a:
	expression
	{
		$$ = $1;
	}
|	T_A
	{
		SLIST_INIT(&$$.referenced_syms);
		$$.value = 0;
	}
;

source:
	reg_symbol
	{
		test_readable_symbol($1.symbol);
		$$ = $1;
	}
;

opt_source:
	{
		$$.symbol = NULL;
		$$.offset = 0;
	}
|	',' source
	{ $$ = $2; }
;

ret:
	{ $$ = 0; }
|	T_RET
	{ $$ = 1; }
;

critical_section_start:
	T_BEGIN_CS
	{
		critical_section_t *cs;

		if (in_critical_section != FALSE) {
			stop("Critical Section within Critical Section",
			     EX_DATAERR);
			/* NOTREACHED */
		}
		cs = cs_alloc();
		cs->begin_addr = instruction_ptr;
		in_critical_section = TRUE;
	}

critical_section_end:
	T_END_CS
	{
		critical_section_t *cs;

		if (in_critical_section == FALSE) {
			stop("Unballanced 'end_cs'", EX_DATAERR);
			/* NOTREACHED */
		}
		cs = TAILQ_LAST(&cs_tailq, cs_tailq);
		cs->end_addr = instruction_ptr;
		in_critical_section = FALSE;
	}

label:
	T_SYMBOL ':'
	{
		if ($1->type != UNINITIALIZED) {
			stop("Program label multiply defined", EX_DATAERR);
			/* NOTREACHED */
		}
		$1->type = LABEL;
		initialize_symbol($1);
		$1->info.linfo->address = instruction_ptr;
	}
;

address:
	T_SYMBOL
	{
		$$.symbol = $1;
		$$.offset = 0;
	}
|	T_SYMBOL '+' T_NUMBER
	{
		$$.symbol = $1;
		$$.offset = $3;
	}
|	T_SYMBOL '-' T_NUMBER
	{
		$$.symbol = $1;
		$$.offset = -$3;
	}
|	'.'
	{
		$$.symbol = NULL;
		$$.offset = 0;
	}
|	'.' '+' T_NUMBER
	{
		$$.symbol = NULL;
		$$.offset = $3;
	}
|	'.' '-' T_NUMBER
	{
		$$.symbol = NULL;
		$$.offset = -$3;
	}
;

conditional:
	T_IF T_CEXPR '{'
	{
		scope_t *new_scope;

		add_conditional($2);
		new_scope = scope_alloc();
		new_scope->type = SCOPE_IF;
		new_scope->begin_addr = instruction_ptr;
		new_scope->func_num = $2->info.condinfo->func_num;
	}
|	T_ELSE T_IF T_CEXPR '{'
	{
		scope_t *new_scope;
		scope_t *scope_context;
		scope_t *last_scope;

		/*
		 * Ensure that the previous scope is either an
		 * if or and else if.
		 */
		scope_context = SLIST_FIRST(&scope_stack);
		last_scope = TAILQ_LAST(&scope_context->inner_scope,
					scope_tailq);
		if (last_scope == NULL
		 || last_scope->type == T_ELSE) {

			stop("'else if' without leading 'if'", EX_DATAERR);
			/* NOTREACHED */
		}
		add_conditional($3);
		new_scope = scope_alloc();
		new_scope->type = SCOPE_ELSE_IF;
		new_scope->begin_addr = instruction_ptr;
		new_scope->func_num = $3->info.condinfo->func_num;
	}
|	T_ELSE '{'
	{
		scope_t *new_scope;
		scope_t *scope_context;
		scope_t *last_scope;

		/*
		 * Ensure that the previous scope is either an
		 * if or and else if.
		 */
		scope_context = SLIST_FIRST(&scope_stack);
		last_scope = TAILQ_LAST(&scope_context->inner_scope,
					scope_tailq);
		if (last_scope == NULL
		 || last_scope->type == SCOPE_ELSE) {

			stop("'else' without leading 'if'", EX_DATAERR);
			/* NOTREACHED */
		}
		new_scope = scope_alloc();
		new_scope->type = SCOPE_ELSE;
		new_scope->begin_addr = instruction_ptr;
	}
;

conditional:
	'}'
	{
		scope_t *scope_context;

		scope_context = SLIST_FIRST(&scope_stack);
		if (scope_context->type == SCOPE_ROOT) {
			stop("Unexpected '}' encountered", EX_DATAERR);
			/* NOTREACHED */
		}

		scope_context->end_addr = instruction_ptr;

		/* Pop the scope */
		SLIST_REMOVE_HEAD(&scope_stack, scope_stack_links);

		process_scope(scope_context);

		if (SLIST_FIRST(&scope_stack) == NULL) {
			stop("Unexpected '}' encountered", EX_DATAERR);
			/* NOTREACHED */
		}
	}
;

f1_opcode:
	T_AND { $$ = AIC_OP_AND; }
|	T_XOR { $$ = AIC_OP_XOR; }
|	T_ADD { $$ = AIC_OP_ADD; }
|	T_ADC { $$ = AIC_OP_ADC; }
;

code:
	f1_opcode destination ',' immediate_or_a opt_source ret ';'
	{
		format_1_instr($1, &$2, &$4, &$5, $6);
	}
;

code:
	T_OR reg_symbol ',' immediate_or_a opt_source ret ';'
	{
		format_1_instr(AIC_OP_OR, &$2, &$4, &$5, $6);
	}
;

code:
	T_INC destination opt_source ret ';'
	{
		expression_t immed;

		make_expression(&immed, 1);
		format_1_instr(AIC_OP_ADD, &$2, &immed, &$3, $4);
	}
;

code:
	T_DEC destination opt_source ret ';'
	{
		expression_t immed;

		make_expression(&immed, -1);
		format_1_instr(AIC_OP_ADD, &$2, &immed, &$3, $4);
	}
;

code:
	T_CLC ret ';'
	{
		expression_t immed;

		make_expression(&immed, -1);
		format_1_instr(AIC_OP_ADD, &none, &immed, &allzeros, $2);
	}
|	T_CLC T_MVI destination ',' immediate_or_a ret ';'
	{
		format_1_instr(AIC_OP_ADD, &$3, &$5, &allzeros, $6);
	}
;

code:
	T_STC ret ';'
	{
		expression_t immed;

		make_expression(&immed, 1);
		format_1_instr(AIC_OP_ADD, &none, &immed, &allones, $2);
	}
|	T_STC destination ret ';'
	{
		expression_t immed;

		make_expression(&immed, 1);
		format_1_instr(AIC_OP_ADD, &$2, &immed, &allones, $3);
	}
;

code:
	T_BMOV destination ',' source ',' immediate ret ';'
	{
		format_1_instr(AIC_OP_BMOV, &$2, &$6, &$4, $7);
	}
;

code:
	T_MOV destination ',' source ret ';'
	{
		expression_t immed;

		make_expression(&immed, 1);
		format_1_instr(AIC_OP_BMOV, &$2, &immed, &$4, $5);
	}
;

code:
	T_MVI destination ',' immediate_or_a ret ';'
	{
		format_1_instr(AIC_OP_OR, &$2, &$4, &allzeros, $5);
	}
;

code:
	T_NOT destination opt_source ret ';'
	{
		expression_t immed;

		make_expression(&immed, 0xff);
		format_1_instr(AIC_OP_XOR, &$2, &immed, &$3, $4);
	}
;

code:
	T_CLR destination ret ';'
	{
		expression_t immed;

		make_expression(&immed, 0xff);
		format_1_instr(AIC_OP_AND, &$2, &immed, &allzeros, $3);
	}
;

code:
	T_NOP ret ';'
	{
		expression_t immed;

		make_expression(&immed, 0xff);
		format_1_instr(AIC_OP_AND, &none, &immed, &allzeros, $2);
	}
;

code:
	T_RET ';'
	{
		expression_t immed;

		make_expression(&immed, 0xff);
		format_1_instr(AIC_OP_AND, &none, &immed, &allzeros, TRUE);
	}
;

	/*
	 * This grammer differs from the one in the aic7xxx
	 * reference manual since the grammer listed there is
	 * ambiguous and causes a shift/reduce conflict.
	 * It also seems more logical as the "immediate"
	 * argument is listed as the second arg like the
	 * other formats.
	 */

f2_opcode:
	T_SHL { $$ = AIC_OP_SHL; }
|	T_SHR { $$ = AIC_OP_SHR; }
|	T_ROL { $$ = AIC_OP_ROL; }
|	T_ROR { $$ = AIC_OP_ROR; }
;

code:
	f2_opcode destination ',' expression opt_source ret ';'
	{
		format_2_instr($1, &$2, &$4, &$5, $6);
	}
;

jmp_jc_jnc_call:
	T_JMP	{ $$ = AIC_OP_JMP; }
|	T_JC	{ $$ = AIC_OP_JC; }
|	T_JNC	{ $$ = AIC_OP_JNC; }
|	T_CALL	{ $$ = AIC_OP_CALL; }
;

jz_jnz:
	T_JZ	{ $$ = AIC_OP_JZ; }
|	T_JNZ	{ $$ = AIC_OP_JNZ; }
;

je_jne:
	T_JE	{ $$ = AIC_OP_JE; }
|	T_JNE	{ $$ = AIC_OP_JNE; }
;

code:
	jmp_jc_jnc_call address ';'
	{
		expression_t immed;

		make_expression(&immed, 0);
		format_3_instr($1, &sindex, &immed, &$2);
	}
;

code:
	T_OR reg_symbol ',' immediate jmp_jc_jnc_call address ';'
	{
		format_3_instr($5, &$2, &$4, &$6);
	}
;

code:
	T_TEST source ',' immediate_or_a jz_jnz address ';'
	{
		format_3_instr($5, &$2, &$4, &$6);
	}
;

code:
	T_CMP source ',' immediate_or_a je_jne address ';'
	{
		format_3_instr($5, &$2, &$4, &$6);
	}
;

code:
	T_MOV source jmp_jc_jnc_call address ';'
	{
		expression_t immed;

		make_expression(&immed, 0);
		format_3_instr($3, &$2, &immed, &$4);
	}
;

code:
	T_MVI immediate jmp_jc_jnc_call address ';'
	{
		format_3_instr($3, &allzeros, &$2, &$4);
	}
;

%%

static void
process_bitmask(int mask_type, symbol_t *sym, int mask)
{
	/*
	 * Add the current register to its
	 * symbol list, if it already exists,
	 * warn if we are setting it to a
	 * different value, or in the bit to
	 * the "allowed bits" of this register.
	 */
	if (sym->type == UNINITIALIZED) {
		sym->type = mask_type;
		initialize_symbol(sym);
		if (mask_type == BIT) {
			if (mask == 0) {
				stop("Bitmask with no bits set", EX_DATAERR);
				/* NOTREACHED */
			}
			if ((mask & ~(0x01 << (ffs(mask) - 1))) != 0) {
				stop("Bitmask with more than one bit set",
				     EX_DATAERR);
				/* NOTREACHED */
			}
		}
		sym->info.minfo->mask = mask;
	} else if (sym->type != mask_type) {
		stop("Bit definition mirrors a definition of the same "
		     " name, but a different type", EX_DATAERR);
		/* NOTREACHED */
	} else if (mask != sym->info.minfo->mask) {
		stop("Bitmask redefined with a conflicting value", EX_DATAERR);
		/* NOTREACHED */
	}
	/* Fail if this symbol is already listed */
	if (symlist_search(&(sym->info.minfo->symrefs),
			   cur_symbol->name) != NULL) {
		stop("Bitmask defined multiple times for register", EX_DATAERR);
		/* NOTREACHED */
	}
	symlist_add(&(sym->info.minfo->symrefs), cur_symbol,
		    SYMLIST_INSERT_HEAD);
	cur_symbol->info.rinfo->valid_bitmask |= mask;
	cur_symbol->info.rinfo->typecheck_masks = TRUE;
}

static void
initialize_symbol(symbol_t *symbol)
{
	switch (symbol->type) {
        case UNINITIALIZED:
		stop("Call to initialize_symbol with type field unset",
		     EX_SOFTWARE);
		/* NOTREACHED */
		break;
        case REGISTER:
        case SRAMLOC:
        case SCBLOC:
		symbol->info.rinfo =
		    (struct reg_info *)malloc(sizeof(struct reg_info));
		if (symbol->info.rinfo == NULL) {
			stop("Can't create register info", EX_SOFTWARE);
			/* NOTREACHED */
		}
		memset(symbol->info.rinfo, 0,
		       sizeof(struct reg_info));
		break;
        case ALIAS:
		symbol->info.ainfo =
		    (struct alias_info *)malloc(sizeof(struct alias_info));
		if (symbol->info.ainfo == NULL) {
			stop("Can't create alias info", EX_SOFTWARE);
			/* NOTREACHED */
		}
		memset(symbol->info.ainfo, 0,
		       sizeof(struct alias_info));
		break;
        case MASK:
        case BIT:
		symbol->info.minfo =
		    (struct mask_info *)malloc(sizeof(struct mask_info));
		if (symbol->info.minfo == NULL) {
			stop("Can't create bitmask info", EX_SOFTWARE);
			/* NOTREACHED */
		}
		memset(symbol->info.minfo, 0, sizeof(struct mask_info));
		SLIST_INIT(&(symbol->info.minfo->symrefs));
		break;
        case CONST:
        case DOWNLOAD_CONST:
		symbol->info.cinfo =
		    (struct const_info *)malloc(sizeof(struct const_info));
		if (symbol->info.cinfo == NULL) {
			stop("Can't create alias info", EX_SOFTWARE);
			/* NOTREACHED */
		}
		memset(symbol->info.cinfo, 0,
		       sizeof(struct const_info));
		break;
	case LABEL:
		symbol->info.linfo =
		    (struct label_info *)malloc(sizeof(struct label_info));
		if (symbol->info.linfo == NULL) {
			stop("Can't create label info", EX_SOFTWARE);
			/* NOTREACHED */
		}
		memset(symbol->info.linfo, 0,
		       sizeof(struct label_info));
		break;
	case CONDITIONAL:
		symbol->info.condinfo =
		    (struct cond_info *)malloc(sizeof(struct cond_info));
		if (symbol->info.condinfo == NULL) {
			stop("Can't create conditional info", EX_SOFTWARE);
			/* NOTREACHED */
		}
		memset(symbol->info.condinfo, 0,
		       sizeof(struct cond_info));
		break;
	default:
		stop("Call to initialize_symbol with invalid symbol type",
		     EX_SOFTWARE);
		/* NOTREACHED */
		break;
	}
}

static void
process_register(symbol_t **p_symbol)
{
	char buf[255];
	symbol_t *symbol = *p_symbol;

	if (symbol->type == UNINITIALIZED) {
		snprintf(buf, sizeof(buf), "Undefined register %s",
			 symbol->name);
		stop(buf, EX_DATAERR);
		/* NOTREACHED */
	} else if (symbol->type == ALIAS) {
		*p_symbol = symbol->info.ainfo->parent;
	} else if ((symbol->type != REGISTER)
		&& (symbol->type != SCBLOC)
		&& (symbol->type != SRAMLOC)) {
		snprintf(buf, sizeof(buf),
			 "Specified symbol %s is not a register",
			 symbol->name);
		stop(buf, EX_DATAERR);
	}
}

static void
format_1_instr(int opcode, symbol_ref_t *dest, expression_t *immed,
	       symbol_ref_t *src, int ret)
{
	struct instruction *instr;
	struct ins_format1 *f1_instr;

	if (src->symbol == NULL)
		src = dest;

	/* Test register permissions */
	test_writable_symbol(dest->symbol);
	test_readable_symbol(src->symbol);

	/* Ensure that immediate makes sense for this destination */
	type_check(dest->symbol, immed, opcode);

	/* Allocate sequencer space for the instruction and fill it out */
	instr = seq_alloc();
	f1_instr = &instr->format.format1;
	f1_instr->ret = ret ? 1 : 0;
	f1_instr->opcode = opcode;
	f1_instr->destination = dest->symbol->info.rinfo->address
			      + dest->offset;
	f1_instr->source = src->symbol->info.rinfo->address
			 + src->offset;
	f1_instr->immediate = immed->value;

	if (is_download_const(immed))
		f1_instr->parity = 1;

	symlist_free(&immed->referenced_syms);
	instruction_ptr++;
}

static void
format_2_instr(int opcode, symbol_ref_t *dest, expression_t *places,
	       symbol_ref_t *src, int ret)
{
	struct instruction *instr;
	struct ins_format2 *f2_instr;
	uint8_t shift_control;

	if (src->symbol == NULL)
		src = dest;

	/* Test register permissions */
	test_writable_symbol(dest->symbol);
	test_readable_symbol(src->symbol);

	/* Allocate sequencer space for the instruction and fill it out */
	instr = seq_alloc();
	f2_instr = &instr->format.format2;
	f2_instr->ret = ret ? 1 : 0;
	f2_instr->opcode = AIC_OP_ROL;
	f2_instr->destination = dest->symbol->info.rinfo->address
			      + dest->offset;
	f2_instr->source = src->symbol->info.rinfo->address
			 + src->offset;
	if (places->value > 8 || places->value <= 0) {
		stop("illegal shift value", EX_DATAERR);
		/* NOTREACHED */
	}
	switch (opcode) {
	case AIC_OP_SHL:
		if (places->value == 8)
			shift_control = 0xf0;
		else
			shift_control = (places->value << 4) | places->value;
		break;
	case AIC_OP_SHR:
		if (places->value == 8) {
			shift_control = 0xf8;
		} else {
			shift_control = (places->value << 4)
				      | (8 - places->value)
				      | 0x08;
		}
		break;
	case AIC_OP_ROL:
		shift_control = places->value & 0x7;
		break;
	case AIC_OP_ROR:
		shift_control = (8 - places->value) | 0x08;
		break;
	default:
		shift_control = 0; /* Quiet Compiler */
		stop("Invalid shift operation specified", EX_SOFTWARE);
		/* NOTREACHED */
		break;
	};
	f2_instr->shift_control = shift_control;
	symlist_free(&places->referenced_syms);
	instruction_ptr++;
}

static void
format_3_instr(int opcode, symbol_ref_t *src,
	       expression_t *immed, symbol_ref_t *address)
{
	struct instruction *instr;
	struct ins_format3 *f3_instr;
	int addr;

	/* Test register permissions */
	test_readable_symbol(src->symbol);

	/* Ensure that immediate makes sense for this source */
	type_check(src->symbol, immed, opcode);

	/* Allocate sequencer space for the instruction and fill it out */
	instr = seq_alloc();
	f3_instr = &instr->format.format3;
	if (address->symbol == NULL) {
		/* 'dot' referrence.  Use the current instruction pointer */
		addr = instruction_ptr + address->offset;
	} else if (address->symbol->type == UNINITIALIZED) {
		/* forward reference */
		addr = address->offset;
		instr->patch_label = address->symbol;
	} else
		addr = address->symbol->info.linfo->address + address->offset;
	f3_instr->opcode = opcode;
	f3_instr->address = addr;
	f3_instr->source = src->symbol->info.rinfo->address
			 + src->offset;
	f3_instr->immediate = immed->value;

	if (is_download_const(immed))
		f3_instr->parity = 1;

	symlist_free(&immed->referenced_syms);
	instruction_ptr++;
}

static void
test_readable_symbol(symbol_t *symbol)
{
	if (symbol->info.rinfo->mode == WO) {
		stop("Write Only register specified as source",
		     EX_DATAERR);
		/* NOTREACHED */
	}
}

static void
test_writable_symbol(symbol_t *symbol)
{
	if (symbol->info.rinfo->mode == RO) {
		stop("Read Only register specified as destination",
		     EX_DATAERR);
		/* NOTREACHED */
	}
}

static void
type_check(symbol_t *symbol, expression_t *expression, int opcode)
{
	symbol_node_t *node;
	int and_op;
	char buf[255];

	and_op = FALSE;
	if (opcode == AIC_OP_AND || opcode == AIC_OP_JNZ || AIC_OP_JZ)
		and_op = TRUE;

	/*
	 * Make sure that we aren't attempting to write something
	 * that hasn't been defined.  If this is an and operation,
	 * this is a mask, so "undefined" bits are okay.
	 */
	if (and_op == FALSE
	 && (expression->value & ~symbol->info.rinfo->valid_bitmask) != 0) {
		snprintf(buf, sizeof(buf),
			 "Invalid bit(s) 0x%x in immediate written to %s",
			 expression->value & ~symbol->info.rinfo->valid_bitmask,
			 symbol->name);
		stop(buf, EX_DATAERR);
		/* NOTREACHED */
	}

	/*
	 * Now make sure that all of the symbols referenced by the
	 * expression are defined for this register.
	 */
	if(symbol->info.rinfo->typecheck_masks != FALSE) {
		for(node = expression->referenced_syms.slh_first;
		    node != NULL;
		    node = node->links.sle_next) {
			if ((node->symbol->type == MASK
			  || node->symbol->type == BIT)
			 && symlist_search(&node->symbol->info.minfo->symrefs,
					   symbol->name) == NULL) {
				snprintf(buf, sizeof(buf),
					 "Invalid bit or mask %s "
					 "for register %s",
					 node->symbol->name, symbol->name);
				stop(buf, EX_DATAERR);
				/* NOTREACHED */
			}
		}
	}
}

static void
make_expression(expression_t *immed, int value)
{
	SLIST_INIT(&immed->referenced_syms);
	immed->value = value & 0xff;
}

static void
add_conditional(symbol_t *symbol)
{
	static int numfuncs;

	if (numfuncs == 0) {
		/* add a special conditional, "0" */
		symbol_t *false_func;

		false_func = symtable_get("0");
		if (false_func->type != UNINITIALIZED) {
			stop("Conditional expression '0' "
			     "conflicts with a symbol", EX_DATAERR);
			/* NOTREACHED */
		}
		false_func->type = CONDITIONAL;
		initialize_symbol(false_func);
		false_func->info.condinfo->func_num = numfuncs++;
		symlist_add(&patch_functions, false_func, SYMLIST_INSERT_HEAD);
	}

	/* This condition has occurred before */
	if (symbol->type == CONDITIONAL)
		return;

	if (symbol->type != UNINITIALIZED) {
		stop("Conditional expression conflicts with a symbol",
		     EX_DATAERR);
		/* NOTREACHED */
	}

	symbol->type = CONDITIONAL;
	initialize_symbol(symbol);
	symbol->info.condinfo->func_num = numfuncs++;
	symlist_add(&patch_functions, symbol, SYMLIST_INSERT_HEAD);
}

static void
add_version(const char *verstring)
{
	const char prefix[] = " * ";
	int newlen;
	int oldlen;

	newlen = strlen(verstring) + strlen(prefix);
	oldlen = 0;
	if (versions != NULL)
		oldlen = strlen(versions);
	versions = realloc(versions, newlen + oldlen + 2);
	if (versions == NULL)
		stop("Can't allocate version string", EX_SOFTWARE);
	strcpy(&versions[oldlen], prefix);
	strcpy(&versions[oldlen + strlen(prefix)], verstring);
	versions[newlen + oldlen] = '\n';
	versions[newlen + oldlen + 1] = '\0';
}

void
yyerror(const char *string)
{
	stop(string, EX_DATAERR);
}

static int
is_download_const(expression_t *immed)
{
	if ((immed->referenced_syms.slh_first != NULL)
	 && (immed->referenced_syms.slh_first->symbol->type == DOWNLOAD_CONST))
		return (TRUE);

	return (FALSE);
}
