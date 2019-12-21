/* BZ #22679 getcwd(3) should not succeed without returning an absolute path.

   Copyright (C) 2018 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <https://www.gnu.org/licenses/>.  */

#include <errno.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static int do_test (void);
#define TEST_FUNCTION do_test ()

#include "../test-skeleton.c"

static char *chroot_dir;

static void
become_root (void)
{
#ifdef CLONE_NEWUSER
  if (unshare (CLONE_NEWUSER | CLONE_NEWNS) == 0)
    /* Even if we do not have UID zero, we have extended privileges at
       this point.  */
    return;
#endif
  if (setuid (0) != 0)
    printf ("warning: could not become root outside namespace (%m)\n");
}

static void
isolate_in_subprocess (void (*callback) (void *), void *closure)
{
  pid_t pid = fork ();
  if (pid < 0)
    {
      printf ("fork failed: %m");
      exit (1);
    }

  if (pid == 0)
    {
      /* Child process.  */
      callback (closure);
      _exit (0);
    }

  /* Parent process.  */
  int status;
  if (waitpid (pid, &status, 0) < 0)
    {
      printf ("waitpid failed: %m");
      exit (1);
    }
  if (status != 0)
    {
      printf ("child process exited with status %d", status);
      exit (1);
    }
}

static void
can_chroot_callback (void *closure)
{
  int *result = closure;
  if (chroot ("/dev") != 0)
    {
      *result = errno;
      return;
    }
  *result = 0;
}

static bool
can_chroot (void)
{
  int *result = mmap (NULL, sizeof(*result), PROT_READ | PROT_WRITE,
                      MAP_ANONYMOUS | MAP_SHARED, -1, 0);
  if (result == MAP_FAILED)
    {
      printf ("mmap of %zu bytes failed: %m", sizeof(*result));
      exit (1);
    }
  *result = 0;
  isolate_in_subprocess (can_chroot_callback, result);
  bool ok = *result == 0;
  if (!ok)
    {
      errno = *result;
      printf ("warning: this process does not support chroot: %m\n");
    }
  if (munmap (result, sizeof(*result)) != 0)
    {
      printf ("munmap of %zu bytes failed: %m", sizeof(*result));
      exit (1);
    }
  return ok;
}

/* The actual test.  Run it in a subprocess, so that the test harness
   can remove the temporary directory in --direct mode.  */
static void
getcwd_callback (void *closure)
{
  if (chroot (chroot_dir) != 0)
    {
      printf ("chroot (\"%s\") failed: %m", chroot_dir);
      _exit (1);
    }

  errno = 0;
  char *cwd = getcwd (NULL, 0);
  if (errno != ENOENT)
    {
      puts ("unexpected errno for getcwd");
      _exit (1);
    }
  if (cwd != NULL)
    {
      puts ("getcwd succeeded");
      _exit (1);
    }

  errno = 0;
  cwd = realpath (".", NULL);
  if (errno != ENOENT)
    {
      puts ("unexpected errno for realpath");
      _exit (1);
    }
  if (cwd != NULL)
    {
      _exit (1);
      puts ("realpath succeeded");
    }

  _exit (0);
}

static int
do_test (void)
{
  become_root ();
  if (!can_chroot ())
    return 0;

  if (asprintf(&chroot_dir, "%s/tst-getcwd-abspath-XXXXXX", test_dir) < 0)
    {
      printf ("asprintf failed: %m");
      exit (1);
    }
  if (mkdtemp (chroot_dir) == NULL)
    {
      printf ("error: mkdtemp (\"%s\"): %m", chroot_dir);
      exit (1);
    }
  add_temp_file (chroot_dir);

  isolate_in_subprocess (getcwd_callback, NULL);

  return 0;
}
