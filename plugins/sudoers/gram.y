%{
/*
 * Copyright (c) 1996, 1998-2005, 2007-2013
 *	Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F39502-99-1-0512.
 */

#include <config.h>

#include <sys/types.h>
#include <stdio.h>
#ifdef STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# ifdef HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif /* STDC_HEADERS */
#ifdef HAVE_STRING_H
# include <string.h>
#endif /* HAVE_STRING_H */
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif /* HAVE_STRINGS_H */
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif /* HAVE_UNISTD_H */
#ifdef HAVE_INTTYPES_H
# include <inttypes.h>
#endif
#if defined(YYBISON) && defined(HAVE_ALLOCA_H) && !defined(__GNUC__)
# include <alloca.h>
#endif /* YYBISON && HAVE_ALLOCA_H && !__GNUC__ */
#include <limits.h>

#include "sudoers.h" /* XXX */
#include "parse.h"
#include "toke.h"

/*
 * We must define SIZE_MAX for yacc's skeleton.c.
 * If there is no SIZE_MAX or SIZE_T_MAX we have to assume that size_t
 * could be signed (as it is on SunOS 4.x).
 */
#ifndef SIZE_MAX
# ifdef SIZE_T_MAX
#  define SIZE_MAX	SIZE_T_MAX
# else
#  define SIZE_MAX	INT_MAX
# endif /* SIZE_T_MAX */
#endif /* SIZE_MAX */

/*
 * Globals
 */
extern int sudolineno;
extern int last_token;
extern char *sudoers;
bool sudoers_warnings = true;
bool parse_error = false;
int errorlineno = -1;
char *errorfile = NULL;

struct defaults_list defaults;
struct userspec_list userspecs;

/*
 * Local protoypes
 */
static void  add_defaults(int, struct member *, struct defaults *);
static void  add_userspec(struct member *, struct privilege *);
static struct defaults *new_default(char *, char *, int);
static struct member *new_member(char *, int);
static struct sudo_digest *new_digest(int, const char *);
%}

%union {
    struct cmndspec *cmndspec;
    struct defaults *defaults;
    struct member *member;
    struct runascontainer *runas;
    struct privilege *privilege;
    struct sudo_digest *digest;
    struct sudo_command command;
    struct cmndtag tag;
    struct selinux_info seinfo;
    struct solaris_privs_info privinfo;
    char *string;
    int tok;
}

%start file				/* special start symbol */
%token <command> COMMAND		/* absolute pathname w/ optional args */
%token <string>  ALIAS			/* an UPPERCASE alias name */
%token <string>	 DEFVAR			/* a Defaults variable name */
%token <string>  NTWKADDR		/* ipv4 or ipv6 address */
%token <string>  NETGROUP		/* a netgroup (+NAME) */
%token <string>  USERGROUP		/* a usergroup (%NAME) */
%token <string>  WORD			/* a word */
%token <string>  DIGEST			/* a SHA-2 digest */
%token <tok>	 DEFAULTS		/* Defaults entry */
%token <tok>	 DEFAULTS_HOST		/* Host-specific defaults entry */
%token <tok>	 DEFAULTS_USER		/* User-specific defaults entry */
%token <tok>	 DEFAULTS_RUNAS		/* Runas-specific defaults entry */
%token <tok>	 DEFAULTS_CMND		/* Command-specific defaults entry */
%token <tok> 	 NOPASSWD		/* no passwd req for command */
%token <tok> 	 PASSWD			/* passwd req for command (default) */
%token <tok> 	 NOEXEC			/* preload dummy execve() for cmnd */
%token <tok> 	 EXEC			/* don't preload dummy execve() */
%token <tok>	 SETENV			/* user may set environment for cmnd */
%token <tok>	 NOSETENV		/* user may not set environment */
%token <tok>	 LOG_INPUT		/* log user's cmnd input */
%token <tok>	 NOLOG_INPUT		/* don't log user's cmnd input */
%token <tok>	 LOG_OUTPUT		/* log cmnd output */
%token <tok>	 NOLOG_OUTPUT		/* don't log cmnd output */
%token <tok>	 ALL			/* ALL keyword */
%token <tok>	 COMMENT		/* comment and/or carriage return */
%token <tok>	 HOSTALIAS		/* Host_Alias keyword */
%token <tok>	 CMNDALIAS		/* Cmnd_Alias keyword */
%token <tok>	 USERALIAS		/* User_Alias keyword */
%token <tok>	 RUNASALIAS		/* Runas_Alias keyword */
%token <tok>	 ':' '=' ',' '!' '+' '-' /* union member tokens */
%token <tok>	 '(' ')'		/* runas tokens */
%token <tok>	 ERROR
%token <tok>	 TYPE			/* SELinux type */
%token <tok>	 ROLE			/* SELinux role */
%token <tok>	 PRIVS			/* Solaris privileges */
%token <tok>	 LIMITPRIVS		/* Solaris limit privileges */
%token <tok>	 MYSELF			/* run as myself, not another user */
%token <tok>	 SHA224			/* sha224 digest */
%token <tok>	 SHA256			/* sha256 digest */
%token <tok>	 SHA384			/* sha384 digest */
%token <tok>	 SHA512			/* sha512 digest */

%type <cmndspec>  cmndspec
%type <cmndspec>  cmndspeclist
%type <defaults>  defaults_entry
%type <defaults>  defaults_list
%type <member>	  cmnd
%type <member>	  opcmnd
%type <member>	  digcmnd
%type <member>	  cmndlist
%type <member>	  host
%type <member>	  hostlist
%type <member>	  ophost
%type <member>	  opuser
%type <member>	  user
%type <member>	  userlist
%type <member>	  opgroup
%type <member>	  group
%type <member>	  grouplist
%type <runas>	  runasspec
%type <runas>	  runaslist
%type <privilege> privilege
%type <privilege> privileges
%type <tag>	  cmndtag
%type <seinfo>	  selinux
%type <string>	  rolespec
%type <string>	  typespec
%type <privinfo>  solarisprivs
%type <string>	  privsspec
%type <string>	  limitprivsspec
%type <digest>	  digest

%%

file		:	{ ; }
		|	line
		;

line		:	entry
		|	line entry
		;

entry		:	COMMENT {
			    ;
			}
                |       error COMMENT {
			    yyerrok;
			}
		|	userlist privileges {
			    add_userspec($1, $2);
			}
		|	USERALIAS useraliases {
			    ;
			}
		|	HOSTALIAS hostaliases {
			    ;
			}
		|	CMNDALIAS cmndaliases {
			    ;
			}
		|	RUNASALIAS runasaliases {
			    ;
			}
		|	DEFAULTS defaults_list {
			    add_defaults(DEFAULTS, NULL, $2);
			}
		|	DEFAULTS_USER userlist defaults_list {
			    add_defaults(DEFAULTS_USER, $2, $3);
			}
		|	DEFAULTS_RUNAS userlist defaults_list {
			    add_defaults(DEFAULTS_RUNAS, $2, $3);
			}
		|	DEFAULTS_HOST hostlist defaults_list {
			    add_defaults(DEFAULTS_HOST, $2, $3);
			}
		|	DEFAULTS_CMND cmndlist defaults_list {
			    add_defaults(DEFAULTS_CMND, $2, $3);
			}
		;

defaults_list	:	defaults_entry
		|	defaults_list ',' defaults_entry {
			    list_append($1, $3);
			    $$ = $1;
			}
		;

defaults_entry	:	DEFVAR {
			    $$ = new_default($1, NULL, true);
			}
		|	'!' DEFVAR {
			    $$ = new_default($2, NULL, false);
			}
		|	DEFVAR '=' WORD {
			    $$ = new_default($1, $3, true);
			}
		|	DEFVAR '+' WORD {
			    $$ = new_default($1, $3, '+');
			}
		|	DEFVAR '-' WORD {
			    $$ = new_default($1, $3, '-');
			}
		;

privileges	:	privilege
		|	privileges ':' privilege {
			    list_append($1, $3);
			    $$ = $1;
			}
		;

privilege	:	hostlist '=' cmndspeclist {
			    struct privilege *p = ecalloc(1, sizeof(*p));
			    list2tq(&p->hostlist, $1);
			    list2tq(&p->cmndlist, $3);
			    p->prev = p;
			    /* p->next = NULL; */
			    $$ = p;
			}
		;

ophost		:	host {
			    $$ = $1;
			    $$->negated = false;
			}
		|	'!' host {
			    $$ = $2;
			    $$->negated = true;
			}
		;

host		:	ALIAS {
			    $$ = new_member($1, ALIAS);
			}
		|	ALL {
			    $$ = new_member(NULL, ALL);
			}
		|	NETGROUP {
			    $$ = new_member($1, NETGROUP);
			}
		|	NTWKADDR {
			    $$ = new_member($1, NTWKADDR);
			}
		|	WORD {
			    $$ = new_member($1, WORD);
			}
		;

cmndspeclist	:	cmndspec
		|	cmndspeclist ',' cmndspec {
			    list_append($1, $3);
#ifdef HAVE_SELINUX
			    /* propagate role and type */
			    if ($3->role == NULL)
				$3->role = $3->prev->role;
			    if ($3->type == NULL)
				$3->type = $3->prev->type;
#endif /* HAVE_SELINUX */
#ifdef HAVE_PRIV_SET
			    /* propagate privs & limitprivs */
			    if ($3->privs == NULL)
			        $3->privs = $3->prev->privs;
			    if ($3->limitprivs == NULL)
			        $3->limitprivs = $3->prev->limitprivs;
#endif /* HAVE_PRIV_SET */
			    /* propagate tags and runas list */
			    if ($3->tags.nopasswd == UNSPEC)
				$3->tags.nopasswd = $3->prev->tags.nopasswd;
			    if ($3->tags.noexec == UNSPEC)
				$3->tags.noexec = $3->prev->tags.noexec;
			    if ($3->tags.setenv == UNSPEC &&
				$3->prev->tags.setenv != IMPLIED)
				$3->tags.setenv = $3->prev->tags.setenv;
			    if ($3->tags.log_input == UNSPEC)
				$3->tags.log_input = $3->prev->tags.log_input;
			    if ($3->tags.log_output == UNSPEC)
				$3->tags.log_output = $3->prev->tags.log_output;
			    if ((tq_empty(&$3->runasuserlist) &&
				 tq_empty(&$3->runasgrouplist)) &&
				(!tq_empty(&$3->prev->runasuserlist) ||
				 !tq_empty(&$3->prev->runasgrouplist))) {
				$3->runasuserlist = $3->prev->runasuserlist;
				$3->runasgrouplist = $3->prev->runasgrouplist;
			    }
			    $$ = $1;
			}
		;

cmndspec	:	runasspec selinux solarisprivs cmndtag digcmnd {
			    struct cmndspec *cs = ecalloc(1, sizeof(*cs));
			    if ($1 != NULL) {
				list2tq(&cs->runasuserlist, $1->runasusers);
				list2tq(&cs->runasgrouplist, $1->runasgroups);
				efree($1);
			    } else {
				tq_init(&cs->runasuserlist);
				tq_init(&cs->runasgrouplist);
			    }
#ifdef HAVE_SELINUX
			    cs->role = $2.role;
			    cs->type = $2.type;
#endif
#ifdef HAVE_PRIV_SET
			    cs->privs = $3.privs;
			    cs->limitprivs = $3.limitprivs;
#endif
			    cs->tags = $4;
			    cs->cmnd = $5;
			    cs->prev = cs;
			    cs->next = NULL;
			    /* sudo "ALL" implies the SETENV tag */
			    if (cs->cmnd->type == ALL && !cs->cmnd->negated &&
				cs->tags.setenv == UNSPEC)
				cs->tags.setenv = IMPLIED;
			    $$ = cs;
			}
		;

digest		:	SHA224 ':' DIGEST {
			    $$ = new_digest(SUDO_DIGEST_SHA224, $3);
			}
		|	SHA256 ':' DIGEST {
			    $$ = new_digest(SUDO_DIGEST_SHA256, $3);
			}
		|	SHA384 ':' DIGEST {
			    $$ = new_digest(SUDO_DIGEST_SHA384, $3);
			}
		|	SHA512 ':' DIGEST {
			    $$ = new_digest(SUDO_DIGEST_SHA512, $3);
			}
		;

digcmnd		:	opcmnd {
			    $$ = $1;
			}
		|	digest opcmnd {
			    /* XXX - yuck */
			    struct sudo_command *c = (struct sudo_command *)($2->name);
			    c->digest = $1;
			    $$ = $2;
			}
		;

opcmnd		:	cmnd {
			    $$ = $1;
			    $$->negated = false;
			}
		|	'!' cmnd {
			    $$ = $2;
			    $$->negated = true;
			}
		;

rolespec	:	ROLE '=' WORD {
			    $$ = $3;
			}
		;

typespec	:	TYPE '=' WORD {
			    $$ = $3;
			}
		;

selinux		:	/* empty */ {
			    $$.role = NULL;
			    $$.type = NULL;
			}
		|	rolespec {
			    $$.role = $1;
			    $$.type = NULL;
			}
		|	typespec {
			    $$.type = $1;
			    $$.role = NULL;
			}
		|	rolespec typespec {
			    $$.role = $1;
			    $$.type = $2;
			}
		|	typespec rolespec {
			    $$.type = $1;
			    $$.role = $2;
			}
		;

privsspec	:	PRIVS '=' WORD {
			    $$ = $3;
			}
		;
limitprivsspec	:	LIMITPRIVS '=' WORD {
			    $$ = $3;
			}
		;

solarisprivs	:	/* empty */ {
			    $$.privs = NULL;
			    $$.limitprivs = NULL;
			}
		|	privsspec {
			    $$.privs = $1;
			    $$.limitprivs = NULL;
			}
		|	limitprivsspec {
			    $$.privs = NULL;
			    $$.limitprivs = $1;
			}
		|	privsspec limitprivsspec {
			    $$.privs = $1;
			    $$.limitprivs = $2;
			}
		|	limitprivsspec privsspec {
			    $$.limitprivs = $1;
			    $$.privs = $2;
			}
		;

runasspec	:	/* empty */ {
			    $$ = NULL;
			}
		|	'(' runaslist ')' {
			    $$ = $2;
			}
		;

runaslist	:	/* empty */ {
			    $$ = ecalloc(1, sizeof(struct runascontainer));
			    $$->runasusers = new_member(NULL, MYSELF);
			    /* $$->runasgroups = NULL; */
			}
		|	userlist {
			    $$ = ecalloc(1, sizeof(struct runascontainer));
			    $$->runasusers = $1;
			    /* $$->runasgroups = NULL; */
			}
		|	userlist ':' grouplist {
			    $$ = ecalloc(1, sizeof(struct runascontainer));
			    $$->runasusers = $1;
			    $$->runasgroups = $3;
			}
		|	':' grouplist {
			    $$ = ecalloc(1, sizeof(struct runascontainer));
			    /* $$->runasusers = NULL; */
			    $$->runasgroups = $2;
			}
		|	':' {
			    $$ = ecalloc(1, sizeof(struct runascontainer));
			    $$->runasusers = new_member(NULL, MYSELF);
			    /* $$->runasgroups = NULL; */
			}
		;

cmndtag		:	/* empty */ {
			    $$.nopasswd = $$.noexec = $$.setenv =
				$$.log_input = $$.log_output = UNSPEC;
			}
		|	cmndtag NOPASSWD {
			    $$.nopasswd = true;
			}
		|	cmndtag PASSWD {
			    $$.nopasswd = false;
			}
		|	cmndtag NOEXEC {
			    $$.noexec = true;
			}
		|	cmndtag EXEC {
			    $$.noexec = false;
			}
		|	cmndtag SETENV {
			    $$.setenv = true;
			}
		|	cmndtag NOSETENV {
			    $$.setenv = false;
			}
		|	cmndtag LOG_INPUT {
			    $$.log_input = true;
			}
		|	cmndtag NOLOG_INPUT {
			    $$.log_input = false;
			}
		|	cmndtag LOG_OUTPUT {
			    $$.log_output = true;
			}
		|	cmndtag NOLOG_OUTPUT {
			    $$.log_output = false;
			}
		;

cmnd		:	ALL {
			    $$ = new_member(NULL, ALL);
			}
		|	ALIAS {
			    $$ = new_member($1, ALIAS);
			}
		|	COMMAND {
			    struct sudo_command *c = ecalloc(1, sizeof(*c));
			    c->cmnd = $1.cmnd;
			    c->args = $1.args;
			    $$ = new_member((char *)c, COMMAND);
			}
		;

hostaliases	:	hostalias
		|	hostaliases ':' hostalias
		;

hostalias	:	ALIAS '=' hostlist {
			    char *s;
			    if ((s = alias_add($1, HOSTALIAS, $3)) != NULL) {
				sudoerserror(s);
				YYERROR;
			    }
			}
		;

hostlist	:	ophost
		|	hostlist ',' ophost {
			    list_append($1, $3);
			    $$ = $1;
			}
		;

cmndaliases	:	cmndalias
		|	cmndaliases ':' cmndalias
		;

cmndalias	:	ALIAS '=' cmndlist {
			    char *s;
			    if ((s = alias_add($1, CMNDALIAS, $3)) != NULL) {
				sudoerserror(s);
				YYERROR;
			    }
			}
		;

cmndlist	:	digcmnd
		|	cmndlist ',' digcmnd {
			    list_append($1, $3);
			    $$ = $1;
			}
		;

runasaliases	:	runasalias
		|	runasaliases ':' runasalias
		;

runasalias	:	ALIAS '=' userlist {
			    char *s;
			    if ((s = alias_add($1, RUNASALIAS, $3)) != NULL) {
				sudoerserror(s);
				YYERROR;
			    }
			}
		;

useraliases	:	useralias
		|	useraliases ':' useralias
		;

useralias	:	ALIAS '=' userlist {
			    char *s;
			    if ((s = alias_add($1, USERALIAS, $3)) != NULL) {
				sudoerserror(s);
				YYERROR;
			    }
			}
		;

userlist	:	opuser
		|	userlist ',' opuser {
			    list_append($1, $3);
			    $$ = $1;
			}
		;

opuser		:	user {
			    $$ = $1;
			    $$->negated = false;
			}
		|	'!' user {
			    $$ = $2;
			    $$->negated = true;
			}
		;

user		:	ALIAS {
			    $$ = new_member($1, ALIAS);
			}
		|	ALL {
			    $$ = new_member(NULL, ALL);
			}
		|	NETGROUP {
			    $$ = new_member($1, NETGROUP);
			}
		|	USERGROUP {
			    $$ = new_member($1, USERGROUP);
			}
		|	WORD {
			    $$ = new_member($1, WORD);
			}
		;

grouplist	:	opgroup
		|	grouplist ',' opgroup {
			    list_append($1, $3);
			    $$ = $1;
			}
		;

opgroup		:	group {
			    $$ = $1;
			    $$->negated = false;
			}
		|	'!' group {
			    $$ = $2;
			    $$->negated = true;
			}
		;

group		:	ALIAS {
			    $$ = new_member($1, ALIAS);
			}
		|	ALL {
			    $$ = new_member(NULL, ALL);
			}
		|	WORD {
			    $$ = new_member($1, WORD);
			}
		;

%%
void
sudoerserror(const char *s)
{
    debug_decl(sudoerserror, SUDO_DEBUG_PARSER)

    /* If we last saw a newline the error is on the preceding line. */
    if (last_token == COMMENT)
	sudolineno--;

    /* Save the line the first error occurred on. */
    if (errorlineno == -1) {
	errorlineno = sudolineno;
	errorfile = estrdup(sudoers);
    }
    if (sudoers_warnings && s != NULL) {
	LEXTRACE("<*> ");
#ifndef TRACELEXER
	if (trace_print == NULL || trace_print == sudoers_trace_print) {
	    const char fmt[] = ">>> %s: %s near line %d <<<\n";
	    int oldlocale;

	    /* Warnings are displayed in the user's locale. */
	    sudoers_setlocale(SUDOERS_LOCALE_USER, &oldlocale);
	    sudo_printf(SUDO_CONV_ERROR_MSG, _(fmt), sudoers, _(s), sudolineno);
	    sudoers_setlocale(oldlocale, NULL);
	}
#endif
    }
    parse_error = true;
    debug_return;
}

static struct defaults *
new_default(char *var, char *val, int op)
{
    struct defaults *d;
    debug_decl(new_default, SUDO_DEBUG_PARSER)

    d = ecalloc(1, sizeof(struct defaults));
    d->var = var;
    d->val = val;
    tq_init(&d->binding);
    /* d->type = 0; */
    d->op = op;
    d->prev = d;
    /* d->next = NULL; */

    debug_return_ptr(d);
}

static struct member *
new_member(char *name, int type)
{
    struct member *m;
    debug_decl(new_member, SUDO_DEBUG_PARSER)

    m = ecalloc(1, sizeof(struct member));
    m->name = name;
    m->type = type;
    m->prev = m;
    /* m->next = NULL; */

    debug_return_ptr(m);
}

struct sudo_digest *
new_digest(int digest_type, const char *digest_str)
{
    struct sudo_digest *dig;
    debug_decl(new_digest, SUDO_DEBUG_PARSER)

    dig = emalloc(sizeof(*dig));
    dig->digest_type = digest_type;
    dig->digest_str = estrdup(digest_str);

    debug_return_ptr(dig);
}

/*
 * Add a list of defaults structures to the defaults list.
 * The binding, if non-NULL, specifies a list of hosts, users, or
 * runas users the entries apply to (specified by the type).
 */
static void
add_defaults(int type, struct member *bmem, struct defaults *defs)
{
    struct defaults *d;
    struct member_list binding;
    debug_decl(add_defaults, SUDO_DEBUG_PARSER)

    /*
     * We can only call list2tq once on bmem as it will zero
     * out the prev pointer when it consumes bmem.
     */
    list2tq(&binding, bmem);

    /*
     * Set type and binding (who it applies to) for new entries.
     */
    for (d = defs; d != NULL; d = d->next) {
	d->type = type;
	d->binding = binding;
    }
    tq_append(&defaults, defs);

    debug_return;
}

/*
 * Allocate a new struct userspec, populate it, and insert it at the
 * and of the userspecs list.
 */
static void
add_userspec(struct member *members, struct privilege *privs)
{
    struct userspec *u;
    debug_decl(add_userspec, SUDO_DEBUG_PARSER)

    u = ecalloc(1, sizeof(*u));
    list2tq(&u->users, members);
    list2tq(&u->privileges, privs);
    u->prev = u;
    /* u->next = NULL; */
    tq_append(&userspecs, u);

    debug_return;
}

/*
 * Free up space used by data structures from a previous parser run and sets
 * the current sudoers file to path.
 */
void
init_parser(const char *path, bool quiet)
{
    struct defaults *d;
    struct member *m, *binding;
    struct userspec *us;
    struct privilege *priv;
    struct cmndspec *cs;
    struct sudo_command *c;
    debug_decl(init_parser, SUDO_DEBUG_PARSER)

    while ((us = tq_pop(&userspecs)) != NULL) {
	while ((m = tq_pop(&us->users)) != NULL) {
	    efree(m->name);
	    efree(m);
	}
	while ((priv = tq_pop(&us->privileges)) != NULL) {
	    struct member *runasuser = NULL, *runasgroup = NULL;
#ifdef HAVE_SELINUX
	    char *role = NULL, *type = NULL;
#endif /* HAVE_SELINUX */
#ifdef HAVE_PRIV_SET
	    char *privs = NULL, *limitprivs = NULL;
#endif /* HAVE_PRIV_SET */

	    while ((m = tq_pop(&priv->hostlist)) != NULL) {
		efree(m->name);
		efree(m);
	    }
	    while ((cs = tq_pop(&priv->cmndlist)) != NULL) {
#ifdef HAVE_SELINUX
		/* Only free the first instance of a role/type. */
		if (cs->role != role) {
		    role = cs->role;
		    efree(cs->role);
		}
		if (cs->type != type) {
		    type = cs->type;
		    efree(cs->type);
		}
#endif /* HAVE_SELINUX */
#ifdef HAVE_PRIV_SET
		/* Only free the first instance of privs/limitprivs. */
		if (cs->privs != privs) {
		    privs = cs->privs;
		    efree(cs->privs);
		}
		if (cs->limitprivs != limitprivs) {
		    limitprivs = cs->limitprivs;
		    efree(cs->limitprivs);
		}
#endif /* HAVE_PRIV_SET */
		if (tq_last(&cs->runasuserlist) != runasuser) {
		    runasuser = tq_last(&cs->runasuserlist);
		    while ((m = tq_pop(&cs->runasuserlist)) != NULL) {
			efree(m->name);
			efree(m);
		    }
		}
		if (tq_last(&cs->runasgrouplist) != runasgroup) {
		    runasgroup = tq_last(&cs->runasgrouplist);
		    while ((m = tq_pop(&cs->runasgrouplist)) != NULL) {
			efree(m->name);
			efree(m);
		    }
		}
		if (cs->cmnd->type == COMMAND) {
			c = (struct sudo_command *) cs->cmnd->name;
			efree(c->cmnd);
			efree(c->args);
		}
		efree(cs->cmnd->name);
		efree(cs->cmnd);
		efree(cs);
	    }
	    efree(priv);
	}
	efree(us);
    }
    tq_init(&userspecs);

    binding = NULL;
    while ((d = tq_pop(&defaults)) != NULL) {
	if (tq_last(&d->binding) != binding) {
	    binding = tq_last(&d->binding);
	    while ((m = tq_pop(&d->binding)) != NULL) {
		if (m->type == COMMAND) {
			c = (struct sudo_command *) m->name;
			efree(c->cmnd);
			efree(c->args);
		}
		efree(m->name);
		efree(m);
	    }
	}
	efree(d->var);
	efree(d->val);
	efree(d);
    }
    tq_init(&defaults);

    init_aliases();

    init_lexer();

    efree(sudoers);
    sudoers = path ? estrdup(path) : NULL;

    parse_error = false;
    errorlineno = -1;
    errorfile = sudoers;
    sudoers_warnings = !quiet;

    debug_return;
}
