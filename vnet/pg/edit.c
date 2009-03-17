/*
 * pg_edit.c: packet generator edits
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
#include <vnet/pg/pg.h>

static void
pg_edit_set_value_helper (pg_edit_t * e, u64 value, u8 * result)
{
  int i, j, n_bits_left;
  u8 * v, tmp[8];

  v = tmp;

  n_bits_left = e->n_bits;
  i = 0;
  j = e->bit_offset % BITS (v[0]);

  if (n_bits_left > 0 && j != 0)
    {
      v[i] = (value & 0xff) << j;
      value >>= j;
      n_bits_left -= j;
      i += 1;
    }

  while (n_bits_left > 0)
    {
      v[i] = value & 0xff;
      value >>= 8;
      n_bits_left -= 8;
      i += 1;
    }

  /* Convert to network byte order. */
  for (j = 0; j < i; j++)
    result[j] = tmp[i - 1 - j];
}

void
pg_edit_set_value (pg_edit_t * e, int hi_or_lo, u64 value)
{
  pg_edit_alloc_value (e, hi_or_lo);
  pg_edit_set_value_helper (e, value, e->values[hi_or_lo]);
}

/* Parse an int either %d or 0x%x into network byte order. */
uword unformat_pg_number (unformat_input_t * input, va_list * args)
{
  u8 * result = va_arg (*args, u8 *);
  pg_edit_t * e = va_arg (*args, pg_edit_t *);
  u64 value;

  ASSERT (BITS (value) >= e->n_bits);

  if (! unformat (input, "0x%X", sizeof (value), &value)
      && ! unformat (input, "%D", sizeof (value), &value))
    return 0;

  /* Number given does not fit into bit field. */
  if (e->n_bits < 64
      && value >= (u64) 1 << (u64) e->n_bits)
    return 0;

  pg_edit_set_value_helper (e, value, result);
  return 1;
}

uword
unformat_pg_edit (unformat_input_t * input, va_list * args)
{
  unformat_function_t * f = va_arg (*args, unformat_function_t *);
  pg_edit_t * e = va_arg (*args, pg_edit_t *);

  pg_edit_alloc_value (e, PG_EDIT_LO);
  if (! unformat_user (input, f, e->values[PG_EDIT_LO], e))
    return 0;

  pg_edit_alloc_value (e, PG_EDIT_HI);
  if (unformat (input, "-%U", f, e->values[PG_EDIT_HI], e))
    e->type = PG_EDIT_INCREMENT;
  else if (unformat (input, "+%U", f, e->values[PG_EDIT_HI], e))
    e->type = PG_EDIT_RANDOM;
  else
    e->type = PG_EDIT_FIXED;

  return 1;
}

uword
unformat_pg_payload (unformat_input_t * input, va_list * args)
{
  pg_stream_t * s = va_arg (*args, pg_stream_t *);
  pg_edit_t * e;
  u32 i, len;
  u8 * v;
  
  v = 0;
  if (unformat (input, "fixed %d", &len))
    {
      vec_resize (v, len);
      for (i = 0; i < len; i++)
	v[i] = i;
    }
  else if (unformat (input, "0x%U", unformat_hex_string, &v))
    ;

  else
    return 0;

  e = pg_create_edit_group (s, sizeof (e[0]), 0);

  e->type = PG_EDIT_FIXED;
  e->bit_offset = 0;
  e->n_bits = vec_len (v) * BITS (v[0]);
  e->values[PG_EDIT_LO] = v;

  return 1;
}
