/*
 * tkcond.c
 *
 * Eric Youngdale was the original author of xconfig.
 * Michael Elizabeth Chastain (mec@shout.net) is the current maintainer.
 *
 * This file takes the tokenized statement list and transforms 'if ...'
 * statements.  For each simple statement, I find all of the 'if' statements
 * that enclose it, and attach the aggregate conditionals of those 'if'
 * statements to the cond list of the simple statement.
 *
 * 14 January 1999, Michael Elizabeth Chastain, <mec@shout.net>
 * - Steam-clean this file.  I tested this by generating kconfig.tk for
 *   every architecture and comparing it character-for-character against
 *   the output of the old tkparse.
 *
 * TO DO:
 * - xconfig is at the end of its life cycle.  Contact <mec@shout.net> if
 *   you are interested in working on the replacement.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tkparse.h"



/*
 * Transform op_variable to op_kvariable.
 *
 * This works, but it's gross, speed-wise.  It would benefit greatly
 * from a simple hash table that maps names to cfg.
 *
 * Note well: this is actually better than the loop structure xconfig
 * has been staggering along with for three years, which performs
 * this whole procedure inside *another* loop on active conditionals.
 */
void transform_to_kvariable( struct kconfig * scfg )
{
    struct kconfig * cfg;

    for ( cfg = scfg; cfg != NULL; cfg = cfg->next )
    {
	struct condition * cond;

	for ( cond = cfg->cond; cond != NULL; cond = cond->next )
	{
	    if ( cond->op == op_variable )
	    {
		/* Here's where it gets DISGUSTING. */
		struct kconfig * cfg1;

		for ( cfg1 = scfg; cfg1 != NULL; cfg1 = cfg1->next )
		{
		    if ( cfg1->token == token_bool
		    ||   cfg1->token == token_choice_item
		    ||   cfg1->token == token_dep_tristate
		    ||   cfg1->token == token_hex
		    ||   cfg1->token == token_int
		    ||   cfg1->token == token_string
		    ||   cfg1->token == token_tristate )
		    {
			if ( strcmp( cond->str, cfg1->optionname ) == 0 )
			{
			    cond->op  = op_kvariable;
			    cond->str = NULL;
			    cond->cfg = cfg1;
			    break;
			}
		    }
		}
	    }

#if 0
	    /*
	     * Maybe someday this will be useful, but right now it
	     * gives a lot of false positives on files like
	     * drivers/video/Config.in that are meant for more
	     * than one architecture.  Turn it on if you want to play
	     * with it though; it does work.  -- mec
	     */
	    if ( cond->op == op_variable )
	    {
		if ( strcmp( cond->str, "ARCH"       ) != 0
		&&   strcmp( cond->str, "CONSTANT_Y" ) != 0
		&&   strcmp( cond->str, "CONSTANT_M" ) != 0
		&&   strcmp( cond->str, "CONSTANT_N" ) != 0 )
		{
		    fprintf( stderr, "warning: $%s used but not defined\n",
			cond->str );
		}
	    }
#endif
	}
    }
}



/*
 * Make a new condition chain by joining the current condition stack with
 * the "&&" operator for glue.
 */
struct condition * join_condition_stack( struct condition * conditions [],
    int depth )
{
    struct condition * cond_list;
    struct condition * cond_last;
    int i;

    cond_list = cond_last = NULL;
    for ( i = 0; i < depth; i++ )
    {
	struct condition * cond;
	struct condition * cnew;

	/* add a '(' */
	cnew = malloc( sizeof(*cnew) );
	memset( cnew, 0, sizeof(*cnew) );
	cnew->op = op_lparen;
	if ( cond_last == NULL )
	    { cond_list = cond_last = cnew; }
	else
	    { cond_last->next = cnew; cond_last = cnew; }

	/* duplicate the chain */
	for ( cond = conditions [i]; cond != NULL; cond = cond->next )
	{
	    cnew            = malloc( sizeof(*cnew) );
	    cnew->next      = NULL;
	    cnew->op        = cond->op;
	    cnew->str       = cond->str ? strdup( cond->str ) : NULL;
	    cnew->cfg       = cond->cfg;
	    cond_last->next = cnew;
	    cond_last       = cnew;
	}

	/* add a ')' */
	cnew = malloc( sizeof(*cnew) );
	memset( cnew, 0, sizeof(*cnew) );
	cnew->op = op_rparen;
	cond_last->next = cnew;
	cond_last = cnew;

	/* if i have another condition, add an '&&' operator */
	if ( i < depth - 1 )
	{
	    cnew = malloc( sizeof(*cnew) );
	    memset( cnew, 0, sizeof(*cnew) );
	    cnew->op = op_and;
	    cond_last->next = cnew;
	    cond_last = cnew;
	}
    }

    /*
     * Remove duplicate conditions.
     */
    {
	struct condition *cond1, *cond1b, *cond1c, *cond1d, *cond1e, *cond1f;

	for ( cond1 = cond_list; cond1 != NULL; cond1 = cond1->next )
	{
	    if ( cond1->op == op_lparen )
	    {
		cond1b = cond1 ->next; if ( cond1b == NULL ) break;
		cond1c = cond1b->next; if ( cond1c == NULL ) break;
		cond1d = cond1c->next; if ( cond1d == NULL ) break;
		cond1e = cond1d->next; if ( cond1e == NULL ) break;
		cond1f = cond1e->next; if ( cond1f == NULL ) break;

		if ( cond1b->op == op_kvariable
		&& ( cond1c->op == op_eq || cond1c->op == op_neq )
		&&   cond1d->op == op_constant 
		&&   cond1e->op == op_rparen )
		{
		    struct condition *cond2, *cond2b, *cond2c, *cond2d, *cond2e, *cond2f;

		    for ( cond2 = cond1f->next; cond2 != NULL; cond2 = cond2->next )
		    {
			if ( cond2->op == op_lparen )
			{
			    cond2b = cond2 ->next; if ( cond2b == NULL ) break;
			    cond2c = cond2b->next; if ( cond2c == NULL ) break;
			    cond2d = cond2c->next; if ( cond2d == NULL ) break;
			    cond2e = cond2d->next; if ( cond2e == NULL ) break;
			    cond2f = cond2e->next;

			    /* look for match */
			    if ( cond2b->op == op_kvariable
			    &&   cond2b->cfg == cond1b->cfg
			    &&   cond2c->op == cond1c->op
			    &&   cond2d->op == op_constant
			    &&   strcmp( cond2d->str, cond1d->str ) == 0
			    &&   cond2e->op == op_rparen )
			    {
				/* one of these must be followed by && */
				if ( cond1f->op == op_and
				|| ( cond2f != NULL && cond2f->op == op_and ) )
				{
				    /* nuke the first duplicate */
				    cond1 ->op = op_nuked;
				    cond1b->op = op_nuked;
				    cond1c->op = op_nuked;
				    cond1d->op = op_nuked;
				    cond1e->op = op_nuked;
				    if ( cond1f->op == op_and )
					cond1f->op = op_nuked;
				    else
					cond2f->op = op_nuked;
				}
			    }
			}
		    }
		}
	    }
	}
    }

    return cond_list;
}



/*
 * This is the main transformation function.
 */
void fix_conditionals( struct kconfig * scfg )
{
    struct kconfig * cfg;

    /*
     * Transform op_variable to op_kvariable.
     */
    transform_to_kvariable( scfg );

    /*
     * Transform conditions that use variables from "choice" statements.
     * Choice values appear to the user as a collection of booleans, and the
     * script can test the individual booleans.  But internally, all I have is
     * the N-way value of an unnamed temporary for the whole statement.  So I
     * have to tranform '"$CONFIG_M386" != "y"'
     * into '"$tmpvar_N" != "CONFIG_M386"'.
     */
    for ( cfg = scfg; cfg != NULL; cfg = cfg->next )
    {
	struct condition * cond;

	for ( cond = cfg->cond; cond != NULL; cond = cond->next )
	{
	    if ( cond->op == op_kvariable && cond->cfg->token == token_choice_item )
	    {
		/*
		 * Look two more tokens down for the comparison token.
		 * It has to be "y" for this trick to work.
		 *
		 * If you get this error, don't even think about relaxing the
		 * strcmp test.  You will produce incorrect TK code.  Instead,
		 * look for the place in your Config.in script where you are
		 * comparing a 'choice' variable to a value other than 'y',
		 * and rewrite the comparison to be '= "y"' or '!= "y"'.
		 */
		struct condition * cond2 = cond->next->next;
		const char * label;

		if ( strcmp( cond2->str, "y" ) != 0 )
		{
		    fprintf( stderr, "tkparse choked in fix_choice_cond\n" );
		    exit( 1 );
		}

		label = cond->cfg->label;
		cond->cfg  = cond->cfg->cfg_parent;
		cond2->str = strdup( label );
	    }
	}
    }

    /*
     * Walk the statement list, maintaining a stack of current conditions.
     *   token_if      push its condition onto the stack.
     *   token_else    invert the condition on the top of the stack.
     *   token_endif   pop the stack.
     *
     * For a simple statement, create a condition chain by joining together
     * all of the conditions on the stack.
     */
    {
	struct condition * cond_stack [32];
	int depth = 0;

	for ( cfg = scfg; cfg != NULL; cfg = cfg->next )
	{
	    switch ( cfg->token )
	    {
	    default:
		break;

	    case token_if:
		cond_stack [depth++] = cfg->cond;
		cfg->cond = NULL;
		break;

	    case token_else:
		{
		    /*
		     * Invert the condition chain.
		     *
		     * Be careful to transfrom op_or to op_and1, not op_and.
		     * I will need this later in the code that removes
		     * duplicate conditions.
		     */
		    struct condition * cond;

		    for ( cond  = cond_stack [depth-1];
			  cond != NULL;
			  cond  = cond->next )
		    {
			switch( cond->op )
			{
			default:     break;
			case op_and: cond->op = op_or;   break;
			case op_or:  cond->op = op_and1; break;
			case op_neq: cond->op = op_eq;   break;
			case op_eq:  cond->op = op_neq;  break;
			}
		    }
		}
		break;

	    case token_fi:
		--depth;
		break;

	    case token_bool:
	    case token_choice_item:
	    case token_comment:
	    case token_define_bool:
	    case token_hex:
	    case token_int:
	    case token_mainmenu_option:
	    case token_string:
	    case token_tristate:
		cfg->cond = join_condition_stack( cond_stack, depth );
		break;

	    case token_dep_tristate:
		/*
		 * Same as the other simple statements, plus an additional
		 * condition for the dependency.
		 */
		cond_stack [depth] = cfg->cond;
		cfg->cond = join_condition_stack( cond_stack, depth+1 );
		break;
	    }
	}
    }
}
