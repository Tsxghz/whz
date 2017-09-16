/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 * Released under the terms of the GNU GPL v2.0.
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

#define LKC_DIRECT_LINK
#include "lkc.h"

struct symbol symbol_yes = {
	.name = "y",
	.curr = { "y", yes },
	.flags = SYMBOL_YES|SYMBOL_VALID,
}, symbol_mod = {
	.name = "m",
	.curr = { "m", mod },
	.flags = SYMBOL_MOD|SYMBOL_VALID,
}, symbol_no = {
	.name = "n",
	.curr = { "n", no },
	.flags = SYMBOL_NO|SYMBOL_VALID,
}, symbol_empty = {
	.name = "",
	.curr = { "", no },
	.flags = SYMBOL_VALID,
};

int sym_change_count;
struct symbol *modules_sym;
tristate modules_val;

void sym_add_default(struct symbol *sym, const char *def)
{
	struct property *prop = prop_alloc(P_DEFAULT, sym);

	prop->expr = expr_alloc_symbol(sym_lookup(def, 1));
}

void sym_init(void)
{
	struct symbol *sym;
	struct utsname uts;
	char *p;
	static bool inited = false;

	if (inited)
		return;
	inited = true;

	uname(&uts);

	sym = sym_lookup("ARCH", 0);
	sym->type = S_STRING;
	sym->flags |= SYMBOL_AUTO;
	p = getenv("ARCH");
	if (p)
		sym_add_default(sym, p);

	sym = sym_lookup("KERNELRELEASE", 0);
	sym->type = S_STRING;
	sym->flags |= SYMBOL_AUTO;
	p = getenv("KERNELRELEASE");
	if (p)
		sym_add_default(sym, p);

	sym = sym_lookup("UNAME_RELEASE", 0);
	sym->type = S_STRING;
	sym->flags |= SYMBOL_AUTO;
	sym_add_default(sym, uts.release);
}

enum symbol_type sym_get_type(struct symbol *sym)
{
	enum symbol_type type = sym->type;

	if (type == S_TRISTATE) {
		if (sym_is_choice_value(sym) && sym->visible == yes)
			type = S_BOOLEAN;
		else if (modules_val == no)
			type = S_BOOLEAN;
	}
	return type;
}

const char *sym_type_name(enum symbol_type type)
{
	switch (type) {
	case S_BOOLEAN:
		return "boolean";
	case S_TRISTATE:
		return "tristate";
	case S_INT:
		return "integer";
	case S_HEX:
		return "hex";
	case S_STRING:
		return "string";
	case S_UNKNOWN:
		return "unknown";
	case S_OTHER:
		break;
	}
	return "???";
}

struct property *sym_get_choice_prop(struct symbol *sym)
{
	struct property *prop;

	for_all_choices(sym, prop)
		return prop;
	return NULL;
}

struct property *sym_get_default_prop(struct symbol *sym)
{
	struct property *prop;

	for_all_defaults(sym, prop) {
		prop->visible.tri = expr_calc_value(prop->visible.expr);
		if (prop->visible.tri != no)
			return prop;
	}
	return NULL;
}

struct property *sym_get_range_prop(struct symbol *sym)
{
	struct property *prop;

	for_all_properties(sym, prop, P_RANGE) {
		prop->visible.tri = expr_calc_value(prop->visible.expr);
		if (prop->visible.tri != no)
			return prop;
	}
	return NULL;
}

static void sym_calc_visibility(struct symbol *sym)
{
	struct property *prop;
	tristate tri;

	/* any prompt visible? */
	tri = no;
	for_all_prompts(sym, prop) {
		prop->visible.tri = expr_calc_value(prop->visible.expr);
		tri = E_OR(tri, prop->visible.tri);
	}
	if (tri == mod && (sym->type != S_TRISTATE || modules_val == no))
		tri = yes;
	if (sym->visible != tri) {
		sym->visible = tri;
		sym_set_changed(sym);
	}
	if (sym_is_choice_value(sym))
		return;
	tri = no;
	if (sym->rev_dep.expr)
		tri = expr_calc_value(sym->rev_dep.expr);
	if (tri == mod && sym_get_type(sym) == S_BOOLEAN)
		tri = yes;
	if (sym->rev_dep.tri != tri) {
		sym->rev_dep.tri = tri;
		sym_set_changed(sym);
	}
}

static struct symbol *sym_calc_choice(struct symbol *sym)
{
	struct symbol *def_sym;
	struct property *prop;
	struct expr *e;

	/* is the user choice visible? */
	def_sym = sym->user.val;
	if (def_sym) {
		sym_calc_visibility(def_sym);
		if (def_sym->visible != no)
			return def_sym;
	}

	/* any of the defaults visible? */
	for_all_defaults(sym, prop) {
		prop->visible.tri = expr_calc_value(prop->visible.expr);
		if (prop->visible.tri == no)
			continue;
		def_sym = prop_get_symbol(prop);
		sym_calc_visibility(def_sym);
		if (def_sym->visible != no)
			return def_sym;
	}

	/* just get the first visible value */
	prop = sym_get_choice_prop(sym);
	for (e = prop->expr; e; e = e->left.expr) {
		def_sym = e->right.sym;
		sym_calc_visibility(def_sym);
		if (def_sym->visible != no)
			return def_sym;
	}

	/* no choice? reset tristate value */
	sym->curr.tri = no;
	return NULL;
}

void sym_calc_value(struct symbol *sym)
{
	struct symbol_value newval, oldval;
	struct property *prop;
	struct expr *e;

	if (!sym)
		return;

	if (sym->flags & SYMBOL_VALID)
		return;
	sym->flags |= SYMBOL_VALID;

	oldval = sym->curr;

	switch (sym->type) {
	case S_INT:
	case S_HEX:
	case S_STRING:
		newval = symbol_empty.curr;
		break;
	case S_BOOLEAN:
	case S_TRISTATE:
		newval = symbol_no.curr;
		break;
	default:
		sym->curr.val = sym->name;
		sym->curr.tri = no;
		return;
	}
	if (!sym_is_choice_value(sym))
		sym->flags &= ~SYMBOL_WRITE;

	sym_calc_visibility(sym);

	/* set default if recursively called */
	sym->curr = newval;

	switch (sym_get_type(sym)) {
	case S_BOOLEAN:
	case S_TRISTATE:
		if (sym_is_choice_value(sym) && sym->visible == yes) {
			prop = sym_get_choice_prop(sym);
			newval.tri = (prop_get_symbol(prop)->curr.val == sym) ? yes : no;
		} else if (E_OR(sym->visible, sym->rev_dep.tri) != no) {
			sym->flags |= SYMBOL_WRITE;
			if (sym_has_value(sym))
				newval.tri = sym->user.tri;
			else if (!sym_is_choice(sym)) {
				prop = sym_get_default_prop(sym);
				if (prop)
					newval.tri = expr_calc_value(prop->expr);
			}
			newval.tri = E_OR(E_AND(newval.tri, sym->visible), sym->rev_dep.tri);
		} else if (!sym_is_choice(sym)) {
			prop = sym_get_default_prop(sym);
			if (prop) {
				sym->flags |= SYMBOL_WRITE;
				newval.tri = expr_calc_value(prop->expr);
			}
		}
		if (newval.tri == mod && sym_get_type(sym) == S_BOOLEAN)
			newval.tri = yes;
		break;
	case S_STRING:
	case S_HEX:
	case S_INT:
		if (sym->visible != no) {
			sym->flags |= SYMBOL_WRITE;
			if (sym_has_value(sym)) {
				newval.val = sym->user.val;
				break;
			}
		}
		prop = sym_get_default_prop(sym);
		if (prop) {
			struct symbol *ds = prop_get_symbol(prop);
			if (ds) {
				sym->flags |= SYMBOL_WRITE;
				sym_calc_value(ds);
				newval.val = ds->curr.val;
			}
		}
		break;
	default:
		;
	}

	sym->curr = newval;
	if (sym_is_choice(sym) && newval.tri == yes)
		sym->curr.val = sym_calc_choice(sym);

	if (memcmp(&oldval, &sym->curr, sizeof(oldval)))
		sym_set_changed(sym);
	if (modules_sym == sym)
		modules_val = modules_sym->curr.tri;

	if (sym_is_choice(sym)) {
		int flags = sym->flags & (SYMBOL_CHANGED | SYMBOL_WRITE);
		prop = sym_get_choice_prop(sym);
		for (e = prop->expr; e; e = e->left.expr) {
			e->right.sym->flags |= flags;
			if (flags & SYMBOL_CHANGED)
				sym_set_changed(e->right.sym);
		}
	}
}

void sym_clear_all_valid(void)
{
	struct symbol *sym;
	int i;

	for_all_symbols(i, sym)
		sym->flags &= ~SYMBOL_VALID;
	sym_change_count++;
	if (modules_sym)
		sym_calc_value(modules_sym);
}

void sym_set_changed(struct symbol *sym)
{
	struct property *prop;

	sym->flags |= SYMBOL_CHANGED;
	for (prop = sym->prop; prop; prop = prop->next) {
		if (prop->menu)
			prop->menu->flags |= MENU_CHANGED;
	}
}

void sym_set_all_changed(void)
{
	struct symbol *sym;
	int i;

	for_all_symbols(i, sym)
		sym_set_changed(sym);
}

bool sym_tristate_within_range(struct symbol *sym, tristate val)
{
	int type = sym_get_type(sym);

	if (sym->visible == no)
		return false;

	if (type != S_BOOLEAN && type != S_TRISTATE)
		return false;

	if (type == S_BOOLEAN && val == mod)
		return false;
	if (sym->visible <= sym->rev_dep.tri)
		return false;
	if (sym_is_choice_value(sym) && sym->visible == yes)
		return val == yes;
	return val >= sym->rev_dep.tri && val <= sym->visible;
}

bool sym_set_tristate_value(struct symbol *sym, tristate val)
{
	tristate oldval = sym_get_tristate_value(sym);

	if (oldval != val && !sym_tristate_within_range(sym, val))
		return false;

	if (sym->flags & SYMBOL_NEW) {
		sym->flags &= ~SYMBOL_NEW;
		sym_set_changed(sym);
	}
	if (sym_is_choice_value(sym) && val == yes) {
		struct symbol *cs = prop_get_symbol(sym_get_choice_prop(sym));

		cs->user.val = sym;
		cs->flags &= ~SYMBOL_NEW;
	}

	sym->user.tri = val;
	if (oldval != val) {
		sym_clear_all_valid();
		if (sym == modules_sym)
			sym_set_all_changed();
	}

	return true;
}

tristate sym_toggle_tristate_value(struct symbol *sym)
{
	tristate oldval, newval;

	oldval = newval = sym_get_tristate_value(sym);
	do {
		switch (newval) {
		case no:
			newval = mod;
			break;
		case mod:
			newval = yes;
			break;
		case yes:
			newval = no;
			break;
		}
		if (sym_set_tristate_value(sym, newval))
			break;
	} while (oldval != newval);
	return newval;
}

bool sym_string_valid(struct symbol *sym, const char *str)
{
	signed char ch;

	switch (sym->type) {
	case S_STRING:
		return true;
	case S_INT:
		ch = *str++;
		if (ch == '-')
			ch = *str++;
		if (!isdigit(ch))
			return false;
		if (ch == '0' && *str != 0)
			return false;
		while ((ch = *str++)) {
			if (!isdigit(ch))
				return false;
		}
		return true;
	case S_HEX:
		if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X'))
			str += 2;
		ch = *str++;
		do {
			if (!isxdigit(ch))
				return false;
		} while ((ch = *str++));
		return true;
	case S_BOOLEAN:
	case S_TRISTATE:
		switch (str[0]) {
		case 'y': case 'Y':
		case 'm': case 'M':
		case 'n': case 'N':
			return true;
		}
		return false;
	default:
		return false;
	}
}

bool sym_string_within_range(struct symbol *sym, const char *str)
{
	struct property *prop;
	int val;

	switch (sym->type) {
	case S_STRING:
		return sym_string_valid(sym, str);
	case S_INT:
		if (!sym_string_valid(sym, str))
			return false;
		prop = sym_get_range_prop(sym);
		if (!prop)
			return true;
		val = strtol(str, NULL, 10);
		return val >= strtol(prop->expr->left.sym->name, NULL, 10) &&
		       val <= strtol(prop->expr->right.sym->name, NULL, 10);
	case S_HEX:
		if (!sym_string_valid(sym, str))
			return false;
		prop = sym_get_range_prop(sym);
		if (!prop)
			return true;
		val = strtol(str, NULL, 16);
		return val >= strtol(prop->expr->left.sym->name, NULL, 16) &&
		       val <= strtol(prop->expr->right.sym->name, NULL, 16);
	case S_BOOLEAN:
	case S_TRISTATE:
		switch (str[0]) {
		case 'y': case 'Y':
			return sym_tristate_within_range(sym, yes);
		case 'm': case 'M':
			return sym_tristate_within_range(sym, mod);
		case 'n': case 'N':
			return sym_tristate_within_range(sym, no);
		}
		return false;
	default:
		return false;
	}
}

bool sym_set_string_value(struct symbol *sym, const char *newval)
{
	const char *oldval;
	char *val;
	int size;

	switch (sym->type) {
	case S_BOOLEAN:
	case S_TRISTATE:
		switch (newval[0]) {
		case 'y': case 'Y':
			return sym_set_tristate_value(sym, yes);
		case 'm': case 'M':
			return sym_set_tristate_value(sym, mod);
		case 'n': case 'N':
			return sym_set_tristate_value(sym, no);
		}
		return false;
	default:
		;
	}

	if (!sym_string_within_range(sym, newval))
		return false;

	if (sym->flags & SYMBOL_NEW) {
		sym->flags &= ~SYMBOL_NEW;
		sym_set_changed(sym);
	}

	oldval = sym->user.val;
	size = strlen(newval) + 1;
	if (sym->type == S_HEX && (newval[0] != '0' || (newval[1] != 'x' && newval[1] != 'X'))) {
		size += 2;
		sym->user.val = val = malloc(size);
		*val++ = '0';
		*val++ = 'x';
	} else if (!oldval || strcmp(oldval, newval))
		sym->user.val = val = malloc(size);
	else
		return true;

	strcpy(val, newval);
	free((void *)oldval);
	sym_clear_all_valid();

	return true;
}

const char *sym_get_string_value(struct symbol *sym)
{
	tristate val;

	switch (sym->type) {
	case S_BOOLEAN:
	case S_TRISTATE:
		val = sym_get_tristate_value(sym);
		switch (val) {
		case no:
			return "n";
		case mod:
			return "m";
		case yes:
			return "y";
		}
		break;
	default:
		;
	}
	return (const char *)sym->curr.val;
}

bool sym_is_changable(struct symbol *sym)
{
	return sym->visible > sym->rev_dep.tri;
}

struct symbol *sym_lookup(const char *name, int isconst)
{
	struct symbol *symbol;
	const char *ptr;
	char *new_name;
	int hash = 0;

	if (name) {
		if (name[0] && !name[1]) {
			switch (name[0]) {
			case 'y': return &symbol_yes;
			case 'm': return &symbol_mod;
			case 'n': return &symbol_no;
			}
		}
		for (ptr = name; *ptr; ptr++)
			hash += *ptr;
		hash &= 0xff;

		for (symbol = symbol_hash[hash]; symbol; symbol = symbol->next) {
			if (!strcmp(symbol->name, name)) {
				if ((isconst && symbol->flags & SYMBOL_CONST) ||
				    (!isconst && !(symbol->flags & SYMBOL_CONST)))
					return symbol;
			}
		}
		new_name = strdup(name);
	} else {
		new_name = NULL;
		hash = 256;
	}

	symbol = malloc(sizeof(*symbol));
	memset(symbol, 0, sizeof(*symbol));
	symbol->name = new_name;
	symbol->type = S_UNKNOWN;
	symbol->flags = SYMBOL_NEW;
	if (isconst)
		symbol->flags |= SYMBOL_CONST;

	symbol->next = symbol_hash[hash];
	symbol_hash[hash] = symbol;

	return symbol;
}

struct symbol *sym_find(const char *name)
{
	struct symbol *symbol = NULL;
	const char *ptr;
	int hash = 0;

	if (!name)
		return NULL;

	if (name[0] && !name[1]) {
		switch (name[0]) {
		case 'y': return &symbol_yes;
		case 'm': return &symbol_mod;
		case 'n': return &symbol_no;
		}
	}
	for (ptr = name; *ptr; ptr++)
		hash += *ptr;
	hash &= 0xff;

	for (symbol = symbol_hash[hash]; symbol; symbol = symbol->next) {
		if (!strcmp(symbol->name, name) &&
		    !(symbol->flags & SYMBOL_CONST))
				break;
	}

	return symbol;
}

struct symbol *sym_check_deps(struct symbol *sym);

static struct symbol *sym_check_expr_deps(struct expr *e)
{
	struct symbol *sym;

	if (!e)
		return NULL;
	switch (e->type) {
	case E_OR:
	case E_AND:
		sym = sym_check_expr_deps(e->left.expr);
		if (sym)
			return sym;
		return sym_check_expr_deps(e->right.expr);
	case E_NOT:
		return sym_check_expr_deps(e->left.expr);
	case E_EQUAL:
	case E_UNEQUAL:
		sym = sym_check_deps(e->left.sym);
		if (sym)
			return sym;
		return sym_check_deps(e->right.sym);
	case E_SYMBOL:
		return sym_check_deps(e->left.sym);
	default:
		break;
	}
	printf("Oops! How to check %d?\n", e->type);
	return NULL;
}

struct symbol *sym_check_deps(struct symbol *sym)
{
	struct symbol *sym2;
	struct property *prop;

	if (sym->flags & SYMBOL_CHECK_DONE)
		return NULL;
	if (sym->flags & SYMBOL_CHECK) {
		printf("Warning! Found recursive dependency: %s", sym->name);
		return sym;
	}

	sym->flags |= (SYMBOL_CHECK | SYMBOL_CHECKED);
	sym2 = sym_check_expr_deps(sym->rev_dep.expr);
	if (sym2)
		goto out;

	for (prop = sym->prop; prop; prop = prop->next) {
		if (prop->type == P_CHOICE || prop->type == P_SELECT)
			continue;
		sym2 = sym_check_expr_deps(prop->visible.expr);
		if (sym2)
			goto out;
		if (prop->type != P_DEFAULT || sym_is_choice(sym))
			continue;
		sym2 = sym_check_expr_deps(prop->expr);
		if (sym2)
			goto out;
	}
out:
	if (sym2)
		printf(" %s", sym->name);
	sym->flags &= ~SYMBOL_CHECK;
	return sym2;
}

struct property *prop_alloc(enum prop_type type, struct symbol *sym)
{
	struct property *prop;
	struct property **propp;

	prop = malloc(sizeof(*prop));
	memset(prop, 0, sizeof(*prop));
	prop->type = type;
	prop->sym = sym;
	prop->file = current_file;
	prop->lineno = zconf_lineno();

	/* append property to the prop list of symbol */
	if (sym) {
		for (propp = &sym->prop; *propp; propp = &(*propp)->next)
			;
		*propp = prop;
	}

	return prop;
}

struct symbol *prop_get_symbol(struct property *prop)
{
	if (prop->expr && (prop->expr->type == E_SYMBOL ||
			   prop->expr->type == E_CHOICE))
		return prop->expr->left.sym;
	return NULL;
}

const char *prop_get_type_name(enum prop_type type)
{
	switch (type) {
	case P_PROMPT:
		return "prompt";
	case P_COMMENT:
		return "comment";
	case P_MENU:
		return "menu";
	case P_DEFAULT:
		return "default";
	case P_CHOICE:
		return "choice";
	case P_SELECT:
		return "select";
	case P_RANGE:
		return "range";
	case P_UNKNOWN:
		break;
	}
	return "unknown";
}
