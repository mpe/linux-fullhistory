/*
 * tkparse.h
 */

/*
 * Define this symbol to generate exactly the same output, byte for byte,
 * as the previous version of xconfig.  I need to do this to make sure I
 * I don't break anything in my moby edit. -- mec
 */

#define BUG_COMPATIBLE

/*
 * Token types (mostly statement types).
 */

enum e_token
{
    token_UNKNOWN,
    token_bool, 
    token_choice_header,
    token_choice_item,
    token_comment, 
    token_define_bool,
    token_dep_tristate,
    token_else, 
    token_endmenu,
    token_fi, 
    token_hex,
    token_if, 
    token_int,
    token_mainmenu_name, 
    token_mainmenu_option, 
    token_source,
    token_string,
    token_then,
    token_tristate, 
    token_unset,
};

/*
 * Operator types for conditionals.
 */

enum operator
{
    op_eq,
    op_neq,
    op_and,
    op_and1,
    op_or,
    op_bang,
    op_lparen,
    op_rparen,
    op_constant,
    op_variable,
    op_kvariable,
    op_nuked
};

/*
 * Conditions come in linked lists.
 * Some operators take strings:
 *
 *   op_constant   "foo"
 *   op_variable   "$ARCH", "$CONFIG_PMAC"
 *   op_kvariable  "$CONFIG_EXPERIMENTAL"
 *
 * Most "$..." constructs refer to a variable which is defined somewhere
 * in the script, so they become op_kvariable's instead.  Note that it
 * is legal to test variables which are never defined, such as variables
 * that are meaningful only on other architectures.
 */

struct condition
{
    struct condition * next;
    enum operator op;
    const char * str;		/* op_constant, op_variable */
    struct kconfig * cfg;	/* op_kvariable */
};

/*
 * A statement from a config.in file
 */

struct kconfig
{
    struct kconfig *	next;
    enum e_token	token;
    char *		optionname;
    char *		label;
    char *		value;
    struct condition *	cond;
    char *		depend;		/* token_dep_tristate */
    struct kconfig *	cfg_parent;	/* token_choice_item */

    /* used only in tkgen.c */
    char		global_written;
    int			menu_number;
    int			menu_line;
    struct kconfig *	menu_next;
};



/*
 * Prototypes
 */

extern void fix_conditionals ( struct kconfig * scfg );		/* tkcond.c */
extern void dump_tk_script   ( struct kconfig * scfg );		/* tkgen.c  */
