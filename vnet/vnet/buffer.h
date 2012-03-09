/*
 * vnet/buffer.h: vnet buffer flags
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

#ifndef included_vnet_buffer_h
#define included_vnet_buffer_h

/* VLIB buffer flags for ip4/ip6 packets.  Set by input interfaces for ip4/ip6
   tcp/udp packets with hardware computed checksums. */
#define LOG2_IP_BUFFER_L4_CHECKSUM_COMPUTED LOG2_VLIB_BUFFER_FLAG_USER(1)
#define LOG2_IP_BUFFER_L4_CHECKSUM_CORRECT  LOG2_VLIB_BUFFER_FLAG_USER(2)
#define IP_BUFFER_L4_CHECKSUM_COMPUTED (1 << LOG2_IP_BUFFER_L4_CHECKSUM_COMPUTED)
#define IP_BUFFER_L4_CHECKSUM_CORRECT  (1 << LOG2_IP_BUFFER_L4_CHECKSUM_CORRECT)

#endif /* included_vnet_buffer_h */
