/*
 * Copyright (c) 1993-1996, 1998-2005, 2007-2013
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
 *
 * Sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F39502-99-1-0512.
 */

#ifndef _SUDOERS_SUDOERS_H
#define _SUDOERS_SUDOERS_H

#include <limits.h>
#ifdef HAVE_STDBOOL_H
# include <stdbool.h>
#else
# include "compat/stdbool.h"
#endif /* HAVE_STDBOOL_H */

#include <pathnames.h>
#include "missing.h"
#include "error.h"
#include "alloc.h"
#include "list.h"
#include "fileops.h"
#include "defaults.h"
#include "logging.h"
#include "sudo_nss.h"
#include "sudo_plugin.h"
#include "sudo_debug.h"

#define DEFAULT_TEXT_DOMAIN	"sudoers"
#include "gettext.h"

/*
 * Password db and supplementary group IDs with associated group names.
 */
struct group_list {
    char **groups;
    GETGROUPS_T *gids;
    int ngroups;
    int ngids;
};

/*
 * Info pertaining to the invoking user.
 */
struct sudo_user {
    struct passwd *pw;
    struct passwd *_runas_pw;
    struct group *_runas_gr;
    struct stat *cmnd_stat;
    char *name;
    char *path;
    char *tty;
    char *ttypath;
    char *host;
    char *shost;
    char *prompt;
    char *cmnd;
    char *cmnd_args;
    char *cmnd_base;
    char *cmnd_safe;
    char *class_name;
    char *krb5_ccname;
    struct group_list *group_list;
    char * const * env_vars;
#ifdef HAVE_SELINUX
    char *role;
    char *type;
#endif
#ifdef HAVE_PRIV_SET
    char *privs;
    char *limitprivs;
#endif
    const char *cwd;
    char *iolog_file;
    GETGROUPS_T *gids;
    int   ngids;
    int   closefrom;
    int   lines;
    int   cols;
    int   flags;
    int   max_groups;
    mode_t umask;
    uid_t uid;
    uid_t gid;
    pid_t sid;
};

/*
 * sudo_user flag values
 */
#define RUNAS_USER_SPECIFIED	0x01
#define RUNAS_GROUP_SPECIFIED	0x02

/*
 * Return values for sudoers_lookup(), also used as arguments for log_auth()
 * Note: cannot use '0' as a value here.
 */
/* XXX - VALIDATE_SUCCESS and VALIDATE_FAILURE instead? */
#define VALIDATE_ERROR          0x001
#define VALIDATE_OK		0x002
#define VALIDATE_NOT_OK		0x004
#define FLAG_CHECK_USER		0x010
#define FLAG_NO_USER		0x020
#define FLAG_NO_HOST		0x040
#define FLAG_NO_CHECK		0x080
#define FLAG_NON_INTERACTIVE	0x100
#define FLAG_BAD_PASSWORD	0x200
#define FLAG_AUTH_ERROR		0x400

/*
 * find_path()/load_cmnd() return values
 */
#define FOUND                   0
#define NOT_FOUND               1
#define NOT_FOUND_DOT		2

/*
 * Various modes sudo can be in (based on arguments) in hex
 */
#define MODE_RUN		0x00000001
#define MODE_EDIT		0x00000002
#define MODE_VALIDATE		0x00000004
#define MODE_INVALIDATE		0x00000008
#define MODE_KILL		0x00000010
#define MODE_VERSION		0x00000020
#define MODE_HELP		0x00000040
#define MODE_LIST		0x00000080
#define MODE_CHECK		0x00000100
#define MODE_LISTDEFS		0x00000200
#define MODE_MASK		0x0000ffff

/* Mode flags */
#define MODE_BACKGROUND		0x00010000 /* XXX - unused */
#define MODE_SHELL		0x00020000
#define MODE_LOGIN_SHELL	0x00040000
#define MODE_IMPLIED_SHELL	0x00080000
#define MODE_RESET_HOME		0x00100000
#define MODE_PRESERVE_GROUPS	0x00200000
#define MODE_PRESERVE_ENV	0x00400000
#define MODE_NONINTERACTIVE	0x00800000
#define MODE_IGNORE_TICKET	0x01000000

/*
 * Used with set_perms()
 */
#define PERM_INITIAL             0x00
#define PERM_ROOT                0x01
#define PERM_USER                0x02
#define PERM_FULL_USER           0x03
#define PERM_SUDOERS             0x04
#define PERM_RUNAS               0x05
#define PERM_TIMESTAMP           0x06
#define PERM_NOEXIT              0x10 /* flag */
#define PERM_MASK                0xf0

/*
 * Shortcuts for sudo_user contents.
 */
#define user_name		(sudo_user.name)
#define user_uid		(sudo_user.uid)
#define user_gid		(sudo_user.gid)
#define user_sid		(sudo_user.sid)
#define user_umask		(sudo_user.umask)
#define user_passwd		(sudo_user.pw->pw_passwd)
#define user_dir		(sudo_user.pw->pw_dir)
#define user_gids		(sudo_user.gids)
#define user_ngids		(sudo_user.ngids)
#define user_group_list		(sudo_user.group_list)
#define user_tty		(sudo_user.tty)
#define user_ttypath		(sudo_user.ttypath)
#define user_cwd		(sudo_user.cwd)
#define user_cmnd		(sudo_user.cmnd)
#define user_args		(sudo_user.cmnd_args)
#define user_base		(sudo_user.cmnd_base)
#define user_stat		(sudo_user.cmnd_stat)
#define user_path		(sudo_user.path)
#define user_prompt		(sudo_user.prompt)
#define user_host		(sudo_user.host)
#define user_shost		(sudo_user.shost)
#define user_ccname		(sudo_user.krb5_ccname)
#define safe_cmnd		(sudo_user.cmnd_safe)
#define login_class		(sudo_user.class_name)
#define runas_pw		(sudo_user._runas_pw)
#define runas_gr		(sudo_user._runas_gr)
#define user_role		(sudo_user.role)
#define user_type		(sudo_user.type)
#define user_closefrom		(sudo_user.closefrom)
#define	runas_privs		(sudo_user.privs)
#define	runas_limitprivs	(sudo_user.limitprivs)

#ifdef __TANDEM
# define ROOT_UID	65535
#else
# define ROOT_UID	0
#endif
#define ROOT_GID	0

/*
 * We used to use the system definition of PASS_MAX or _PASSWD_LEN,
 * but that caused problems with various alternate authentication
 * methods.  So, we just define our own and assume that it is >= the
 * system max.
 */
#define SUDO_PASS_MAX	256

struct lbuf;
struct passwd;
struct stat;
struct timeval;

/*
 * Function prototypes
 */
#define YY_DECL int sudoerslex(void)

/* goodpath.c */
bool sudo_goodpath(const char *, struct stat *);

/* findpath.c */
int find_path(char *, char **, struct stat *, char *, int);

/* check.c */
int check_user(int validate, int mode);
bool user_is_exempt(void);

/* prompt.c */
char *expand_prompt(const char *old_prompt, const char *user, const char *host);

/* timestamp.c */
void remove_timestamp(bool);
bool set_lectured(void);

/* sudo_auth.c */
bool sudo_auth_needs_end_session(void);
int verify_user(struct passwd *pw, char *prompt, int validated);
int sudo_auth_begin_session(struct passwd *pw, char **user_env[]);
int sudo_auth_end_session(struct passwd *pw);
int sudo_auth_init(struct passwd *pw);
int sudo_auth_cleanup(struct passwd *pw);

/* parse.c */
int sudo_file_open(struct sudo_nss *);
int sudo_file_close(struct sudo_nss *);
int sudo_file_setdefs(struct sudo_nss *);
int sudo_file_lookup(struct sudo_nss *, int, int);
int sudo_file_parse(struct sudo_nss *);
int sudo_file_display_cmnd(struct sudo_nss *, struct passwd *);
int sudo_file_display_defaults(struct sudo_nss *, struct passwd *, struct lbuf *);
int sudo_file_display_bound_defaults(struct sudo_nss *, struct passwd *, struct lbuf *);
int sudo_file_display_privs(struct sudo_nss *, struct passwd *, struct lbuf *);

/* set_perms.c */
void rewind_perms(void);
int set_perms(int);
void restore_perms(void);
int pam_prep_user(struct passwd *);

/* gram.y */
int sudoersparse(void);

/* toke.l */
YY_DECL;
extern const char *sudoers_file;
extern mode_t sudoers_mode;
extern uid_t sudoers_uid;
extern gid_t sudoers_gid;

/* defaults.c */
void dump_defaults(void);
void dump_auth_methods(void);

/* getspwuid.c */
char *sudo_getepw(const struct passwd *);

/* zero_bytes.c */
void zero_bytes(volatile void *, size_t);

/* sudo_nss.c */
void display_privs(struct sudo_nss_list *, struct passwd *);
bool display_cmnd(struct sudo_nss_list *, struct passwd *);

/* pwutil.c */
__dso_public struct group *sudo_getgrgid(gid_t);
__dso_public struct group *sudo_getgrnam(const char *);
__dso_public void sudo_gr_addref(struct group *);
__dso_public void sudo_gr_delref(struct group *);
bool user_in_group(struct passwd *, const char *);
struct group *sudo_fakegrnam(const char *);
struct group_list *sudo_get_grlist(struct passwd *pw);
struct passwd *sudo_fakepwnam(const char *, gid_t);
struct passwd *sudo_mkpwent(const char *user, uid_t uid, gid_t gid, const char *home, const char *shell);
struct passwd *sudo_getpwnam(const char *);
struct passwd *sudo_getpwuid(uid_t);
void sudo_endgrent(void);
void sudo_endpwent(void);
void sudo_endspent(void);
void sudo_grlist_addref(struct group_list *);
void sudo_grlist_delref(struct group_list *);
void sudo_pw_addref(struct passwd *);
void sudo_pw_delref(struct passwd *);
void sudo_set_grlist(struct passwd *pw, char * const *groups,
    char * const *gids);
void sudo_setgrent(void);
void sudo_setpwent(void);
void sudo_setspent(void);

/* timestr.c */
char *get_timestr(time_t, int);

/* atobool.c */
int atobool(const char *str);

/* boottime.c */
int get_boottime(struct timeval *);

/* iolog.c */
int io_set_max_sessid(const char *sessid);
void io_nextid(char *iolog_dir, char *iolog_dir_fallback, char sessid[7]);

/* iolog_path.c */
char *expand_iolog_path(const char *prefix, const char *dir, const char *file,
    char **slashp);

/* env.c */
char **env_get(void);
void env_merge(char * const envp[], bool overwrite);
void env_init(char * const envp[]);
void init_envtables(void);
void insert_env_vars(char * const envp[]);
void read_env_file(const char *, int);
void rebuild_env(void);
void validate_env_vars(char * const envp[]);
int sudo_setenv(const char *var, const char *val, int overwrite);
int sudo_unsetenv(const char *var);
char *sudo_getenv(const char *name);
int sudoers_hook_getenv(const char *name, char **value, void *closure);
int sudoers_hook_putenv(char *string, void *closure);
int sudoers_hook_setenv(const char *name, const char *value, int overwrite, void *closure);
int sudoers_hook_unsetenv(const char *name, void *closure);

/* fmt_string.c */
char *fmt_string(const char *, const char *);

/* sudoers.c */
FILE *open_sudoers(const char *, bool, bool *);
int sudoers_policy_init(void *info, char * const envp[]);
int sudoers_policy_main(int argc, char * const argv[], int pwflag, char *env_add[], void *closure);
void sudoers_cleanup(void);

/* policy.c */
int sudoers_policy_deserialize_info(void *v, char **runas_user, char **runas_group);
int sudoers_policy_exec_setup(char *argv[], char *envp[], mode_t cmnd_umask, char *iolog_path, void *v);
extern const char *path_ldap_conf;
extern const char *path_ldap_secret;

/* aix.c */
void aix_restoreauthdb(void);
void aix_setauthdb(char *user);

/* group_plugin.c */
int group_plugin_load(char *plugin_info);
void group_plugin_unload(void);
int group_plugin_query(const char *user, const char *group,
    const struct passwd *pwd);

/* setgroups.c */
int sudo_setgroups(int ngids, const GETGROUPS_T *gids);

#ifndef _SUDO_MAIN
extern struct sudo_user sudo_user;
extern struct passwd *list_pw;
extern int long_list;
extern int sudo_mode;
extern uid_t timestamp_uid;
extern sudo_conv_t sudo_conv;
#endif

#endif /* _SUDOERS_SUDOERS_H */
