/* vifm
 * Copyright (C) 2001 Ken Steen.
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

#include "signals.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <curses.h>

#include <errno.h> /* errno */
#include <signal.h>
#include <stdio.h> /* fprintf() */
#include <string.h> /* strsignal() */

#include "cfg/info.h"
#include "int/fuse.h"
#include "int/term_title.h"
#include "ui/cancellation.h"
#include "ui/ui.h"
#include "utils/log.h"
#include "utils/macros.h"

static void _gnuc_noreturn shutdown_nicely(int sig, const char descr[]);

#ifndef _WIN32

#include <sys/types.h> /* pid_t */
#include <sys/wait.h> /* WEXITSTATUS() WIFEXITED() waitpid() */

#include <stdlib.h> /* EXIT_FAILURE _Exit() */

#include "utils/macros.h"
#include "background.h"
#include "status.h"

/* Handle term resizing in X */
static void
received_sigwinch(void)
{
	if(curr_stats.save_msg != 2)
		curr_stats.save_msg = 0;

	if(!isendwin())
	{
		stats_redraw_schedule();
	}
	else
	{
		curr_stats.need_update = UT_FULL;
	}
}

static void
received_sigcont(void)
{
	reset_prog_mode();
	stats_redraw_schedule();
}

static void
received_sigchld(void)
{
	int status;
	pid_t pid;

	/* This needs to be a loop in case of multiple blocked signals. */
	while((pid = waitpid(-1, &status, WNOHANG)) > 0)
	{
		if(WIFEXITED(status))
		{
			bg_process_finished_cb(pid, WEXITSTATUS(status));
		}
		else if(WIFSIGNALED(status))
		{
			/* Child was terminated by a signal. */
			bg_process_finished_cb(pid, -1);
		}
	}
}

static void
handle_signal(int sig)
{
	/* Try to not change errno value in the main program. */
	const int saved_errno = errno;

	switch(sig)
	{
		case SIGINT:
			ui_cancellation_request();
			break;
		case SIGCHLD:
			received_sigchld();
			break;
		case SIGWINCH:
			received_sigwinch();
			break;
		case SIGCONT:
			received_sigcont();
			break;
		/* Shutdown nicely */
		case SIGHUP:
		case SIGQUIT:
		case SIGTERM:
			shutdown_nicely(sig, strsignal(sig));
			break;
	}

	errno = saved_errno;
}

#else
BOOL WINAPI
ctrl_handler(DWORD dwCtrlType)
{
  LOG_FUNC_ENTER;

	switch(dwCtrlType)
	{
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
			ui_cancellation_request();
			break;
		case CTRL_CLOSE_EVENT:
			shutdown_nicely(dwCtrlType, "Ctrl-C");
			break;
		case CTRL_LOGOFF_EVENT:
			shutdown_nicely(dwCtrlType, "Logoff");
			break;
		case CTRL_SHUTDOWN_EVENT:
			shutdown_nicely(dwCtrlType, "Shutdown");
			break;
	}

	return TRUE;
}
#endif

static void _gnuc_noreturn
shutdown_nicely(int sig, const char descr[])
{
  LOG_FUNC_ENTER;

	ui_shutdown();
	term_title_update(NULL);
	fuse_unmount_all();
	write_info_file();
	fprintf(stdout, "Vifm killed by signal: %d (%s).\n", sig, descr);

	/* Alternatively we could do this sequence:
	 *     signal(sig, SIG_DFL);
	 *     raise(sig);
	 * but only on *nix systems. */
	_Exit(EXIT_FAILURE);
}

void
setup_signals(void)
{
  LOG_FUNC_ENTER;

#ifndef _WIN32
	struct sigaction handle_signal_action;

	handle_signal_action.sa_handler = &handle_signal;
	sigemptyset(&handle_signal_action.sa_mask);
	handle_signal_action.sa_flags = SA_RESTART;

	/* Assumption: we work under shell with job control support.  If it's not the
	 * case, this code enables handling of terminal related signals the shell
	 * wanted us to have disabled (e.g. the app will catch Ctrl-C send to another
	 * process). */

	sigaction(SIGCHLD, &handle_signal_action, NULL);
	sigaction(SIGHUP, &handle_signal_action, NULL);
	sigaction(SIGINT, &handle_signal_action, NULL);
	sigaction(SIGQUIT, &handle_signal_action, NULL);
	sigaction(SIGCONT, &handle_signal_action, NULL);
	sigaction(SIGTERM, &handle_signal_action, NULL);
	sigaction(SIGWINCH, &handle_signal_action, NULL);
	signal(SIGUSR1, SIG_IGN);
	signal(SIGUSR2, SIG_IGN);
	signal(SIGALRM, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
#else
	if(!SetConsoleCtrlHandler(ctrl_handler, TRUE))
	{
		LOG_WERROR(GetLastError());
	}
	signal(SIGINT, SIG_IGN);
#endif
}

void
sigint_a(void)
{
	return;
	struct sigaction handle_signal_action;

	handle_signal_action.sa_handler = &handle_signal;
	sigemptyset(&handle_signal_action.sa_mask);
	handle_signal_action.sa_flags = 0;

	sigaction(SIGINT, &handle_signal_action, NULL);
}

void
sigint_b(void)
{
	return;
	struct sigaction handle_signal_action;

	handle_signal_action.sa_handler = &handle_signal;
	sigemptyset(&handle_signal_action.sa_mask);
	handle_signal_action.sa_flags = SA_RESTART;

	sigaction(SIGINT, &handle_signal_action, NULL);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
