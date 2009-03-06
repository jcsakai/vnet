/*
 * pg_init.c: VLIB packet generator
 *
 * Copyright (c) 2008 Eliot Dresselhaus
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 *  LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 *  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <vlib/vlib.h>
#include <pg/pg.h>

/* Global main structure. */
pg_main_t pg_main;

static clib_error_t * pg_init (vlib_main_t * vm)
{
  clib_error_t * error;
  pg_main_t * pg = &pg_main;
  u32 i;

  pg->vlib_main = vm;

  if ((error = vlib_call_init_function (vm, vlib_interface_init)))
    goto done;

  /* Create/free first interface so that it exists and can be
     used as a destination interface for streams. */
  i = pg_interface_find_free (pg);
  vec_add1 (pg->free_interfaces, i);

 done:
  return error;
}

VLIB_INIT_FUNCTION (pg_init);
