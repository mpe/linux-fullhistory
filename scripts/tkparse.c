/*
 * tkparse.c
 *
 * Eric Youngdale was the original author of xconfig.
 * Michael Elizabeth Chastain (mec@shout.net) is the current maintainer.
 *
 * Parse a config.in file and translate it to a wish script.
 * This task has three parts:
 *
 *   tkparse.c	tokenize the input
 *   tkcond.c   transform 'if ...' statements
 *   tkgen.c    generate output
 *
 * Change History
 *
 * 7 January 1999, Michael Elizabeth Chastain, <mec@shout.net>
 * - Teach dep_tristate about a few literals, such as:
 *     dep_tristate 'foo' CONFIG_FOO m
 *   Also have it print an error message and exit on some parse failures.
 *
 * 14 January 1999, Michael Elizabeth Chastain, <mec@shout.net>
 * - Don't fclose stdin.  Thanks to Tony Hoyle for nailing this one.
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

static struct kconfig * config_list = NULL;
static struct kconfig * config_last = NULL;
static const char * current_file = "<unknown file>";
static int lineno = 0;

static void do_source( const char * );

#undef strcmp
int my_strcmp( const char * s1, const char * s2 ) { return strcmp( s1, s2 ); }
#define strcmp my_strcmp



/*
 * Report a syntax error.
 */
static void syntax_error( const char * msg )
{
    fprintf( stderr, "%s: %d: %s\n", current_file, lineno, msg );
    exit( 1 );
}



/*
 * Get a string.
 */
static const char * get_string( const char * pnt, char ** label )
{
    const char * word;

    word = pnt;
    for ( ; ; )
    {
	if ( *pnt == '\0' || *pnt == ' ' || *pnt == '\t' )
	    break;
	pnt++;
    }

    *label = malloc( pnt - word + 1 );
    memcpy( *label, word, pnt - word );
    (*label)[pnt - word] = '\0';

    if ( *pnt != '\0' )
	pnt++;
    return pnt;
}



/*
 * Get a quoted string.
 * Insert a '\' before any characters that need quoting.
 */
static const char * get_qstring( const char * pnt, char ** label )
{
    char quote_char;
    char newlabel [1024];
    char * pnt1;

    /* advance to the open quote */
    for ( ; ; )
    {
	if ( *pnt == '\0' )
	    return pnt;
	quote_char = *pnt++;
	if ( quote_char == '"' || quote_char == '\'' )
	    break;
    }

    /* copy into an intermediate buffer */
    pnt1 = newlabel;
    for ( ; ; )
    {
	if ( *pnt == '\0' )
	    syntax_error( "unterminated quoted string" );
	if ( *pnt == quote_char && pnt[-1] != '\\' )
	    break;

	/* copy the character, quoting if needed */
	if ( *pnt == '"' || *pnt == '\'' || *pnt == '[' || *pnt == ']' )
	    *pnt1++ = '\\';
	*pnt1++ = *pnt++;
    }

    /* copy the label into a permanent location */
    *pnt1++ = '\0';
    *label = (char *) malloc( pnt1 - newlabel );
    memcpy( *label, newlabel, pnt1 - newlabel );

    /* skip over last quote and next whitespace */
    pnt++;
    while ( *pnt == ' ' || *pnt == '\t' )
	pnt++;
    return pnt;
}


/*
 * Tokenize an 'if' statement condition.
 */
static struct condition * tokenize_if( const char * pnt )
{
    struct condition * list;
    struct condition * last;

    /* eat the open bracket */
    while ( *pnt == ' ' || *pnt == '\t' )
	pnt++;
    if ( *pnt != '[' )
	syntax_error( "bad 'if' condition" );
    pnt++;

    list = last = NULL;
    for ( ; ; )
    {
	struct condition * cond;

	/* advance to the next token */
	while ( *pnt == ' ' || *pnt == '\t' )
	    pnt++;
	if ( *pnt == '\0' )
	    syntax_error( "unterminated 'if' condition" );
	if ( *pnt == ']' )
	    return list;

	/* allocate a new token */
	cond = malloc( sizeof(*cond) );
	memset( cond, 0, sizeof(*cond) );
	if ( last == NULL )
	    { list = last = cond; }
	else
	    { last->next = cond; last = cond; }

	/* determine the token value */
	if ( *pnt == '-' && pnt[1] == 'a' )
	    { cond->op = op_and;  pnt += 2; continue; }

	if ( *pnt == '-' && pnt[1] == 'o' )
	    { cond->op = op_or;   pnt += 2; continue; }

	if ( *pnt == '!' && pnt[1] == '=' )
	    { cond->op = op_neq;  pnt += 2; continue; }

	if ( *pnt == '=' )
	    { cond->op = op_eq;   pnt += 1; continue; }

	if ( *pnt == '!' )
	    { cond->op = op_bang; pnt += 1; continue; }

	if ( *pnt == '"' )
	{
	    const char * word;

	    /* advance to the word */
	    pnt++;
	    if ( *pnt == '$' )
		{ cond->op = op_variable; pnt++; }
	    else
		{ cond->op = op_constant; }

	    /* find the end of the word */
	    word = pnt;
	    for ( ; ; )
	    {
		if ( *pnt == '\0' )
		    syntax_error( "unterminated double quote" );
		if ( *pnt == '"' )
		    break;
		pnt++;
	    }

	    /* store a copy of this word */
	    {
		char * str = malloc( pnt - word + 1 );
		memcpy( str, word, pnt - word );
		str [pnt - word] = '\0';
		cond->str = str;
	    }

	    pnt++;
	    continue;
	}

	/* unknown token */
	syntax_error( "bad if condition" );
    }
}



/*
 * Tokenize a choice list.  Choices appear as pairs of strings;
 * note that I am parsing *inside* the double quotes.  Ugh.
 */
static const char * tokenize_choices( struct kconfig * cfg_choose,
    const char * pnt )
{
    for ( ; ; )
    {
	struct kconfig * cfg;

	/* skip whitespace */
	while ( *pnt == ' ' || *pnt == '\t' )
	    pnt++;
	if ( *pnt == '\0' )
	    return pnt;

	/* allocate a new kconfig line */
	cfg = malloc( sizeof(*cfg) );
	memset( cfg, 0, sizeof(*cfg) );
	if ( config_last == NULL )
	    { config_last = config_list = cfg; }
	else
	    { config_last->next = cfg; config_last = cfg; }

	/* fill out the line */
	cfg->token      = token_choice_item;
	cfg->cfg_parent = cfg_choose;
	pnt = get_string( pnt, &cfg->label );
	while ( *pnt == ' ' || *pnt == '\t' )
	    pnt++;
	pnt = get_string( pnt, &cfg->optionname );
    }

    return pnt;
}





/*
 * Tokenize one line.
 */
static void tokenize_line( const char * pnt )
{
    static struct kconfig * last_menuoption = NULL;
    enum e_token token;
    struct kconfig * cfg;

    /* skip white space */
    while ( *pnt == ' ' || *pnt == '\t' )
	pnt++;

    /*
     * categorize the next token
     */

#define match_token(t, s) \
    if (strncmp(pnt, s, strlen(s)) == 0) { token = t; pnt += strlen(s); break; }

    token = token_UNKNOWN;
    switch ( *pnt )
    {
    default:
	break;

    case '#':
    case '\0':
	return;

    case 'b':
	match_token( token_bool, "bool" );
	break;

    case 'c':
	match_token( token_choice_header, "choice"  );
	match_token( token_comment, "comment" );
	break;

    case 'd':
	match_token( token_define_bool, "define_bool" );
	match_token( token_dep_tristate, "dep_tristate" );
	break;

    case 'e':
	match_token( token_else, "else" );
	match_token( token_endmenu, "endmenu" );
	break;

    case 'f':
	match_token( token_fi, "fi" );
	break;

    case 'h':
	match_token( token_hex, "hex" );
	break;

    case 'i':
	match_token( token_if, "if" );
	match_token( token_int, "int" );
	break;

    case 'm':
	match_token( token_mainmenu_name, "mainmenu_name" );
	match_token( token_mainmenu_option, "mainmenu_option" );
	break;

    case 's':
	match_token( token_source, "source" );
	match_token( token_string, "string" );
	break;

    case 't':
	match_token( token_then, "then" );
	match_token( token_tristate, "tristate" );
	break;

    case 'u':
	match_token( token_unset, "unset" );
	break;
    }

#undef match_token

    if ( token == token_source )
    {
	while ( *pnt == ' ' || *pnt == '\t' )
	    pnt++;
	do_source( pnt );
	return;
    }

    if ( token == token_then )
    {
	if ( config_last != NULL && config_last->token == token_if )
	    return;
	syntax_error( "bogus 'then'" );
    }

    if ( token == token_unset )
    {
	fprintf( stderr, "Ignoring 'unset' command\n" );
	return;
    }

    if ( token == token_UNKNOWN )
	syntax_error( "unknown command" );

    /*
     * Allocate an item.
     */
    cfg = malloc( sizeof(*cfg) );
    memset( cfg, 0, sizeof(*cfg) );
    if ( config_last == NULL )
	{ config_last = config_list = cfg; }
    else
	{ config_last->next = cfg; config_last = cfg; }

    /*
     * Tokenize the arguments.
     */
    while ( *pnt == ' ' || *pnt == '\t' )
	pnt++;

    cfg->token = token;
    switch ( token )
    {
    default:
	syntax_error( "unknown token" );

    case token_bool:
    case token_tristate:
	pnt = get_qstring ( pnt, &cfg->label      );
	pnt = get_string  ( pnt, &cfg->optionname );
	break;

    case token_choice_header:
	{
	    static int choose_number = 0;
	    char * choice_list;

	    pnt = get_qstring ( pnt, &cfg->label  );
	    pnt = get_qstring ( pnt, &choice_list );
	    pnt = get_string  ( pnt, &cfg->value  );

	    cfg->optionname = malloc( 32 );
	    sprintf( cfg->optionname, "tmpvar_%d", choose_number++ );

	    tokenize_choices( cfg, choice_list );
	    free( choice_list );
	}
	break;

    case token_comment:
	pnt = get_qstring(pnt, &cfg->label);
	if ( last_menuoption != NULL )
	{
	    pnt = get_qstring(pnt, &cfg->label);
	    last_menuoption->label = cfg->label;
	    last_menuoption = NULL;
	}
	break;

    case token_define_bool:
	pnt = get_string( pnt, &cfg->optionname );
#if ! defined(BUG_COMPATIBLE)
	while ( *pnt == ' ' || *pnt == '\t' )
	    pnt++;
#endif
	if      ( *pnt == 'n' || *pnt == 'N' ) cfg->value = "0";
	else if ( *pnt == 'y' || *pnt == 'Y' ) cfg->value = "1";
	else if ( *pnt == 'm' || *pnt == 'M' ) cfg->value = "2";
	else
	{
#if ! defined(BUG_COMPATIBLE)
	    syntax_error( "unknown define_bool value" );
#else
	    /*
	     * This ought to give the same output as printf'ing
	     * through the null pointer ... I don't want to be
	     * SIGSEGV compatible!
	     */
	    cfg->value = "(null)";
#endif
	}
	break;

    case token_dep_tristate:
	pnt = get_qstring ( pnt, &cfg->label      );
	pnt = get_string  ( pnt, &cfg->optionname );

	while ( *pnt == ' ' || *pnt == '\t' )
	    pnt++;

	if ( ( pnt[0] == 'Y'  || pnt[0] == 'M' || pnt[0] == 'N'
	||     pnt[0] == 'y'  || pnt[0] == 'm' || pnt[0] == 'n'  )
	&&   ( pnt[1] == '\0' || pnt[1] == ' ' || pnt[1] == '\t' ) )
	{
	    /* dep_tristate 'foo' CONFIG_FOO m */
	    if      ( pnt[0] == 'Y' || pnt[0] == 'y' )
		cfg->depend = strdup( "CONSTANT_Y" );
	    else if ( pnt[0] == 'M' || pnt[0] == 'm' )
		cfg->depend = strdup( "CONSTANT_M" );
	    else
		cfg->depend = strdup( "CONSTANT_N" );
	    pnt++;
	}
	else if ( *pnt == '$' )
	{
	    pnt++;
	    pnt = get_string( pnt, &cfg->depend );
	}
	else
	{
	    syntax_error( "can't handle dep_tristate condition" );
	}

	/*
	 * Create a conditional for this object's dependency.
	 */
	{
	    char fake_if [1024];
	    sprintf( fake_if, "[ \"$%s\" = \"y\" -o \"$%s\" = \"m\" ]; then",
		cfg->depend, cfg->depend );
	    cfg->cond = tokenize_if( fake_if );
	}
	break;

    case token_else:
    case token_endmenu:
    case token_fi:
	break;

    case token_hex:
    case token_int:
    case token_string:
	pnt = get_qstring ( pnt, &cfg->label      );
	pnt = get_string  ( pnt, &cfg->optionname );
	pnt = get_string  ( pnt, &cfg->value      );
	break;

    case token_if:
	cfg->cond = tokenize_if( pnt );
	break;

    case token_mainmenu_name:
	pnt = get_qstring( pnt, &cfg->label );
	break;

    case token_mainmenu_option:
	if ( strncmp( pnt, "next_comment", 12 ) == 0 )
	    last_menuoption = cfg;
	else
	    pnt = get_qstring( pnt, &cfg->label );
	break;
    }

    return;
}



/*
 * Implement the "source" command.
 */
static void do_source( const char * filename )
{
    char buffer [1024];
    FILE * infile;
    const char * old_file;
    int old_lineno;
    int offset;

    /* open the file */
    if ( strcmp( filename, "-" ) == 0 )
	infile = stdin;
    else
	infile = fopen( filename, "r" );

    /* if that failed, try ../filename */
    if ( infile == NULL )
    {
	sprintf( buffer, "../%s", filename );
	infile = fopen( buffer, "r" );
    }

    if ( infile == NULL )
    {
	sprintf( buffer, "unable to open %s", filename );
#if defined(BUG_COMPATIBLE)
	fprintf( stderr, "%s\n", buffer );
	return;
#else
	syntax_error( buffer );
#endif
    }

    /* push the new file name and line number */
    old_file     = current_file;
    old_lineno   = lineno;
    current_file = filename;
    lineno       = 0;

    /* read and process lines */
    for ( offset = 0; ; )
    {
	char * pnt;

	/* read a line */
	fgets( buffer + offset, sizeof(buffer) - offset, infile );
	if ( feof( infile ) )
	    break;
	lineno++;

	/* strip the trailing return character */
	pnt = buffer + strlen(buffer) - 1;
	if ( *pnt == '\n' )
	    *pnt-- = '\0';

	/* eat \ NL pairs */
	if ( *pnt == '\\' )
	{
	    offset = pnt - buffer;
	    continue;
	}

	/* tokenize this line */
	tokenize_line( buffer );
	offset = 0;
    }

    /* that's all, folks */
    if ( infile != stdin )
	fclose( infile );
    current_file = old_file;
    lineno       = old_lineno;
    return;
}



/*
 * Main program.
 */
int main( int argc, const char * argv [] )
{
    do_source        ( "-"         );
    fix_conditionals ( config_list );
    dump_tk_script   ( config_list );
    return 0;
}
