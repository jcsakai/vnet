/*
 * pcap.c: libpcap packet capture format
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

#include <vnet/unix/pcap.h>
#include <sys/fcntl.h>

/* Usage

#include <vnet/unix/pcap.h>

static pcap_main_t pcap = {
  .file_name = "/tmp/ip4",
  .n_packets_to_capture = 2,
  .packet_type = PCAP_PACKET_TYPE_ip,
};

To add a buffer:

  pcap_add_buffer (&pcap, vm, pi0, 128);

file will be written after n_packets_to_capture or call to pcap_write (&pcap).

*/

clib_error_t *
pcap_write (pcap_main_t * pm)
{
  clib_error_t * error = 0;

  if (! (pm->flags & PCAP_MAIN_INIT_DONE))
    {
      pcap_file_header_t fh;
      int n;

      if (! pm->file_name)
	pm->file_name = "/tmp/vnet.pcap";

      pm->file_descriptor = open (pm->file_name, O_CREAT | O_TRUNC | O_WRONLY, 0664);
      if (pm->file_descriptor < 0)
	{
	  error = clib_error_return_unix (0, "failed to open `%s'", pm->file_name);
	  goto done;
	}

      pm->flags |= PCAP_MAIN_INIT_DONE;
      pm->n_packets_captured = 0;
      pm->n_pcap_data_written = 0;

      /* Write file header. */
      memset (&fh, 0, sizeof (fh));
      fh.magic = 0xa1b2c3d4;
      fh.major_version = 2;
      fh.minor_version = 4;
      fh.time_zone = 0;
      fh.max_packet_size_in_bytes = 1 << 16;
      fh.packet_type = pm->packet_type;
      n = write (pm->file_descriptor, &fh, sizeof (fh));
      if (n != sizeof (fh))
	{
	  if (n < 0)
	    error = clib_error_return_unix (0, "write file header `%s'", pm->file_name);
	  else
	    error = clib_error_return (0, "short write of file header `%s'", pm->file_name);
	  goto done;
	}
    }

  do {
    int n = vec_len (pm->pcap_data) - pm->n_pcap_data_written;

    if (n > 0)
      {
	n = write (pm->file_descriptor,
		   vec_elt_at_index (pm->pcap_data, pm->n_pcap_data_written),
		   n);
	if (n < 0 && unix_error_is_fatal (errno))
	  {
	    error = clib_error_return_unix (0, "write `%s'", pm->file_name);
	    goto done;
	  }
      }
    pm->n_pcap_data_written += n;
    if (pm->n_pcap_data_written >= vec_len (pm->pcap_data))
      {
	_vec_len (pm->pcap_data) = 0;
	break;
      }
  } while (pm->n_packets_captured >= pm->n_packets_to_capture);

  if (pm->n_packets_captured >= pm->n_packets_to_capture)
    {
      close (pm->file_descriptor);
      pm->flags &= ~PCAP_MAIN_INIT_DONE;
      pm->file_descriptor = -1;
    }

 done:
  if (error)
    {
      if (pm->file_descriptor >= 0)
	close (pm->file_descriptor);
    }
  return error;
}

clib_error_t * pcap_read (pcap_main_t * pm)
{
  clib_error_t * error = 0;
  int fd, need_swap, n;
  pcap_file_header_t fh;
  pcap_packet_header_t ph;

  fd = open (pm->file_name, O_RDONLY);
  if (fd < 0)
    {
      error = clib_error_return_unix (0, "open `%s'", pm->file_name);
      goto done;
    }

  if (read (fd, &fh, sizeof (fh)) != sizeof (fh))
    {
      error = clib_error_return_unix (0, "read file header `%s'", pm->file_name);
      goto done;
    }

  need_swap = 0;
  if (fh.magic == 0xd4c3b2a1)
    {
      need_swap = 1;
#define _(t,f) fh.f = clib_byte_swap_##t (fh.f);
      foreach_pcap_file_header;
#undef _
    }    

  if (fh.magic != 0xa1b2c3d4)
    {
      error = clib_error_return (0, "bad magic `%s'", pm->file_name);
      goto done;
    }

  pm->min_packet_bytes = 0;
  pm->max_packet_bytes = 0;
  while ((n = read (fd, &ph, sizeof (ph))) != 0)
    {
      u8 * data;

      if (need_swap)
	{
#define _(t,f) ph.f = clib_byte_swap_##t (ph.f);
	  foreach_pcap_packet_header;
#undef _
	}

      data = vec_new (u8, ph.n_bytes_in_packet);
      if (read (fd, data, ph.n_packet_bytes_stored_in_file) != ph.n_packet_bytes_stored_in_file)
	{
	  error = clib_error_return (0, "short read `%s'", pm->file_name);
	  goto done;
	}
	
      if (vec_len (pm->packets_read) == 0)
	pm->min_packet_bytes = pm->max_packet_bytes = ph.n_bytes_in_packet;
      else
	{
	  pm->min_packet_bytes = clib_min (pm->min_packet_bytes, ph.n_bytes_in_packet);
	  pm->max_packet_bytes = clib_max (pm->max_packet_bytes, ph.n_bytes_in_packet);
	}
	
      vec_add1 (pm->packets_read, data);
    }

 done:
  if (fd >= 0)
    close (fd);
  return error;
  
}
