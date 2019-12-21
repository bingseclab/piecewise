/* siginterrupt.c - Generic siginterrupt implementation.
   Copyright (C) 2008 Free Software Foundation, Inc.
   Written by Neal H. Walfield <neal@gnu.org>.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public License
   as published by the Free Software Foundation; either version 3 of
   the License, or (at your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this program.  If not, see
   <http://www.gnu.org/licenses/>.  */

#include "sig-internal.h"

int
siginterrupt (int sig, int flag)
{
  int ret;
  struct sigaction act;

  sigaction (sig, NULL, &act);
  if (flag)
    act.sa_flags &= ~SA_RESTART;
  else
    act.sa_flags |= SA_RESTART;
  ret = sigaction(sig, &act, NULL);
  return ret;
}
