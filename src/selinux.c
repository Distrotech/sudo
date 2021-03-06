/*
 * Copyright (c) 2009-2013 Todd C. Miller <Todd.Miller@courtesan.com>
 * Copyright (c) 2008 Dan Walsh <dwalsh@redhat.com>
 *
 * Borrowed heavily from newrole source code
 * Authors:
 *	Anthony Colatrella
 *	Tim Fraser
 *	Steve Grubb <sgrubb@redhat.com>
 *	Darrel Goeddel <DGoeddel@trustedcs.com>
 *	Michael Thompson <mcthomps@us.ibm.com>
 *	Dan Walsh <dwalsh@redhat.com>
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
 */

#include <config.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>

#include <selinux/flask.h>             /* for SECCLASS_CHR_FILE */
#include <selinux/selinux.h>           /* for is_selinux_enabled() */
#include <selinux/context.h>           /* for context-mangling functions */
#include <selinux/get_default_type.h>
#include <selinux/get_context_list.h>

#ifdef HAVE_LINUX_AUDIT
# include <libaudit.h>
#endif

#include "sudo.h"
#include "sudo_exec.h"

static struct selinux_state {
    security_context_t old_context;
    security_context_t new_context;
    security_context_t tty_context;
    security_context_t new_tty_context;
    const char *ttyn;
    int ttyfd;
    int enforcing;
} se_state;

#ifdef HAVE_LINUX_AUDIT
static int
audit_role_change(const security_context_t old_context,
    const security_context_t new_context, const char *ttyn)
{
    int au_fd, rc = -1;
    char *message;
    debug_decl(audit_role_change, SUDO_DEBUG_SELINUX)

    au_fd = audit_open();
    if (au_fd == -1) {
        /* Kernel may not have audit support. */
        if (errno != EINVAL && errno != EPROTONOSUPPORT && errno != EAFNOSUPPORT
)
            fatal(_("unable to open audit system"));
    } else {
	/* audit role change using the same format as newrole(1) */
	easprintf(&message, "newrole: old-context=%s new-context=%s",
	    old_context, new_context);
	rc = audit_log_user_message(au_fd, AUDIT_USER_ROLE_CHANGE,
	    message, NULL, NULL, ttyn, 1);
	if (rc <= 0)
	    warning(_("unable to send audit message"));
	efree(message);
	close(au_fd);
    }

    debug_return_int(rc);
}
#endif

/*
 * This function attempts to revert the relabeling done to the tty.
 * fd		   - referencing the opened ttyn
 * ttyn		   - name of tty to restore
 *
 * Returns zero on success, non-zero otherwise
 */
int
selinux_restore_tty(void)
{
    int retval = 0;
    security_context_t chk_tty_context = NULL;
    debug_decl(selinux_restore_tty, SUDO_DEBUG_SELINUX)

    if (se_state.ttyfd == -1 || se_state.new_tty_context == NULL)
	goto skip_relabel;

    /* Verify that the tty still has the context set by sudo. */
    if ((retval = fgetfilecon(se_state.ttyfd, &chk_tty_context)) < 0) {
	warning(_("unable to fgetfilecon %s"), se_state.ttyn);
	goto skip_relabel;
    }

    if ((retval = strcmp(chk_tty_context, se_state.new_tty_context))) {
	warningx(_("%s changed labels"), se_state.ttyn);
	goto skip_relabel;
    }

    if ((retval = fsetfilecon(se_state.ttyfd, se_state.tty_context)) < 0)
	warning(_("unable to restore context for %s"), se_state.ttyn);

skip_relabel:
    if (se_state.ttyfd != -1) {
	close(se_state.ttyfd);
	se_state.ttyfd = -1;
    }
    if (chk_tty_context != NULL) {
	freecon(chk_tty_context);
	chk_tty_context = NULL;
    }
    debug_return_int(retval);
}

/*
 * This function attempts to relabel the tty. If this function fails, then
 * the contexts are free'd and -1 is returned. On success, 0 is returned
 * and tty_context and new_tty_context are set.
 *
 * This function will not fail if it can not relabel the tty when selinux is
 * in permissive mode.
 */
static int
relabel_tty(const char *ttyn, int ptyfd)
{
    security_context_t tty_con = NULL;
    security_context_t new_tty_con = NULL;
    int fd;
    debug_decl(relabel_tty, SUDO_DEBUG_SELINUX)

    se_state.ttyfd = ptyfd;

    /* It is perfectly legal to have no tty. */
    if (ptyfd == -1 && ttyn == NULL)
	debug_return_int(0);

    /* If sudo is not allocating a pty for the command, open current tty. */
    if (ptyfd == -1) {
	se_state.ttyfd = open(ttyn, O_RDWR|O_NONBLOCK);
	if (se_state.ttyfd == -1) {
	    warning(_("unable to open %s, not relabeling tty"), ttyn);
	    if (se_state.enforcing)
		goto bad;
	}
	(void)fcntl(se_state.ttyfd, F_SETFL,
	    fcntl(se_state.ttyfd, F_GETFL, 0) & ~O_NONBLOCK);
    }

    if (fgetfilecon(se_state.ttyfd, &tty_con) < 0) {
	warning(_("unable to get current tty context, not relabeling tty"));
	if (se_state.enforcing)
	    goto bad;
    }

    if (tty_con && (security_compute_relabel(se_state.new_context, tty_con,
	SECCLASS_CHR_FILE, &new_tty_con) < 0)) {
	warning(_("unable to get new tty context, not relabeling tty"));
	if (se_state.enforcing)
	    goto bad;
    }

    if (new_tty_con != NULL) {
	if (fsetfilecon(se_state.ttyfd, new_tty_con) < 0) {
	    warning(_("unable to set new tty context"));
	    if (se_state.enforcing)
		goto bad;
	}
    }

    if (ptyfd != -1) {
	/* Reopen pty that was relabeled, std{in,out,err} are reset later. */
	se_state.ttyfd = open(ttyn, O_RDWR|O_NOCTTY, 0);
	if (se_state.ttyfd == -1) {
	    warning(_("unable to open %s"), ttyn);
	    if (se_state.enforcing)
		goto bad;
	}
	if (dup2(se_state.ttyfd, ptyfd) == -1) {
	    warning("dup2");
	    goto bad;
	}
    } else {
	/* Re-open tty to get new label and reset std{in,out,err} */
	close(se_state.ttyfd);
	se_state.ttyfd = open(ttyn, O_RDWR|O_NONBLOCK);
	if (se_state.ttyfd == -1) {
	    warning(_("unable to open %s"), ttyn);
	    goto bad;
	}
	(void)fcntl(se_state.ttyfd, F_SETFL,
	    fcntl(se_state.ttyfd, F_GETFL, 0) & ~O_NONBLOCK);
	for (fd = STDIN_FILENO; fd <= STDERR_FILENO; fd++) {
	    if (isatty(fd) && dup2(se_state.ttyfd, fd) == -1) {
		warning("dup2");
		goto bad;
	    }
	}
    }
    /* Retain se_state.ttyfd so we can restore label when command finishes. */
    (void)fcntl(se_state.ttyfd, F_SETFD, FD_CLOEXEC);

    se_state.ttyn = ttyn;
    se_state.tty_context = tty_con;
    se_state.new_tty_context = new_tty_con;
    debug_return_int(0);

bad:
    if (se_state.ttyfd != -1 && se_state.ttyfd != ptyfd) {
	close(se_state.ttyfd);
	se_state.ttyfd = -1;
    }
    freecon(tty_con);
    debug_return_int(-1);
}

/*
 * Returns a new security context based on the old context and the
 * specified role and type.
 */
security_context_t
get_exec_context(security_context_t old_context, const char *role, const char *type)
{
    security_context_t new_context = NULL;
    context_t context = NULL;
    char *typebuf = NULL;
    debug_decl(get_exec_context, SUDO_DEBUG_SELINUX)
    
    /* We must have a role, the type is optional (we can use the default). */
    if (!role) {
	warningx(_("you must specify a role for type %s"), type);
	errno = EINVAL;
	goto bad;
    }
    if (!type) {
	if (get_default_type(role, &typebuf)) {
	    warningx(_("unable to get default type for role %s"), role);
	    errno = EINVAL;
	    goto bad;
	}
	type = typebuf;
    }
    
    /* 
     * Expand old_context into a context_t so that we extract and modify 
     * its components easily. 
     */
    context = context_new(old_context);
    
    /*
     * Replace the role and type in "context" with the role and
     * type we will be running the command as.
     */
    if (context_role_set(context, role)) {
	warning(_("failed to set new role %s"), role);
	goto bad;
    }
    if (context_type_set(context, type)) {
	warning(_("failed to set new type %s"), type);
	goto bad;
    }
      
    /*
     * Convert "context" back into a string and verify it.
     */
    new_context = estrdup(context_str(context));
    if (security_check_context(new_context) < 0) {
	warningx(_("%s is not a valid context"), new_context);
	errno = EINVAL;
	goto bad;
    }

#ifdef DEBUG
    warningx("Your new context is %s", new_context);
#endif

    context_free(context);
    debug_return_ptr(new_context);

bad:
    efree(typebuf);
    context_free(context);
    freecon(new_context);
    debug_return_ptr(NULL);
}

/* 
 * Set the exec and tty contexts in preparation for fork/exec.
 * Must run as root, before the uid change.
 * If ptyfd is not -1, it indicates we are running
 * in a pty and do not need to reset std{in,out,err}.
 * Returns 0 on success and -1 on failure.
 */
int
selinux_setup(const char *role, const char *type, const char *ttyn,
    int ptyfd)
{
    int rval = -1;
    debug_decl(selinux_setup, SUDO_DEBUG_SELINUX)

    /* Store the caller's SID in old_context. */
    if (getprevcon(&se_state.old_context)) {
	warning(_("failed to get old_context"));
	goto done;
    }

    se_state.enforcing = security_getenforce();
    if (se_state.enforcing < 0) {
	warning(_("unable to determine enforcing mode."));
	goto done;
    }

#ifdef DEBUG
    warningx("your old context was %s", se_state.old_context);
#endif
    se_state.new_context = get_exec_context(se_state.old_context, role, type);
    if (!se_state.new_context)
	goto done;
    
    if (relabel_tty(ttyn, ptyfd) < 0) {
	warning(_("unable to setup tty context for %s"), se_state.new_context);
	goto done;
    }

#ifdef DEBUG
    if (se_state.ttyfd != -1) {
	warningx("your old tty context is %s", se_state.tty_context);
	warningx("your new tty context is %s", se_state.new_tty_context);
    }
#endif

#ifdef HAVE_LINUX_AUDIT
    audit_role_change(se_state.old_context, se_state.new_context,
	se_state.ttyn);
#endif

    rval = 0;

done:
    debug_return_int(rval);
}

void
selinux_execve(const char *path, char *const argv[], char *const envp[],
    int noexec)
{
    char **nargv;
    const char *sesh;
    int argc, serrno;
    debug_decl(selinux_execve, SUDO_DEBUG_SELINUX)

    sesh = sudo_conf_sesh_path();
    if (sesh == NULL) {
	warningx("internal error: sesh path not set");
	errno = EINVAL;
	debug_return;
    }

    if (setexeccon(se_state.new_context)) {
	warning(_("unable to set exec context to %s"), se_state.new_context);
	if (se_state.enforcing)
	    debug_return;
    }

#ifdef HAVE_SETKEYCREATECON
    if (setkeycreatecon(se_state.new_context)) {
	warning(_("unable to set key creation context to %s"), se_state.new_context);
	if (se_state.enforcing)
	    debug_return;
    }
#endif /* HAVE_SETKEYCREATECON */

    /*
     * Build new argv with sesh as argv[0].
     * If argv[0] ends in -noexec, sesh will disable execute
     * for the command it runs.
     */
    for (argc = 0; argv[argc] != NULL; argc++)
	continue;
    nargv = emalloc2(argc + 2, sizeof(char *));
    if (noexec)
	nargv[0] = *argv[0] == '-' ? "-sesh-noexec" : "sesh-noexec";
    else
	nargv[0] = *argv[0] == '-' ? "-sesh" : "sesh";
    nargv[1] = (char *)path;
    memcpy(&nargv[2], &argv[1], argc * sizeof(char *)); /* copies NULL */

    /* sesh will handle noexec for us. */
    sudo_execve(sesh, nargv, envp, 0);
    serrno = errno;
    free(nargv);
    errno = serrno;
    debug_return;
}
