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

static int debugfd = -1;
static int debugmode;
static pthread_mutex_t readmutex = PTHREAD_MUTEX_INITIALIZER;

static void kill_child(struct sndctx *ctx);
static struct sndctx *uadefs_open_file(int *success, const char *path);


/*
 * xread() is the same a read(), but it automatically restarts read()
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

static struct sndctx *create_ctx(void)
{
	struct sndctx *ctx;

	ctx = calloc(1, sizeof ctx[0]);
	if (ctx == NULL)
		return NULL;

	ctx->pipefd = -1;
	ctx->pid = -1;

	return ctx;
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
		ctx->nblocks = -1;
	}

}

static void destroy_ctx(struct sndctx *ctx)
{
	destroy_cache(ctx);

	free(ctx->fname);
	ctx->fname = NULL;

	if (ctx->normalfile == 0) {
		close(ctx->pipefd);
		ctx->pipefd = -1;

		kill_child(ctx);
	}

	free(ctx);
}

static inline struct sndctx *get_uadefs_file(struct fuse_file_info *fi)
{
	return (struct sndctx *) (uintptr_t) fi->fh;
}

static void kill_child(struct sndctx *ctx)
{
	kill(ctx->pid, SIGINT);
	while (waitpid(ctx->pid, NULL, 0) <= 0);
	ctx->pid = -1;
}

static int uadefs_getattr(const char *path, struct stat *stbuf)
{
	int res;
	struct sndctx *ctx;

	res = lstat(path, stbuf);
	if (res == -1)
		return -errno;

	ctx = uadefs_open_file(&res, path);
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

	return 0;
}

static int uadefs_access(const char *path, int mask)
{
	int res;

	res = access(path, mask);
	if (res == -1)
		return -errno;

	return 0;
}

static int uadefs_readlink(const char *path, char *buf, size_t size)
{
	int res;

	res = readlink(path, buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
}


static int uadefs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			  off_t offset, struct fuse_file_info *fi)
{
	DIR *dp;
	struct dirent *de;

	(void) offset;
	(void) fi;

	dp = opendir(path);
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

static int uadefs_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;

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
	if (res == -1)
		return -errno;

	return 0;
}

static int uadefs_mkdir(const char *path, mode_t mode)
{
	int res;

	res = mkdir(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int uadefs_unlink(const char *path)
{
	int res;

	res = unlink(path);
	if (res == -1)
		return -errno;

	return 0;
}

static int uadefs_rmdir(const char *path)
{
	int res;

	res = rmdir(path);
	if (res == -1)
		return -errno;

	return 0;
}

static int uadefs_symlink(const char *from, const char *to)
{
	int res;

	res = symlink(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int uadefs_rename(const char *from, const char *to)
{
	int res;

	res = rename(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int uadefs_link(const char *from, const char *to)
{
	int res;

	res = link(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int uadefs_chmod(const char *path, mode_t mode)
{
	int res;

	res = chmod(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int uadefs_chown(const char *path, uid_t uid, gid_t gid)
{
	int res;

	res = lchown(path, uid, gid);
	if (res == -1)
		return -errno;

	return 0;
}

static int uadefs_truncate(const char *path, off_t size)
{
	int res;

	res = truncate(path, size);
	if (res == -1)
		return -errno;

	return 0;
}

static int uadefs_utimens(const char *path, const struct timespec ts[2])
{
	int res;
	struct timeval tv[2];

	tv[0].tv_sec = ts[0].tv_sec;
	tv[0].tv_usec = ts[0].tv_nsec / 1000;
	tv[1].tv_sec = ts[1].tv_sec;
	tv[1].tv_usec = ts[1].tv_nsec / 1000;

	res = utimes(path, tv);
	if (res == -1)
		return -errno;

	return 0;
}

static struct sndctx *uadefs_open_file(int *success, const char *path)
{
	int ret;
	struct sndctx *ctx = NULL;
	int fds[2];
	struct stat st;
	char crapbuf[MAGIC_LENGTH];

	ctx = create_ctx();
	if (ctx == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	ctx->fname = strdup(path);
	if (ctx->fname == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	if (stat(ctx->fname, &st)) {
		ret = -errno;
		goto err;
	}

	if (!S_ISREG(st.st_mode)) {
		ctx->normalfile = 1;
		*success = 0;
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
		ctx->normalfile = 1;

		destroy_cache(ctx);

		kill_child(ctx);

		close(ctx->pipefd);
		ctx->pipefd = -1;
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

static int uadefs_open(const char *path, struct fuse_file_info *fi)
{
	int ret;
	struct sndctx *ctx;

	ctx = uadefs_open_file(&ret, path);
	if (ctx == NULL)
		return ret;

	fi->direct_io = 1;
	fi->fh = (uint64_t) ctx;

	DEBUG("Opened %s as %s file\n", ctx->fname, ctx->normalfile ? "normal" : "UADE");
	return 0;
}

static int uadefs_read(const char *path, char *buf, size_t size, off_t off,
		       struct fuse_file_info *fi)
{
	int fd;
	ssize_t res;
	struct sndctx *ctx = get_uadefs_file(fi);
	ssize_t totalread = 0;
	size_t bsize;

	if (ctx->normalfile) {
		fd = open(path, O_RDONLY);
		if (fd == -1)
			return -errno;

		totalread = pread(fd, buf, size, off);
		if (totalread == -1)
			totalread = -errno;

		close(fd);

		return totalread;
	}

	pthread_mutex_lock(&readmutex);
	DEBUG("offset %zd size %zd\n", off, size);

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

static int uadefs_write(const char *path, const char *buf, size_t size,
			off_t offset, struct fuse_file_info *fi)
{
	int fd;
	int res;

	(void) fi;
	fd = open(path, O_WRONLY);
	if (fd == -1)
		return -errno;

	res = pwrite(fd, buf, size, offset);
	if (res == -1)
		res = -errno;

	close(fd);
	return res;
}

static int uadefs_statfs(const char *path, struct statvfs *stbuf)
{
	int res;

	res = statvfs(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int uadefs_release(const char *path, struct fuse_file_info *fi)
{
	(void) path;

	destroy_ctx(get_uadefs_file(fi));

	DEBUG("release %s\n", path);

	return 0;
}

static int uadefs_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void) path;
	(void) isdatasync;
	(void) fi;
	return 0;
}

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int uadefs_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
{
	int res = lsetxattr(path, name, value, size, flags);
	if (res == -1)
		return -errno;
	return 0;
}

static int uadefs_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
	int res = lgetxattr(path, name, value, size);
	if (res == -1)
		return -errno;
	return res;
}

static int uadefs_listxattr(const char *path, char *list, size_t size)
{
	int res = llistxattr(path, list, size);
	if (res == -1)
		return -errno;
	return res;
}

static int uadefs_removexattr(const char *path, const char *name)
{
	int res = lremovexattr(path, name);
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

int main(int argc, char *argv[])
{
	char logfname[4096];
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "-f") == 0) {
			debugmode = 1;
			break;
		}
	}

	if (getenv("HOME")) {
		int flags = O_WRONLY | O_TRUNC | O_APPEND | O_CREAT;
		int fmode = S_IRUSR | S_IWUSR;

		snprintf(logfname, sizeof logfname, "%s/.uade2/uadefs.log", getenv("HOME"));
		debugfd = open(logfname, flags, fmode);
	}

	umask(0);
	return fuse_main(argc, argv, &uadefs_oper, NULL);
}
