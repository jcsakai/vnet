/*
 * ip/ip_init.c: ip generic initialization
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

ip_main_t ip_main;

clib_error_t *
ip_main_init (vlib_main_t * vm)
{
  ip_main_t * im = &ip_main;

  memset (im, 0, sizeof (im[0]));

  {
    ip_protocol_info_t * pi;
    u32 i;

#define ip_protocol(n,s)			\
do {						\
  vec_add2 (im->protocol_infos, pi, 1);		\
  pi->protocol = n;				\
  pi->name = (u8 *) #s;				\
} while (0);

#include "protocols.def"

#undef ip_protocol

    im->protocol_info_by_name = hash_create_string (0, sizeof (uword));
    for (i = 0; i < vec_len (im->protocol_infos); i++)
      {
	pi = im->protocol_infos + i;

	hash_set_mem (im->protocol_info_by_name, pi->name, i);
	hash_set (im->protocol_info_by_protocol, pi->protocol, i);
      }
  }

  {
    tcp_udp_port_info_t * pi;
    u32 i;
    static char * port_names[] = 
      {
#define ip_port(s,n) #s,
#include "ports.def"
#undef ip_port
      };
    static u16 ports[] = 
      {
#define ip_port(s,n) n,
#include "ports.def"
#undef ip_port
      };

    vec_resize (im->port_infos, ARRAY_LEN (port_names));
    im->port_info_by_name = hash_create_string (0, sizeof (uword));

    for (i = 0; i < vec_len (im->port_infos); i++)
      {
	pi = im->port_infos + i;
	pi->port = clib_host_to_net_u16 (ports[i]);
	pi->name = (u8 *) port_names[i];
	hash_set_mem (im->port_info_by_name, pi->name, i);
	hash_set (im->port_info_by_port, pi->port, i);
      }
  }

  return 0;
}

VLIB_INIT_FUNCTION (ip_main_init);
