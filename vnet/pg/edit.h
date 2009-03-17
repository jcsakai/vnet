/*
 * pg_edit.h: packet generator edits
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

#ifndef included_packet_generator_pg_edit_h
#define included_packet_generator_pg_edit_h

typedef enum {
  /* Invalid type used to poison edits. */
  PG_EDIT_INVALID_TYPE,

  /* Value is fixed: does not change for all packets in sequence. */
  PG_EDIT_FIXED,

  /* Value v increments between low and high values v_low <= v <= v_high. */
  PG_EDIT_INCREMENT,

  /* Random value between low and high values v_low <= v <= v_high. */
  PG_EDIT_RANDOM,

  /* Unspecified value; will be specified by some edit function. */
  PG_EDIT_UNSPECIFIED,
} pg_edit_type_t;

typedef struct {
  pg_edit_type_t type;

  /* Bit offset within packet where value is to be written.
     Negative offsets are encode special edits. */
  i32 bit_offset;

  /* Special offset indicating this edit is for packet length. */
#define PG_EDIT_PACKET_LENGTH (-1)

  /* Number of bits in edit. */
  u32 n_bits;

  /* Low and high values for this edit.  Network byte order. */
  u8 * values[2];
#define PG_EDIT_LO 0
#define PG_EDIT_HI 1

  /* Last value used for increment edit type. */
  u64 last_increment_value;
} pg_edit_t;

static inline void
pg_edit_free (pg_edit_t * e)
{
  int i;
  for (i = 0; i < ARRAY_LEN (e->values); i++)
    vec_free (e->values[i]);
}

#define pg_edit_init(e,type,field)				\
do {								\
  (e)->bit_offset = BITS (u8) * STRUCT_OFFSET_OF (type, field);	\
  (e)->n_bits = BITS (u8) * STRUCT_SIZE_OF (type, field);	\
} while (0)

#define pg_edit_init_bitfield(e,type,field,field_offset,field_n_bits)	\
do {									\
  u32 _bo;								\
									\
  /* Starting byte offset. */						\
  _bo = STRUCT_OFFSET_OF (type, field);					\
									\
  /* Big endian network byte position. */				\
  _bo += (STRUCT_SIZE_OF (type, field)					\
	  - 1 - ((field_n_bits) + (field_offset) - 1) / BITS (u8));	\
									\
  /* Add in bit position within byte. */				\
  _bo = BITS (u8) * _bo + ((field_offset) % BITS (u8)); 		\
									\
  (e)->bit_offset = _bo;						\
  (e)->n_bits = (field_n_bits);						\
} while (0)

static inline void
pg_edit_alloc_value (pg_edit_t * e, int i)
{
  int i0, i1, n, n_bits_left;

  ASSERT (i < ARRAY_LEN (e->values));

  i0 = e->bit_offset;
  i1 = i0 + e->n_bits;

  n = 0;
  n_bits_left = e->n_bits;
  if (n_bits_left > 0 && (i0 % BITS (u8)))
    {
      n++;
      n_bits_left -= i0 % BITS (u8);
    }

  if (n_bits_left > 0)
    {
      n += n_bits_left / BITS (u8);
      n += (n_bits_left % BITS (u8)) != 0;
    }

  /* Make sure we have enough room for an int. */
  if (n < sizeof (int))
    n = sizeof (int);

  vec_validate (e->values[i], n - 1);
}

extern void pg_edit_set_value (pg_edit_t * e, int hi_or_lo, u64 value);

static inline void
pg_edit_set_fixed (pg_edit_t * e, u64 value)
{
  e->type = PG_EDIT_FIXED;
  pg_edit_set_value (e, PG_EDIT_LO, value);
}

static inline void
pg_edit_copy_type_and_values (pg_edit_t * dst, pg_edit_t * src)
{
  int i;
  dst->type = src->type;
  src->type = PG_EDIT_INVALID_TYPE;
  for (i = 0; i < ARRAY_LEN (dst->values); i++)
    {
      dst->values[i] = src->values[i];
      src->values[i] = 0;
    }
}

static inline u64
pg_edit_get_value (pg_edit_t * e, int hi_or_lo)
{
  u64 r = 0;
  int i, n;
  u8 * v = e->values[hi_or_lo];

  n = round_pow2 (e->n_bits, BITS (u8)) / BITS (u8);

  ASSERT (n <= vec_len (v));
  ASSERT (n <= sizeof (r));

  for (i = 0; i < n; i++)
    r = (r << BITS (v[i])) + v[i];

  return r;
}

static inline uword
pg_edit_is_fixed_with_value (pg_edit_t * e, u64 value)
{
  return (e->type == PG_EDIT_FIXED
	  && value == pg_edit_get_value (e, PG_EDIT_LO));
}

uword unformat_pg_edit (unformat_input_t * input, va_list * args);
uword unformat_pg_payload (unformat_input_t * input, va_list * args);
uword unformat_pg_number (unformat_input_t * input, va_list * args);
uword unformat_pg_interface (unformat_input_t * input, va_list * args);

#endif /* included_packet_generator_pg_edit_h */
