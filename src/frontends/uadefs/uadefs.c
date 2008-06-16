/*
 * uadefs decodes Amiga songs transparently into WAV files
 *
 * The code was forked from fusexmp example.
 *
 * Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
 * Copyright (C) 2008       Heikki Orsila <heikki.orsila@iki.fi>
 * 
 * This program can be distributed under the terms of the GNU GPL.
 * See the file COPYING.
 */

#define _GNU_SOURCE

#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif

#include <pthread.h>
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>

#define MAGIC_LENGTH 0x1000

#define CACHE_BLOCK_SHIFT 12  /* 4096 bytes per cache block */
#define CACHE_BLOCK_SIZE (1 << CACHE_BLOCK_SHIFT)
#define CACHE_LSB_MASK (CACHE_BLOCK_SIZE - 1)
#define CACHE_SECONDS 512
#define SND_PER_SECOND (44100 * 4)

#define DEBUG(fmt, args...) if (debugmode) { fprintf(stderr, fmt, ## args); }

#define LOG(fmt, args...) if (debugfd != -1) { \
        char debugmsg[4096]; \
        int debuglen; \
        DEBUG(fmt, ## args); \
        debuglen = snprintf(debugmsg, sizeof debugmsg, fmt, ## args); \
        write(debugfd, debugmsg, debuglen); \
    } while (0)

#define MAX(x, y) (x >= y) ? (x) : (y)
#define MIN(x, y) (x <= y) ? (x) : (y)

struct cacheblock {
	unsigned int bytes; /* 0 <= bytes <= CACHE_BLOCK_SIZE */
	void *data;
};

struct sndctx {
	int normalfile;   /* if non-zero, the file is not decoded */
	int pipefd;       /* pipefd from which to read sound data */
	pid_t pid;        /* pid of the decoding process */
	char *fname;      /* filename of the song being played */

	size_t nblocks;
	size_t start_bi;
	size_t end_bi;
	struct cacheblock *blocks;
};

static char *srcdir = NULL;
static int debugfd = -1;
static int debugmode;
static pthread_mutex_t readmutex = PTHREAD_MUTEX_INITIALIZER;

static char *uadefs_get_path(const char *path)
{
	char *realpath;

	if (asprintf(&realpath, "%s%s", srcdir, path) < 0) {
		fprintf(stderr, "No memory for path name: %s\n", path);
		exit(1);
	}

	return realpath;
}

/*
 * xread() is the same as the read(), but it automatically restarts read()
 * operations with a recoverable error (EAGAIN and EINTR). xread()
 * DOES NOT GUARANTEE that "len" bytes is read even if the data is available.
 */
static ssize_t xread(int fd, void *buf, size_t len)
{
	ssize_t nr;
	while (1) {
		nr = read(fd, buf, len);
		if ((nr < 0) && (errno == EAGAIN || errno == EINTR))
			continue;
		return nr;
	}
}

ssize_t read_in_full(int fd, void *buf, size_t count)
{
	char *p = buf;
	ssize_t total = 0;

	while (count > 0) {
		ssize_t loaded = xread(fd, p, count);
		if (loaded <= 0)
			return total ? total : loaded;
		count -= loaded;
		p += loaded;
		total += loaded;
	}

	return total;
}

static ssize_t cache_block_read(struct sndctx *ctx, char *buf, size_t offset,
				size_t size)
{
	size_t toread;
	size_t offset_bi;
	struct cacheblock *cb;

	offset_bi = offset >> CACHE_BLOCK_SHIFT;

	if (offset_bi < ctx->start_bi) {
		LOG("offset < cache offset: %s\n", ctx->fname);
		return 0;
	}

	if (offset_bi >= (ctx->start_bi + ctx->nblocks)) {
		LOG("Too much sound data: %s\n", ctx->fname);
		return 0;
	}

	cb = &ctx->blocks[offset_bi - ctx->start_bi];

	if (cb->bytes > 0) {
		size_t lsb = offset & CACHE_LSB_MASK;

		if ((lsb + size) > CACHE_BLOCK_SIZE) {
			LOG("lsb + size (%zd) failed: %zd %zd\n", lsb + size, offset, size);
			abort();
		}

		if (lsb >= cb->bytes)
			return 0;

		toread = MIN(size, cb->bytes - lsb);

		memcpy(buf, ((char *) cb->data) + lsb, toread);

		return toread;
	}

	return -1;
}

static void cache_init(struct sndctx *ctx)
{
	ctx->start_bi = 0;
	ctx->end_bi = 0;
	ctx->nblocks = (SND_PER_SECOND * CACHE_SECONDS) >> CACHE_BLOCK_SHIFT;
	ctx->blocks = calloc(1, ctx->nblocks * sizeof(ctx->blocks[0]));
	if (ctx->blocks == NULL) {
		LOG("No memory for cache\n");
		abort();
	}
}

static ssize_t cache_read(struct sndctx *ctx, char *buf, size_t offset,
			  size_t size)
{
	size_t offset_bi;
	size_t cbi;
	struct cacheblock *cb;
	ssize_t res;
	size_t length_bi;

	res = cache_block_read(ctx, buf, offset, size);
	if (res >= 0)
		return res;

	offset_bi = offset >> CACHE_BLOCK_SHIFT;

	if (offset_bi < ctx->start_bi) {
		LOG("offset < cache offset: %s\n", ctx->fname);
		return 0;
	}

	if (offset_bi >= (ctx->start_bi + ctx->nblocks)) {
		LOG("Too much sound data: %s\n", ctx->fname);
		return 0;
	}

	cbi = offset_bi - ctx->start_bi;
	length_bi = ctx->end_bi - ctx->start_bi;

	while (length_bi <= cbi) {
		cb = &ctx->blocks[length_bi];

		cb->data = malloc(CACHE_BLOCK_SIZE);
		if (cb->data == NULL) {
			LOG("Out of memory: %s\n", ctx->fname);
			break;
		}

		res = read_in_full(ctx->pipefd, cb->data, CACHE_BLOCK_SIZE);
		if (res <= 0) {
			free(cb->data);
			cb->data = NULL;
			DEBUG("EOF at %zd: %s\n", (length_bi + ctx->start_bi) << CACHE_BLOCK_SHIFT, ctx->fname);
			break;
		}

		cb->bytes = res;

		length_bi++;

		if (res < CACHE_BLOCK_SIZE)
			break;
	}

	ctx->end_bi = ctx->start_bi + length_bi;

	res = cache_block_read(ctx, buf, offset, size);
	if (res >= 0)
		return res;

	return 0;
}

static struct sndctx *create_ctx(const char *path)
{
	struct sndctx *ctx;

	ctx = calloc(1, sizeof ctx[0]);
	if (ctx == NULL)
		goto err;

	ctx->fname = strdup(path);
	if (ctx->fname == NULL)
		goto err;

	ctx->pipefd = -1;
	ctx->pid = -1;

	return ctx;

 err:
	if (ctx) {
		free(ctx->fname);
		ctx->fname = NULL;
		free(ctx);
	}
	return NULL;
}

static void destroy_cache(struct sndctx *ctx)
{
	size_t cbi;

	if (ctx->blocks != NULL) {
		for (cbi = 0; cbi < ctx->nblocks; cbi++) {
			ctx->blocks[cbi].bytes = 0;
			free(ctx->blocks[cbi].data);
			ctx->blocks[cbi].data = NULL;
		}

		free(ctx->blocks);
		ctx->blocks = NULL;
		ctx->start_bi = -1;
		ctx->end_bi = -1;
		ctx->nblocks = 0;
	}
}

static void kill_child(struct sndctx *ctx)
{
	if (ctx->pid != -1) {
		kill(ctx->pid, SIGINT);
		while (waitpid(ctx->pid, NULL, 0) <= 0);
		ctx->pid = -1;
	}
}

static void set_no_snd_file(struct sndctx *ctx)
{
	ctx->normalfile = 1;

	destroy_cache(ctx);

	kill_child(ctx);

	close(ctx->pipefd);
	ctx->pipefd = -1;
}

static void destroy_ctx(struct sndctx *ctx)
{
	free(ctx->fname);
	ctx->fname = NULL;

	if (ctx->normalfile == 0)
		set_no_snd_file(ctx);

	free(ctx);
}

static inline struct sndctx *get_uadefs_file(struct fuse_file_info *fi)
{
	return (struct sndctx *) (uintptr_t) fi->fh;
}

static struct sndctx *open_file(int *success, const char *path)
{
	int ret;
	struct sndctx *ctx;
	int fds[2];
	struct stat st;
	char crapbuf[MAGIC_LENGTH];

	ctx = create_ctx(path);
	if (ctx == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	if (stat(path, &st)) {
		ret = -errno;
		goto err;
	}

	if (!S_ISREG(st.st_mode)) {
		set_no_snd_file(ctx);
		goto out;
	}

	if (pipe(fds)) {
		LOG("Can not create a pipe\n");
		ret = -errno;
		goto err;
	}

	ctx->pid = fork();
	if (ctx->pid == 0) {
		char *argv[] = {"uade123", "-c", "-k0", "--stderr", "-v",
				ctx->fname, NULL};
		int fd;

		close(0);
		close(2);
		close(fds[0]);

		fd = open("/dev/null", O_RDWR);
		if (fd < 0)
			LOG("Can not open /dev/null\n");

		dup2(fd, 0);
		dup2(fds[1], 1);
		dup2(fd, 2);

		DEBUG("Execute %s\n", UADENAME);

		execv(UADENAME, argv);

		LOG("Could not execute %s\n", UADENAME);
		abort();
	}

	ctx->pipefd = fds[0];
	close(fds[1]);

	cache_init(ctx);

	ret = cache_read(ctx, crapbuf, 0, sizeof(crapbuf));

	if (ret < sizeof(crapbuf)) {
		DEBUG("File is not playable: %s\n", path);
		set_no_snd_file(ctx);
	} else if (ret == -1) {
		LOG("Something odd happened: %s (%s)\n", path, strerror(errno));
		ret = -errno;
		goto err;
	}
 out:
	*success = 0;
	return ctx;

 err:
	if (ctx)
		destroy_ctx(ctx);

	*success = ret;
	return NULL;
}

static int uadefs_getattr(const char *fpath, struct stat *stbuf)
{
	int res;
	struct sndctx *ctx;
	char *path = uadefs_get_path(fpath);

	res = lstat(path, stbuf);
	if (res == -1) {
		free(path);
		return -errno;
	}

	ctx = open_file(&res, path);
	if (ctx != NULL) {
		if (ctx->normalfile == 0) {
			/*
			 * HACK HACK. Lie about the size, because we don't know
			 * it yet. Maybe we could use song content database
			 * here.
			 */
			stbuf->st_size = SND_PER_SECOND * CACHE_SECONDS;
			stbuf->st_blocks = stbuf->st_size / 512;
		}

		destroy_ctx(ctx);
	}

	free(path);
	return 0;
}

static int uadefs_access(const char *fpath, int mask)
{
	int res;
	char *path = uadefs_get_path(fpath);

	res = access(path, mask);

	free(path);

	if (res == -1)
		return -errno;

	return 0;
}

static int uadefs_readlink(const char *fpath, char *buf, size_t size)
{
	int res;
	char *path = uadefs_get_path(fpath);

	res = readlink(path, buf, size - 1);

	free(path);

	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
}


static int uadefs_readdir(const char *fpath, void *buf, fuse_fill_dir_t filler,
			  off_t offset, struct fuse_file_info *fi)
{
	DIR *dp;
	struct dirent *de;
	char *path = uadefs_get_path(fpath);

	(void) offset;
	(void) fi;

	dp = opendir(path);

	free(path);

	if (dp == NULL)
		return -errno;

	while ((de = readdir(dp)) != NULL) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;
		if (filler(buf, de->d_name, &st, 0))
			break;
	}

	closedir(dp);
	return 0;
}

static int uadefs_mknod(const char *fpath, mode_t mode, dev_t rdev)
{
	int res;
	char *path = uadefs_get_path(fpath);

	/* On Linux this could just be 'mknod(path, mode, rdev)' but this
	   is more portable */
	if (S_ISREG(mode)) {
		res = open(path, O_CREAT | O_EXCL | O_WRONLY, mode);
		if (res >= 0)
			res = close(res);
	} else if (S_ISFIFO(mode))
		res = mkfifo(path, mode);
	else
		res = mknod(path, mode, rdev);

	free(path);

	if (res == -1)
		return -errno;

	return 0;
}

static int uadefs_mkdir(const char *fpath, mode_t mode)
{
	int res;
	char *path = uadefs_get_path(fpath);

	res = mkdir(path, mode);

	free(path);

	if (res == -1)
		return -errno;

	return 0;
}

static int uadefs_unlink(const char *fpath)
{
	int res;
	char *path = uadefs_get_path(fpath);

	res = unlink(path);

	free(path);

	if (res == -1)
		return -errno;

	return 0;
}

static int uadefs_rmdir(const char *fpath)
{
	int res;
	char *path = uadefs_get_path(fpath);

	res = rmdir(path);

	free(path);

	if (res == -1)
		return -errno;

	return 0;
}

static int uadefs_symlink(const char *ffrom, const char *fto)
{
	int res;
	char *from = uadefs_get_path(ffrom);
	char *to = uadefs_get_path(fto);

	res = symlink(from, to);

	free(from);
	free(to);

	if (res == -1)
		return -errno;

	return 0;
}

static int uadefs_rename(const char *ffrom, const char *fto)
{
	int res;
	char *from = uadefs_get_path(ffrom);
	char *to = uadefs_get_path(fto);

	res = rename(from, to);

	free(from);
	free(to);

	if (res == -1)
		return -errno;

	return 0;
}

static int uadefs_link(const char *ffrom, const char *fto)
{
	int res;
	char *from = uadefs_get_path(ffrom);
	char *to = uadefs_get_path(fto);

	res = link(from, to);

	free(from);
	free(to);

	if (res == -1)
		return -errno;

	return 0;
}

static int uadefs_chmod(const char *fpath, mode_t mode)
{
	int res;
	char *path = uadefs_get_path(fpath);

	res = chmod(path, mode);

	free(path);

	if (res == -1)
		return -errno;

	return 0;
}

static int uadefs_chown(const char *fpath, uid_t uid, gid_t gid)
{
	int res;
	char *path = uadefs_get_path(fpath);

	res = lchown(path, uid, gid);

	free(path);

	if (res == -1)
		return -errno;

	return 0;
}

static int uadefs_truncate(const char *fpath, off_t size)
{
	(void) fpath;
	(void) size;

	return -EIO;
}

static int uadefs_utimens(const char *fpath, const struct timespec ts[2])
{
	int res;
	struct timeval tv[2];
	char *path = uadefs_get_path(fpath);

	tv[0].tv_sec = ts[0].tv_sec;
	tv[0].tv_usec = ts[0].tv_nsec / 1000;
	tv[1].tv_sec = ts[1].tv_sec;
	tv[1].tv_usec = ts[1].tv_nsec / 1000;

	res = utimes(path, tv);

	free(path);

	if (res == -1)
		return -errno;

	return 0;
}

static int uadefs_open(const char *fpath, struct fuse_file_info *fi)
{
	int ret;
	struct sndctx *ctx;
	char *path = uadefs_get_path(fpath);

	ctx = open_file(&ret, path);

	free(path);

	if (ctx == NULL)
		return ret;

	fi->direct_io = 1;
	fi->fh = (uint64_t) (uintptr_t) ctx;

	DEBUG("Opened %s as %s file\n", ctx->fname, ctx->normalfile ? "normal" : "UADE");
	return 0;
}

static int uadefs_read(const char *fpath, char *buf, size_t size, off_t off,
		       struct fuse_file_info *fi)
{
	int fd;
	ssize_t res;
	struct sndctx *ctx = get_uadefs_file(fi);
	ssize_t totalread = 0;
	size_t bsize;

	if (ctx->normalfile) {
		char *path = uadefs_get_path(fpath);

		fd = open(path, O_RDONLY);

		free(path);

		if (fd == -1)
			return -errno;

		totalread = pread(fd, buf, size, off);
		if (totalread == -1)
			totalread = -errno;

		close(fd);

		return totalread;
	}

	pthread_mutex_lock(&readmutex);
	DEBUG("offset %zu size %zu\n", (size_t) off, size);

	while (size > 0) {
		bsize = MIN(CACHE_BLOCK_SIZE - (off & CACHE_LSB_MASK), size);
		res = cache_read(ctx, buf, off, bsize);
		if (res <= 0) {
			if (res == -1 && totalread == 0)
				totalread = -errno;
			break;
		}
		totalread += res;
		buf += res;
		off += res;
		size -= res;
	}

	DEBUG("ret %zd\n", totalread);
	pthread_mutex_unlock(&readmutex);

	return totalread;
}

static int uadefs_write(const char *fpath, const char *buf, size_t size,
			off_t offset, struct fuse_file_info *fi)
{
	(void) fpath;
	(void) buf;
	(void) size;
	(void) offset;
	(void) fi;

	return -EIO;
}

static int uadefs_statfs(const char *fpath, struct statvfs *stbuf)
{
	int res;
	char *path = uadefs_get_path(fpath);

	res = statvfs(path, stbuf);

	free(path);

	if (res == -1)
		return -errno;

	return 0;
}

static int uadefs_release(const char *fpath, struct fuse_file_info *fi)
{
	destroy_ctx(get_uadefs_file(fi));

	DEBUG("release %s\n", fpath);

	return 0;
}

static int uadefs_fsync(const char *fpath, int isdatasync,
		     struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void) fpath;
	(void) isdatasync;
	(void) fi;
	return 0;
}

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int uadefs_setxattr(const char *fpath, const char *name, const char *value,
			size_t size, int flags)
{
	char *path = uadefs_get_path(fpath);
	int res;

	res = lsetxattr(path, name, value, size, flags);

	free(path);

	if (res == -1)
		return -errno;
	return 0;
}

static int uadefs_getxattr(const char *fpath, const char *name, char *value,
			size_t size)
{
	char *path = uadefs_get_path(fpath);
	int res;

	res = lgetxattr(path, name, value, size);

	free(path);

	if (res == -1)
		return -errno;
	return res;
}

static int uadefs_listxattr(const char *fpath, char *list, size_t size)
{
	char *path = uadefs_get_path(fpath);
	int res;

	res = llistxattr(path, list, size);

	free(path);

	if (res == -1)
		return -errno;
	return res;
}

static int uadefs_removexattr(const char *fpath, const char *name)
{
	char *path = uadefs_get_path(fpath);
	int res;

	res = lremovexattr(path, name);

	free(path);

	if (res == -1)
		return -errno;
	return 0;
}
#endif /* HAVE_SETXATTR */

static struct fuse_operations uadefs_oper = {
	.getattr	= uadefs_getattr,
	.access		= uadefs_access,
	.readlink	= uadefs_readlink,
	.readdir	= uadefs_readdir,
	.mknod		= uadefs_mknod,
	.mkdir		= uadefs_mkdir,
	.symlink	= uadefs_symlink,
	.unlink		= uadefs_unlink,
	.rmdir		= uadefs_rmdir,
	.rename		= uadefs_rename,
	.link		= uadefs_link,
	.chmod		= uadefs_chmod,
	.chown		= uadefs_chown,
	.truncate	= uadefs_truncate,
	.utimens	= uadefs_utimens,
	.open		= uadefs_open,
	.read		= uadefs_read,
	.write		= uadefs_write,
	.statfs		= uadefs_statfs,
	.release	= uadefs_release,
	.fsync		= uadefs_fsync,
#ifdef HAVE_SETXATTR
	.setxattr	= uadefs_setxattr,
	.getxattr	= uadefs_getxattr,
	.listxattr	= uadefs_listxattr,
	.removexattr	= uadefs_removexattr,
#endif
};

static void usage(const char *progname)
{
	fprintf(stderr,
"usage: %s musicdir mountpoint [options]\n"
"\n"
"general options:\n"
"    -o opt,[opt...]        mount options\n"
"    -h   --help            print help\n"
"    -V   --version         print version\n"
"\n", progname);
}

enum {
	KEY_HELP,
	KEY_VERSION,
	KEY_FOREGROUND,
};

static struct fuse_opt uadefs_opts[] = {
	FUSE_OPT_KEY("-V",             KEY_VERSION),
	FUSE_OPT_KEY("--version",      KEY_VERSION),
	FUSE_OPT_KEY("-h",             KEY_HELP),
	FUSE_OPT_KEY("--help",         KEY_HELP),
	FUSE_OPT_KEY("debug",          KEY_FOREGROUND),
	FUSE_OPT_KEY("-d",             KEY_FOREGROUND),
	FUSE_OPT_KEY("-f",             KEY_FOREGROUND),
	FUSE_OPT_END
};

static int uadefs_fuse_main(struct fuse_args *args)
{
#if FUSE_VERSION >= 26
	return fuse_main(args->argc, args->argv, &uadefs_oper, NULL);
#else
	return fuse_main(args->argc, args->argv, &uadefs_oper);
#endif
}

static int uadefs_opt_proc(void *data, const char *arg, int key,
			   struct fuse_args *outargs)
{
	(void) data;
	char dname[4096];

	switch (key) {
	case FUSE_OPT_KEY_NONOPT:
		if (!srcdir) {
			if (arg[0] == '/') {
				srcdir = strdup(arg);
				if (srcdir == NULL) {
					fprintf(stderr, "No memory for srcdir\n");
					exit(1);
				}
			} else {
				getcwd(dname, sizeof dname);
				if (asprintf(&srcdir, "%s/%s", dname, arg) == -1) {
					fprintf(stderr, "asprintf() failed\n");
					exit(1);
				}
			}

			while (1) {
				size_t l = strlen(srcdir);

				if (l == 1 && srcdir[0] == '/')
					break;

				if (srcdir[l - 1] != '/')
					break;

				srcdir[l - 1] = 0;
			}

			return 0;
		}
		return 1;

	case KEY_HELP:
		usage(outargs->argv[0]);
		fuse_opt_add_arg(outargs, "-ho");
		uadefs_fuse_main(outargs);
		exit(1);

	case KEY_VERSION:
		fprintf(stderr, "uadefs version N/A\n");
#if FUSE_VERSION >= 25
		fuse_opt_add_arg(outargs, "--version");
		uadefs_fuse_main(outargs);
#endif
		exit(0);

	case KEY_FOREGROUND:
		debugmode = 1;
		return 1;

	default:
		fprintf(stderr, "internal error\n");
		abort();
	}

	return 0;
}

int main(int argc, char *argv[])
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	if (fuse_opt_parse(&args, NULL, uadefs_opts, uadefs_opt_proc) == -1)
		exit(1);

	DEBUG("srcdir: %s\n", srcdir);

	if (getenv("HOME")) {
		int flags = O_WRONLY | O_TRUNC | O_APPEND | O_CREAT;
		int fmode = S_IRUSR | S_IWUSR;
		char logfname[4096];

		snprintf(logfname, sizeof logfname, "%s/.uade2/uadefs.log", getenv("HOME"));
		debugfd = open(logfname, flags, fmode);
	}

	umask(0);
	return uadefs_fuse_main(&args);
}
