/* parser config.in
 *
 * Version 1.0
 * Eric Youngdale
 * 10/95
 *
 * The general idea here is that we want to parse a config.in file and 
 * from this, we generate a wish script which gives us effectively the
 * same functionality that the original config.in script provided.
 *
 * This task is split roughly into 3 parts.  The first parse is the parse
 * of the input file itself.  The second part is where we analyze the 
 * #ifdef clauses, and attach a linked list of tokens to each of the
 * menu items.  In this way, each menu item has a complete list of
 * dependencies that are used to enable/disable the options.
 * The third part is to take the configuration database we have build,
 * and build the actual wish script.
 *
 * This file contains the code to do the first parse of config.in.
 */
#include <stdio.h>
#include <string.h>
#include "tkparse.h"

struct kconfig * config = NULL;
struct kconfig * clast = NULL;
struct kconfig * koption = NULL;
static int lineno = 0;
static int menus_seen = 0;
static char * current_file = NULL;
static int do_source(char * filename);
/*
 * Simple function just to skip over spaces and tabs in config.in.
 */
static char * skip_whitespace(char * pnt)
{
  while( *pnt && (*pnt == ' ' || *pnt == '\t')) pnt++;
  return pnt;
}

/*
 * This function parses a conditional from a config.in (i.e. from an ifdef)
 * and generates a linked list of tokens that describes the conditional.
 */
static struct condition * parse_if(char * pnt)
{
  char * opnt;
  struct condition *list;
  struct condition *last;
  struct condition *cpnt;
  char varname[64];
  char * pnt1;

  opnt = pnt;

  /*
   * We need to find the various tokens, and build the linked list.
   */
  pnt = skip_whitespace(pnt);
  if( *pnt != '[' ) return NULL;
  pnt++;
  pnt = skip_whitespace(pnt);

  list = last = NULL;
  while(*pnt && *pnt != ']') {

    pnt = skip_whitespace(pnt);
    if(*pnt== '\0' || *pnt == ']') break;

    /*
     * Allocate memory for the token we are about to parse, and insert
     * it in the linked list.
     */
    cpnt = (struct condition *) malloc(sizeof(struct condition));
    memset(cpnt, 0, sizeof(struct condition));
    if( last == NULL )
      {
	list = last = cpnt;
      }
    else
      {
	last->next = cpnt;
	last = cpnt;
      }

    /*
     * Determine what type of operation this token represents.
     */
    if( *pnt == '-' && pnt[1] == 'a' )
      {
	cpnt->op = op_and;
	pnt += 2;
	continue;
      }

    if( *pnt == '!' && pnt[1] == '=' )
      {
	cpnt->op = op_neq;
	pnt += 2;
	continue;
      }

    if( *pnt == '=')
      {
	cpnt->op = op_eq;
	pnt += 1;
	continue;
      }

    if( *pnt == '!')
      {
	cpnt->op = op_bang;
	pnt += 1;
	continue;
      }

    if( *pnt != '"' ) goto error;  /* This cannot be right. */
    pnt++;
    if( *pnt == '$' )
      {
	cpnt->op = op_variable;
	pnt1 = varname;
	pnt++;
	while(*pnt && *pnt != '"') *pnt1++ = *pnt++;
	*pnt1++ = '\0';
	cpnt->variable = strdup(varname);
	if( *pnt == '"' ) pnt++;
	continue;
      }

    cpnt->op = op_constant;
    pnt1 = varname;
    while(*pnt && *pnt != '"') *pnt1++ = *pnt++;
    *pnt1++ = '\0';
    cpnt->variable = strdup(varname);
    if( *pnt == '"' ) pnt++;
    continue;
  }

  return list;

 error:
  if(current_file != NULL) 
    printf("Bad if clause at line %d(%s):%s\n", lineno, current_file, opnt);
  else
    printf("Bad if clause at line %d:%s\n", lineno, opnt);
  return NULL;
}

/*
 * This function looks for a quoted string, from the input buffer, and
 * returns a pointer to a copy of this string.  Any characters in
 * the string that need to be "quoted" have a '\' character inserted
 * in front - this way we can directly write these strings into
 * wish scripts.
 */
static char * get_qstring(char *pnt, char ** labl)
{
  char quotechar;
  char newlabel[1024];
  char * pnt1;
  char * pnt2;

  while( *pnt && *pnt != '"' && *pnt != '\'') pnt++;
  if (*pnt == '\0') return pnt;

  quotechar = *pnt++;
  pnt1 = newlabel;
  while(*pnt && *pnt != quotechar && pnt[-1] != '\\')
    {
      /*
       * Quote the character if we need to.
       */
      if( *pnt == '"' || *pnt == '\'' || *pnt == '[' || *pnt == ']')
	*pnt1++ = '\\';

      *pnt1++ = *pnt++;
    }
  *pnt1++ = '\0';

  pnt2 = (char *) malloc(strlen(newlabel) + 1);
  strcpy(pnt2, newlabel);
  *labl = pnt2;

  /*
   * Skip over last quote, and whitespace.
   */
  pnt++;
  pnt = skip_whitespace(pnt);
  return pnt;
}

/*
 * This function grabs one text token from the input buffer
 * and returns a pointer to a copy of just the identifier.
 * This can be either a variable name (i.e. CONFIG_NET),
 * or it could be the default value for the option.
 */
static char * get_string(char *pnt, char ** labl)
{
  char quotechar;
  char newlabel[1024];
  char * pnt1;
  char * pnt2;

  if (*pnt == '\0') return pnt;

  pnt1 = newlabel;
  while(*pnt && *pnt != ' ' && *pnt != '\t')
    {
      *pnt1++ = *pnt++;
    }
  *pnt1++ = '\0';

  pnt2 = (char *) malloc(strlen(newlabel) + 1);
  strcpy(pnt2, newlabel);
  *labl = pnt2;

  if( *pnt ) pnt++;
  return pnt;
}


/*
 * Top level parse function.  Input pointer is one complete line from config.in
 * and the result is that we create a token that describes this line
 * and insert it into our linked list.
 */
int parse(char * pnt) {
  enum token tok;
  struct kconfig * kcfg;
  /*
   * Ignore comments and leading whitespace.
   */

  pnt = skip_whitespace(pnt);
  while( *pnt && (*pnt == ' ' || *pnt == '\t')) pnt++;
  if(! *pnt ) return;
  if( *pnt == '#' ) return;

  /*
   * Now categorize the next token.
   */
  tok = tok_unknown;
  if      (strncmp(pnt, "mainmenu_name", 13) == 0) 
    {
      tok = tok_menuname;
      pnt += 13;
    }
  else if      (strncmp(pnt, "source", 6) == 0) 
    {
      pnt += 7;
      pnt = skip_whitespace(pnt);
      do_source(pnt);
      return;
    }
  else if (strncmp(pnt, "mainmenu_option", 15) == 0) 
    {
      menus_seen++;
      tok = tok_menuoption;
      pnt += 15;
    }
  else if (strncmp(pnt, "comment", 7) == 0) 
    {
      tok = tok_comment;
      pnt += 7;
    }
  else if (strncmp(pnt, "bool", 4) == 0) 
    {
      tok = tok_bool;
      pnt += 4;
    }
  else if (strncmp(pnt, "tristate", 8) == 0) 
    {
      tok = tok_tristate;
      pnt += 8;
    }
  else if (strncmp(pnt, "dep_tristate", 12) == 0) 
    {
      tok = tok_dep_tristate;
      pnt += 12;
    }
  else if (strncmp(pnt, "int", 3) == 0) 
    {
      tok = tok_int;
      pnt += 3;
    }
  else if (strncmp(pnt, "if", 2) == 0) 
    {
      tok = tok_if;
      pnt += 2;
    }
  else if (strncmp(pnt, "else", 4) == 0) 
    {
      tok = tok_else;
      pnt += 4;
    }
  else if (strncmp(pnt, "fi", 2) == 0) 
    {
      tok = tok_fi;
      pnt += 2;
    }

  if( tok == tok_unknown)
    {
      printf("unknown command=%s\n", pnt);
      return 1;
    }

  /*
   * Allocate memory for this item, and attach it to the end of the linked
   * list.
   */
  kcfg = (struct kconfig *) malloc(sizeof(struct kconfig));
  memset(kcfg, 0, sizeof(struct kconfig));
  kcfg->tok = tok;
  if( clast != NULL )
    {
      clast->next = kcfg;
      clast = kcfg;
    }
  else
    {
      clast = config = kcfg;
    }

  pnt = skip_whitespace(pnt);

  /*
   * Now parse the remaining parts of the option, and attach the results
   * to the structure.
   */
  switch (tok)
    {
    case tok_menuname:
      pnt = get_qstring(pnt, &kcfg->label);
      break;
    case tok_bool:
    case tok_tristate:
    case tok_int:
      pnt = get_qstring(pnt, &kcfg->label);
      pnt = get_string(pnt, &kcfg->optionname);
      pnt = get_string(pnt, &kcfg->dflt);
      break;
    case tok_dep_tristate:
      pnt = get_qstring(pnt, &kcfg->label);
      pnt = get_string(pnt, &kcfg->optionname);
      pnt = get_string(pnt, &kcfg->dflt);
      pnt = skip_whitespace(pnt);
      if( *pnt == '$') pnt++;
      pnt = get_string(pnt, &kcfg->depend.str);
      break;
    case tok_comment:
      pnt = get_qstring(pnt, &kcfg->label);
      if( koption != NULL )
	{
	  pnt = get_qstring(pnt, &kcfg->label);
	  koption->label = kcfg->label;
	  koption = NULL;
	}
      break;
    case tok_menuoption:
      if( strncmp(pnt, "next_comment", 12) == 0)
	{
	  koption = kcfg;
	}
      else
	{
	  pnt = get_qstring(pnt, &kcfg->label);
	}
      break;
    case tok_else:
    case tok_fi:
      break;
    case tok_if:
      /*
       * Conditionals are different.  For the first level parse, only
       * tok_if items have a ->cond chain attached.
       */
      kcfg->cond = parse_if(pnt);
      if(kcfg->cond == NULL )
	{
	  exit(1);
	}
      break;
    default:
      exit(0);

    }
}

/*
 * Simple function to dump to the screen what the condition chain looks like.
 */
dump_if(struct condition * cond)
{
  printf(" ");
  while(cond != NULL )
    {
      switch(cond->op){
      case op_eq:
	printf(" = ");
	break;
      case op_bang:
	printf(" ! ");
	break;
      case op_neq:
	printf(" != ");
	break;
      case op_and:
	printf(" -a ");
	break;
      case op_lparen:
	printf("(");
	break;
      case op_rparen:
	printf(")");
	break;
      case op_variable:
	printf("$%s", cond->variable);
	break;
      case op_constant:
	printf("'%s'", cond->variable);
	break;
      }
      cond = cond->next;
    }

  printf("\n");
}

static int do_source(char * filename)
{
  char buffer[1024];
  int old_lineno;
  char * pnt;
  FILE * infile;

  infile = fopen(filename,"r");
  if(!infile) {
    fprintf(stderr,"Unable to open file %s\n", filename);
    return 1;
  }
  old_lineno = lineno;
  lineno = 0;
  current_file = filename;
  while (fgets(buffer, sizeof(buffer), infile))
    {
      /*
       * Strip the trailing return character.
       */
      pnt = buffer + strlen(buffer) - 1;
      if( *pnt == '\n') *pnt = 0;
      lineno++;
      parse(buffer);
    }
  fclose(infile);
  current_file = NULL;
  lineno = old_lineno;
  return 0;
}

main(int argc, char * argv[])
{
  char * pnt;
  struct kconfig * cfg;
  char buffer[1024];
  int    i;

  /*
   * Loop over every input line, and parse it into the tables.
   */
  while(fgets(buffer, sizeof(buffer), stdin))
    {
      /*
       * Strip the trailing return character.
       */
      pnt = buffer + strlen(buffer) - 1;
      if( *pnt == '\n') *pnt = 0;
      lineno++;
      parse(buffer);
    }


  if( menus_seen == 0 )
    {
      fprintf(stderr,"The config.in file for this platform does not support\n");
      fprintf(stderr,"menus.\n");
      exit(1);
    }
  /*
   * Input file is now parsed.  Next we need to go through and attach
   * the correct conditions to each of the actual menu items and kill
   * the if/else/endif tokens from the list.  We also flag the menu items
   * that have other things that depend upon it's setting.
   */
  fix_conditionals(config);

  /*
   * Finally, we generate the wish script.
   */
  dump_tk_script(config);

#if 0
  /*
   * Now dump what we have so far.  This is only for debugging so that
   * we can display what we think we have in the list.
   */
  for(cfg = config; cfg; cfg = cfg->next)
    {

      if(cfg->cond != NULL && cfg->tok != tok_if)
	dump_if(cfg->cond);

      switch(cfg->tok)
	{
	case tok_menuname:
	  printf("main_menuname ");
	  break;
	case tok_bool:
	  printf("bool ");
	  break;
	case tok_tristate:
	  printf("tristate ");
	  break;
	case tok_dep_tristate:
	  printf("dep_tristate ");
	  break;
	case tok_int:
	  printf("int ");
	  break;
	case tok_comment:
	  printf("comment ");
	  break;
	case tok_menuoption:
	  printf("menuoption ");
	  break;
	case tok_else:
	  printf("else");
	  break;
	case tok_fi:
	  printf("fi");
	  break;
	case tok_if:
	  printf("if");
	  break;
	default:
	}

      switch(cfg->tok)
	{
	case tok_menuoption:
	case tok_comment:
	case tok_menuname:
	  printf("%s\n", cfg->label);
	  break;
	case tok_bool:
	case tok_tristate:
	case tok_dep_tristate:
	case tok_int:
	  printf("%s %s %s\n", cfg->label, cfg->optionname, cfg->dflt);
	  break;
	case tok_if:
	  dump_if(cfg->cond);
	  break;
	case tok_nop:
	  break;
	default:
	  printf("\n");
	}
    }
#endif

  return 0;

}
