/*
 * Copyright (c) 1993-1996,1998-2005, 2007-2013
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
#include <pwd.h>
#include <grp.h>

#include "sudoers.h"

/*
 * Expand %h and %u escapes in the prompt and pass back the dynamically
 * allocated result.  Returns the same string if there are no escapes.
 */
char *
expand_prompt(const char *old_prompt, const char *user, const char *host)
{
    size_t len, n;
    int subst;
    const char *p;
    char *np, *new_prompt, *endp;
    debug_decl(expand_prompt, SUDO_DEBUG_AUTH)

    /* How much space do we need to malloc for the prompt? */
    subst = 0;
    for (p = old_prompt, len = strlen(old_prompt); *p; p++) {
	if (p[0] =='%') {
	    switch (p[1]) {
		case 'h':
		    p++;
		    len += strlen(user_shost) - 2;
		    subst = 1;
		    break;
		case 'H':
		    p++;
		    len += strlen(user_host) - 2;
		    subst = 1;
		    break;
		case 'p':
		    p++;
		    if (def_rootpw)
			    len += 2;
		    else if (def_targetpw || def_runaspw)
			    len += strlen(runas_pw->pw_name) - 2;
		    else
			    len += strlen(user_name) - 2;
		    subst = 1;
		    break;
		case 'u':
		    p++;
		    len += strlen(user_name) - 2;
		    subst = 1;
		    break;
		case 'U':
		    p++;
		    len += strlen(runas_pw->pw_name) - 2;
		    subst = 1;
		    break;
		case '%':
		    p++;
		    len--;
		    subst = 1;
		    break;
		default:
		    break;
	    }
	}
    }

    if (subst) {
	new_prompt = emalloc(++len);
	endp = new_prompt + len;
	for (p = old_prompt, np = new_prompt; *p; p++) {
	    if (p[0] =='%') {
		switch (p[1]) {
		    case 'h':
			p++;
			n = strlcpy(np, user_shost, np - endp);
			if (n >= np - endp)
			    goto oflow;
			np += n;
			continue;
		    case 'H':
			p++;
			n = strlcpy(np, user_host, np - endp);
			if (n >= np - endp)
			    goto oflow;
			np += n;
			continue;
		    case 'p':
			p++;
			if (def_rootpw)
				n = strlcpy(np, "root", np - endp);
			else if (def_targetpw || def_runaspw)
				n = strlcpy(np, runas_pw->pw_name, np - endp);
			else
				n = strlcpy(np, user_name, np - endp);
			if (n >= np - endp)
				goto oflow;
			np += n;
			continue;
		    case 'u':
			p++;
			n = strlcpy(np, user_name, np - endp);
			if (n >= np - endp)
			    goto oflow;
			np += n;
			continue;
		    case 'U':
			p++;
			n = strlcpy(np,  runas_pw->pw_name, np - endp);
			if (n >= np - endp)
			    goto oflow;
			np += n;
			continue;
		    case '%':
			/* convert %% -> % */
			p++;
			break;
		    default:
			/* no conversion */
			break;
		}
	    }
	    *np++ = *p;
	    if (np >= endp)
		goto oflow;
	}
	*np = '\0';
    } else
	new_prompt = estrdup(old_prompt);

    debug_return_str(new_prompt);

oflow:
    /* We pre-allocate enough space, so this should never happen. */
    fatalx(_("internal error, %s overflow"), "expand_prompt()");
}
