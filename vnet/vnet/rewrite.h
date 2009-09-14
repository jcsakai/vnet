/*
 * rewrite.h: packet rewrite
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

#ifndef included_vnet_rewrite_h
#define included_vnet_rewrite_h

#include <vlib/vlib.h>

/* Basic data type for painting rewrite strings. */
typedef vlib_copy_unit_t vnet_rewrite_data_t;

typedef PACKED (struct {
  /* Interface to mark re-written packets with. */
  u32 sw_if_index;

  /* Packet processing node where rewrite happens. */
  u32 node_index;

  /* Next node to feed after packet rewrite is done. */
  u16 next_index;

  /* Number of bytes in rewrite data. */
  u16 data_bytes;

  /* Max packet size layer 2 (MTU) for output interface.
     Used for MTU check after packet rewrite. */
  u16 max_packet_bytes;

  /* Rewrite string starting at end and going backwards. */
  u8 data[0];
}) vnet_rewrite_header_t;

/*
  Helper macro for declaring rewrite string w/ given max-size.

  Typical usage:
    typedef struct {
      // User data.
      int a, b;

      // Total adjacency is 64 bytes.
      vnet_rewrite_declare(64 - 2*sizeof(int)) rw;
    } my_adjacency_t;
*/
#define vnet_declare_rewrite(total_bytes)				\
struct {								\
  vnet_rewrite_header_t rewrite_header;  			        \
									\
  u8 rewrite_data[(total_bytes) - sizeof (vnet_rewrite_header_t)];	\
}

static always_inline void
vnet_rewrite_set_data_internal (vnet_rewrite_header_t * rw,
				int max_size,
				void * data,
				int data_bytes)
{
  /* Rewrite string must fit. */
  ASSERT (data_bytes < max_size);

  rw->data_bytes = data_bytes;
  memcpy (rw->data + max_size - data_bytes, data, data_bytes);
  memset (rw->data, 0xfe, max_size - data_bytes);
}

#define vnet_rewrite_set_data(rw,data,data_bytes)		\
  vnet_rewrite_set_data_internal (&((rw).rewrite_header),	\
				  sizeof ((rw).rewrite_data),	\
				  (data),			\
				  (data_bytes))

static always_inline void
vnet_rewrite_copy_one (vnet_rewrite_data_t * p0, vnet_rewrite_data_t * rw0, int i)
{ clib_mem_unaligned (p0 - i, vnet_rewrite_data_t) = rw0[-i]; }

static always_inline void
_vnet_rewrite_one_header (vnet_rewrite_header_t * h0,
			  void * packet0,
			  int max_size,
			  int most_likely_size)
{
  vnet_rewrite_data_t * p0 = packet0;
  vnet_rewrite_data_t * rw0 = (vnet_rewrite_data_t *) (h0->data + max_size);

#define _(i)								\
  do {									\
    if (most_likely_size > ((i)-1)*sizeof (vnet_rewrite_data_t))	\
      vnet_rewrite_copy_one (p0, rw0, (i));				\
  } while (0)

  _ (4);
  _ (3);
  _ (2);
  _ (1);

#undef _
    
  if (PREDICT_FALSE (h0->data_bytes > most_likely_size))
    {
      int n_done, n_left;

      n_done = round_pow2 (most_likely_size, sizeof (rw0[0]));
      n_left = round_pow2 (h0->data_bytes, sizeof (rw0[0])) - n_done;
      p0 -= n_done;
      rw0 -= n_done;
      do {
	vnet_rewrite_copy_one (p0, rw0, 0);
	p0--;
	rw0--;
	n_left--;
      } while (n_left != 0);
    }
}

static always_inline void
_vnet_rewrite_two_headers (vnet_rewrite_header_t * h0,
			   vnet_rewrite_header_t * h1,
			   void * packet0,
			   void * packet1,
			   int max_size,
			   int most_likely_size)
{
  vnet_rewrite_data_t * p0 = packet0;
  vnet_rewrite_data_t * p1 = packet1;
  vnet_rewrite_data_t * rw0 = (vnet_rewrite_data_t *) (h0->data + max_size);
  vnet_rewrite_data_t * rw1 = (vnet_rewrite_data_t *) (h1->data + max_size);

#define _(i)								\
  do {									\
    if (most_likely_size > ((i)-1)*sizeof (vnet_rewrite_data_t))	\
      {									\
	vnet_rewrite_copy_one (p0, rw0, (i));				\
	vnet_rewrite_copy_one (p1, rw1, (i));				\
      }									\
  } while (0)

  _ (4);
  _ (3);
  _ (2);
  _ (1);

#undef _
    
  if (PREDICT_FALSE (h0->data_bytes > most_likely_size
		     || h1->data_bytes > most_likely_size))
    {
      int n_done, n_left;

      if (h0->data_bytes > most_likely_size)
	{
	  n_done = round_pow2 (most_likely_size, sizeof (rw0[0]));
	  n_left = round_pow2 (h0->data_bytes, sizeof (rw0[0])) - n_done;
	  p0 -= n_done;
	  rw0 -= n_done;
	  do {
	    vnet_rewrite_copy_one (p0, rw0, 0);
	    p0--;
	    rw0--;
	    n_left--;
	  } while (n_left != 0);
	}

      if (h1->data_bytes > most_likely_size)
	{
	  n_done = round_pow2 (most_likely_size, sizeof (rw1[0]));
	  n_left = round_pow2 (h1->data_bytes, sizeof (rw1[0])) - n_done;
	  p1 -= n_done;
	  rw1 -= n_done;
	  do {
	    vnet_rewrite_copy_one (p1, rw1, 0);
	    p1--;
	    rw1--;
	    n_left--;
	  } while (n_left != 0);
	}
    }
}

#define vnet_rewrite_one_header(rw0,p0,most_likely_size)	\
  _vnet_rewrite_one_header (&((rw0).rewrite_header), (p0),	\
			    sizeof ((rw0).rewrite_data),	\
			    (most_likely_size))

#define vnet_rewrite_two_headers(rw0,rw1,p0,p1,most_likely_size)	\
  _vnet_rewrite_two_headers (&((rw0).rewrite_header), &((rw1).rewrite_header), \
			     (p0), (p1),				\
			     sizeof ((rw0).rewrite_data),		\
			     (most_likely_size))

/* Parser for unformat header & rewrite string. */
uword unformat_vnet_rewrite (unformat_input_t * input, va_list * args);

u8 * format_vnet_rewrite (u8 * s, va_list * args);
u8 * format_vnet_rewrite_header (u8 * s, va_list * args);

#endif /* included_vnet_rewrite_h */