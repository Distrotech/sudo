/*
 * Copyright (c) 2010-2013 Todd C. Miller <Todd.Miller@courtesan.com>
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
#include <sys/stat.h>

#include <stdio.h>
#ifdef STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# ifdef HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif /* STDC_HEADERS */
#ifdef HAVE_STDBOOL_H
# include <stdbool.h>
#else
# include "compat/stdbool.h"
#endif /* HAVE_STDBOOL_H */
#ifdef HAVE_STRING_H
# if defined(HAVE_MEMORY_H) && !defined(STDC_HEADERS)
#  include <memory.h>
# endif
# include <string.h>
#endif /* HAVE_STRING_H */
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif /* HAVE_STRINGS_H */
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif /* HAVE_UNISTD_H */
#ifdef HAVE_DLOPEN
# include <dlfcn.h>
#else
# include "compat/dlfcn.h"
#endif
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <grp.h>
#include <pwd.h>

#include "sudo_plugin.h"
#include "missing.h"

#ifndef RTLD_DEFAULT
# define RTLD_DEFAULT	NULL
#endif

/*
 * Sudoers group plugin that does group name-based lookups using the system
 * group database functions, similar to how sudo behaved prior to 1.7.3.
 * This can be used on systems where lookups by group ID are problematic.
 */

static sudo_printf_t sudo_log;

typedef struct group * (*sysgroup_getgrnam_t)(const char *);
typedef struct group * (*sysgroup_getgrgid_t)(gid_t);
typedef void (*sysgroup_gr_delref_t)(struct group *);

static sysgroup_getgrnam_t sysgroup_getgrnam;
static sysgroup_getgrgid_t sysgroup_getgrgid;
static sysgroup_gr_delref_t sysgroup_gr_delref;
static bool need_setent;

static int
sysgroup_init(int version, sudo_printf_t sudo_printf, char *const argv[])
{
    void *handle;

    sudo_log = sudo_printf;

    if (GROUP_API_VERSION_GET_MAJOR(version) != GROUP_API_VERSION_MAJOR) {
	sudo_log(SUDO_CONV_ERROR_MSG,
	    "sysgroup_group: incompatible major version %d, expected %d\n",
	    GROUP_API_VERSION_GET_MAJOR(version),
	    GROUP_API_VERSION_MAJOR);
	return -1;
    }

    /* Share group cache with sudo if possible. */
    handle = dlsym(RTLD_DEFAULT, "sudo_getgrnam");
    if (handle != NULL) {
	sysgroup_getgrnam = (sysgroup_getgrnam_t)handle;
    } else {
	sysgroup_getgrnam = (sysgroup_getgrnam_t)getgrnam;
	need_setent = true;
    }

    handle = dlsym(RTLD_DEFAULT, "sudo_getgrgid");
    if (handle != NULL) {
	sysgroup_getgrgid = (sysgroup_getgrgid_t)handle;
    } else {
	sysgroup_getgrgid = (sysgroup_getgrgid_t)getgrgid;
	need_setent = true;
    }

    handle = dlsym(RTLD_DEFAULT, "sudo_gr_delref");
    if (handle != NULL)
	sysgroup_gr_delref = (sysgroup_gr_delref_t)handle;

    if (need_setent)
	setgrent();

    return true;
}

static void
sysgroup_cleanup(void)
{
    if (need_setent)
	endgrent();
}

/*
 * Returns true if "user" is a member of "group", else false.
 */
static int
sysgroup_query(const char *user, const char *group, const struct passwd *pwd)
{
    char **member, *ep = '\0';
    struct group *grp;

    grp = sysgroup_getgrnam(group);
    if (grp == NULL && group[0] == '#' && group[1] != '\0') {
	long lval = strtol(group + 1, &ep, 10);
	if (*ep == '\0') {
	    if ((lval != LONG_MAX && lval != LONG_MIN) || errno != ERANGE)
		grp = sysgroup_getgrgid((gid_t)lval);
	}
    }
    if (grp != NULL) {
	for (member = grp->gr_mem; *member != NULL; member++) {
	    if (strcasecmp(user, *member) == 0) {
		if (sysgroup_gr_delref)
		    sysgroup_gr_delref(grp);
		return true;
	    }
	}
	if (sysgroup_gr_delref)
	    sysgroup_gr_delref(grp);
    }

    return false;
}

__dso_public struct sudoers_group_plugin group_plugin = {
    GROUP_API_VERSION,
    sysgroup_init,
    sysgroup_cleanup,
    sysgroup_query
};
