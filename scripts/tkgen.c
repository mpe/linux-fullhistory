/* Generate tk script based upon config.in
 *
 * Version 1.0
 * Eric Youngdale
 * 10/95
 *
 * 1996 01 04
 * Avery Pennarun - Aesthetic improvements.
 *
 * 1996 01 24
 * Avery Pennarun - Bugfixes and more aesthetics.
 *
 * 1996 03 08
 * Avery Pennarun - The int and hex config.in commands work right.
 *                - Choice buttons are more user-friendly.
 *                - Disabling a text entry line greys it out properly.
 *                - dep_tristate now works like in Configure. (not pretty)
 *                - No warnings in gcc -Wall. (Fixed some "interesting" bugs.)
 *                - Faster/prettier "Help" lookups.
 *
 * 1996 03 15
 * Avery Pennarun - Added new sed script from Axel Boldt to make help even
 *                  faster. (Actually awk is downright slow on some machines.)
 *                - Fixed a bug I introduced into Choice dependencies.  Thanks
 *                  to Robert Krawitz for pointing this out.
 *
 * 1996 03 16
 * Avery Pennarun - basic "do_make" support added to let sound config work.
 *
 * 1996 03 25
 *     Axel Boldt - Help now works on "choice" buttons.
 *
 * 1996 04 06
 * Avery Pennarun - Improved sound config stuff. (I think it actually works
 *                  now!)
 *                - Window-resize-limits don't use ugly /usr/lib/tk4.0 hack.
 *                - int/hex work with tk3 again. (The "cget" error.)
 *                - Next/Prev buttons switch between menus.  I can't take
 *                  much credit for this; the code was already there, but
 *                  ifdef'd out for some reason.  It flickers a lot, but
 *                  I suspect there's no "easy" fix for that.
 *                - Labels no longer highlight as you move the mouse over
 *                  them (although you can still press them... oh well.)
 *                - Got rid of the last of the literal color settings, to
 *                  help out people with mono X-Windows systems. 
 *                  (Apparently there still are some out there!)
 *                - Tabstops seem sensible now.
 *
 * 1996 04 14
 * Avery Pennarun - Reduced flicker when creating windows, even with "update
 *                  idletasks" hack.
 *
 * 1997 12 08
 * Michael Chastain - Remove sound driver special cases.
 *
 * 1997 11 15
 * Michael Chastain - For choice buttons, write values for all options,
 *                    not just the single chosen one.  This is compatible
 *                    with 'make config' and 'make oldconfig', and is
 *                    needed so smart-config dependencies work if the
 *                    user switches from one configuration method to
 *                    another.
 *
 * 1998 03 09
 * Axel Boldt - Smaller layout of main menu - it's still too big for 800x600.
 *            - Display help in text window to allow for cut and paste.
 *            - Allow for empty lines in help texts.
 *            - update_define should not set all variables unconditionally to
 *              0: they may have been set to 1 elsewhere. CONFIG_NETLINK is
 *              an example.
 *
 * 1999 01 04
 * Michael Elizabeth Chastain <mec@shout.net>
 * - Call clear_globalflags when writing out update_mainmenu.
 *   This fixes the missing global/vfix lines for ARCH=alpha on 2.2.0-pre4.
 *
 * 8 January 1999, Michael Elizabeth Chastain <mec@shout.net>
 * - Emit menus_per_column
 *
 * 14 January 1999, Michael Elizabeth Chastain <mec@shout.net>
 * - Steam-clean this file.  I tested this by generating kconfig.tk for every
 *   architecture and comparing it character-for-character against the output
 *   of the old tkparse.
 * - Fix flattening of nested menus.  The old code simply assigned items to
 *   the most recent token_mainmenu_option, without paying attention to scope.
 *   For example: "menu-1 bool-a menu-2 bool-b endmenu bool-c bool-d endmenu".
 *   The old code would put bool-a in menu-1, bool-b in menu-2, and bool-c
 *   and bool-d in *menu-2*.  This hosed the nested submenus in
 *   drives/net/Config.in and other places.
 * - Fix menu line wraparound at 128 menus (some fool used a 'char' for
 *   a counter).
 */

#include <stdio.h>
#include <unistd.h>
#include "tkparse.h"



/*
 * Total number of menus.
 */
static int tot_menu_num = 0;



/*
 * Generate portion of wish script for the beginning of a submenu.
 * The guts get filled in with the various options.
 */
static void start_proc( char * label, int menu_num, int flag )
{
    if ( flag )
	printf( "menu_option menu%d %d \"%s\"\n", menu_num, menu_num, label );
    printf( "proc menu%d {w title} {\n", menu_num );
    printf( "\tcatch {destroy $w}\n" );
    printf( "\ttoplevel $w -class Dialog\n" );
    printf( "\twm withdraw $w\n" );
    printf( "\tmessage $w.m -width 400 -aspect 300 -text \\\n" );
    printf( "\t\t\"%s\"  -relief raised\n", label );
    printf( "\tpack $w.m -pady 10 -side top -padx 10\n" );
    printf( "\twm title $w \"%s\" \n\n", label );

    /*
     * Attach the "Prev", "Next" and "OK" buttons at the end of the window.
     */
    printf( "\tset oldFocus [focus]\n" );
    printf( "\tframe $w.f\n" );
    printf( "\tbutton $w.f.back -text \"Main Menu\" \\\n" );
    printf( "\t\t-width 15 -command \"destroy $w; focus $oldFocus; update_mainmenu $w\"\n" );
    printf( "\tbutton $w.f.next -text \"Next\" \\\n" );
    printf( "\t\t-width 15 -command \" destroy $w; focus $oldFocus;  menu%d .menu%d \\\"$title\\\"\"\n", menu_num+1, menu_num+1 );
    if ( menu_num == tot_menu_num )
	printf( "\t$w.f.next configure -state disabled\n" );
    printf( "\tbutton $w.f.prev -text \"Prev\" \\\n" );
    printf( "\t\t-width 15 -command \" destroy $w; focus $oldFocus; menu%d .menu%d \\\"$title\\\"\"\n", menu_num-1, menu_num-1 );
    if ( menu_num == 1 )
	printf( "\t$w.f.prev configure -state disabled\n" );
    printf( "\tpack $w.f.back $w.f.next $w.f.prev -side left -expand on\n" );
    printf( "\tpack $w.f -pady 10 -side bottom -anchor w -fill x\n" );

    /*
     * Lines between canvas and other areas of the window.
     */
    printf( "\tframe $w.topline -relief ridge -borderwidth 2 -height 2\n" );
    printf( "\tpack $w.topline -side top -fill x\n\n" );
    printf( "\tframe $w.botline -relief ridge -borderwidth 2 -height 2\n" );
    printf( "\tpack $w.botline -side bottom -fill x\n\n" );

    /*
     * The "config" frame contains the canvas and a scrollbar.
     */
    printf( "\tframe $w.config\n" );
    printf( "\tpack $w.config -fill y -expand on\n\n" );
    printf( "\tscrollbar $w.config.vscroll -command \"$w.config.canvas yview\"\n" );
    printf( "\tpack $w.config.vscroll -side right -fill y\n\n" );

    /*
     * The scrollable canvas itself, where the real work (and mess) gets done.
     */
    printf( "\tcanvas $w.config.canvas -height 1\\\n" );
    printf( "\t\t-relief flat -borderwidth 0 -yscrollcommand \"$w.config.vscroll set\" \\\n" );
    printf( "\t\t-width [expr [winfo screenwidth .] * 1 / 2] \n" );
    printf( "\tframe $w.config.f\n" );
    printf( "\tpack $w.config.canvas -side right -fill y\n" );
    printf("\n\n");
}



/*
 * Each proc we create needs a global declaration for any global variables we
 * use.  To minimize the size of the file, we set a flag each time we output
 * a global declaration so we know whether we need to insert one for a
 * given function or not.
 */
void clear_globalflags( struct kconfig * scfg )
{
    struct kconfig * cfg;

    for ( cfg = scfg; cfg != NULL; cfg = cfg->next )
	cfg->global_written = 0;
}



/*
 * Output a "global" line for a given variable.  Also include the
 * call to "vfix".  (If vfix is not needed, then it's fine to just printf
 * a "global" line).
 */
void global( const char *var )
{
    printf( "\tglobal %s; vfix %s\n", var, var );
}



/*
 * This function walks the chain of conditions that we got from cond.c
 * and creates a TCL conditional to enable/disable a given widget.
 */
void generate_if( struct kconfig * cfg, struct condition * ocond,
    int menu_num, int line_num )
{
    struct condition * cond;

    /*
     * First write any global declarations we need for this conditional.
     */
    for ( cond = ocond; cond != NULL; cond = cond->next )
    {
	switch ( cond->op )
	{
	default:
	    break;

	case op_variable:
	    global( cond->str );
	    break;

	case op_kvariable:
	    if ( ! cond->cfg->global_written )
	    {
		cond->cfg->global_written = 1;
		global( cond->cfg->optionname );
	    }
	    break;
	}
    }

    /*
     * Now write this option.
     */
    if ( ! cfg->global_written && cfg->optionname != NULL )
    {
	cfg->global_written = 1;
	global( cfg->optionname );
    }

    /*
     * Generate the body of the conditional.
     */
    printf( "\tif {" );
    for ( cond = ocond; cond != NULL; cond = cond->next )
    {
	switch ( cond->op )
	{
	default:
	    break;

	case op_bang:   printf( " ! "  ); break;
	case op_eq:     printf( " == " ); break;
	case op_neq:    printf( " != " ); break;
	case op_and:    printf( " && " ); break;
	case op_and1:   printf( " && " ); break;
	case op_or:     printf( " || " ); break;
	case op_lparen: printf( "("    ); break;
	case op_rparen: printf( ")"    ); break;

	case op_variable:
	    printf( "$%s", cond->str );
	    break;

	case op_kvariable:
	    printf( "$%s", cond->cfg->optionname );
	    break;

	case op_constant:
	    if      ( strcmp( cond->str, "y" ) == 0 ) printf( "1" );
	    else if ( strcmp( cond->str, "n" ) == 0 ) printf( "0" );
	    else if ( strcmp( cond->str, "m" ) == 0 ) printf( "2" );
	    else
		printf( "\"%s\"", cond->str );
	    break;
	}
    }
    printf( "} then { " );

    /*
     * Generate a procedure call to write the value.
     * This code depends on procedures in header.tk.
     */
    switch ( cfg->token )
    {
    default:
	printf( " }\n" );
	break;

    case token_bool:
	printf( ".menu%d.config.f.x%d.y configure -state normal;",
	    menu_num, line_num );
	printf( ".menu%d.config.f.x%d.n configure -state normal;",
	    menu_num, line_num );
	printf( ".menu%d.config.f.x%d.l configure -state normal;",
	    menu_num, line_num );
	printf( "set %s [expr $%s&15];",
	    cfg->optionname, cfg->optionname );
	printf( "} else { ");
	printf( ".menu%d.config.f.x%d.y configure -state disabled;",
	    menu_num, line_num );
	printf( ".menu%d.config.f.x%d.n configure -state disabled;",
	    menu_num, line_num );
	printf( ".menu%d.config.f.x%d.l configure -state disabled;",
	    menu_num, line_num );
	printf( "set %s [expr $%s|16];}\n",
	    cfg->optionname, cfg->optionname );
	break;

    case token_choice_header:
	fprintf( stderr, "Internal error on token_choice_header\n" );
	exit( 1 );

    case token_choice_item:
	fprintf( stderr, "Internal error on token_choice_item\n" );
	exit( 1 );

    case token_define_bool:
	printf( "set %s %s } \n",
	    cfg->optionname, cfg->value );
	break;

    case token_dep_tristate:
    case token_tristate:
	if ( cfg->token == token_dep_tristate )
	{
	    global( cfg->depend );
	    printf( "if { $%s != 1 && $%s != 0 } then {",
		cfg->depend, cfg->depend );
	    printf( ".menu%d.config.f.x%d.y configure -state disabled;",
		menu_num, line_num );
	    printf( "} else {" );
	    printf( ".menu%d.config.f.x%d.y configure -state normal;",
		menu_num, line_num);
	    printf( "}; " );
	}
	else
	{
	    printf( ".menu%d.config.f.x%d.y configure -state normal;",
		menu_num, line_num );
	}

	printf( ".menu%d.config.f.x%d.n configure -state normal;",
	    menu_num, line_num );
	printf( "global CONFIG_MODULES; if {($CONFIG_MODULES == 1)} then { .menu%d.config.f.x%d.m configure -state normal };",
	    menu_num, line_num );
	printf( ".menu%d.config.f.x%d.l configure -state normal;",
	    menu_num, line_num );

	/*
	 * Or in a bit to the variable - this causes all of the radiobuttons
	 * to be deselected (i.e. not be red).
	 */
	printf( "set %s [expr $%s&15];",
	    cfg->optionname, cfg->optionname );
	printf( "} else { " );
	printf( ".menu%d.config.f.x%d.y configure -state disabled;",
	    menu_num, line_num );
	printf( ".menu%d.config.f.x%d.n configure -state disabled;",
	    menu_num, line_num );
	printf( ".menu%d.config.f.x%d.m configure -state disabled;",
	    menu_num, line_num );
	printf( ".menu%d.config.f.x%d.l configure -state disabled;",
	    menu_num, line_num );

	/*
	 * Clear the disable bit to enable the correct radiobutton.
	 */
	printf( "set %s [expr $%s|16];}\n",
	    cfg->optionname, cfg->optionname );
	break;

    case token_hex:
    case token_int:
    case token_string:
	printf( ".menu%d.config.f.x%d.x configure -state normal -foreground [ cget .ref -foreground ]; ",
	    menu_num, line_num);
	printf( ".menu%d.config.f.x%d.l configure -state normal; ",
	    menu_num, line_num);
	printf( "} else { " );
	printf( ".menu%d.config.f.x%d.x configure -state disabled -foreground [ cget .ref -disabledforeground ];",
	    menu_num, line_num );
	printf( ".menu%d.config.f.x%d.l configure -state disabled;}\n",
	    menu_num, line_num );
	break;

    case token_mainmenu_option:
	printf( ".f0.x%d configure -state normal } else { .f0.x%d configure -state disabled }\n",
	    menu_num, menu_num );
	break;
    }
}



/*
 * Generate a line that writes a variable to the output file.
 */
void generate_writeconfig( struct kconfig * cfg )
{
    struct condition * cond;

    /*
     * Generate global declaration for this symbol.
     */
    if ( cfg->token != token_comment )
    {
	if ( ! cfg->global_written )
	{
	    cfg->global_written = 1;
	    printf( "\tglobal %s\n", cfg->optionname );
	}
    }

    /*
     * Generate global declarations for the condition chain.
     */
    for ( cond = cfg->cond; cond != NULL; cond = cond->next )
    {
	switch( cond->op )
	{
	default:
	    break;

	case op_variable:
	    global( cond->str );
	    break;

	case op_kvariable:
	    if ( ! cond->cfg->global_written )
	    {
		cond->cfg->global_written = 1;
		global( cond->cfg->optionname );
	    }
	    break;
	}
    }

    /*
     * Generate indentation.
     */
    if ( cfg->token != token_choice_header )
	printf( "\t" );

    /*
     * Generate the conditional.
     */
    if ( cfg->cond != NULL )
    {
	printf( "if {" );
	for ( cond = cfg->cond; cond != NULL; cond = cond->next )
	{
	    switch ( cond->op )
	    {
	    default:           break;
	    case op_bang:      printf( " ! "  ); break;
	    case op_eq:        printf( " == " ); break;
	    case op_neq:       printf( " != " ); break;
	    case op_and:       printf( " && " ); break;
	    case op_and1:      printf( " && " ); break;
	    case op_or:        printf( " || " ); break;
	    case op_lparen:    printf( "("    ); break;
	    case op_rparen:    printf( ")"    ); break;

	    case op_variable:
		printf( "$%s", cond->str );
		break;

	    case op_kvariable:
		printf( "$%s", cond->cfg->optionname );
		break;

	    case op_constant:
		if      ( strcmp( cond->str, "n" ) == 0 ) printf( "0" );
		else if ( strcmp( cond->str, "y" ) == 0 ) printf( "1" );
		else if ( strcmp( cond->str, "m" ) == 0 ) printf( "2" );
		else
		    printf( "\"%s\"", cond->str );
		break;
	    }
	}
	printf( "} then {" );
    }

    /*
     * Generate a procedure call to write the value.
     * This code depends on the write_* procedures in header.tk.
     */
    switch ( cfg->token )
    {
    default:
	if ( cfg->cond != NULL )
	    printf( " }" );
	printf( "\n" );
	break;

    case token_bool:
    case token_tristate:
	if ( cfg->cond )
	    printf( " " );
	printf( "write_tristate $cfg $autocfg %s $%s $notmod", 
	    cfg->optionname, cfg->optionname );
	if ( cfg->cond != NULL )
	    printf( " }" );
	printf( "\n" );
	break;

    case token_choice_header:
	/*
	 * This is funky code -- it fails if there were any conditionals.
	 * Fortunately all the conditionals got stripped off somewhere
	 * else.
	 */
	{
	    struct kconfig * cfg1;
	    for ( cfg1  = cfg->next;
		  cfg1 != NULL && cfg1->token == token_choice_item;
		  cfg1  = cfg1->next )
	    {
		printf("\tif { $%s == \"%s\" } then { write_tristate $cfg $autocfg %s 1 $notmod } else { write_tristate $cfg $autocfg %s 0 $notmod }\n",
		    cfg->optionname, cfg1->label,
		    cfg1->optionname,
		    cfg1->optionname );
	    }
	}
	break;

    case token_choice_item:
	fprintf( stderr, "Internal error on token_choice_item\n" );
	exit( 1 );

    case token_comment:
	printf( "write_comment $cfg $autocfg \"%s\"",
	    cfg->label );
	if ( cfg->cond != NULL )
	    printf( "}" );
	printf( "\n" );
	break;

    case token_define_bool:
	if ( cfg->cond == NULL )
	{
	    printf( "write_tristate $cfg $autocfg %s $%s $notmod\n",
		cfg->optionname, cfg->optionname );
	}
	else
	{
	    printf( "write_tristate $cfg $autocfg %s %s $notmod }\n",
		cfg->optionname, cfg->value );
	}
	break;

    case token_dep_tristate:
	if ( cfg->cond )
	    printf( " " );
	printf( "write_tristate $cfg $autocfg %s $%s $%s",
	    cfg->optionname, cfg->optionname, cfg->depend );
	if ( cfg->cond != NULL )
	    printf( " }" );
	printf( " \n" );
	break;

    case token_hex:
	if ( cfg->cond != NULL )
	    printf( " " );
	printf( "write_hex $cfg $autocfg %s $%s $notmod",
	    cfg->optionname, cfg->optionname );
	if ( cfg->cond != NULL )
	    printf( " }" );
	printf( "\n" );
	break;

    case token_int:
	if ( cfg->cond != NULL )
	    printf( " " );
	printf( "write_int $cfg $autocfg %s $%s $notmod",
	    cfg->optionname, cfg->optionname );
	if ( cfg->cond != NULL )
	    printf( " }" );
	printf( "\n" );
	break;

    case token_string:
	if ( cfg->cond != NULL )
	    printf( " " );
	printf( "write_string $cfg $autocfg %s $%s $notmod",
	    cfg->optionname, cfg->optionname );
	if ( cfg->cond != NULL )
	    printf( " }" );
	printf( "\n" );
	break;
    }
}



/*
 * Generates the end of a menu procedure.
 */
static void end_proc( struct kconfig * scfg, int menu_num )
{
    struct kconfig * cfg;

    printf( "\n\n\n" );
    printf( "\tfocus $w\n" );
    printf( "\tupdate_menu%d $w.config.f\n",
	menu_num );
    printf( "\tglobal winx; global winy\n" );
    printf( "\tset winx [expr [winfo x .]+30]; set winy [expr [winfo y .]+30]\n" );
    printf( "\twm geometry $w +$winx+$winy\n" );

    /*
     * Now that the whole window is in place, we need to wait for an "update"
     * so we can tell the canvas what its virtual size should be.
     *
     * Unfortunately, this causes some ugly screen-flashing because the whole
     * window is drawn, and then it is immediately resized.  It seems
     * unavoidable, though, since "frame" objects won't tell us their size
     * until after an update, and "canvas" objects can't automatically pack
     * around frames.  Sigh.
     */
    printf( "\tupdate idletasks\n" );
    printf( "\t$w.config.canvas create window 0 0 -anchor nw -window $w.config.f\n\n" );
    printf( "\t$w.config.canvas configure \\\n" );
    printf( "\t\t-width [expr [winfo reqwidth $w.config.f] + 1]\\\n" );
    printf( "\t\t-scrollregion \"-1 -1 [expr [winfo reqwidth $w.config.f] + 1] \\\n" );
    printf( "\t\t\t [expr [winfo reqheight $w.config.f] + 1]\"\n\n" );
	 
    /*
     * If the whole canvas will fit in 3/4 of the screen height, do it;
     * otherwise, resize to around 1/2 the screen and let us scroll.
     */
    printf( "\tset winy [expr [winfo reqh $w] - [winfo reqh $w.config.canvas]]\n" );
    printf( "\tset scry [expr [winfo screenh $w] / 2]\n" );
    printf( "\tset maxy [expr [winfo screenh $w] * 3 / 4]\n" );
    printf( "\tset canvtotal [expr [winfo reqh $w.config.f] + 2]\n" );
    printf( "\tif [expr $winy + $canvtotal < $maxy] {\n" );
    printf( "\t\t$w.config.canvas configure -height $canvtotal\n" );
    printf( "\t} else {\n" );
    printf( "\t\t$w.config.canvas configure -height [expr $scry - $winy]\n" );
    printf( "\t}\n" );

    /*
     * Limit the min/max window size.  Height can vary, but not width,
     * because of the limitations of canvas and our laziness.
     */
    printf( "\tupdate idletasks\n" );
    printf( "\twm maxsize $w [winfo width $w] [winfo screenheight $w]\n" );
    printf( "\twm minsize $w [winfo width $w] 100\n\n" );
    printf( "\twm deiconify $w\n" );
    printf( "}\n\n\n" );

    /*
     * Now we generate the companion procedure for the menu we just
     * generated.  This procedure contains all of the code to
     * disable/enable widgets based upon the settings of the other
     * widgets, and will be called first when the window is mapped,
     * and each time one of the buttons in the window are clicked.
     */
    printf( "proc update_menu%d {w}  {\n", menu_num );
    printf( "\tupdate_define\n" );

    /*
     * Clear all of the booleans that are defined in this menu.
     */
    clear_globalflags( scfg );
    for ( cfg = scfg; cfg != NULL; cfg = cfg->next )
    {
	if ( cfg->menu_number == menu_num && cfg->token == token_define_bool
	&&   cfg->optionname  != NULL )
	{
	    if ( ! cfg->global_written )
	    {
		cfg->global_written = 1;
		printf( "\tglobal %s\n", cfg->optionname );
		printf( "\tset %s 0\n",  cfg->optionname );
	    }
	}
    }

    for ( cfg = scfg; cfg != NULL; cfg = cfg->next )
    {
	if ( cfg->menu_number == menu_num
	&&   cfg->token != token_mainmenu_option
	&&   cfg->token != token_choice_item )
	{
	    if ( cfg->cond != NULL )
		generate_if( cfg, cfg->cond, cfg->menu_number, cfg->menu_line );
	    else
	    {
		/*
		 * Treat tristate like conditional here.
		 */
		if ( cfg->token == token_dep_tristate )
		{
		    global( cfg->depend );
		    printf( "\tif {$%s != 1 && $%s != 0 } then { .menu%d.config.f.x%d.y configure -state disabled } else { .menu%d.config.f.x%d.y configure -state normal}\n",
			cfg->depend, cfg->depend,
			menu_num, cfg->menu_line,
			menu_num, cfg->menu_line );
		}
	    }
	}
    }

    printf("}\n\n\n");
}



/*
 * This is the top level function for generating the tk script.
 */
void dump_tk_script( struct kconfig * scfg )
{
    int menu_depth;
    int menu_num [64];
    struct kconfig * menu_first [256];
    struct kconfig * menu_last  [256];
    int imenu;
    struct kconfig * cfg;
    struct kconfig * cfg1 = NULL;
    const char * name = "No Name";

    /*
    * Thread the menu pointers so I can walk each menu separately.
    */
    tot_menu_num = 0;
    menu_depth   = 0;
    for ( cfg = scfg; cfg != NULL; cfg = cfg->next )
    {
	switch ( cfg->token )
	{
	default:
	    break;

	case token_mainmenu_name:
	    name = cfg->label;
	    break;

	case token_mainmenu_option:
	    if ( ++menu_depth >= 64 )
		{ fprintf( stderr, "menus too deep\n" ); exit( 1 ); }
	    if ( ++tot_menu_num >= 256 )
		{ fprintf( stderr, "too many menus\n" ); exit( 1 ); }
	    menu_num   [menu_depth]   = tot_menu_num;
	    menu_first [tot_menu_num] = cfg;
	    menu_last  [tot_menu_num] = cfg;
	    break;

	case token_endmenu:
#if ! defined(BUG_COMPATIBLE)
	    /* flatten menus with proper scoping */
	    if ( --menu_depth < 0 )
		{ fprintf( stderr, "unmatched endmenu\n" ); exit( 1 ); }
#endif
	    break;

	case token_bool:
	case token_choice_header:
	case token_choice_item:
	case token_dep_tristate:
	case token_hex:
	case token_int:
	case token_string:
	case token_tristate:
	    if ( menu_depth == 0 )
		{ fprintf( stderr, "statement not in menu\n" ); exit( 1 ); }
	    menu_last [menu_num [menu_depth]]->menu_next = cfg;
	    menu_last [menu_num [menu_depth]]            = cfg;
	    cfg->menu_next                               = NULL;
	    break;

	case token_define_bool:
	    break;
	}
    }

    /*
     * Generate menus per column setting.
     * There are:
     *   four extra buttons for save/quit/load/store;
     *   one blank button
     *   add two to round up for division
     */
    printf( "set menus_per_column %d\n\n", (tot_menu_num + 4 + 1 + 2) / 3 );

    /*
     * Generate the menus.
     */
    printf( "mainmenu_name \"%s\"\n", name );
    for ( imenu = 1; imenu <= tot_menu_num; ++imenu )
    {
	int menu_line = 0;

	clear_globalflags( scfg );
	start_proc( menu_first[imenu]->label, imenu, 1 );

	for ( cfg = menu_first[imenu]; cfg != NULL; cfg = cfg->menu_next )
	{
	    cfg->menu_number = imenu;

	    switch ( cfg->token )
	    {
	    default:
		break;

	    case token_bool:
		cfg->menu_line = menu_line++;
		printf( "\tbool $w.config.f %d %d \"%s\" %s\n",
		    cfg->menu_number, cfg->menu_line, cfg->label,
		    cfg->optionname );
		break;

	    case token_choice_header:
		/*
		 * I need the first token_choice_item to pick out the right
		 * help text from Documentation/Configure.help.
		 */
		cfg->menu_line = menu_line++;
		printf( "\tglobal %s\n", cfg->optionname );
		printf( "\tminimenu $w.config.f %d %d \"%s\" %s %s\n",
		    cfg->menu_number, cfg->menu_line, cfg->label,
		    cfg->optionname, cfg->next->optionname );
		printf( "\tmenu $w.config.f.x%d.x.menu\n", cfg->menu_line );
		cfg1 = cfg;
		break;

	    case token_choice_item:
		/* note: no menu line; uses choice header menu line */
		printf( "\t$w.config.f.x%d.x.menu add radiobutton -label \"%s\" -variable %s -value \"%s\" -command \"update_menu%d .menu%d.config.f\"\n",
		    cfg1->menu_line, cfg->label, cfg1->optionname,
		    cfg->label, cfg1->menu_number, cfg1->menu_number );
		break;

	    case token_dep_tristate:
		cfg->menu_line = menu_line++;
		printf( "\tdep_tristate $w.config.f %d %d \"%s\" %s %s\n",
		    cfg->menu_number, cfg->menu_line, cfg->label,
		    cfg->optionname, cfg->depend );
		break;

	    case token_hex:
		cfg->menu_line = menu_line++;
		printf( "\thex $w.config.f %d %d \"%s\" %s\n",
		    cfg->menu_number, cfg->menu_line, cfg->label,
		    cfg->optionname );
		break;

	    case token_int:
		cfg->menu_line = menu_line++;
		printf( "\tint $w.config.f %d %d \"%s\" %s\n",
		    cfg->menu_number, cfg->menu_line, cfg->label,
		    cfg->optionname );
		break;

	    case token_string:
		cfg->menu_line = menu_line++;
		printf( "\tistring $w.config.f %d %d \"%s\" %s\n",
		    cfg->menu_number, cfg->menu_line, cfg->label,
		    cfg->optionname );
		break;

	    case token_tristate:
		cfg->menu_line = menu_line++;
		printf( "\ttristate $w.config.f %d %d \"%s\" %s\n",
		    cfg->menu_number, cfg->menu_line, cfg->label,
		    cfg->optionname );
		break;
	    }
	}

	end_proc( scfg, imenu );
    }

    /*
     * The top level menu also needs an update function.  When we exit a
     * submenu, we may need to disable one or more of the submenus on
     * the top level menu, and this procedure will ensure that things are
     * correct.
     */
    clear_globalflags( scfg );
    printf( "proc update_mainmenu {w}  {\n" );
    for ( cfg = scfg; cfg != NULL; cfg = cfg->next )
    {
	if ( cfg->token == token_mainmenu_option && cfg->cond != NULL )
	    generate_if( cfg, cfg->cond, cfg->menu_number, cfg->menu_line );
    }
    printf( "}\n\n\n" );

#if 0
    /*
     * Generate code to set the variables that are "defined".
     */
    for ( cfg = config; cfg != NULL; cfg = cfg->next )
    {
	if ( cfg->token == token_define_bool )
	{
	    if ( cfg->cond != NULL ) 
		generate_if( cfg, cfg->cond, menu_num, cfg->menu_line );
	    else
		printf( "\twrite_define %s %s\n", cfg->optionname, cfg->value );
	}
    }
    #endif

    /*
     * Generate code to load the default settings into the variables.
     * The script in tail.tk will attempt to load .config,
     * which may override these settings, but that's OK.
     */
    for ( cfg = scfg; cfg != NULL; cfg = cfg->next )
    {
	switch ( cfg->token )
	{
	default:
	    break;

	case token_bool:
	case token_choice_item:
	case token_dep_tristate:
	case token_tristate:
	    printf( "set %s 0\n", cfg->optionname );
	    break;

	case token_choice_header:
	    printf( "set %s \"(not set)\"\n", cfg->optionname );
	    break;

	case token_hex:
	case token_int:
	case token_string:
	    printf( "set %s %s\n", cfg->optionname, cfg->value );
	    break;
	}
    }

    /*
     * Generate a function to write all of the variables to a file.
     */
    printf( "proc writeconfig {file1 file2} {\n" );
    printf( "\tset cfg [open $file1 w]\n" );
    printf( "\tset autocfg [open $file2 w]\n" );
    printf( "\tset notmod 1\n" );
    printf( "\tset notset 0\n" );
    printf( "\tputs $cfg \"#\"\n");
    printf( "\tputs $cfg \"# Automatically generated make config: don't edit\"\n");
    printf( "\tputs $cfg \"#\"\n" );

    printf( "\tputs $autocfg \"/*\"\n" );
    printf( "\tputs $autocfg \" * Automatically generated C config: don't edit\"\n" );
    printf( "\tputs $autocfg \" */\"\n" );
    printf( "\tputs $autocfg \"#define AUTOCONF_INCLUDED\"\n" );

    clear_globalflags( scfg );
    for ( cfg = scfg; cfg != NULL; cfg = cfg->next )
    {
	switch ( cfg->token )
	{
	default:
	    break;

	case token_bool:
	case token_choice_header:
	case token_comment:
	case token_define_bool:
	case token_dep_tristate:
	case token_hex:
	case token_int:
	case token_string:
	case token_tristate:
	    generate_writeconfig( cfg );
	    break;
	}
    }
    printf( "\tclose $cfg\n" );
    printf( "\tclose $autocfg\n" );
    printf( "}\n\n\n" );

    /*
     * Generate a simple function that updates the master choice
     * variable depending upon what values were loaded from a .config
     * file.  
     */
    printf( "proc clear_choices { } {\n" );
    for ( cfg = scfg; cfg != NULL; cfg = cfg->next )
    {
	if ( cfg->token == token_choice_header )
	{
	    for ( cfg1  = cfg->next; 
		  cfg1 != NULL && cfg1->token == token_choice_item;
		  cfg1  = cfg1->next )
	    {
		printf( "\tglobal %s; set %s 0\n",
		    cfg1->optionname, cfg1->optionname );
	    }
	}
    }
    printf( "}\n\n\n" );

    printf( "proc update_choices { } {\n" );
    for ( cfg = scfg; cfg != NULL; cfg = cfg->next )
    {
	if ( cfg->token == token_choice_header )
	{
	    printf( "\tglobal %s\n", cfg->optionname );
	    for ( cfg1  = cfg->next; 
		  cfg1 != NULL && cfg1->token == token_choice_item;
		  cfg1  = cfg1->next )
	    {
		printf( "\tglobal %s\n", cfg1->optionname );
		printf( "\tif { $%s == 1 } then { set %s \"%s\" }\n",
		    cfg1->optionname, cfg->optionname, cfg1->label );
	    }
	}
    }
    printf( "}\n\n\n" );

    printf( "proc update_define { } {\n" );
    clear_globalflags( scfg );
    for ( cfg = scfg; cfg != NULL; cfg = cfg->next )
    {
	if ( cfg->token == token_define_bool )
	{
	    cfg->global_written = 1;
	    printf( "\tglobal %s\n", cfg->optionname );
	}
    }

    for ( cfg = scfg; cfg != NULL; cfg = cfg->next )
    {
	if( cfg->token == token_define_bool )
	{
	    if ( cfg->cond == NULL )
		printf( "\tset %s %s\n", cfg->optionname, cfg->value );
	    else
		generate_if( cfg, cfg->cond, -1, 0 );
	}
    }
    printf( "}\n\n\n" );

    /*
     * That's it.  We are done.  The output of this file will have header.tk
     * prepended and tail.tk appended to create an executable wish script.
     */
}
