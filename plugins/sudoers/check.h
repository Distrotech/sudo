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

#ifndef _SUDOERS_CHECK_H
#define _SUDOERS_CHECK_H

/* Status codes for timestamp_status() */
#define TS_CURRENT		0
#define TS_OLD			1
#define TS_MISSING		2
#define TS_NOFILE		3
#define TS_ERROR		4

/* This may be a function in some implementations. */
#define already_lectured(s)	(s != TS_MISSING && s != TS_ERROR)

/*
 * Info stored in tty ticket from stat(2) to help with tty matching.
 */
struct sudo_tty_info {
    dev_t dev;			/* ID of device tty resides on */
    dev_t rdev;			/* tty device ID */
    ino_t ino;			/* tty inode number */
    uid_t uid;			/* tty owner */
    gid_t gid;			/* tty group */
    pid_t sid;			/* ID of session with controlling tty */
};

bool  update_timestamp(struct passwd *pw);
int   build_timestamp(struct passwd *pw);
int   timestamp_status(struct passwd *pw);

#endif /* _SUDOERS_CHECK_H */
