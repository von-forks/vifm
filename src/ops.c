/* vifm
 * Copyright (C) 2011 xaizek.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "ops.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include <sys/stat.h> /* gid_t uid_t lstat() stat() */

#include <assert.h> /* assert() */
#include <stddef.h> /* NULL size_t */
#include <stdio.h> /* snprintf() */
#include <stdlib.h> /* calloc() free() */

#include "cfg/config.h"
#include "io/ioeta.h"
#include "io/iop.h"
#include "io/ior.h"
#include "menus/menus.h"
#ifdef _WIN32
#include "utils/fs.h"
#endif
#include "utils/fs_limits.h"
#include "utils/log.h"
#include "utils/macros.h"
#include "utils/path.h"
#include "background.h"
#include "status.h"
#include "trash.h"
#include "undo.h"

#ifdef SUPPORT_NO_CLOBBER
#define NO_CLOBBER "-n"
#else /* SUPPORT_NO_CLOBBER */
#define NO_CLOBBER ""
#endif /* SUPPORT_NO_CLOBBER */

#ifdef GNU_TOOLCHAIN
#define PRESERVE_FLAGS "--preserve=mode,timestamps"
#else
#define PRESERVE_FLAGS "-p"
#endif

static int op_none(ops_t *ops, void *data, const char *src, const char *dst);
static int op_remove(ops_t *ops, void *data, const char *src, const char *dst);
static int op_removesl(ops_t *ops, void *data, const char *src,
		const char *dst);
static int op_copy(ops_t *ops, void *data, const char src[], const char dst[]);
static int op_copyf(ops_t *ops, void *data, const char src[], const char dst[]);
static int op_cp(ops_t *ops, void *data, const char src[], const char dst[],
		int overwrite);
static int op_move(ops_t *ops, void *data, const char src[], const char dst[]);
static int op_movef(ops_t *ops, void *data, const char src[], const char dst[]);
static int op_mv(ops_t *ops, void *data, const char src[], const char dst[],
		int overwrite);
static int op_chown(ops_t *ops, void *data, const char *src, const char *dst);
static int op_chgrp(ops_t *ops, void *data, const char *src, const char *dst);
#ifndef _WIN32
static int op_chmod(ops_t *ops, void *data, const char *src, const char *dst);
static int op_chmodr(ops_t *ops, void *data, const char *src, const char *dst);
#else
static int op_addattr(ops_t *ops, void *data, const char *src, const char *dst);
static int op_subattr(ops_t *ops, void *data, const char *src, const char *dst);
#endif
static int op_symlink(ops_t *ops, void *data, const char *src, const char *dst);
static int op_mkdir(ops_t *ops, void *data, const char *src, const char *dst);
static int op_rmdir(ops_t *ops, void *data, const char *src, const char *dst);
static int op_mkfile(ops_t *ops, void *data, const char *src, const char *dst);
static int exec_io_op(ops_t *ops, int (*func)(io_args_t *const),
		io_args_t *const args);

typedef int (*op_func)(ops_t *ops, void *data, const char *src, const char *dst);

static op_func op_funcs[] = {
	op_none,     /* OP_NONE */
	op_none,     /* OP_USR */
	op_remove,   /* OP_REMOVE */
	op_removesl, /* OP_REMOVESL */
	op_copy,     /* OP_COPY */
	op_copyf,    /* OP_COPYF */
	op_move,     /* OP_MOVE */
	op_movef,    /* OP_MOVEF */
	op_move,     /* OP_MOVETMP1 */
	op_move,     /* OP_MOVETMP2 */
	op_move,     /* OP_MOVETMP3 */
	op_move,     /* OP_MOVETMP4 */
	op_chown,    /* OP_CHOWN */
	op_chgrp,    /* OP_CHGRP */
#ifndef _WIN32
	op_chmod,    /* OP_CHMOD */
	op_chmodr,   /* OP_CHMODR */
#else
	op_addattr,  /* OP_ADDATTR */
	op_subattr,  /* OP_SUBATTR */
#endif
	op_symlink,  /* OP_SYMLINK */
	op_symlink,  /* OP_SYMLINK2 */
	op_mkdir,    /* OP_MKDIR */
	op_rmdir,    /* OP_RMDIR */
	op_mkfile,   /* OP_MKFILE */
};
ARRAY_GUARD(op_funcs, OP_COUNT);

/* Operation descriptions for ops_describe(). */
static const char * op_descr[] = {
	"None",     /* OP_NONE */
	"Usr",      /* OP_USR */
	"Deleting", /* OP_REMOVE */
	"Deleting", /* OP_REMOVESL */
	"Copying",  /* OP_COPY */
	"Copying",  /* OP_COPYF */
	"Moving",   /* OP_MOVE */
	"Moving",   /* OP_MOVEF */
	"Moving",   /* OP_MOVETMP1 */
	"Moving",   /* OP_MOVETMP2 */
	"Moving",   /* OP_MOVETMP3 */
	"Moving",   /* OP_MOVETMP4 */
	"Chown",    /* OP_CHOWN */
	"Chgrp",    /* OP_CHGRP */
#ifndef _WIN32
	"Chmod",    /* OP_CHMOD */
	"Chmod",    /* OP_CHMODR */
#else
	"Attr",     /* OP_ADDATTR */
	"Attr",     /* OP_SUBATTR */
#endif
	"Symlink",  /* OP_SYMLINK */
	"Symlink",  /* OP_SYMLINK2 */
	"Mkdir",    /* OP_MKDIR */
	"Rmdir",    /* OP_RMDIR */
	"Mkfile",   /* OP_MKFILE */
};
ARRAY_GUARD(op_descr, OP_COUNT);

ops_t *
ops_alloc(OPS main_op)
{
	ops_t *const ops = calloc(1, sizeof(*ops));
	ops->main_op = main_op;
	return ops;
}

const char *
ops_describe(ops_t *ops)
{
	return op_descr[ops->main_op];
}

void
ops_enqueue(ops_t *ops, const char path[])
{
	++ops->total;

	if(ops->estim != NULL)
	{
		ioeta_calculate(ops->estim, path);
	}
}

void
ops_advance(ops_t *ops, int succeeded)
{
	++ops->current;
	assert(ops->current <= ops->total && "Current and total are out of sync.");

	if(succeeded)
	{
		++ops->succeeded;
	}
}

void
ops_free(ops_t *ops)
{
	if(ops == NULL)
	{
		return;
	}

	ioeta_free(ops->estim);
	free(ops);
}

int
perform_operation(OPS op, ops_t *ops, void *data, const char src[],
		const char dst[])
{
	return op_funcs[op](ops, data, src, dst);
}

static int
op_none(ops_t *ops, void *data, const char *src, const char *dst)
{
	return 0;
}

static int
op_remove(ops_t *ops, void *data, const char *src, const char *dst)
{
	if(cfg.confirm && !curr_stats.confirmed)
	{
		curr_stats.confirmed = query_user_menu("Permanent deletion",
				"Are you sure? If you undoing a command and want to see file names, "
				"use :undolist! command");
		if(!curr_stats.confirmed)
			return SKIP_UNDO_REDO_OPERATION;
	}

	return op_removesl(ops, data, src, dst);
}

static int
op_removesl(ops_t *ops, void *data, const char *src, const char *dst)
{
#ifndef _WIN32
	if(!cfg.use_system_calls)
	{
		char *escaped;
		char cmd[16 + PATH_MAX];
		int result;
		const int cancellable = data == NULL;

		escaped = escape_filename(src, 0);
		if(escaped == NULL)
			return -1;

		snprintf(cmd, sizeof(cmd), "rm -rf %s", escaped);
		LOG_INFO_MSG("Running rm command: \"%s\"", cmd);
		result = background_and_wait_for_errors(cmd, cancellable);

		free(escaped);
		return result;
	}
#endif

	io_args_t args =
	{
		.arg1.path = src,

		.cancellable = data == NULL,
	};
	return exec_io_op(ops, &ior_rm, &args);
}

/* OP_COPY operation handler.  Copies file/directory without overwriting
 * destination files (when it's supported by the system).  Returns non-zero on
 * error, otherwise zero is returned. */
static int
op_copy(ops_t *ops, void *data, const char src[], const char dst[])
{
	return op_cp(ops, data, src, dst, 0);
}

/* OP_COPYF operation handler.  Copies file/directory overwriting destination
 * files.  Returns non-zero on error, otherwise zero is returned. */
static int
op_copyf(ops_t *ops, void *data, const char src[], const char dst[])
{
	return op_cp(ops, data, src, dst, 1);
}

/* Copies file/directory overwriting destination files if requested.  Returns
 * non-zero on error, otherwise zero is returned. */
static int
op_cp(ops_t *ops, void *data, const char src[], const char dst[], int overwrite)
{
	if(!cfg.use_system_calls)
	{
#ifndef _WIN32
		char *escaped_src, *escaped_dst;
		char cmd[6 + PATH_MAX*2 + 1];
		int result;
		const int cancellable = data == NULL;

		escaped_src = escape_filename(src, 0);
		escaped_dst = escape_filename(dst, 0);
		if(escaped_src == NULL || escaped_dst == NULL)
		{
			free(escaped_dst);
			free(escaped_src);
			return -1;
		}

		snprintf(cmd, sizeof(cmd),
				"cp %s -R " PRESERVE_FLAGS " %s %s",
				overwrite ? "" : NO_CLOBBER, escaped_src, escaped_dst);
		LOG_INFO_MSG("Running cp command: \"%s\"", cmd);
		result = background_and_wait_for_errors(cmd, cancellable);

		free(escaped_dst);
		free(escaped_src);
		return result;
#else
		int ret;

		if(is_dir(src))
		{
			char cmd[6 + PATH_MAX*2 + 1];
			snprintf(cmd, sizeof(cmd), "xcopy \"%s\" \"%s\" ", src, dst);
			to_back_slash(cmd);

			if(is_vista_and_above())
				strcat(cmd, "/B ");
			if(overwrite)
			{
				strcat(cmd, "/Y ");
			}
			strcat(cmd, "/E /I /H /R > NUL");
			ret = system(cmd);
		}
		else
		{
			ret = (CopyFileA(src, dst, 0) == 0);
		}

		return ret;
#endif
	}

	io_args_t args =
	{
		.arg1.src = src,
		.arg2.dst = dst,
		.arg3.crs = IO_CRS_REPLACE_FILES,

		.cancellable = data == NULL,
	};
	return exec_io_op(ops, &ior_cp, &args);
}

/* OP_MOVE operation handler.  Moves file/directory without overwriting
 * destination files (when it's supported by the system).  Returns non-zero on
 * error, otherwise zero is returned. */
static int
op_move(ops_t *ops, void *data, const char src[], const char dst[])
{
	return op_mv(ops, data, src, dst, 0);
}

/* OP_MOVEF operation handler.  Moves file/directory overwriting destination
 * files.  Returns non-zero on error, otherwise zero is returned. */
static int
op_movef(ops_t *ops, void *data, const char src[], const char dst[])
{
	return op_mv(ops, data, src, dst, 1);
}

/* Moves file/directory overwriting destination files if requested.  Returns
 * non-zero on error, otherwise zero is returned. */
static int
op_mv(ops_t *ops, void *data, const char src[], const char dst[], int overwrite)
{
#ifndef _WIN32
	if(!cfg.use_system_calls)
	{
		struct stat st;
		char *escaped_src, *escaped_dst;
		char cmd[6 + PATH_MAX*2 + 1];
		int result;
		const int cancellable = data == NULL;

		if(!overwrite && lstat(dst, &st) == 0)
		{
			return -1;
		}

		escaped_src = escape_filename(src, 0);
		escaped_dst = escape_filename(dst, 0);
		if(escaped_src == NULL || escaped_dst == NULL)
		{
			free(escaped_dst);
			free(escaped_src);
			return -1;
		}

		snprintf(cmd, sizeof(cmd), "mv %s %s %s", overwrite ? "" : NO_CLOBBER,
				escaped_src, escaped_dst);
		free(escaped_dst);
		free(escaped_src);

		LOG_INFO_MSG("Running mv command: \"%s\"", cmd);
		if((result = background_and_wait_for_errors(cmd, cancellable)) != 0)
			return result;

		if(is_under_trash(dst))
			add_to_trash(src, dst);
		else if(is_under_trash(src))
			remove_from_trash(src);
		return 0;
	}
#endif

	io_args_t args =
	{
		.arg1.src = src,
		.arg2.dst = dst,
		.arg3.crs = IO_CRS_REPLACE_FILES,

		.cancellable = data == NULL,
	};
	return exec_io_op(ops, &ior_mv, &args);
}

static int
op_chown(ops_t *ops, void *data, const char *src, const char *dst)
{
#ifndef _WIN32
	char cmd[10 + 32 + PATH_MAX];
	char *escaped;
	uid_t uid = (uid_t)(long)data;

	escaped = escape_filename(src, 0);
	snprintf(cmd, sizeof(cmd), "chown -fR %u %s", uid, escaped);
	free(escaped);

	LOG_INFO_MSG("Running chown command: \"%s\"", cmd);
	return background_and_wait_for_errors(cmd, 1);
#else
	return -1;
#endif
}

static int
op_chgrp(ops_t *ops, void *data, const char *src, const char *dst)
{
#ifndef _WIN32
	char cmd[10 + 32 + PATH_MAX];
	char *escaped;
	gid_t gid = (gid_t)(long)data;

	escaped = escape_filename(src, 0);
	snprintf(cmd, sizeof(cmd), "chown -fR :%u %s", gid, escaped);
	free(escaped);

	LOG_INFO_MSG("Running chgrp command: \"%s\"", cmd);
	return background_and_wait_for_errors(cmd, 1);
#else
	return -1;
#endif
}

#ifndef _WIN32
static int
op_chmod(ops_t *ops, void *data, const char *src, const char *dst)
{
	char cmd[128 + PATH_MAX];
	char *escaped;

	escaped = escape_filename(src, 0);
	snprintf(cmd, sizeof(cmd), "chmod %s %s", (char *)data, escaped);
	free(escaped);

	LOG_INFO_MSG("Running chmod command: \"%s\"", cmd);
	return background_and_wait_for_errors(cmd, 1);
}

static int
op_chmodr(ops_t *ops, void *data, const char *src, const char *dst)
{
	char cmd[128 + PATH_MAX];
	char *escaped;

	escaped = escape_filename(src, 0);
	snprintf(cmd, sizeof(cmd), "chmod -R %s %s", (char *)data, escaped);
	free(escaped);
	start_background_job(cmd, 0);
	return 0;
}
#else
static int
op_addattr(ops_t *ops, void *data, const char *src, const char *dst)
{
	const DWORD add_mask = (size_t)data;
	const DWORD attrs = GetFileAttributesA(src);
	if(attrs == INVALID_FILE_ATTRIBUTES)
	{
		LOG_WERROR(GetLastError());
		return -1;
	}
	if(!SetFileAttributesA(src, attrs | add_mask))
	{
		LOG_WERROR(GetLastError());
		return -1;
	}
	return 0;
}

static int
op_subattr(ops_t *ops, void *data, const char *src, const char *dst)
{
	const DWORD sub_mask = (size_t)data;
	const DWORD attrs = GetFileAttributesA(src);
	if(attrs == INVALID_FILE_ATTRIBUTES)
	{
		LOG_WERROR(GetLastError());
		return -1;
	}
	if(!SetFileAttributesA(src, attrs & ~sub_mask))
	{
		LOG_WERROR(GetLastError());
		return -1;
	}
	return 0;
}
#endif

static int
op_symlink(ops_t *ops, void *data, const char *src, const char *dst)
{
#ifndef _WIN32
	if(!cfg.use_system_calls)
	{
		char *escaped_src, *escaped_dst;
		char cmd[6 + PATH_MAX*2 + 1];
		int result;

		escaped_src = escape_filename(src, 0);
		escaped_dst = escape_filename(dst, 0);
		if(escaped_src == NULL || escaped_dst == NULL)
		{
			free(escaped_dst);
			free(escaped_src);
			return -1;
		}

		snprintf(cmd, sizeof(cmd), "ln -s %s %s", escaped_src, escaped_dst);
		LOG_INFO_MSG("Running ln command: \"%s\"", cmd);
		result = background_and_wait_for_errors(cmd, 1);

		free(escaped_dst);
		free(escaped_src);
		return result;
	}
#endif

	io_args_t args =
	{
		.arg1.path = src,
		.arg2.target = dst,
		.arg3.crs = IO_CRS_REPLACE_FILES,
	};
	return exec_io_op(ops, &iop_ln, &args);
}

static int
op_mkdir(ops_t *ops, void *data, const char *src, const char *dst)
{
#ifndef _WIN32
	if(!cfg.use_system_calls)
	{
		char cmd[128 + PATH_MAX];
		char *escaped;

		escaped = escape_filename(src, 0);
		snprintf(cmd, sizeof(cmd), "mkdir %s %s", (data == NULL) ? "" : "-p",
				escaped);
		free(escaped);
		LOG_INFO_MSG("Running mkdir command: \"%s\"", cmd);
		return background_and_wait_for_errors(cmd, 1);
	}
#endif

	io_args_t args =
	{
		.arg1.path = src,
		.arg2.process_parents = data != NULL,
		.arg3.mode = 0755,
	};
	return exec_io_op(ops, &iop_mkdir, &args);
}

static int
op_rmdir(ops_t *ops, void *data, const char *src, const char *dst)
{
#ifndef _WIN32
	if(!cfg.use_system_calls)
	{
		char cmd[128 + PATH_MAX];
		char *escaped;

		escaped = escape_filename(src, 0);
		snprintf(cmd, sizeof(cmd), "rmdir %s", escaped);
		free(escaped);
		LOG_INFO_MSG("Running rmdir command: \"%s\"", cmd);
		return background_and_wait_for_errors(cmd, 1);
	}
#endif

	io_args_t args =
	{
		.arg1.path = src,
	};
	return exec_io_op(ops, &iop_rmdir, &args);
}

static int
op_mkfile(ops_t *ops, void *data, const char *src, const char *dst)
{
#ifndef _WIN32
	if(!cfg.use_system_calls)
	{
		char cmd[128 + PATH_MAX];
		char *escaped;

		escaped = escape_filename(src, 0);
		snprintf(cmd, sizeof(cmd), "touch %s", escaped);
		free(escaped);
		LOG_INFO_MSG("Running touch command: \"%s\"", cmd);
		return background_and_wait_for_errors(cmd, 1);
	}
#endif

	io_args_t args =
	{
		.arg1.path = src,
	};
	return exec_io_op(ops, &iop_mkfile, &args);
}

/* Executes i/o operation with some predefined pre/post actions.  Returns exit
 * code of i/o operation. */
static int
exec_io_op(ops_t *ops, int (*func)(io_args_t *const), io_args_t *const args)
{
	int result;

	args->estim = (ops == NULL) ? NULL : ops->estim;

	if(args->cancellable)
	{
		ui_cancellation_enable();
	}

	result = func(args);

	if(args->cancellable)
	{
		ui_cancellation_disable();
	}

	return result;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 : */
