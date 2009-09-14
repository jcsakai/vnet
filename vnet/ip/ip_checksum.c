/*
 * ip4/ip_checksum.c: ip/tcp/udp checksums
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

#include <vnet/ip/ip.h>

ip_csum_t
ip_incremental_checksum (ip_csum_t sum, void * _data, uword n_bytes)
{
  uword data = pointer_to_uword (_data);
  ip_csum_t sum0, sum1;

  sum0 = 0;
  sum1 = sum;

  /* Align data pointer to 64 bits. */
#define _(t)					\
  if (n_bytes >= sizeof (t)			\
      && sizeof (t) < sizeof (ip_csum_t)	\
      && (data % (2 * sizeof (t))) != 0)	\
    {						\
      sum0 += * uword_to_pointer (data, t *);	\
      data += sizeof (t);			\
      n_bytes -= sizeof (t);			\
    }

  _ (u8);
  _ (u16);
  _ (u32);

#undef _

 {
   ip_csum_t * d = uword_to_pointer (data, ip_csum_t *);

   while (n_bytes >= 2 * sizeof (d[0]))
     {
       sum0 = ip_csum_with_carry (sum0, d[0]);
       sum1 = ip_csum_with_carry (sum1, d[1]);
       d += 2;
       n_bytes -= 2 * sizeof (d[0]);
     }

   data = pointer_to_uword (d);
 }
   
#define _(t)								\
 if (n_bytes >= sizeof (t) && sizeof (t) <= sizeof (ip_csum_t))		\
   {									\
     sum0 = ip_csum_with_carry (sum0, * uword_to_pointer (data, t *));	\
     data += sizeof (t);						\
     n_bytes -= sizeof (t);						\
   }

 _ (u64);
 _ (u32);
 _ (u16);
 _ (u8);

#undef _

 /* Combine even and odd sums. */
 sum0 = ip_csum_with_carry (sum0, sum1);

 return sum0;
}

