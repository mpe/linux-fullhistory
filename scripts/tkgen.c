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
 * This prevents the Prev/Next buttons from going through the entire sequence
 * of submenus.  I need to fix the window titles before it would really be
 * appropriate to enable this.
 */
#define PREVLAST_LIMITED_RANGE

/*
 * This is the total number of submenus that we have.
 */
static int tot_menu_num =0;

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
  printf("\t\t\"%s\"  -relief raised -bg grey\n",label);
  printf("\tpack $w.m -pady 10 -side top -padx 10\n");
  printf("\twm title $w \"%s\" \n\n\n", label);
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
  int i;
  struct condition * ocond;

  ocond = cond;

  /*
   * First write any global declarations we need for this conditional.
   */
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
   * Now write this option.
   */
  if(   (item->flags & GLOBAL_WRITTEN) == 0
     && (item->optionname != NULL) )
    {
      printf("\tglobal %s\n", item->optionname);
      item->flags |= GLOBAL_WRITTEN;
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
      case op_and1:
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
      case op_shellcmd:
	printf("[exec %s]", cond->variable.str);
	break;
      case op_constant:
	if( strcmp(cond->variable.str, "y") == 0 )
	  printf("1");
	else if( strcmp(cond->variable.str, "n") == 0 )
	  printf("0");
	else if( strcmp(cond->variable.str, "m") == 0 )
	  printf("2");
	else
	  printf("\"%s\"", cond->variable);
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
    case tok_define:
      printf("} then { set %s %s } \n",  item->optionname, item->value);
      break;
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
      printf("set %s [expr $%s&15];", item->optionname, item->optionname);
      printf("} else { ");
      printf(".menu%d.x%d.y configure -state disabled;",menu_num, line_num);
      printf(".menu%d.x%d.n configure -state disabled;",menu_num, line_num);
      printf(".menu%d.x%d.l configure -state disabled;",menu_num, line_num);
      printf("set %s [expr $%s|16];", item->optionname, item->optionname);
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
      /*
       * Or in a bit to the variable - this causes all of the radiobuttons
       * to be deselected (i.e. not be red).
       */
      printf("set %s [expr $%s&15];", item->optionname, item->optionname);
      printf("} else { ");
      printf(".menu%d.x%d.y configure -state disabled;",menu_num, line_num);
      printf(".menu%d.x%d.n configure -state disabled;",menu_num, line_num);
      printf(".menu%d.x%d.m configure -state disabled;",menu_num, line_num);
      printf(".menu%d.x%d.l configure -state disabled;",menu_num, line_num);
      /*
       * Clear the disable bit - this causes the correct radiobutton
       * to appear selected (i.e. turn red).
       */
      printf("set %s [expr $%s|16];", item->optionname, item->optionname);
      printf("}\n");
      break;
    case tok_choose:
    case tok_choice:
      fprintf(stderr,"Fixme\n");
      exit(0);
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
      case op_and1:
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
      case op_shellcmd:
	printf("[exec %s]", cond->variable.str);
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
	  printf("\"%s\"", cond->variable);
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
    case tok_define:
      printf("} then {write_variable $cfg $autocfg %s %s $notmod }\n", item->optionname, item->value);
      break;
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
    case tok_choose:
    case tok_choice:
      fprintf(stderr,"Fixme\n");
      exit(0);
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
#ifdef PREVLAST_LIMITED_RANGE
  if(first == menu_num ) printf("\t$w.f.prev configure -state disabled\n");
#else
  if( 1 == menu_num ) printf("\t$w.f.prev configure -state disabled\n");
#endif

  printf("\tbutton $w.f.next -text \"Next\" -activebackground green \\\n");
  printf("\t\t-width 15 -command \" destroy $w; focus $oldFocus;  menu%d .menu%d \\\"$title\\\"\"\n", menu_num+1, menu_num+1);
#ifdef PREVLAST_LIMITED_RANGE
  if(last == menu_num ) printf("\t$w.f.next configure -state disabled\n");
#else
  if(last == tot_menu_num ) printf("\t$w.f.next configure -state disabled\n");
#endif

  printf("\tbutton $w.f.back -text \"Main Menu\" -activebackground green \\\n");
  printf("\t\t-width 15 -command \"destroy $w; focus $oldFocus; update_mainmenu $w\"\n");

  printf("\tpack $w.f.back $w.f.next $w.f.prev -side left -pady 10 -padx 45\n");
  printf("\tpack $w.f -pady 10 -side top -padx 10 -anchor w\n");
  printf("\tfocus $w\n");
  printf("\tupdate_menu%d $w\n", menu_num);
  printf("\tglobal winx; global winy\n");
  printf("\tset winx [expr [winfo x .]+30]; set winy [expr [winfo y .]+30]\n");
  printf("\twm geometry $w +$winx+$winy\n");
  printf("}\n\n\n");

  /*
   * Now we generate the companion procedure for the menu we just
   * generated.  This procedure contains all of the code to
   * disable/enable widgets based upon the settings of the other
   * widgets, and will be called first when the window is mapped,
   * and each time one of the buttons in the window are clicked.
   */
  printf("proc update_menu%d {w}  {\n", menu_num);

  printf("\tupdate_define\n");
  clear_globalflags(config);
  for(cfg = config;cfg != NULL; cfg = cfg->next)
    {
      /*
       * Skip items not for this menu, or ones having no conditions.
       */
      if (cfg->menu_number != menu_num ) continue;
      if (cfg->tok != tok_define) continue;
      /*
       * Clear all of the booleans that are defined in this menu.
       */
      if(   (cfg->flags & GLOBAL_WRITTEN) == 0
	 && (cfg->optionname != NULL) )
	{
	  printf("\tglobal %s\n", cfg->optionname);
	  cfg->flags |= GLOBAL_WRITTEN;
	  printf("\tset %s 0\n", cfg->optionname);
	}

    }
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
      case tok_choose:
      case tok_sound:
	tot++;
	break;
      case tok_choice:
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
  int i;
  int menu_num =0;
  int menu_max =0;
  int menu_min =0;
  int menu_line = 0;
  int menu_maxlines = 0;
  struct kconfig * cfg;
  struct kconfig * cfg1 = NULL;
  char * menulabel;

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
	case tok_choose:
	case tok_sound:
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
	case tok_define:
	  cfg->menu_number = -1;
	case tok_choice:
	default:
	  break;
	};
    }

  /*
   * Record this so we can set up the prev/next buttons correctly.
   */
  tot_menu_num = menu_num;

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
	  menulabel = cfg->label;
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
	      start_proc(menulabel, cfg->menu_number, FALSE);
	      menu_num = cfg->menu_number;
	    }
	  printf("\tbool $w %d %d \"%s\" %s\n",
		 cfg->menu_number,
		 cfg->menu_line,
		 cfg->label,
		 cfg->optionname);
	  break;

	case tok_choice:
	  printf("\t$w.line%d.menu add radiobutton -label \"%s\" -variable %s -value %d -command \"update_menu%d .menu%d\"\n",
		 cfg1->menu_line,
		 cfg->label,
		 cfg1->optionname,
		 cfg->choice_value,
		 cfg->menu_number, cfg->menu_number);
	  break;
	case tok_choose:
	  if( cfg->menu_number != menu_num )
	    {
	      end_proc(menu_num, menu_min, menu_max);
	      start_proc(menulabel, cfg->menu_number, FALSE);
	      menu_num = cfg->menu_number;
	    }
	  printf("\tmenubutton $w.line%d -text \"%s\" -menu $w.line%d.menu \\\n",
		 cfg->menu_line, cfg->label, cfg->menu_line);
	  printf("\t	-relief raised -width 35\n");
	  printf("\tpack $w.line%d -anchor w\n", cfg->menu_line);
	  printf("\tmenu $w.line%d.menu\n", cfg->menu_line);
	  cfg1 = cfg;
	  break;
	case tok_tristate:
	  if( cfg->menu_number != menu_num )
	    {
	      end_proc(menu_num, menu_min, menu_max);
	      start_proc(menulabel, cfg->menu_number, FALSE);
	      menu_num = cfg->menu_number;
	    }
	  printf("\ttristate $w %d %d \"%s\" %s\n",
		 cfg->menu_number,
		 cfg->menu_line,
		 cfg->label,
		 cfg->optionname);
	  break;
	case tok_dep_tristate:
	  if( cfg->menu_number != menu_num )
	    {
	      end_proc(menu_num, menu_min, menu_max);
	      start_proc(menulabel, cfg->menu_number, FALSE);
	      menu_num = cfg->menu_number;
	    }
	  printf("\tdep_tristate $w %d %d \"%s\" %s\n",
		 cfg->menu_number,
		 cfg->menu_line,
		 cfg->label,
		 cfg->optionname,
		 cfg->depend);
	  break;
	case tok_int:
	  if( cfg->menu_number != menu_num )
	    {
	      end_proc(menu_num, menu_min, menu_max);
	      start_proc(menulabel, cfg->menu_number, FALSE);
	      menu_num = cfg->menu_number;
	    }
	  printf("\tint $w %d %d \"%s\" %s\n",
		 cfg->menu_number,
		 cfg->menu_line,
		 cfg->label,
		 cfg->optionname);
	  break;
	case tok_sound:
	  if( cfg->menu_number != menu_num )
	    {
	      end_proc(menu_num, menu_min, menu_max);
	      start_proc(menulabel, cfg->menu_number, FALSE);
	      menu_num = cfg->menu_number;
	    }
	  printf("\tdo_sound $w %d %d\n",
		 cfg->menu_number,
		 cfg->menu_line);
	  break;
	default:
	  break;
	}

    }

  /*
   * Generate the code to close out the last menu.
   */
  end_proc(menu_num, menu_min, menu_max);

#ifdef ERIC_DONT_DEF
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
#endif

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

#if 0
  /*
   * Generate some code to set the variables that are "defined".
   */
  for(cfg = config;cfg != NULL; cfg = cfg->next)
    {
      /*
       * Skip items not for this menu, or ones having no conditions.
       */
      if( cfg->tok != tok_define) continue;
      if (cfg->cond != NULL ) 
	generate_if(cfg, cfg->cond, menu_num, cfg->menu_line);
      else
	{
	  printf("\twrite_define %s %s\n", cfg->optionname, cfg->value);
	}

    }
#endif

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
	case tok_bool:
	case tok_tristate:
	case tok_dep_tristate:
	case tok_choice:
	  printf("set %s 0\n", cfg->optionname);
	  break;
	case tok_choose:
	  printf("set %s %d\n", cfg->optionname, cfg->choice_value);
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
	case tok_define:
	case tok_choose:
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
#if 0
	      else if(cfg->tok == tok_define)
		{
		  printf("\twrite_define %s %s\n", cfg->optionname,
			 cfg->value);
		}
#endif
	      else if (cfg->tok == tok_choose )
		{
		  for(cfg1 = cfg->next; 
		      cfg1 != NULL && cfg1->tok == tok_choice;
		      cfg1 = cfg1->next)
		    {
		      printf("\tif { $%s == %d } then { write_variable $cfg $autocfg %s 1 $notmod }\n",
			     cfg->optionname,
			     cfg1->choice_value,
			     cfg1->optionname);
		    }
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
   * Finally write a simple function that updates the master choice
   * variable depending upon what values were loaded from a .config
   * file.  
   */
  printf("proc clear_choices { } {\n");
  for(cfg = scfg; cfg != NULL; cfg = cfg->next)
    {
      if( cfg->tok != tok_choose ) continue;
      for(cfg1 = cfg->next; 
	  cfg1 != NULL && cfg1->tok == tok_choice;
	  cfg1 = cfg1->next)
	{
	  printf("\tglobal %s; set %s 0\n",  cfg1->optionname, cfg1->optionname);
	}
    }
  printf("}\n\n\n");

  printf("proc update_choices { } {\n");
  for(cfg = scfg; cfg != NULL; cfg = cfg->next)
    {
      if( cfg->tok != tok_choose ) continue;
      printf("\tglobal %s\n", cfg->optionname);
      for(cfg1 = cfg->next; 
	  cfg1 != NULL && cfg1->tok == tok_choice;
	  cfg1 = cfg1->next)
	{
	  printf("\tglobal %s\n", cfg1->optionname);
	  printf("\tif { $%s == 1 } then { set %s %d }\n",
		 cfg1->optionname,
		 cfg->optionname,
		 cfg1->choice_value);
	}
    }
  printf("}\n\n\n");

  printf("proc update_define { } {\n");
  clear_globalflags(config);
  for(cfg = scfg; cfg != NULL; cfg = cfg->next)
    {
      if( cfg->tok != tok_define ) continue;
      printf("\tglobal %s; set %s 0\n",  cfg->optionname,  cfg->optionname);
      cfg->flags |= GLOBAL_WRITTEN;
    }
  for(cfg = scfg; cfg != NULL; cfg = cfg->next)
    {
      if( cfg->tok != tok_define ) continue;
      if (cfg->cond != NULL ) 
	generate_if(cfg, cfg->cond, -1, 0);
      else
	{
	  printf("\tset %s %s\n",
		 cfg->optionname, cfg->value);
	}
    }
  printf("}\n\n\n");
  /*
   * That's it.  We are done.  The output of this file will have header.tk
   * prepended and tail.tk appended to create an executable wish script.
   */
}
