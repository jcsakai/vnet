/*
 * pg_input.c: buffer generator input
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

static int
validate_buffer_data (vlib_buffer_t * b, pg_stream_t * s)
{
  u8 * bd, * bd_copy = 0, * pd, * pm;
  u32 i, n_bytes;

  bd = b->data;
  pd = s->buffer_data;
  pm = s->buffer_data_mask;
  n_bytes = vec_len (pd);

  for (i = 0; i < n_bytes; i++)
    if ((bd[i] & pm[i]) != pd[i])
      break;

  if (i >= n_bytes)
    return 1;

  bd_copy = 0;
  vec_add (bd_copy, bd, n_bytes);

  clib_warning ("differ at index %d", i);
  clib_warning ("is     %U", format_hex_bytes, bd, n_bytes);
  clib_warning ("expect %U", format_hex_bytes, pd, n_bytes);
  ASSERT (0);

  return 0;
}

static always_inline void
set_1 (void * a0,
       u64 v0,
       u64 v_min, u64 v_max,
       u32 n_bits,
       u32 is_net_byte_order)
{
  ASSERT (v0 >= v_min && v0 <= v_max);
  if (n_bits == BITS (u8))
    {
      ((u8 *) a0)[0] = v0;
    }
  else if (n_bits == BITS (u16))
    {
      if (is_net_byte_order)
	v0 = clib_host_to_net_u16 (v0);
      clib_mem_unaligned (a0, u16) = v0;
    }
  else if (n_bits == BITS (u32))
    {
      if (is_net_byte_order)
	v0 = clib_host_to_net_u32 (v0);
      clib_mem_unaligned (a0, u32) = v0;
    }
  else if (n_bits == BITS (u64))
    {
      if (is_net_byte_order)
	v0 = clib_host_to_net_u64 (v0);
      clib_mem_unaligned (a0, u64) = v0;
    }
}

static always_inline void
set_2 (void * a0, void * a1,
       u64 v0, u64 v1,
       u64 v_min, u64 v_max,
       u32 n_bits,
       u32 is_net_byte_order,
       u32 is_increment)
{
  ASSERT (v0 >= v_min && v0 <= v_max);
  ASSERT (v1 >= v_min && v1 <= (v_max + is_increment));
  if (n_bits == BITS (u8))
    {
      ((u8 *) a0)[0] = v0;
      ((u8 *) a1)[0] = v1;
    }
  else if (n_bits == BITS (u16))
    {
      if (is_net_byte_order)
	{
	  v0 = clib_host_to_net_u16 (v0);
	  v1 = clib_host_to_net_u16 (v1);
	}
      clib_mem_unaligned (a0, u16) = v0;
      clib_mem_unaligned (a1, u16) = v1;
    }
  else if (n_bits == BITS (u32))
    {
      if (is_net_byte_order)
	{
	  v0 = clib_host_to_net_u32 (v0);
	  v1 = clib_host_to_net_u32 (v1);
	}
      clib_mem_unaligned (a0, u32) = v0;
      clib_mem_unaligned (a1, u32) = v1;
    }
  else if (n_bits == BITS (u64))
    {
      if (is_net_byte_order)
	{
	  v0 = clib_host_to_net_u64 (v0);
	  v1 = clib_host_to_net_u64 (v1);
	}
      clib_mem_unaligned (a0, u64) = v0;
      clib_mem_unaligned (a1, u64) = v1;
    }
}

static always_inline void
do_set_fixed (pg_main_t * pg,
	      pg_stream_t * s,
	      u32 * buffers,
	      u32 n_buffers,
	      u32 n_bits,
	      u32 byte_offset,
	      u32 is_net_byte_order,
	      u64 v_min, u64 v_max)

{
  vlib_main_t * vm = pg->vlib_main;

  while (n_buffers >= 4)
    {
      vlib_buffer_t * b0, * b1, * b2, * b3;
      void * a0, * a1;

      b0 = vlib_get_buffer (vm, buffers[0]);
      b1 = vlib_get_buffer (vm, buffers[1]);
      b2 = vlib_get_buffer (vm, buffers[2]);
      b3 = vlib_get_buffer (vm, buffers[3]);
      buffers += 2;
      n_buffers -= 2;

      a0 = (void *) b0 + byte_offset;
      a1 = (void *) b1 + byte_offset;
      CLIB_PREFETCH ((void *) b2 + byte_offset, sizeof (v_min), WRITE);
      CLIB_PREFETCH ((void *) b3 + byte_offset, sizeof (v_min), WRITE);

      set_2 (a0, a1, v_min, v_min,
	     v_min, v_max,
	     n_bits, is_net_byte_order,
	     /* is_increment */ 0);

      ASSERT (validate_buffer_data (b0, s));
      ASSERT (validate_buffer_data (b1, s));
    }

  while (n_buffers > 0)
    {
      vlib_buffer_t * b0;
      void * a0;

      b0 = vlib_get_buffer (vm, buffers[0]);
      buffers += 1;
      n_buffers -= 1;

      a0 = (void *) b0 + byte_offset;

      set_1 (a0, v_min,
	     v_min, v_max,
	     n_bits, is_net_byte_order);

      ASSERT (validate_buffer_data (b0, s));
    }
}

static always_inline u64
do_set_increment (pg_main_t * pg,
		  pg_stream_t * s,
		  u32 * buffers,
		  u32 n_buffers,
		  u32 n_bits,
		  u32 byte_offset,
		  u32 is_net_byte_order,
		  u32 want_sum,
		  u64 * sum_result,
		  u64 v_min, u64 v_max,
		  u64 v)
{
  vlib_main_t * vm = pg->vlib_main;
  u64 sum = 0;

  ASSERT (v >= v_min && v <= v_max);

  while (n_buffers >= 4)
    {
      vlib_buffer_t * b0, * b1, * b2, * b3;
      void * a0, * a1;
      u64 v_old;

      b0 = vlib_get_buffer (vm, buffers[0]);
      b1 = vlib_get_buffer (vm, buffers[1]);
      b2 = vlib_get_buffer (vm, buffers[2]);
      b3 = vlib_get_buffer (vm, buffers[3]);
      buffers += 2;
      n_buffers -= 2;

      a0 = (void *) b0 + byte_offset;
      a1 = (void *) b1 + byte_offset;
      CLIB_PREFETCH ((void *) b2 + byte_offset, sizeof (v_min), WRITE);
      CLIB_PREFETCH ((void *) b3 + byte_offset, sizeof (v_min), WRITE);

      v_old = v;
      v = v_old + 2;
      v = v > v_max ? v_min : v;
      set_2 (a0, a1,
	     v_old + 0, v_old + 1,
	     v_min, v_max,
	     n_bits, is_net_byte_order,
	     /* is_increment */ 1);

      if (want_sum)
	sum += 2*v_old + 1;

      if (PREDICT_FALSE (v_old + 1 > v_max))
	{
	  if (want_sum)
	    sum -= 2*v_old + 1;

	  v = v_old;
	  set_1 (a0, v + 0, v_min, v_max, n_bits, is_net_byte_order);
	  if (want_sum)
	    sum += v;
	  v += 1;

	  v = v > v_max ? v_min : v;
	  set_1 (a1, v + 0, v_min, v_max, n_bits, is_net_byte_order);
	  if (want_sum)
	    sum += v;
	  v += 1;
	}

      ASSERT (validate_buffer_data (b0, s));
      ASSERT (validate_buffer_data (b1, s));
    }

  while (n_buffers > 0)
    {
      vlib_buffer_t * b0;
      void * a0;
      u64 v_old;

      b0 = vlib_get_buffer (vm, buffers[0]);
      buffers += 1;
      n_buffers -= 1;

      a0 = (void *) b0 + byte_offset;

      v_old = v;
      if (want_sum)
	sum += v_old;
      v += 1;
      v = v > v_max ? v_min : v;

      ASSERT (v_old >= v_min && v_old <= v_max);
      set_1 (a0, v_old, v_min, v_max, n_bits, is_net_byte_order);

      ASSERT (validate_buffer_data (b0, s));
    }

  if (want_sum)
    *sum_result = sum;

  return v;
}

static always_inline void
do_set_random (pg_main_t * pg,
	       pg_stream_t * s,
	       u32 * buffers,
	       u32 n_buffers,
	       u32 n_bits,
	       u32 byte_offset,
	       u32 is_net_byte_order,
	       u32 want_sum,
	       u64 * sum_result,
	       u64 v_min, u64 v_max)

{
  vlib_main_t * vm = pg->vlib_main;
  u64 v_diff = v_max - v_min + 1;
  u64 r_mask = max_pow2 (v_diff) - 1;
  u64 v0, v1;
  u64 sum = 0;
  void * random_data;

  random_data = clib_random_buffer_get_data
    (&vm->random_buffer, n_buffers * n_bits / BITS (u8));

  v0 = v1 = v_min;

  while (n_buffers >= 4)
    {
      vlib_buffer_t * b0, * b1, * b2, * b3;
      void * a0, * a1;
      u64 r0, r1;

      b0 = vlib_get_buffer (vm, buffers[0]);
      b1 = vlib_get_buffer (vm, buffers[1]);
      b2 = vlib_get_buffer (vm, buffers[2]);
      b3 = vlib_get_buffer (vm, buffers[3]);
      buffers += 2;
      n_buffers -= 2;

      a0 = (void *) b0 + byte_offset;
      a1 = (void *) b1 + byte_offset;
      CLIB_PREFETCH ((void *) b2 + byte_offset, sizeof (v_min), WRITE);
      CLIB_PREFETCH ((void *) b3 + byte_offset, sizeof (v_min), WRITE);

      switch (n_bits)
	{
#define _(n)					\
	  case BITS (u##n):			\
	    {					\
	      u##n * r = random_data;		\
	      r0 = r[0];			\
	      r1 = r[1];			\
	      random_data = r + 2;		\
	    }					\
	  break;

	  _ (8);
	  _ (16);
	  _ (32);
	  _ (64);

#undef _
	}

      /* Add power of 2 sized random number which may be out of range. */
      v0 += r0 & r_mask;
      v1 += r1 & r_mask;

      /* Twice should be enough to reduce to v_min .. v_max range. */
      v0 = v0 > v_max ? v0 - v_diff : v0;
      v1 = v1 > v_max ? v1 - v_diff : v1;
      v0 = v0 > v_max ? v0 - v_diff : v0;
      v1 = v1 > v_max ? v1 - v_diff : v1;

      if (want_sum)
	sum += v0 + v1;

      set_2 (a0, a1,
	     v0, v1,
	     v_min, v_max,
	     n_bits, is_net_byte_order,
	     /* is_increment */ 0);

      ASSERT (validate_buffer_data (b0, s));
      ASSERT (validate_buffer_data (b1, s));
    }

  while (n_buffers > 0)
    {
      vlib_buffer_t * b0;
      void * a0;
      u64 r0;

      b0 = vlib_get_buffer (vm, buffers[0]);
      buffers += 1;
      n_buffers -= 1;

      a0 = (void *) b0 + byte_offset;

      switch (n_bits)
	{
#define _(n)					\
	  case BITS (u##n):			\
	    {					\
	      u##n * r = random_data;		\
	      r0 = r[0];			\
	      random_data = r + 1;		\
	    }					\
	  break;

	  _ (8);
	  _ (16);
	  _ (32);
	  _ (64);

#undef _
	}

      /* Add power of 2 sized random number which may be out of range. */
      v0 += r0 & r_mask;

      /* Twice should be enough to reduce to v_min .. v_max range. */
      v0 = v0 > v_max ? v0 - v_diff : v0;
      v0 = v0 > v_max ? v0 - v_diff : v0;

      if (want_sum)
	sum += v0;

      set_1 (a0, v0, v_min, v_max, n_bits, is_net_byte_order);

      ASSERT (validate_buffer_data (b0, s));
    }

  if (want_sum)
    *sum_result = sum;
}

#define _(i,t)							\
  clib_mem_unaligned (a##i, t) =				\
    clib_host_to_net_##t ((clib_net_to_host_mem_##t (a##i) &~ mask)	\
			  | (v##i << shift))
  
static always_inline void
setbits_1 (void * a0,
	   u64 v0,
	   u64 v_min, u64 v_max,
	   u32 max_bits,
	   u32 n_bits,
	   u64 mask,
	   u32 shift)
{
  ASSERT (v0 >= v_min && v0 <= v_max);
  if (max_bits == BITS (u8))
    ((u8 *) a0)[0] = (((u8 *) a0)[0] &~ mask) | (v0 << shift);

  else if (max_bits == BITS (u16))
    {
      _ (0, u16);
    }
  else if (max_bits == BITS (u32))
    {
      _ (0, u32);
    }
  else if (max_bits == BITS (u64))
    {
      _ (0, u64);
    }
}

static always_inline void
setbits_2 (void * a0, void * a1,
	   u64 v0, u64 v1,
	   u64 v_min, u64 v_max,
	   u32 max_bits,
	   u32 n_bits,
	   u64 mask,
	   u32 shift,
	   u32 is_increment)
{
  ASSERT (v0 >= v_min && v0 <= v_max);
  ASSERT (v1 >= v_min && v1 <= v_max + is_increment);
  if (max_bits == BITS (u8))
    {
      ((u8 *) a0)[0] = (((u8 *) a0)[0] &~ mask) | (v0 << shift);
      ((u8 *) a1)[0] = (((u8 *) a1)[0] &~ mask) | (v1 << shift);
    }

  else if (max_bits == BITS (u16))
    {
      _ (0, u16);
      _ (1, u16);
    }
  else if (max_bits == BITS (u32))
    {
      _ (0, u32);
      _ (1, u32);
    }
  else if (max_bits == BITS (u64))
    {
      _ (0, u64);
      _ (1, u64);
    }
}

#undef _

static always_inline void
do_setbits_fixed (pg_main_t * pg,
		  pg_stream_t * s,
		  u32 * buffers,
		  u32 n_buffers,
		  u32 max_bits,
		  u32 n_bits,
		  u32 byte_offset,
		  u64 v_min, u64 v_max,
		  u64 mask,
		  u32 shift)

{
  vlib_main_t * vm = pg->vlib_main;

  while (n_buffers >= 4)
    {
      vlib_buffer_t * b0, * b1, * b2, * b3;
      void * a0, * a1;

      b0 = vlib_get_buffer (vm, buffers[0]);
      b1 = vlib_get_buffer (vm, buffers[1]);
      b2 = vlib_get_buffer (vm, buffers[2]);
      b3 = vlib_get_buffer (vm, buffers[3]);
      buffers += 2;
      n_buffers -= 2;

      a0 = (void *) b0 + byte_offset;
      a1 = (void *) b1 + byte_offset;
      CLIB_PREFETCH ((void *) b2 + byte_offset, sizeof (v_min), WRITE);
      CLIB_PREFETCH ((void *) b3 + byte_offset, sizeof (v_min), WRITE);

      setbits_2 (a0, a1,
		 v_min, v_min,
		 v_min, v_max,
		 max_bits, n_bits, mask, shift,
		 /* is_increment */ 0);

      ASSERT (validate_buffer_data (b0, s));
      ASSERT (validate_buffer_data (b1, s));
    }

  while (n_buffers > 0)
    {
      vlib_buffer_t * b0;
      void * a0;

      b0 = vlib_get_buffer (vm, buffers[0]);
      buffers += 1;
      n_buffers -= 1;

      a0 = (void *) b0 + byte_offset;

      setbits_1 (a0, v_min, v_min, v_max, max_bits, n_bits, mask, shift);
      ASSERT (validate_buffer_data (b0, s));
    }
}

static always_inline u64
do_setbits_increment (pg_main_t * pg,
		      pg_stream_t * s,
		      u32 * buffers,
		      u32 n_buffers,
		      u32 max_bits,
		      u32 n_bits,
		      u32 byte_offset,
		      u64 v_min, u64 v_max,
		      u64 v,
		      u64 mask,
		      u32 shift)
{
  vlib_main_t * vm = pg->vlib_main;

  ASSERT (v >= v_min && v <= v_max);

  while (n_buffers >= 4)
    {
      vlib_buffer_t * b0, * b1, * b2, * b3;
      void * a0, * a1;
      u64 v_old;

      b0 = vlib_get_buffer (vm, buffers[0]);
      b1 = vlib_get_buffer (vm, buffers[1]);
      b2 = vlib_get_buffer (vm, buffers[2]);
      b3 = vlib_get_buffer (vm, buffers[3]);
      buffers += 2;
      n_buffers -= 2;

      a0 = (void *) b0 + byte_offset;
      a1 = (void *) b1 + byte_offset;
      CLIB_PREFETCH ((void *) b2 + byte_offset, sizeof (v_min), WRITE);
      CLIB_PREFETCH ((void *) b3 + byte_offset, sizeof (v_min), WRITE);

      v_old = v;
      v = v_old + 2;
      v = v > v_max ? v_min : v;
      setbits_2 (a0, a1,
		 v_old + 0, v_old + 1,
		 v_min, v_max,
		 max_bits, n_bits, mask, shift,
		 /* is_increment */ 1);

      if (PREDICT_FALSE (v_old + 1 > v_max))
	{
	  v = v_old;
	  setbits_1 (a0, v + 0, v_min, v_max, max_bits, n_bits, mask, shift);
	  v += 1;

	  v = v > v_max ? v_min : v;
	  setbits_1 (a1, v + 0, v_min, v_max, max_bits, n_bits, mask, shift);
	  v += 1;
	}
      ASSERT (validate_buffer_data (b0, s));
      ASSERT (validate_buffer_data (b1, s));
    }

  while (n_buffers > 0)
    {
      vlib_buffer_t * b0;
      void * a0;
      u64 v_old;

      b0 = vlib_get_buffer (vm, buffers[0]);
      buffers += 1;
      n_buffers -= 1;

      a0 = (void *) b0 + byte_offset;

      v_old = v;
      v = v_old + 1;
      v = v > v_max ? v_min : v;

      ASSERT (v_old >= v_min && v_old <= v_max);
      setbits_1 (a0, v_old, v_min, v_max, max_bits, n_bits, mask, shift);

      ASSERT (validate_buffer_data (b0, s));
    }

  return v;
}

static always_inline void
do_setbits_random (pg_main_t * pg,
		   pg_stream_t * s,
		   u32 * buffers,
		   u32 n_buffers,
		   u32 max_bits,
		   u32 n_bits,
		   u32 byte_offset,
		   u64 v_min, u64 v_max,
		   u64 mask,
		   u32 shift)
{
  vlib_main_t * vm = pg->vlib_main;
  u64 v_diff = v_max - v_min + 1;
  u64 r_mask = max_pow2 (v_diff) - 1;
  u64 v0, v1;
  void * random_data;

  random_data = clib_random_buffer_get_data
    (&vm->random_buffer, n_buffers * max_bits / BITS (u8));
  v0 = v1 = v_min;

  while (n_buffers >= 4)
    {
      vlib_buffer_t * b0, * b1, * b2, * b3;
      void * a0, * a1;
      u64 r0, r1;

      b0 = vlib_get_buffer (vm, buffers[0]);
      b1 = vlib_get_buffer (vm, buffers[1]);
      b2 = vlib_get_buffer (vm, buffers[2]);
      b3 = vlib_get_buffer (vm, buffers[3]);
      buffers += 2;
      n_buffers -= 2;

      a0 = (void *) b0 + byte_offset;
      a1 = (void *) b1 + byte_offset;
      CLIB_PREFETCH ((void *) b2 + byte_offset, sizeof (v_min), WRITE);
      CLIB_PREFETCH ((void *) b3 + byte_offset, sizeof (v_min), WRITE);

      switch (max_bits)
	{
#define _(n)					\
	  case BITS (u##n):			\
	    {					\
	      u##n * r = random_data;		\
	      r0 = r[0];			\
	      r1 = r[1];			\
	      random_data = r + 2;		\
	    }					\
	  break;

	  _ (8);
	  _ (16);
	  _ (32);
	  _ (64);

#undef _
	}

      /* Add power of 2 sized random number which may be out of range. */
      v0 += r0 & r_mask;
      v1 += r1 & r_mask;

      /* Twice should be enough to reduce to v_min .. v_max range. */
      v0 = v0 > v_max ? v0 - v_diff : v0;
      v1 = v1 > v_max ? v1 - v_diff : v1;
      v0 = v0 > v_max ? v0 - v_diff : v0;
      v1 = v1 > v_max ? v1 - v_diff : v1;

      setbits_2 (a0, a1,
		 v0, v1,
		 v_min, v_max,
		 max_bits, n_bits, mask, shift,
		 /* is_increment */ 0);

      ASSERT (validate_buffer_data (b0, s));
      ASSERT (validate_buffer_data (b1, s));
    }

  while (n_buffers > 0)
    {
      vlib_buffer_t * b0;
      void * a0;
      u64 r0;

      b0 = vlib_get_buffer (vm, buffers[0]);
      buffers += 1;
      n_buffers -= 1;

      a0 = (void *) b0 + byte_offset;

      switch (max_bits)
	{
#define _(n)					\
	  case BITS (u##n):			\
	    {					\
	      u##n * r = random_data;		\
	      r0 = r[0];			\
	      random_data = r + 1;		\
	    }					\
	  break;

	  _ (8);
	  _ (16);
	  _ (32);
	  _ (64);

#undef _
	}

      /* Add power of 2 sized random number which may be out of range. */
      v0 += r0 & r_mask;

      /* Twice should be enough to reduce to v_min .. v_max range. */
      v0 = v0 > v_max ? v0 - v_diff : v0;
      v0 = v0 > v_max ? v0 - v_diff : v0;

      setbits_1 (a0, v0, v_min, v_max, max_bits, n_bits, mask, shift);

      ASSERT (validate_buffer_data (b0, s));
    }
}

static u64 do_it (pg_main_t * pg,
		  pg_stream_t * s,
		  u32 * buffers,
		  u32 n_buffers,
		  u32 lo_bit, u32 hi_bit,
		  u64 v_min, u64 v_max,
		  u64 v,
		  pg_edit_type_t edit_type)
{
  u32 max_bits, l0, l1, h0, h1, start_bit;

  if (v_min == v_max)
    edit_type = PG_EDIT_FIXED;

  l0 = lo_bit / BITS (u8);
  l1 = lo_bit % BITS (u8);
  h0 = hi_bit / BITS (u8);
  h1 = hi_bit % BITS (u8);

  start_bit = l0 * BITS (u8);

  max_bits = hi_bit - start_bit;
  ASSERT (max_bits <= 64);

#define _(n)						\
  case (n):						\
    if (edit_type == PG_EDIT_INCREMENT)			\
      v = do_set_increment (pg, s, buffers, n_buffers,	\
			    BITS (u##n),		\
			    l0,				\
			    /* is_net_byte_order */ 1,	\
			    /* want sum */ 0, 0,	\
			    v_min, v_max,		\
			    v);				\
    else if (edit_type == PG_EDIT_RANDOM)		\
      do_set_random (pg, s, buffers, n_buffers,		\
		     BITS (u##n),			\
		     l0,				\
		     /* is_net_byte_order */ 1,		\
		     /* want sum */ 0, 0,		\
		     v_min, v_max);			\
    else /* edit_type == PG_EDIT_FIXED */		\
      do_set_fixed (pg, s, buffers, n_buffers,		\
		    BITS (u##n),			\
		    l0,					\
		    /* is_net_byte_order */ 1,		\
		    v_min, v_max);			\
  goto done;

  if (l1 == 0 && h1 == 0)
    {
      switch (max_bits)
	{
	  _ (8);
	  _ (16);
	  _ (32);
	  _ (64);
	}
    }

#undef _

  {
    u64 mask;
    u32 shift = l1;
    u32 n_bits = max_bits; 

    max_bits = clib_max (max_pow2 (n_bits), 8);

    mask = ((u64) 1 << (u64) n_bits) - 1;
    mask &= ~(((u64) 1 << (u64) shift) - 1);

    mask <<= max_bits - n_bits;
    shift += max_bits - n_bits;

    switch (max_bits)
      {
#define _(n)								\
	case (n):							\
	  if (edit_type == PG_EDIT_INCREMENT)				\
	    v = do_setbits_increment (pg, s, buffers, n_buffers,	\
				      BITS (u##n), n_bits,		\
				      l0, v_min, v_max, v,		\
				      mask, shift);			\
	  else if (edit_type == PG_EDIT_RANDOM)				\
	    do_setbits_random (pg, s, buffers, n_buffers,		\
			       BITS (u##n), n_bits,			\
			       l0, v_min, v_max,			\
			       mask, shift);				\
	  else /* edit_type == PG_EDIT_FIXED */				\
	    do_setbits_fixed (pg, s, buffers, n_buffers,		\
			      BITS (u##n), n_bits,			\
			      l0, v_min, v_max,				\
			      mask, shift);				\
	goto done;

	_ (8);
	_ (16);
	_ (32);
	_ (64);

#undef _
      }
  }

 done:
  return v;
}

static void
pg_generate_set_lengths (pg_main_t * pg,
			 pg_stream_t * s,
			 u32 * buffers,
			 u32 n_buffers)
{
  u64 v_min, v_max, length_sum;
  pg_edit_type_t edit_type;

  v_min = s->min_buffer_bytes;
  v_max = s->max_buffer_bytes;
  edit_type = s->buffer_size_edit_type;

  if (edit_type == PG_EDIT_INCREMENT)
    s->last_increment_buffer_size
      = do_set_increment (pg, s, buffers, n_buffers,
			  8 * STRUCT_SIZE_OF (vlib_buffer_t, current_length),
			  STRUCT_OFFSET_OF (vlib_buffer_t, current_length),
			  /* is_net_byte_order */ 0,
			  /* want sum */ 1, &length_sum,
			  v_min, v_max,
			  s->last_increment_buffer_size);

  else if (edit_type == PG_EDIT_RANDOM)
    do_set_random (pg, s, buffers, n_buffers,
		   8 * STRUCT_SIZE_OF (vlib_buffer_t, current_length),
		   STRUCT_OFFSET_OF (vlib_buffer_t, current_length),
		   /* is_net_byte_order */ 0,
		   /* want sum */ 1, &length_sum,
		   v_min, v_max);

  else /* edit_type == PG_EDIT_FIXED */
    {
      do_set_fixed (pg, s, buffers, n_buffers,
		    8 * STRUCT_SIZE_OF (vlib_buffer_t, current_length),
		    STRUCT_OFFSET_OF (vlib_buffer_t, current_length),
		    /* is_net_byte_order */ 0,
		    v_min, v_max);
      length_sum = v_min * n_buffers;
    }

  {
    vlib_main_t * vm = pg->vlib_main;
    vlib_interface_main_t * im = &vm->interface_main;
    vlib_sw_interface_t * si = vlib_get_sw_interface (vm, s->sw_if_index[VLIB_RX]);

    vlib_increment_combined_counter (im->combined_sw_if_counters
				     + VLIB_INTERFACE_COUNTER_RX,
				     si->sw_if_index,
				     n_buffers,
				     length_sum);
  }
}

static void
pg_generate_edit (pg_main_t * pg,
		  pg_stream_t * s,
		  u32 * buffers,
		  u32 n_buffers)
{
  pg_edit_t * e;

  vec_foreach (e, s->edits)
    {
      switch (e->type)
	{
	case PG_EDIT_RANDOM:
	case PG_EDIT_INCREMENT:
	  {
	    u32 lo_bit, hi_bit;
	    u64 v_min, v_max;

	    v_min = pg_edit_get_value (e, PG_EDIT_LO);
	    v_max = pg_edit_get_value (e, PG_EDIT_HI);

	    hi_bit = (BITS (u8) * STRUCT_OFFSET_OF (vlib_buffer_t, data)
		      + e->lsb_bit_offset);
	    lo_bit = hi_bit - e->n_bits + BITS (u8);

	    e->last_increment_value
	      = do_it (pg, s, buffers, n_buffers, lo_bit, hi_bit, v_min, v_max,
		       e->last_increment_value,
		       e->type);
	  }
	  break;

	case PG_EDIT_UNSPECIFIED:
	  break;

	default:
	  ASSERT (0);
	  break;
	}
    }

  /* Call any edit functions to e.g. completely IP lengths, checksums, ... */
  {
    int i;
    for (i = vec_len (s->edit_groups) - 1; i >= 0; i--)
      {
	pg_edit_group_t * g = s->edit_groups + i;

	if (! g->edit_function)
	  continue;
	g->edit_function (pg, s, g, buffers, n_buffers);
      }
  }
}

static always_inline void
init_buffers_inline (vlib_main_t * vm,
		     pg_stream_t * s,
		     u32 * buffers,
		     u32 n_buffers,
		     u32 set_data)
{
  u32 n_left, * b;
  u8 * data, * mask;
  u32 n_data;

  data = s->buffer_data;
  n_data = vec_len (data);
  mask = s->buffer_data_mask;

  n_left = n_buffers;
  b = buffers;

  while (n_left >= 4)
    {
      u32 bi0, bi1;
      vlib_buffer_t * b0, * b1;

      /* Prefetch next iteration. */
      vlib_prefetch_buffer_with_index (vm, b[2], STORE);
      vlib_prefetch_buffer_with_index (vm, b[3], STORE);

      bi0 = b[0];
      bi1 = b[1];
      b += 2;
      n_left -= 2;

      b0 = vlib_get_buffer (vm, bi0);
      b1 = vlib_get_buffer (vm, bi1);

      b0->sw_if_index[VLIB_RX] =
	b1->sw_if_index[VLIB_RX] = s->sw_if_index[VLIB_RX];
      b0->sw_if_index[VLIB_TX] =
	b1->sw_if_index[VLIB_TX] = s->sw_if_index[VLIB_TX];

      if (set_data)
	{
	  memcpy (b0->data, data, n_data);
	  memcpy (b1->data, data, n_data);
	}
      else
	{
	  ASSERT (validate_buffer_data (b0, s));
	  ASSERT (validate_buffer_data (b1, s));
	}
    }

  while (n_left >= 1)
    {
      u32 bi0;
      vlib_buffer_t * b0;

      bi0 = b[0];
      b += 1;
      n_left -= 1;

      b0 = vlib_get_buffer (vm, bi0);

      b0->sw_if_index[VLIB_RX] = s->sw_if_index[VLIB_RX];
      b0->sw_if_index[VLIB_TX] = s->sw_if_index[VLIB_TX];

      if (set_data)
	memcpy (b0->data, data, n_data);
      else
	ASSERT (validate_buffer_data (b0, s));
    }
}

static void pg_buffer_init (vlib_main_t * vm,
			    vlib_buffer_free_list_t * fl,
			    u32 * buffers,
			    u32 n_buffers)
{
  pg_main_t * pg = &pg_main;
  pg_stream_t * s;
  s = pool_elt_at_index (pg->streams, fl->opaque);
  init_buffers_inline (vm, s, buffers, n_buffers,
		       /* set_data */ 1);
}

static u32
pg_stream_fill_helper (pg_main_t * pg, pg_stream_t * s,
		       u32 * alloc_buffers,
		       u32 n_alloc)
{
  vlib_main_t * vm = pg->vlib_main;
  vlib_buffer_free_list_t * f;

  f = vlib_buffer_get_free_list (vm, s->free_list_index);
  f->buffer_init_function = pg_buffer_init;
  f->opaque = s - pg->streams;

  if (! vlib_buffer_alloc_from_free_list (vm,
					  alloc_buffers,
					  n_alloc,
					  s->free_list_index))
    return 0;
      
  /* No need to do anything with already used buffers unless debugging. */
  if (DEBUG > 0)
    init_buffers_inline (vm, s,
			 alloc_buffers,
			 n_alloc,
			 /* set data */ 0);

  pg_generate_set_lengths (pg, s, alloc_buffers, n_alloc);
  pg_generate_edit (pg, s, alloc_buffers, n_alloc);
  return n_alloc;
}

static u32
pg_stream_fill (pg_main_t * pg, pg_stream_t * s, u32 n_buffers)
{
  word n_in_fifo, n_alloc, n_free, n_added;
  u32 * tail, * start, * end;

  n_in_fifo = clib_fifo_elts (s->buffer_fifo);
  if (n_in_fifo >= n_buffers)
    return n_in_fifo;

  n_alloc = n_buffers - n_in_fifo;

  /* Round up, but never generate more than limit. */
  n_alloc = clib_max (VLIB_FRAME_SIZE, n_alloc);

  if (s->n_buffers_limit > 0
      && s->n_buffers_generated + n_in_fifo + n_alloc >= s->n_buffers_limit)
    {
      n_alloc = s->n_buffers_limit - s->n_buffers_generated - n_in_fifo;
      if (n_alloc < 0)
	n_alloc = 0;
    }

  n_free = clib_fifo_free_elts (s->buffer_fifo);
  if (n_free < n_alloc)
    clib_fifo_resize (s->buffer_fifo, n_alloc - n_free);

  tail = clib_fifo_advance_tail (s->buffer_fifo, n_alloc);
  start = s->buffer_fifo;
  end = clib_fifo_end (s->buffer_fifo);

  if (tail + n_alloc <= end)
    n_added = pg_stream_fill_helper (pg, s, tail, n_alloc);
  else
    {
      u32 n = clib_min (end - tail, n_alloc);
      n_added = pg_stream_fill_helper (pg, s, tail, n);
      if (n_added > 0 && n_alloc > n)
	n_added += pg_stream_fill_helper (pg, s, start, n_alloc - n);
    }

  if (PREDICT_FALSE (n_added < n_alloc))
    clib_fifo_advance_tail (s->buffer_fifo, n_added - n_alloc);

  return n_in_fifo + n_added;
}

typedef struct {
  u32 stream_index;

  u32 buffer_length;

  u8 buffer_data[64 - 2*sizeof(u32)];
} pg_input_trace_t;

static u8 * format_pg_input_trace (u8 * s, va_list * va)
{
  vlib_main_t * vm = va_arg (*va, vlib_main_t *);
  UNUSED (vlib_node_t * node) = va_arg (*va, vlib_node_t *);
  pg_input_trace_t * t = va_arg (*va, pg_input_trace_t *);
  pg_main_t * pg = &pg_main;
  pg_stream_t * stream;
  vlib_node_t * n;
  uword indent = format_get_indent (s);

  stream = 0;
  if (! pool_is_free_index (pg->streams, t->stream_index))
    stream = pool_elt_at_index (pg->streams, t->stream_index);

  if (stream)
    s = format (s, "stream %v", pg->streams[t->stream_index].name);
  else
    s = format (s, "stream %d", t->stream_index);

  s = format (s, ", %d bytes", t->buffer_length);

  s = format (s, "\n%U",
	      format_white_space, indent);

  n = 0;
  if (stream)
    n = vlib_get_node (vm, stream->node_index);

  if (n && n->format_buffer)
    s = format (s, "%U", n->format_buffer,
		t->buffer_data,
		sizeof (t->buffer_data));
  else
    s = format (s, "%U", 
		format_hex_bytes, t->buffer_data, ARRAY_LEN (t->buffer_data));

  return s;
}

static void
pg_input_trace (pg_main_t * pg,
		vlib_node_runtime_t * node,
		pg_stream_t * s,
		u32 * buffers,
		u32 n_buffers)
{
  vlib_main_t * vm = pg->vlib_main;
  u32 * b, n_left, stream_index, next_index;

  n_left = n_buffers;
  b = buffers;
  stream_index = s - pg->streams;
  next_index = s->next_index;

  while (n_left >= 2)
    {
      u32 bi0, bi1;
      vlib_buffer_t * b0, * b1;
      pg_input_trace_t * t0, * t1;

      bi0 = b[0];
      bi1 = b[1];
      b += 2;
      n_left -= 2;

      b0 = vlib_get_buffer (vm, bi0);
      b1 = vlib_get_buffer (vm, bi1);

      vlib_trace_buffer (vm, node, next_index, b0);
      vlib_trace_buffer (vm, node, next_index, b1);

      t0 = vlib_add_trace (vm, node, b0, sizeof (t0[0]));
      t1 = vlib_add_trace (vm, node, b1, sizeof (t1[0]));

      t0->stream_index = stream_index;
      t1->stream_index = stream_index;

      t0->buffer_length = b0->current_length;
      t1->buffer_length = b1->current_length;

      memcpy (t0->buffer_data, b0->data, sizeof (t0->buffer_data));
      memcpy (t1->buffer_data, b1->data, sizeof (t1->buffer_data));
    }

  while (n_left >= 1)
    {
      u32 bi0;
      vlib_buffer_t * b0;
      pg_input_trace_t * t0;

      bi0 = b[0];
      b += 1;
      n_left -= 1;

      b0 = vlib_get_buffer (vm, bi0);

      vlib_trace_buffer (vm, node, next_index, b0);
      t0 = vlib_add_trace (vm, node, b0, sizeof (t0[0]));

      t0->stream_index = stream_index;
      t0->buffer_length = b0->current_length;
      memcpy (t0->buffer_data, b0->data, sizeof (t0->buffer_data));
    }
}

static uword
pg_generate_buffers (vlib_node_runtime_t * node,
		     pg_main_t * pg,
		     pg_stream_t * s,
		     uword n_buffers_to_generate)
{
  vlib_main_t * vm = pg->vlib_main;
  u32 * to_next, n_this_frame, n_left, n_trace, n_buffers_in_fifo;
  uword n_buffers_generated;

  n_buffers_in_fifo = pg_stream_fill (pg, s, n_buffers_to_generate);
  n_buffers_to_generate = clib_min (n_buffers_in_fifo, n_buffers_to_generate);
  n_buffers_generated = 0;

  while (n_buffers_to_generate > 0)
    {
      u32 * head, * start, * end;

      vlib_get_next_frame (vm, node, s->next_index, to_next, n_left);

      n_this_frame = n_buffers_to_generate;
      if (n_this_frame > n_left)
	n_this_frame = n_left;

      start = s->buffer_fifo;
      end = clib_fifo_end (s->buffer_fifo);
      head = clib_fifo_head (s->buffer_fifo);

      if (head + n_this_frame <= end)
	vlib_copy_buffers (to_next, head, n_this_frame);
      else
	{
	  u32 n = end - head;
	  vlib_copy_buffers (to_next + 0, head, n);
	  vlib_copy_buffers (to_next + n, start, n_this_frame - n);
	}

      clib_fifo_advance_head (s->buffer_fifo, n_this_frame);

      n_trace = vlib_get_trace_count (vm);
      if (n_trace > 0)
	{
	  u32 n = clib_min (n_trace, n_this_frame);
	  pg_input_trace (pg, node, s, to_next, n);
	  vlib_set_trace_count (vm, n_trace - n);
	}

      n_buffers_to_generate -= n_this_frame;
      n_buffers_generated += n_this_frame;
      n_left -= n_this_frame;
      vlib_put_next_frame (vm, node, s->next_index, n_left);
    }

  return n_buffers_generated;
}

static uword
pg_input_stream (vlib_node_runtime_t * node,
		 pg_main_t * pg,
		 pg_stream_t * s)
{
  vlib_main_t * vm = pg->vlib_main;
  uword n_buffers;
  f64 time_now, dt;

  if (s->n_buffers_limit > 0
      && s->n_buffers_generated >= s->n_buffers_limit)
    {
      pg_stream_enable_disable (pg, s, /* want_enabled */ 0);
      return 0;
    }

  /* Apply rate limit. */
  time_now = vlib_time_now (vm);
  if (s->time_last_generate == 0)
    s->time_last_generate = time_now;

  dt = time_now - s->time_last_generate;
  s->time_last_generate = time_now;

  n_buffers = VLIB_FRAME_SIZE;
  if (s->rate_buffers_per_second > 0)
    {
      s->buffer_accumulator += dt * s->rate_buffers_per_second;
      n_buffers = s->buffer_accumulator;

      /* Never allow accumulator to grow if we get behind. */
      s->buffer_accumulator -= n_buffers;
    }

  /* Apply fixed limit. */
  if (s->n_buffers_limit > 0
      && s->n_buffers_generated + n_buffers > s->n_buffers_limit)
    n_buffers = s->n_buffers_limit - s->n_buffers_generated;

  /* Generate up to one frame's worth of buffers. */
  if (n_buffers > VLIB_FRAME_SIZE)
    n_buffers = VLIB_FRAME_SIZE;

  if (n_buffers > 0)
    n_buffers = pg_generate_buffers (node, pg, s, n_buffers);

  s->n_buffers_generated += n_buffers;

  return n_buffers;
}

uword
pg_input (vlib_main_t * vm,
	  vlib_node_runtime_t * node,
	  vlib_frame_t * frame)
{
  uword i;
  pg_main_t * pg = &pg_main;
  uword n_buffers = 0;

  clib_bitmap_foreach (i, pg->enabled_streams, ({
	n_buffers += pg_input_stream (node, pg, vec_elt_at_index (pg->streams, i));
      }));

  return n_buffers;
}

VLIB_REGISTER_NODE (pg_input_node) = {
  .function = pg_input,
  .name = "pg-input",
  .type = VLIB_NODE_TYPE_INPUT,

  .format_trace = format_pg_input_trace,

  /* Input node will be left disabled until a stream is active. */
  .flags = VLIB_NODE_FLAG_IS_DISABLED,
};
