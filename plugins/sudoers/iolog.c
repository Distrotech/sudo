/*
 * Copyright (c) 2009-2013 Todd C. Miller <Todd.Miller@courtesan.com>
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
#include <sys/time.h>
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
#if TIME_WITH_SYS_TIME
# include <time.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>
#ifdef HAVE_ZLIB_H
# include <zlib.h>
#endif

#include "sudoers.h"

struct script_buf {
    int len; /* buffer length (how much read in) */
    int off; /* write position (how much already consumed) */
    char buf[16 * 1024];
};

/* XXX - separate sudoers.h and iolog.h? */
#undef runas_pw
#undef runas_gr

struct iolog_details {
    const char *cwd;
    const char *tty;
    const char *user;
    const char *command;
    const char *iolog_path;
    struct passwd *runas_pw;
    struct group *runas_gr;
    int lines;
    int cols;
};

union io_fd {
    FILE *f;
#ifdef HAVE_ZLIB_H
    gzFile g;
#endif
    void *v;
};

static struct io_log_file {
    bool enabled;
    const char *suffix;
    union io_fd fd;
} io_log_files[] = {
#define IOFD_LOG	0
    { true,  "/log" },
#define IOFD_TIMING	1
    { true,  "/timing" },
#define IOFD_STDIN	2
    { false, "/stdin" },
#define IOFD_STDOUT	3
    { false, "/stdout" },
#define IOFD_STDERR	4
    { false, "/stderr" },
#define IOFD_TTYIN	5
    { false, "/ttyin" },
#define IOFD_TTYOUT	6
    { false, "/ttyout" },
#define IOFD_MAX	7
    { false, NULL }
};

#define SESSID_MAX	2176782336U

static int iolog_compress;
static struct timeval last_time;
static unsigned int sessid_max = SESSID_MAX;

/* sudoers_io is declared at the end of this file. */
extern __dso_public struct io_plugin sudoers_io;

/*
 * Create path and any parent directories as needed.
 * If is_temp is set, use mkdtemp() for the final directory.
 */
static void
io_mkdirs(char *path, mode_t mode, bool is_temp)
{
    struct stat sb;
    gid_t parent_gid = 0;
    char *slash = path;
    debug_decl(io_mkdirs, SUDO_DEBUG_UTIL)

    /* Fast path: not a temporary and already exists. */
    if (!is_temp && stat(path, &sb) == 0) {
	if (!S_ISDIR(sb.st_mode)) {
	    log_fatal(0, N_("%s exists but is not a directory (0%o)"),
		path, (unsigned int) sb.st_mode);
	}
	debug_return;
    }

    while ((slash = strchr(slash + 1, '/')) != NULL) {
	*slash = '\0';
	if (stat(path, &sb) != 0) {
	    if (mkdir(path, mode) != 0)
		log_fatal(USE_ERRNO, N_("unable to mkdir %s"), path);
	    ignore_result(chown(path, (uid_t)-1, parent_gid));
	} else if (!S_ISDIR(sb.st_mode)) {
	    log_fatal(0, N_("%s exists but is not a directory (0%o)"),
		path, (unsigned int) sb.st_mode);
	} else {
	    /* Inherit gid of parent dir for ownership. */
	    parent_gid = sb.st_gid;
	}
	*slash = '/';
    }
    /* Create final path component. */
    if (is_temp) {
	if (mkdtemp(path) == NULL)
	    log_fatal(USE_ERRNO, N_("unable to mkdir %s"), path);
	ignore_result(chown(path, (uid_t)-1, parent_gid));
    } else {
	if (mkdir(path, mode) != 0 && errno != EEXIST)
	    log_fatal(USE_ERRNO, N_("unable to mkdir %s"), path);
	ignore_result(chown(path, (uid_t)-1, parent_gid));
    }
    debug_return;
}

/*
 * Set max session ID (aka sequence number)
 */
int
io_set_max_sessid(const char *maxval)
{
    unsigned long ulval;
    char *ep;

    errno = 0;
    ulval = strtoul(maxval, &ep, 0);
    if (*maxval != '\0' && *ep == '\0' &&
	(errno != ERANGE || ulval != ULONG_MAX)) {
	sessid_max = MIN((unsigned int)ulval, SESSID_MAX);
	return true;
    }
    return false;
}

/*
 * Read the on-disk sequence number, set sessid to the next
 * number, and update the on-disk copy.
 * Uses file locking to avoid sequence number collisions.
 */
void
io_nextid(char *iolog_dir, char *iolog_dir_fallback, char sessid[7])
{
    struct stat sb;
    char buf[32], *ep;
    int fd, i;
    unsigned long id = 0;
    int len;
    ssize_t nread;
    char pathbuf[PATH_MAX];
    static const char b36char[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    debug_decl(io_nextid, SUDO_DEBUG_UTIL)

    /*
     * Create I/O log directory if it doesn't already exist.
     */
    io_mkdirs(iolog_dir, S_IRWXU, false);

    /*
     * Open sequence file
     */
    len = snprintf(pathbuf, sizeof(pathbuf), "%s/seq", iolog_dir);
    if (len <= 0 || len >= sizeof(pathbuf)) {
	errno = ENAMETOOLONG;
	log_fatal(USE_ERRNO, "%s/seq", pathbuf);
    }
    fd = open(pathbuf, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR);
    if (fd == -1)
	log_fatal(USE_ERRNO, N_("unable to open %s"), pathbuf);
    lock_file(fd, SUDO_LOCK);

    /*
     * If there is no seq file in iolog_dir and a fallback dir was
     * specified, look for seq in the fallback dir.  This is to work
     * around a bug in sudo 1.8.5 and older where iolog_dir was not
     * expanded before the sequence number was updated.
     */
    if (iolog_dir_fallback != NULL && fstat(fd, &sb) == 0 && sb.st_size == 0) {
	char fallback[PATH_MAX];

	len = snprintf(fallback, sizeof(fallback), "%s/seq",
	    iolog_dir_fallback);
	if (len > 0 && len < sizeof(fallback)) {
	    int fd2 = open(fallback, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR);
	    if (fd2 != -1) {
		nread = read(fd2, buf, sizeof(buf));
		if (nread > 0) {
		    id = strtoul(buf, &ep, 36);
		    if (buf == ep || id >= sessid_max)
			id = 0;
		}
		close(fd2);
	    }
	}
    }

    /* Read current seq number (base 36). */
    if (id == 0) {
	nread = read(fd, buf, sizeof(buf));
	if (nread != 0) {
	    if (nread == -1)
		log_fatal(USE_ERRNO, N_("unable to read %s"), pathbuf);
	    id = strtoul(buf, &ep, 36);
	    if (buf == ep || id >= sessid_max)
		id = 0;
	}
    }
    id++;

    /*
     * Convert id to a string and stash in sessid.
     * Note that that least significant digits go at the end of the string.
     */
    for (i = 5; i >= 0; i--) {
	buf[i] = b36char[id % 36];
	id /= 36;
    }
    buf[6] = '\n';

    /* Stash id for logging purposes. */
    memcpy(sessid, buf, 6);
    sessid[6] = '\0';

    /* Rewind and overwrite old seq file. */
    if (lseek(fd, (off_t)0, SEEK_SET) == (off_t)-1 || write(fd, buf, 7) != 7)
	log_fatal(USE_ERRNO, N_("unable to write to %s"), pathbuf);
    close(fd);

    debug_return;
}

/*
 * Copy iolog_path to pathbuf and create the directory and any intermediate
 * directories.  If iolog_path ends in 'XXXXXX', use mkdtemp().
 */
static size_t
mkdir_iopath(const char *iolog_path, char *pathbuf, size_t pathsize)
{
    size_t len;
    bool is_temp = false;
    debug_decl(mkdir_iopath, SUDO_DEBUG_UTIL)

    len = strlcpy(pathbuf, iolog_path, pathsize);
    if (len >= pathsize) {
	errno = ENAMETOOLONG;
	log_fatal(USE_ERRNO, "%s", iolog_path);
    }

    /*
     * Create path and intermediate subdirs as needed.
     * If path ends in at least 6 Xs (ala POSIX mktemp), use mkdtemp().
     */
    if (len >= 6 && strcmp(&pathbuf[len - 6], "XXXXXX") == 0)
	is_temp = true;
    io_mkdirs(pathbuf, S_IRWXU, is_temp);

    debug_return_size_t(len);
}

/*
 * Append suffix to pathbuf after len chars and open the resulting file.
 * Note that the size of pathbuf is assumed to be PATH_MAX.
 * Uses zlib if docompress is true.
 * Stores the open file handle which has the close-on-exec flag set.
 */
static void
open_io_fd(char *pathbuf, size_t len, struct io_log_file *iol, bool docompress)
{
    int fd;
    debug_decl(open_io_fd, SUDO_DEBUG_UTIL)

    pathbuf[len] = '\0';
    strlcat(pathbuf, iol->suffix, PATH_MAX);
    if (iol->enabled) {
	fd = open(pathbuf, O_CREAT|O_WRONLY, S_IRUSR|S_IWUSR);
	if (fd != -1) {
	    fcntl(fd, F_SETFD, FD_CLOEXEC);
#ifdef HAVE_ZLIB_H
	    if (docompress)
		iol->fd.g = gzdopen(fd, "w");
	    else
#endif
		iol->fd.f = fdopen(fd, "w");
	}
	if (fd == -1 || iol->fd.v == NULL) {
	    log_fatal(USE_ERRNO, N_("unable to create %s"), pathbuf);
	    if (fd != -1)
		close(fd);
	}
    } else {
	/* Remove old log file if we recycled sequence numbers. */
	unlink(pathbuf);
    }
    debug_return;
}

/*
 * Pull out I/O log related data from user_info and command_info arrays.
 * Returns true if I/O logging is enabled, else false.
 */
static bool
iolog_deserialize_info(struct iolog_details *details, char * const user_info[],
    char * const command_info[])
{
    const char *runas_uid_str = "0", *runas_euid_str = NULL;
    const char *runas_gid_str = "0", *runas_egid_str = NULL;
    char id[MAX_UID_T_LEN + 2], *ep;
    char * const *cur;
    unsigned long ulval;
    uid_t runas_uid = 0;
    gid_t runas_gid = 0;
    debug_decl(iolog_deserialize_info, SUDO_DEBUG_UTIL)

    details->lines = 24;
    details->cols = 80;

    for (cur = user_info; *cur != NULL; cur++) {
	switch (**cur) {
	case 'c':
	    if (strncmp(*cur, "cols=", sizeof("cols=") - 1) == 0) {
		details->cols = atoi(*cur + sizeof("cols=") - 1);
		continue;
	    }
	    if (strncmp(*cur, "cwd=", sizeof("cwd=") - 1) == 0) {
		details->cwd = *cur + sizeof("cwd=") - 1;
		continue;
	    }
	    break;
	case 'l':
	    if (strncmp(*cur, "lines=", sizeof("lines=") - 1) == 0) {
		details->lines = atoi(*cur + sizeof("lines=") - 1);
		continue;
	    }
	    break;
	case 't':
	    if (strncmp(*cur, "tty=", sizeof("tty=") - 1) == 0) {
		details->tty = *cur + sizeof("tty=") - 1;
		continue;
	    }
	    break;
	case 'u':
	    if (strncmp(*cur, "user=", sizeof("user=") - 1) == 0) {
		details->user = *cur + sizeof("user=") - 1;
		continue;
	    }
	    break;
	}
    }

    for (cur = command_info; *cur != NULL; cur++) {
	switch (**cur) {
	case 'c':
	    if (strncmp(*cur, "command=", sizeof("command=") - 1) == 0) {
		details->command = *cur + sizeof("command=") - 1;
		continue;
	    }
	    break;
	case 'i':
	    if (strncmp(*cur, "iolog_path=", sizeof("iolog_path=") - 1) == 0) {
		details->iolog_path = *cur + sizeof("iolog_path=") - 1;
		continue;
	    }
	    if (strncmp(*cur, "iolog_stdin=", sizeof("iolog_stdin=") - 1) == 0) {
		if (atobool(*cur + sizeof("iolog_stdin=") - 1) == true)
		    io_log_files[IOFD_STDIN].enabled = true;
		continue;
	    }
	    if (strncmp(*cur, "iolog_stdout=", sizeof("iolog_stdout=") - 1) == 0) {
		if (atobool(*cur + sizeof("iolog_stdout=") - 1) == true)
		    io_log_files[IOFD_STDOUT].enabled = true;
		continue;
	    }
	    if (strncmp(*cur, "iolog_stderr=", sizeof("iolog_stderr=") - 1) == 0) {
		if (atobool(*cur + sizeof("iolog_stderr=") - 1) == true)
		    io_log_files[IOFD_STDERR].enabled = true;
		continue;
	    }
	    if (strncmp(*cur, "iolog_ttyin=", sizeof("iolog_ttyin=") - 1) == 0) {
		if (atobool(*cur + sizeof("iolog_ttyin=") - 1) == true)
		    io_log_files[IOFD_TTYIN].enabled = true;
		continue;
	    }
	    if (strncmp(*cur, "iolog_ttyout=", sizeof("iolog_ttyout=") - 1) == 0) {
		if (atobool(*cur + sizeof("iolog_ttyout=") - 1) == true)
		    io_log_files[IOFD_TTYOUT].enabled = true;
		continue;
	    }
	    if (strncmp(*cur, "iolog_compress=", sizeof("iolog_compress=") - 1) == 0) {
		if (atobool(*cur + sizeof("iolog_compress=") - 1) == true)
		    iolog_compress = true; /* must be global */
		continue;
	    }
	    break;
	case 'm':
	    if (strncmp(*cur, "maxseq=", sizeof("maxseq=") - 1) == 0)
		io_set_max_sessid(*cur + sizeof("maxseq=") - 1);
	    break;
	case 'r':
	    if (strncmp(*cur, "runas_gid=", sizeof("runas_gid=") - 1) == 0) {
		runas_gid_str = *cur + sizeof("runas_gid=") - 1;
		continue;
	    }
	    if (strncmp(*cur, "runas_egid=", sizeof("runas_egid=") - 1) == 0) {
		runas_egid_str = *cur + sizeof("runas_egid=") - 1;
		continue;
	    }
	    if (strncmp(*cur, "runas_uid=", sizeof("runas_uid=") - 1) == 0) {
		runas_uid_str = *cur + sizeof("runas_uid=") - 1;
		continue;
	    }
	    if (strncmp(*cur, "runas_euid=", sizeof("runas_euid=") - 1) == 0) {
		runas_euid_str = *cur + sizeof("runas_euid=") - 1;
		continue;
	    }
	    break;
	}
    }

    /*
     * Lookup runas user and group, preferring effective over real uid/gid.
     */
    if (runas_euid_str != NULL)
	runas_uid_str = runas_euid_str;
    if (runas_uid_str != NULL) {
	errno = 0;
	ulval = strtoul(runas_uid_str, &ep, 0);
	if (*runas_uid_str != '\0' && *ep == '\0' &&
	    (errno != ERANGE || ulval != ULONG_MAX)) {
	    runas_uid = (uid_t)ulval;
	}
    }
    if (runas_egid_str != NULL)
	runas_gid_str = runas_egid_str;
    if (runas_gid_str != NULL) {
	errno = 0;
	ulval = strtoul(runas_gid_str, &ep, 0);
	if (*runas_gid_str != '\0' && *ep == '\0' &&
	    (errno != ERANGE || ulval != ULONG_MAX)) {
	    runas_gid = (gid_t)ulval;
	}
    }

    details->runas_pw = sudo_getpwuid(runas_uid);
    if (details->runas_pw == NULL) {
	id[0] = '#';
	strlcpy(&id[1], runas_uid_str, sizeof(id) - 1);
	details->runas_pw = sudo_fakepwnam(id, runas_gid);
    }

    if (runas_gid != details->runas_pw->pw_gid) {
	details->runas_gr = sudo_getgrgid(runas_gid);
	if (details->runas_gr == NULL) {
	    id[0] = '#';
	    strlcpy(&id[1], runas_gid_str, sizeof(id) - 1);
	    details->runas_gr = sudo_fakegrnam(id);
	}
    }
    debug_return_bool(
	io_log_files[IOFD_STDIN].enabled || io_log_files[IOFD_STDOUT].enabled ||
	io_log_files[IOFD_STDERR].enabled || io_log_files[IOFD_TTYIN].enabled ||
	io_log_files[IOFD_TTYOUT].enabled);
}

static int
sudoers_io_open(unsigned int version, sudo_conv_t conversation,
    sudo_printf_t plugin_printf, char * const settings[],
    char * const user_info[], char * const command_info[],
    int argc, char * const argv[], char * const user_env[], char * const args[])
{
    struct iolog_details details;
    char pathbuf[PATH_MAX], sessid[7];
    char *tofree = NULL;
    char * const *cur;
    const char *debug_flags = NULL;
    size_t len;
    int i, rval = -1;
    debug_decl(sudoers_io_open, SUDO_DEBUG_PLUGIN)

    sudo_conv = conversation;
    sudo_printf = plugin_printf;

    /* If we have no command (because -V was specified) just return. */
    if (argc == 0)
	debug_return_bool(true);

    memset(&details, 0, sizeof(details));

    if (fatal_setjmp() != 0) {
	/* called via fatal(), fatalx() or log_fatal() */
	rval = -1;
	goto done;
    }

    bindtextdomain("sudoers", LOCALEDIR);

    sudo_setpwent();
    sudo_setgrent();

    /*
     * Check for debug flags in settings list.
     */
    for (cur = settings; *cur != NULL; cur++) {
	if (strncmp(*cur, "debug_flags=", sizeof("debug_flags=") - 1) == 0)
	    debug_flags = *cur + sizeof("debug_flags=") - 1;
    }
    if (debug_flags != NULL)
	sudo_debug_init(NULL, debug_flags);

    /*
     * Pull iolog settings out of command_info.
     */
    if (!iolog_deserialize_info(&details, user_info, command_info)) {
	rval = false;
	goto done;
    }

    /* If no I/O log path defined we need to figure it out ourselves. */
    if (details.iolog_path == NULL) {
	/* Get next session ID and convert it into a path. */
	tofree = emalloc(sizeof(_PATH_SUDO_IO_LOGDIR) + sizeof(sessid) + 2);
	memcpy(tofree, _PATH_SUDO_IO_LOGDIR, sizeof(_PATH_SUDO_IO_LOGDIR));
	io_nextid(tofree, NULL, sessid);
	snprintf(tofree + sizeof(_PATH_SUDO_IO_LOGDIR), sizeof(sessid) + 2,
	    "%c%c/%c%c/%c%c", sessid[0], sessid[1], sessid[2], sessid[3],
	    sessid[4], sessid[5]);
	details.iolog_path = tofree;
    }

    /*
     * Make local copy of I/O log path and create it, along with any
     * intermediate subdirs.  Calls mkdtemp() if iolog_path ends in XXXXXX.
     */
    len = mkdir_iopath(details.iolog_path, pathbuf, sizeof(pathbuf));
    if (len >= sizeof(pathbuf))
	goto done;

    /*
     * We create 7 files: a log file, a timing file and 5 for input/output.
     */
    for (i = 0; i < IOFD_MAX; i++) {
	open_io_fd(pathbuf, len, &io_log_files[i], i ? iolog_compress : false);
    }

    gettimeofday(&last_time, NULL);
    fprintf(io_log_files[IOFD_LOG].fd.f, "%lld:%s:%s:%s:%s:%d:%d\n%s\n%s",
	(long long)last_time.tv_sec,
	details.user ? details.user : "unknown", details.runas_pw->pw_name,
	details.runas_gr ? details.runas_gr->gr_name : "",
	details.tty ? details.tty : "unknown", details.lines, details.cols,
	details.cwd ? details.cwd : "unknown",
	details.command ? details.command : "unknown");
    for (cur = &argv[1]; *cur != NULL; cur++) {
	fputc(' ', io_log_files[IOFD_LOG].fd.f);
	fputs(*cur, io_log_files[IOFD_LOG].fd.f);
    }
    fputc('\n', io_log_files[IOFD_LOG].fd.f);
    fclose(io_log_files[IOFD_LOG].fd.f);
    io_log_files[IOFD_LOG].fd.f = NULL;

    /*
     * Clear I/O log function pointers for disabled log functions.
     */
    if (!io_log_files[IOFD_STDIN].enabled)
	sudoers_io.log_stdin = NULL;
    if (!io_log_files[IOFD_STDOUT].enabled)
	sudoers_io.log_stdout = NULL;
    if (!io_log_files[IOFD_STDERR].enabled)
	sudoers_io.log_stderr = NULL;
    if (!io_log_files[IOFD_TTYIN].enabled)
	sudoers_io.log_ttyin = NULL;
    if (!io_log_files[IOFD_TTYOUT].enabled)
	sudoers_io.log_ttyout = NULL;

    rval = true;

done:
    fatal_disable_setjmp();
    efree(tofree);
    if (details.runas_pw)
	sudo_pw_delref(details.runas_pw);
    sudo_endpwent();
    if (details.runas_gr)
	sudo_gr_delref(details.runas_gr);
    sudo_endgrent();

    debug_return_bool(rval);
}

static void
sudoers_io_close(int exit_status, int error)
{
    int i;
    debug_decl(sudoers_io_close, SUDO_DEBUG_PLUGIN)

    if (fatal_setjmp() != 0) {
	/* called via fatal(), fatalx() or log_fatal() */
	fatal_disable_setjmp();
	debug_return;
    }

    for (i = 0; i < IOFD_MAX; i++) {
	if (io_log_files[i].fd.v == NULL)
	    continue;
#ifdef HAVE_ZLIB_H
	if (iolog_compress)
	    gzclose(io_log_files[i].fd.g);
	else
#endif
	    fclose(io_log_files[i].fd.f);
    }
    debug_return;
}

static int
sudoers_io_version(int verbose)
{
    debug_decl(sudoers_io_version, SUDO_DEBUG_PLUGIN)

    if (fatal_setjmp() != 0) {
	/* called via fatal(), fatalx() or log_fatal() */
	fatal_disable_setjmp();
	debug_return_bool(-1);
    }

    sudo_printf(SUDO_CONV_INFO_MSG, "Sudoers I/O plugin version %s\n",
	PACKAGE_VERSION);

    debug_return_bool(true);
}

/*
 * Generic I/O logging function.  Called by the I/O logging entry points.
 */
static int
sudoers_io_log(const char *buf, unsigned int len, int idx)
{
    struct timeval now, delay;
    debug_decl(sudoers_io_version, SUDO_DEBUG_PLUGIN)

    gettimeofday(&now, NULL);

    if (fatal_setjmp() != 0) {
	/* called via fatal(), fatalx() or log_fatal() */
	fatal_disable_setjmp();
	debug_return_bool(-1);
    }

#ifdef HAVE_ZLIB_H
    if (iolog_compress)
	ignore_result(gzwrite(io_log_files[idx].fd.g, (const voidp)buf, len));
    else
#endif
	ignore_result(fwrite(buf, 1, len, io_log_files[idx].fd.f));
    delay.tv_sec = now.tv_sec;
    delay.tv_usec = now.tv_usec;
    timevalsub(&delay, &last_time);
#ifdef HAVE_ZLIB_H
    if (iolog_compress)
	gzprintf(io_log_files[IOFD_TIMING].fd.g, "%d %f %d\n", idx,
	    delay.tv_sec + ((double)delay.tv_usec / 1000000), len);
    else
#endif
	fprintf(io_log_files[IOFD_TIMING].fd.f, "%d %f %d\n", idx,
	    delay.tv_sec + ((double)delay.tv_usec / 1000000), len);
    last_time.tv_sec = now.tv_sec;
    last_time.tv_usec = now.tv_usec;

    debug_return_bool(true);
}

static int
sudoers_io_log_ttyin(const char *buf, unsigned int len)
{
    return sudoers_io_log(buf, len, IOFD_TTYIN);
}

static int
sudoers_io_log_ttyout(const char *buf, unsigned int len)
{
    return sudoers_io_log(buf, len, IOFD_TTYOUT);
}

static int
sudoers_io_log_stdin(const char *buf, unsigned int len)
{
    return sudoers_io_log(buf, len, IOFD_STDIN);
}

static int
sudoers_io_log_stdout(const char *buf, unsigned int len)
{
    return sudoers_io_log(buf, len, IOFD_STDOUT);
}

static int
sudoers_io_log_stderr(const char *buf, unsigned int len)
{
    return sudoers_io_log(buf, len, IOFD_STDERR);
}

__dso_public struct io_plugin sudoers_io = {
    SUDO_IO_PLUGIN,
    SUDO_API_VERSION,
    sudoers_io_open,
    sudoers_io_close,
    sudoers_io_version,
    sudoers_io_log_ttyin,
    sudoers_io_log_ttyout,
    sudoers_io_log_stdin,
    sudoers_io_log_stdout,
    sudoers_io_log_stderr
};
