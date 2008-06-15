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

#define MAGIC_LENGTH 0x1000

#define LOG(fmt, args...) do { \
        char debugmsg[4096]; \
        int debuglen; \
        debuglen = snprintf(debugmsg, sizeof debugmsg, fmt, ## args); \
        write(debugfd, debugmsg, debuglen); \
    } while (0)


struct sndctx {
	int normalfile;   /* if non-zero, the file is not decoded */
	size_t length;    /* length of sound data that has been synthesized */
	int pipefd;       /* pipefd from which to read sound data */
	pid_t pid;        /* pid of the decoding process */
	char *fname;      /* filename of the song being played */
	char buf[MAGIC_LENGTH];
};

static int debugfd = -1;


static int uadefs_getattr(const char *path, struct stat *stbuf)
{
	int res;

	res = lstat(path, stbuf);
	if (res == -1)
		return -errno;

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

static int uadefs_open(const char *path, struct fuse_file_info *fi)
{
	int ret;
	int bytes;
	struct sndctx *ctx = NULL;
	int fds[2];

	LOG("Trying to open %s\n", path);

	ctx = calloc(1, sizeof *ctx);
	ctx->fname = strdup(path);
	if (ctx->fname == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	ret = open(ctx->fname, fi->flags);
	if (ret == -1) {
		ret = -errno;
		goto err;
	}
	close(ret);

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
		dup2(debugfd, 2);

		execv("/home/shd/bin/uade123", argv);

		LOG("Could not execute uade123\n");
		abort();
	}

	ctx->pipefd = fds[0];
	close(fds[1]);

	fi->fh = (uint64_t) ctx;

	bytes = 0;
	while (bytes < MAGIC_LENGTH) {
		ret = read(ctx->pipefd, ctx->buf + bytes, MAGIC_LENGTH - bytes);
		if (ret == 0) {
			LOG("File is not playable: %s\n", path);
			ctx->normalfile = 1;
			break;
		} else if (ret == -1) {
			LOG("Something odd happened: %s (%s)\n", path, strerror(errno));
			ret = -errno;
			goto err;
		}
		bytes += ret;
	}

	LOG("Opened %s\n", ctx->fname);

	return 0;

 err:
	if (ctx)
		free(ctx);

	return ret;
}

static int uadefs_read(const char *path, char *buf, size_t size, off_t offset,
		       struct fuse_file_info *fi)
{
	int fd;
	ssize_t res;
	struct sndctx *ctx = (struct sndctx *) fi->fh;
	size_t totalread = 0;

	if (ctx->normalfile) {
		fd = open(path, O_RDONLY);
		if (fd == -1)
			return -errno;

		totalread = pread(fd, buf, size, offset);
		if (totalread == -1)
			totalread = -errno;

		close(fd);

		return totalread;
	}

	LOG("Offset %zd Size %zd\n", offset, size);

	if (offset < MAGIC_LENGTH) {
		ssize_t msize = size;

		if (msize > (MAGIC_LENGTH - offset))
			msize = MAGIC_LENGTH - offset;

		memcpy(buf, ctx->buf + offset, msize);
		LOG("copy %zd\n", msize);

		ctx->length = offset + msize;
		offset += msize;
		size -= msize;
		buf += msize;
		totalread += msize;

		if (size == 0)
			return totalread;

		/* More data expected, continue reading */
	}

	if (offset > ctx->length) {
		/* Skip sound data */
		char skipbuf[4096];
		size_t toread;

		while (ctx->length < offset) {
			toread = offset - ctx->length;
			if (toread > sizeof(skipbuf))
				toread = sizeof(skipbuf);

			res = read(ctx->pipefd, skipbuf, toread);
			LOG("read %zd\n", res);

			if (res == 0) {
				/* What to do here? */
				return 0;
			} else if (res == -1) {
				return -errno;
			}

			ctx->length += res;
		}
	}

	if (offset < ctx->length) {
		LOG("Offset < ctx->length. Not implemented.\n");
		totalread = 0;

	} else if (offset == ctx->length) {

		while (size > 0) {
			res = read(ctx->pipefd, buf, size);
			LOG("read %zd\n", res);
			if (res == -1) {
				if (totalread == 0)
					totalread = -errno;
				break;
			} else if (res == 0) {
				break;
			}
			ctx->length += res;
			totalread += res;
			buf += res;
			size -= res;
		}
	}

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
	struct sndctx *ctx = (struct sndctx *) fi->fh;
	(void) path;

	if (waitpid(ctx->pid, NULL, WNOHANG) == 0)
		kill(ctx->pid, SIGINT);

	while (waitpid(ctx->pid, NULL, WNOHANG) > 0);

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
	debugfd = open("uadefs.log", O_WRONLY | O_TRUNC | O_CREAT);

	umask(0);
	return fuse_main(argc, argv, &uadefs_oper, NULL);
}
