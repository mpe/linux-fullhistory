/* Generate tk script based upon config.in
 *
 * Version 1.0
 * Eric Youngdale
 * 10/95
 */
#include <stdio.h>
#include "tkparse.h"

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

/*
 * Generate portion of wish script for the beginning of a submenu.
 * The guts get filled in with the various options.
 */
static start_proc(char * label, int menu_num, int flag)
{
  if( flag )
    printf("menu_option menu%d %d \"%s\"\n", menu_num, menu_num, label);
  printf("proc menu%d {w title} {\n", menu_num);
  printf("\tcatch {destroy $w}\n");
  printf("\ttoplevel $w -class Dialog\n");
  printf("\tmessage $w.m -width 400 -aspect 300 -background grey -text \\\n");
  printf("\t\t\"$title\"  -relief raised -bg grey\n");
  printf("\tpack $w.m -pady 10 -side top -padx 10\n");
  printf("\twm title $w \"$title\" \n\n\n");
}

/*
 * Each proc we create needs a global declaration for any global variables we
 * use.  To minimize the size of the file, we set a flag each time we output
 * a global declaration so we know whether we need to insert one for a
 * given function or not.
 */
clear_globalflags(struct kconfig * cfg)
{
  for(; cfg != NULL; cfg = cfg->next)
  {
    cfg->flags &= ~GLOBAL_WRITTEN;
  }
}

/*
 * This function walks the chain of conditions that we got from cond.c,
 * and creates a wish conditional to enable/disable a given widget.
 */
generate_if(struct kconfig * item,
	    struct condition * cond,
	    int menu_num,
	    int line_num)
{
  struct condition * ocond;

  /*
   * First write any global declarations we need for this conditional.
   */
  ocond = cond;
  while(cond != NULL )
    {
      switch(cond->op){
      case op_variable:
	printf("\tglobal %s\n", cond->variable.str);
	break;
      case op_kvariable:
	if(cond->variable.cfg->flags & GLOBAL_WRITTEN) break;
	cond->variable.cfg->flags |= GLOBAL_WRITTEN;
	printf("\tglobal %s\n", cond->variable.cfg->optionname);
	break;
      default:
	break;
      }
      cond = cond->next;
    }

  /*
   * Now generate the body of the conditional.
   */
  printf("\tif {");
  cond = ocond;
  while(cond != NULL )
    {
      switch(cond->op){
      case op_bang:
	printf(" ! ");
	break;
      case op_eq:
	printf(" == ");
	break;
      case op_neq:
	printf(" != ");
	break;
      case op_and:
	printf(" && ");
	break;
      case op_or:
	printf(" || ");
	break;
      case op_lparen:
	printf("(");
	break;
      case op_rparen:
	printf(")");
	break;
      case op_variable:
	printf("$%s", cond->variable.str);
	break;
      case op_kvariable:
	printf("$%s", cond->variable.cfg->optionname);
	break;
      case op_constant:
	if( strcmp(cond->variable.str, "y") == 0 )
	  printf("1");
	else if( strcmp(cond->variable.str, "n") == 0 )
	  printf("0");
	else if( strcmp(cond->variable.str, "m") == 0 )
	  printf("2");
	else
	  printf("'%s'", cond->variable);
	break;
      }
      cond = cond->next;
    }

  /*
   * Now we generate what we do depending upon the value of the conditional.
   * Depending upon what the token type is, there are different things
   * we must do to enable/disable the given widget - this code needs to
   * be closely coordinated with the widget creation procedures in header.tk.
   */
  switch(item->tok)
    {
    case tok_menuoption:
      printf("} then { .f0.x%d configure -state normal } else { .f0.x%d configure -state disabled }\n",
	     menu_num, menu_num);
      break;
    case tok_int:
      printf("} then { ");
      printf(".menu%d.x%d.x configure -state normal; ", menu_num, line_num);
      printf(".menu%d.x%d.l configure -state normal; ", menu_num, line_num);
      printf("} else { ");
      printf(".menu%d.x%d.x configure -state disabled;", menu_num, line_num );
      printf(".menu%d.x%d.l configure -state disabled;", menu_num, line_num );
      printf("}\n");
      break;
    case tok_bool:
#ifdef BOOL_IS_BUTTON
      /*
       * If a bool is just a button, then use this definition.
       */
      printf("} then { .menu%d.x%d configure -state normal } else { .menu%d.x%d configure -state disabled }\n",
	     menu_num, line_num,
	     menu_num, line_num );
#else
      /*
       * If a bool is a radiobutton, then use this instead.
       */
      printf("} then { ");
      printf(".menu%d.x%d.y configure -state normal;",menu_num, line_num);
      printf(".menu%d.x%d.n configure -state normal;",menu_num, line_num);
      printf(".menu%d.x%d.l configure -state normal;",menu_num, line_num);
      printf("} else { ");
      printf(".menu%d.x%d.y configure -state disabled;",menu_num, line_num);
      printf(".menu%d.x%d.n configure -state disabled;",menu_num, line_num);
      printf(".menu%d.x%d.l configure -state disabled;",menu_num, line_num);
      printf("}\n");
#endif
      break;
    case tok_tristate:
    case tok_dep_tristate:
      printf("} then { ");
      if( item->tok == tok_dep_tristate )
	{
	  printf("if { $%s == 2 } then {", item->depend.str);
	  printf(".menu%d.x%d.y configure -state disabled;",menu_num, line_num);
	  printf("} else {");
	  printf(".menu%d.x%d.y configure -state normal;",menu_num, line_num);
	  printf("}; ");
	}
      else
	{
	  printf(".menu%d.x%d.y configure -state normal;",menu_num, line_num);
	}
      
      printf(".menu%d.x%d.n configure -state normal;",menu_num, line_num);
      printf(".menu%d.x%d.m configure -state normal;",menu_num, line_num);
      printf(".menu%d.x%d.l configure -state normal;",menu_num, line_num);
      printf("} else { ");
      printf(".menu%d.x%d.y configure -state disabled;",menu_num, line_num);
      printf(".menu%d.x%d.n configure -state disabled;",menu_num, line_num);
      printf(".menu%d.x%d.m configure -state disabled;",menu_num, line_num);
      printf(".menu%d.x%d.l configure -state disabled;",menu_num, line_num);
      printf("}\n");
      break;
    default:
      break;
    }
}

/*
 * Similar to generate_if, except we come here when generating an
 * output file.  Thus instead of enabling/disabling a widget, we
 * need to decide whether to write out a given configuration variable
 * to the output file.
 */
generate_if_for_outfile(struct kconfig * item,
	    struct condition * cond)
{
  struct condition * ocond;

  /*
   * First write any global declarations we need for this conditional.
   */
  ocond = cond;
  for(; cond != NULL; cond = cond->next )
    {
      switch(cond->op){
      case op_variable:
	printf("\tglobal %s\n", cond->variable.str);
	break;
      case op_kvariable:
	if(cond->variable.cfg->flags & GLOBAL_WRITTEN) break;
	cond->variable.cfg->flags |= GLOBAL_WRITTEN;
	printf("\tglobal %s\n", cond->variable.cfg->optionname);
	break;
      default:
	break;
      }
    }

  /*
   * Now generate the body of the conditional.
   */
  printf("\tif {");
  cond = ocond;
  while(cond != NULL )
    {
      switch(cond->op){
      case op_bang:
	printf(" ! ");
	break;
      case op_eq:
	printf(" == ");
	break;
      case op_neq:
	printf(" != ");
	break;
      case op_and:
	printf(" && ");
	break;
      case op_or:
	printf(" || ");
	break;
      case op_lparen:
	printf("(");
	break;
      case op_rparen:
	printf(")");
	break;
      case op_variable:
	printf("$%s", cond->variable.str);
	break;
      case op_kvariable:
	printf("$%s", cond->variable.cfg->optionname);
	break;
      case op_constant:
	if( strcmp(cond->variable.str, "y") == 0 )
	  printf("1");
	else if( strcmp(cond->variable.str, "n") == 0 )
	  printf("0");
	else if( strcmp(cond->variable.str, "m") == 0 )
	  printf("2");
	else
	  printf("'%s'", cond->variable);
	break;
      }
      cond = cond->next;
    }

  /*
   * Now we generate what we do depending upon the value of the
   * conditional.  Depending upon what the token type is, there are
   * different things we must do write the value the given widget -
   * this code needs to be closely coordinated with the widget
   * creation procedures in header.tk.  
   */
  switch(item->tok)
    {
    case tok_comment:
      printf("} then {write_comment $cfg $autocfg \"%s\"}\n", item->label);
      break;
    case tok_dep_tristate:
      printf("} then { write_variable $cfg $autocfg %s $%s $%s } \n", 
	     item->optionname, item->optionname, item->depend.str);
      break;
    case tok_tristate:
    case tok_bool:
    case tok_int:
      printf("} then { write_variable $cfg $autocfg %s $%s $notmod }\n", 
	     item->optionname, item->optionname);
      break;
    default:
      break;
    }
}

/*
 * Generates a fragment of wish script that closes out a submenu procedure.
 */
static end_proc(int menu_num, int first, int last)
{
  struct kconfig * cfg;

  printf("\n\n\n");
  printf("\tset oldFocus [focus]\n");
  printf("\tframe $w.f\n");

  /*
   * Attach the "Prev", "Next" and "OK" buttons at the end of the window.
   */
  printf("\tbutton $w.f.prev -text \"Prev\" -activebackground green \\\n");
      printf("\t\t-width 15 -command \" destroy $w; focus $oldFocus; menu%d .menu%d \\\"$title\\\"\"\n", menu_num-1, menu_num-1);
  if(first == menu_num ) printf("\t$w.f.prev configure -state disabled\n");

  printf("\tbutton $w.f.next -text \"Next\" -activebackground green \\\n");
  printf("\t\t-width 15 -command \" destroy $w; focus $oldFocus;  menu%d .menu%d \\\"$title\\\"\"\n", menu_num+1, menu_num+1);
  if(last == menu_num ) printf("\t$w.f.next configure -state disabled\n");

  printf("\tbutton $w.f.back -text \"Main Menu\" -activebackground green \\\n");
  printf("\t\t-width 15 -command \"destroy $w; focus $oldFocus; update_mainmenu $w\"\n");

  printf("\tpack $w.f.back $w.f.next $w.f.prev -side left -pady 10 -padx 45\n");
  printf("\tpack $w.f -pady 10 -side top -padx 10 -anchor w\n");
  printf("\tfocus $w\n");
  printf("\tupdate_menu%d $w\n", menu_num);
  printf("\twm geometry $w +30+35\n");
  printf("}\n\n\n");

  /*
   * Now we generate the companion procedure for the muen we just
   * generated.  This procedure contains all of the code to
   * disable/enable widgets based upon the settings of the other
   * widgets, and will be called first when the window is mapped,
   * and each time one of the buttons in the window are clicked.
   */
  printf("proc update_menu%d {w}  {\n", menu_num);

  clear_globalflags(config);
  for(cfg = config;cfg != NULL; cfg = cfg->next)
    {
      /*
       * Skip items not for this menu, or ones having no conditions.
       */
      if (cfg->menu_number != menu_num ) continue;
      if (cfg->tok == tok_menuoption) continue;
      if (cfg->cond != NULL ) 
	generate_if(cfg, cfg->cond, menu_num, cfg->menu_line);
      else
	{
	  /*
	   * If this token has no conditionals, check to see whether
	   * it is a tristate - if so, then generate the conditional
	   * to enable/disable the "y" button based upon the setting
	   * of the option it depends upon.
	   */
	  if(cfg->tok == tok_dep_tristate)
	    {
	      printf("\tif {$%s == 2 } then { .menu3.x5.y configure -state normal} else { .menu3.x5.y configure -state disabled}\n",cfg->depend.str,
		     menu_num, cfg->menu_line,
		     menu_num, cfg->menu_line);
	      
	    }
	}

    }


  printf("}\n\n\n");
}

/*
 * This function goes through and counts up the number of items in
 * each submenu. If there are too many options, we need to split it
 * into submenus.  This function just calculates how many submenus,
 * and how many items go in each submenu.
 */
static int find_menu_size(struct kconfig *cfg,
			  int *menu_max, 
			  int *menu_maxlines)

{
  struct kconfig * pnt;
  int tot;
  int div;
  
  /*
   * First count up the number of options in this menu.
   */
  tot = 0;
  for(pnt = cfg->next; pnt; pnt = pnt->next)
  {
    if( pnt->tok == tok_menuoption) break;
    switch (pnt->tok)
      {
      case tok_bool:
      case tok_tristate:
      case tok_dep_tristate:
      case tok_int:
	tot++;
	break;
      default:
	break;
      }
  }

  /*
   * Now figure out how many items go on each page.
   */
  div = 1;
  while(tot / div > 15) div++;
  *menu_max = cfg->menu_number + div - 1;
  *menu_maxlines = (tot + div -1) / div;
}

/*
 * This is the top level function for generating the tk script.
 */
dump_tk_script(struct kconfig *scfg)
{
  int menu_num =0;
  int menu_max =0;
  int menu_min =0;
  int menu_line = 0;
  int menu_maxlines = 0;
  struct kconfig * cfg;

  /*
   * Start by assigning menu numbers, and submenu numbers.
   */
  for(cfg = scfg;cfg != NULL; cfg = cfg->next)
    {
      switch (cfg->tok)
	{
	case tok_menuname:
	  break;
	case tok_menuoption:
	  /*
	   * At the start of a new menu, calculate the number of items
	   * we will put into each submenu so we know when to bump the
	   * menu number. The submenus are really no different from a
	   * normal menu, but the top level buttons only access the first
	   * of the chain of menus, and the prev/next buttons are used
	   * access the submenus.
	   */
	  cfg->menu_number = ++menu_num;
	  find_menu_size(cfg, &menu_max, &menu_maxlines);
	  cfg->submenu_start = menu_num;
	  cfg->submenu_end = menu_max;
	  menu_line = 0;
	  break;
	case tok_bool:
	case tok_tristate:
	case tok_dep_tristate:
	case tok_int:
	  /*
	   * If we have overfilled the menu, then go to the next one.
	   */
	  if( menu_line == menu_maxlines )
	    {
	      menu_line = 0;
	      menu_num++;
	    }
	  cfg->menu_number = menu_num;
	  cfg->submenu_start = menu_min;
	  cfg->submenu_end = menu_max;
	  cfg->menu_line = menu_line++;
	  break;
	default:
	  break;
	};
    }

  /*
   * Now start generating the actual wish script that we will use.
   * We need to keep track of the menu numbers of the min/max menu
   * for a range of submenus so that we can correctly limit the
   * prev and next buttons so that they don't go over into some other
   * category.
   */
  for(cfg = scfg; cfg != NULL; cfg = cfg->next)
    {
      switch (cfg->tok)
	{
	case tok_menuname:
	  printf("mainmenu_name \"%s\"\n", cfg->label);
	  break;
	case tok_menuoption:
	  /*
	   * We are at the start of a new menu. If we had one that
	   * we were working on before, close it out, and then generate
	   * the script to start the new one.
	   */
	  if( cfg->menu_number > 1 )
	    {
	      end_proc(menu_num, menu_min, menu_max);
	    }
	  start_proc(cfg->label, cfg->menu_number, TRUE);
	  menu_num = cfg->menu_number;
	  menu_max = cfg->submenu_end;
	  menu_min = cfg->submenu_start;
	  break;
	case tok_bool:
	  /*
	   * If we reached the point where we need to switch over
	   * to the next submenu, then bump the menu number and generate
	   * the code to close out the old menu and start the new one.
	   */
	  if( cfg->menu_number != menu_num )
	    {
	      end_proc(menu_num, menu_min, menu_max);
	      start_proc(cfg->label, cfg->menu_number, FALSE);
	      menu_num = cfg->menu_number;
	    }
	  printf("\tbool $w %d %d \"%s\" %s %s\n",
		 cfg->menu_number,
		 cfg->menu_line,
		 cfg->label,
		 cfg->optionname,
		 cfg->dflt);
	  break;

	case tok_tristate:
	  if( cfg->menu_number != menu_num )
	    {
	      end_proc(menu_num, menu_min, menu_max);
	      start_proc(cfg->label, cfg->menu_number, FALSE);
	      menu_num = cfg->menu_number;
	    }
	  printf("\ttristate $w %d %d \"%s\" %s %s\n",
		 cfg->menu_number,
		 cfg->menu_line,
		 cfg->label,
		 cfg->optionname,
		 cfg->dflt);
	  break;
	case tok_dep_tristate:
	  if( cfg->menu_number != menu_num )
	    {
	      end_proc(menu_num, menu_min, menu_max);
	      start_proc(cfg->label, cfg->menu_number, FALSE);
	      menu_num = cfg->menu_number;
	    }
	  printf("\tdep_tristate $w %d %d \"%s\" %s %s\n",
		 cfg->menu_number,
		 cfg->menu_line,
		 cfg->label,
		 cfg->optionname,
		 cfg->dflt,
		 cfg->depend);
	  break;
	case tok_int:
	  printf("\tint $w %d %d \"%s\" %s %s\n",
		 cfg->menu_number,
		 cfg->menu_line,
		 cfg->label,
		 cfg->optionname,
		 cfg->dflt);
	default:
	  break;
	}

    }

  /*
   * Generate the code to close out the last menu.
   */
  end_proc(menu_num, menu_min, menu_max);

  /*
   * Generate the code for configuring the sound driver.  Right now this
   * cannot be done from the X script, but we insert the menu anyways.
   */
  start_proc("Configure sound driver", ++menu_num, TRUE);
#if 0
  printf("\tdo_make -C drivers/sound config\n");
  printf("\techo check_sound_config %d\n",menu_num);
#endif
  printf("\tlabel $w.m0 -bitmap error\n");
  printf("\tmessage $w.m1 -width 400 -aspect 300 -text \"The sound drivers cannot as of yet be configured via the X-based interface\" -relief raised\n");
  printf("\tpack $w.m0 $w.m1 -side top -pady 10\n");

  /*
   * Close out the last menu.
   */
  end_proc(menu_num, menu_num, menu_num);

  /*
   * The top level menu also needs an update function.  When we exit a
   * submenu, we may need to disable one or more of the submenus on
   * the top level menu, and this procedure will ensure that things are
   * correct.
   */
  printf("proc update_mainmenu {w}  {\n");
  for(cfg = scfg; cfg != NULL; cfg = cfg->next)
    {
      switch (cfg->tok)
	{
	case tok_menuoption:
	  if (cfg->cond != NULL ) 
	    generate_if(cfg, cfg->cond, cfg->menu_number, cfg->menu_line);
	  break;
	default:
	  break;
	}
    }

  printf("}\n\n\n");

  /*
   * Now generate code to load the default settings into the variables.
   * Note that the script in tail.tk will attempt to load .config,
   * which may override these settings, but that's OK.
   */
  for(cfg = scfg; cfg != NULL; cfg = cfg->next)
    {
      switch (cfg->tok)
	{
	case tok_int:
	    printf("set %s %s\n", cfg->optionname, cfg->dflt);
	    break;
	case tok_bool:
	case tok_tristate:
	case tok_dep_tristate:
	  if( strcmp(cfg->dflt, "y") == 0 )
	    printf("set %s 1\n", cfg->optionname);
	  else if( strcmp(cfg->dflt, "n") == 0 )
	    printf("set %s 0\n", cfg->optionname);
	  else if( strcmp(cfg->dflt, "m") == 0 )
	    printf("set %s 2\n", cfg->optionname);
	  break;
	default:
	  break;
	}
    }

  /*
   * Next generate a function that can be called from the main menu that will
   * write all of the variables out.  This also serves double duty - we can
   * save configuration to a file using this.
   */
  printf("proc writeconfig {file1 file2} {\n");
  printf("\tset cfg [open $file1 w]\n");
  printf("\tset autocfg [open $file2 w]\n");
  printf("\tset notmod 1\n");
  printf("\tset notset 0\n");
  clear_globalflags(config);
  printf("\tputs $cfg \"#\"\n");
  printf("\tputs $cfg \"# Automatically generated make config: don't edit\"\n");
  printf("\tputs $cfg \"#\"\n");

  printf("\tputs $autocfg \"/*\"\n");
  printf("\tputs $autocfg \" * Automatically generated C config: don't edit\"\n");
  printf("\tputs $autocfg \" */\"\n");
  for(cfg = scfg; cfg != NULL; cfg = cfg->next)
    {
      switch (cfg->tok)
	{
	case tok_int:
	case tok_bool:
	case tok_tristate:
	case tok_dep_tristate:
	  if(cfg->flags & GLOBAL_WRITTEN) break;
	  cfg->flags |= GLOBAL_WRITTEN;
	  printf("\tglobal %s\n", cfg->optionname);

	case tok_comment:
	  if (cfg->cond != NULL ) 
	    generate_if_for_outfile(cfg, cfg->cond);
	  else
	    {
	      if(cfg->tok == tok_dep_tristate)
		{
		  printf("\tif {$%s == 2 } then { write_variable $cfg $autocfg %s $%s %s } else { write_variable $cfg $autocfg %s $notset $notmod }\n",
			 cfg->optionname,
			 cfg->optionname,
			 cfg->depend.str,
			 cfg->optionname);
		}
	      else if(cfg->tok == tok_comment)
		{
		  printf("\twrite_comment $cfg $autocfg \"%s\"\n", cfg->label);
		}
	      else
		{
		  printf("\twrite_variable $cfg $autocfg %s $%s $notmod\n",
			 cfg->optionname,
			 cfg->optionname);
			 
		}
	    }
	  break;
	default:
	  break;
	}
    }
  printf("\tclose $cfg\n");
  printf("\tclose $autocfg\n");
  printf("}\n\n\n");

  /*
   * That's it.  We are done.  The output of this file will have header.tk
   * prepended and tail.tk appended to create an executable wish script.
   */
}
