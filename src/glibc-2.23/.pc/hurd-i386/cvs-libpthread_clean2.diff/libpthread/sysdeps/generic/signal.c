/* signal.c - Generic signal implementation.
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

void (*signal (int sig, void (*handler)(int)))(int)
{
  struct sigaction sa;

  sa.sa_handler = handler;
  sa.sa_flags = SA_RESTART;

  if (sigemptyset (&sa.sa_mask) < 0
      || sigaddset (&sa.sa_mask, sig) < 0)
    return SIG_ERR;

  struct sigaction osa;
  if (sigaction (sig, &sa, &osa) < 0)
    return SIG_ERR;

  return osa.sa_handler;
}

void (*bsd_signal (int sig, void (*func)(int)))(int)
{
  return signal (sig, func);
}
