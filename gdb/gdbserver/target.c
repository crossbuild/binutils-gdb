/* Target operations for the remote server for GDB.
   Copyright (C) 2002-2015 Free Software Foundation, Inc.

   Contributed by MontaVista Software.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include "server.h"
#include "tracepoint.h"

struct target_ops *the_target;

int
set_desired_thread (int use_general)
{
  struct thread_info *found;

  if (use_general == 1)
    found = find_thread_ptid (general_thread);
  else
    found = find_thread_ptid (cont_thread);

  current_thread = found;
  return (current_thread != NULL);
}

int
read_inferior_memory (CORE_ADDR memaddr, unsigned char *myaddr, int len)
{
  int res;
  res = (*the_target->read_memory) (memaddr, myaddr, len);
  check_mem_read (memaddr, myaddr, len);
  return res;
}

/* See target/target.h.  */

int
target_read_memory (CORE_ADDR memaddr, gdb_byte *myaddr, ssize_t len)
{
  return read_inferior_memory (memaddr, myaddr, len);
}

/* See target/target.h.  */

int
target_read_uint32 (CORE_ADDR memaddr, uint32_t *result)
{
  return read_inferior_memory (memaddr, (gdb_byte *) result, sizeof (*result));
}

int
write_inferior_memory (CORE_ADDR memaddr, const unsigned char *myaddr,
		       int len)
{
  /* Lacking cleanups, there is some potential for a memory leak if the
     write fails and we go through error().  Make sure that no more than
     one buffer is ever pending by making BUFFER static.  */
  static unsigned char *buffer = 0;
  int res;

  if (buffer != NULL)
    free (buffer);

  buffer = (unsigned char *) xmalloc (len);
  memcpy (buffer, myaddr, len);
  check_mem_write (memaddr, buffer, myaddr, len);
  res = (*the_target->write_memory) (memaddr, buffer, len);
  free (buffer);
  buffer = NULL;

  return res;
}

/* See target/target.h.  */

int
target_write_memory (CORE_ADDR memaddr, const gdb_byte *myaddr, ssize_t len)
{
  return write_inferior_memory (memaddr, myaddr, len);
}

ptid_t
mywait (ptid_t ptid, struct target_waitstatus *ourstatus, int options,
	int connected_wait)
{
  ptid_t ret;

  if (connected_wait)
    server_waiting = 1;

  ret = (*the_target->wait) (ptid, ourstatus, options);

  /* We don't expose _LOADED events to gdbserver core.  See the
     `dlls_changed' global.  */
  if (ourstatus->kind == TARGET_WAITKIND_LOADED)
    ourstatus->kind = TARGET_WAITKIND_STOPPED;

  /* If GDB is connected through TCP/serial, then GDBserver will most
     probably be running on its own terminal/console, so it's nice to
     print there why is GDBserver exiting.  If however, GDB is
     connected through stdio, then there's no need to spam the GDB
     console with this -- the user will already see the exit through
     regular GDB output, in that same terminal.  */
  if (!remote_connection_is_stdio ())
    {
      if (ourstatus->kind == TARGET_WAITKIND_EXITED)
	fprintf (stderr,
		 "\nChild exited with status %d\n", ourstatus->value.integer);
      else if (ourstatus->kind == TARGET_WAITKIND_SIGNALLED)
	fprintf (stderr, "\nChild terminated with signal = 0x%x (%s)\n",
		 gdb_signal_to_host (ourstatus->value.sig),
		 gdb_signal_to_name (ourstatus->value.sig));
    }

  if (connected_wait)
    server_waiting = 0;

  return ret;
}

/* See target/target.h.  */

void
target_stop_and_wait (ptid_t ptid)
{
  struct target_waitstatus status;
  int was_non_stop = non_stop;
  struct thread_resume resume_info;

  resume_info.thread = ptid;
  resume_info.kind = resume_stop;
  resume_info.sig = GDB_SIGNAL_0;
  (*the_target->resume) (&resume_info, 1);

  non_stop = 1;
  mywait (ptid, &status, 0, 0);
  non_stop = was_non_stop;
}

/* See target/target.h.  */

void
target_continue_no_signal (ptid_t ptid)
{
  struct thread_resume resume_info;

  resume_info.thread = ptid;
  resume_info.kind = resume_continue;
  resume_info.sig = GDB_SIGNAL_0;
  (*the_target->resume) (&resume_info, 1);
}

int
start_non_stop (int nonstop)
{
  if (the_target->start_non_stop == NULL)
    {
      if (nonstop)
	return -1;
      else
	return 0;
    }

  return (*the_target->start_non_stop) (nonstop);
}

void
set_target_ops (struct target_ops *target)
{
  the_target = XNEW (struct target_ops);
  memcpy (the_target, target, sizeof (*the_target));
}

/* Convert pid to printable format.  */

const char *
target_pid_to_str (ptid_t ptid)
{
  static char buf[80];

  if (ptid_equal (ptid, minus_one_ptid))
    xsnprintf (buf, sizeof (buf), "<all threads>");
  else if (ptid_equal (ptid, null_ptid))
    xsnprintf (buf, sizeof (buf), "<null thread>");
  else if (ptid_get_tid (ptid) != 0)
    xsnprintf (buf, sizeof (buf), "Thread %d.0x%lx",
	       ptid_get_pid (ptid), ptid_get_tid (ptid));
  else if (ptid_get_lwp (ptid) != 0)
    xsnprintf (buf, sizeof (buf), "LWP %d.%ld",
	       ptid_get_pid (ptid), ptid_get_lwp (ptid));
  else
    xsnprintf (buf, sizeof (buf), "Process %d",
	       ptid_get_pid (ptid));

  return buf;
}

int
kill_inferior (int pid)
{
  gdb_agent_about_to_close (pid);

  return (*the_target->kill) (pid);
}

/* Target can do hardware single step.  */

int
target_can_do_hardware_single_step (void)
{
  return 1;
}
