/*
 * pg_cli.c: packet generator cli
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

/* Root of all packet generator cli commands. */
VLIB_CLI_COMMAND (vlib_cli_pg_command) = {
  .name = "packet-generator",
  .short_help = "Packet generator commands",
};

static clib_error_t *
enable_disable_stream (vlib_main_t * vm,
		       unformat_input_t * input,
		       vlib_cli_command_t * cmd)
{
  pg_main_t * pg = &pg_main;
  pg_stream_t * s;
  int is_enable = cmd->function_arg != 0;
  u32 stream_index = ~0;

  if (unformat (input, "%U", unformat_eof))
    ;
  else if (unformat (input, "%U", unformat_hash_vec_string,
		     pg->stream_index_by_name, &stream_index))
    ;
  else
    return clib_error_create ("unknown input `%U'",
			      format_unformat_error, input);

  /* No stream specified: enable/disable all streams. */
  if (stream_index == ~0)
    pool_foreach (s, pg->streams, ({
      pg_stream_enable_disable (pg, s, is_enable);
    }));
  else
    {
      /* enable/disable specified stream. */
      s = pool_elt_at_index (pg->streams, stream_index);
      pg_stream_enable_disable (pg, s, is_enable);
    }
		      
  return 0;
}

static VLIB_CLI_COMMAND (enable_streams_cli) = {
  .name = "enable",
  .short_help = "Enable packet generator streams",
  .function = enable_disable_stream,
  .function_arg = 1,		/* is_enable */
  .parent = &vlib_cli_pg_command,
};

static VLIB_CLI_COMMAND (disable_streams_cli) = {
  .name = "disable",
  .short_help = "Disable packet generator streams",
  .function = enable_disable_stream,
  .function_arg = 0,		/* is_enable */
  .parent = &vlib_cli_pg_command,
};

static u8 * format_pg_stream (u8 * s, va_list * va)
{
  pg_stream_t * t = va_arg (*va, pg_stream_t *);
  u8 * v;

  if (! t)
    return format (s, "%=16s%=12s%=16s%s",
		   "Name", "Enabled", "Count", "Options");

  s = format (s, "%-16v%=12s%16Ld",
	      t->name,
	      pg_stream_is_enabled (t) ? "Yes" : "No",
	      t->n_buffers_generated);

  v = 0;
  if (t->n_buffers_limit > 0)
    v = format (v, "limit %Ld, ", t->n_buffers_limit);
  if (t->rate_buffers_per_second > 0)
    v = format (v, "rate %.2epps, ", t->rate_buffers_per_second);
  if (v)
    {
      s = format (s, "  %v", v);
      vec_free (v);
    }

  return s;
}

static clib_error_t *
show_streams (vlib_main_t * vm,
	      unformat_input_t * input,
	      vlib_cli_command_t * cmd)
{
  pg_main_t * pg = &pg_main;
  pg_stream_t * s;

  if (pool_elts (pg->streams) == 0)
    {
      vlib_cli_output (vm, "no streams currently defined");
      goto done;
    }

  vlib_cli_output (vm, "%U", format_pg_stream, 0);
  pool_foreach (s, pg->streams, ({
      vlib_cli_output (vm, "%U", format_pg_stream, s);
    }));

 done:
  return 0;
}

static VLIB_CLI_COMMAND (show_streams_cli) = {
  .name = "packet-generator",
  .short_help = "Show packet generator streams",
  .function = show_streams,
  .parent = &vlib_cli_show_command,
};

static clib_error_t *
new_stream (vlib_main_t * vm,
	    unformat_input_t * input,
	    vlib_cli_command_t * cmd)
{
  clib_error_t * error = 0;
  u8 * tmp = 0;
  u32 hw_if_index;
  unformat_input_t sub_input = {0};
  int sub_input_given = 0;
  pg_main_t * pg = &pg_main;
  pg_stream_t s = {0};
  f64 float_limit;
  
  s.sw_if_index[VLIB_RX] = s.sw_if_index[VLIB_TX] = ~0;
  s.node_index = ~0;
  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      if (unformat (input, "name %v", &tmp))
	{
	  if (s.name)
	    vec_free (s.name);
	  s.name = tmp;
	}

      else if (unformat (input, "node %U",
			 unformat_vlib_hw_interface, vm, &hw_if_index))
	{
	  vlib_hw_interface_t * hi = vlib_get_hw_interface (vm, hw_if_index);

	  s.node_index = hi->output_node_index;
	  s.sw_if_index[VLIB_TX] = hi->sw_if_index;
	}

      else if (unformat (input, "node %U",
			 unformat_vlib_node, vm, &s.node_index))
	;
			 
      else if (unformat (input, "interface %U",
			 unformat_vlib_sw_interface, vm, &s.sw_if_index[VLIB_RX]))
	;

      else if (! sub_input_given
	       && unformat (input, "data %U", unformat_input, &sub_input))
	sub_input_given++;
			 
      else if (unformat (input, "limit %f", &float_limit))
	s.n_buffers_limit = float_limit;

      else if (unformat (input, "rate %f", &s.rate_buffers_per_second))
	;

      else if (unformat (input, "increment-size %d-%d",
			 &s.min_buffer_bytes,
			 &s.max_buffer_bytes))
	s.buffer_size_edit_type = PG_EDIT_INCREMENT;

      else if (unformat (input, "random-size %d-%d",
			 &s.min_buffer_bytes,
			 &s.max_buffer_bytes))
	s.buffer_size_edit_type = PG_EDIT_RANDOM;

      else
	{
	  error = clib_error_create ("unknown input `%U'",
				     format_unformat_error, input);
	  goto done;
	}
    }

  if (s.max_buffer_bytes < s.min_buffer_bytes)
    {
      error = clib_error_create ("max-size >= min-size");
      goto done;
    }

  if (! sub_input_given)
    {
      error = clib_error_create ("no buffer data given");
      goto done;
    }

  if (s.node_index == ~0)
    {
      error = clib_error_create ("output interface or node not given");
      goto done;
    }

  {
    pg_node_t * n;

    if (s.node_index < vec_len (pg->nodes))
      n = pg->nodes + s.node_index;
    else
      n = 0;

    if (n && n->unformat_edit
	&& unformat_user (&sub_input, n->unformat_edit, &s))
      ;

    else if (! unformat_user (&sub_input, unformat_pg_payload, &s))
      {
	error = clib_error_create
	  ("failed to parse buffer data from `%U'",
	   format_unformat_error, &sub_input);
	goto done;
      }
  }

  pg_stream_add (pg, &s);
  return 0;

 done:
  pg_stream_free (&s);
  unformat_free (&sub_input);
  return error;
}

static VLIB_CLI_COMMAND (new_stream_cli) = {
  .name = "new",
  .function = new_stream,
  .parent = &vlib_cli_pg_command,
  .short_help = "Create packet generator stream",
  .long_help =
  "Create packet generator stream\n"
  "\n"
  "Arguments:\n"
  "\n"
  "name STRING          sets stream name\n"
  "interface STRING     interface for stream output \n"
  "node NODE-NAME       node for stream output\n"
  "data STRING          specifies buffer data\n",
};

static clib_error_t *
del_stream (vlib_main_t * vm,
	    unformat_input_t * input,
	    vlib_cli_command_t * cmd)
{
  pg_main_t * pg = &pg_main;
  u32 i;
  
  if (! unformat (input, "%U",
		  &unformat_hash_vec_string, pg->stream_index_by_name, &i))
    return clib_error_create ("expected stream name `%U'",
			      format_unformat_error, input);

  pg_stream_del (pg, i);
  return 0;
}

static VLIB_CLI_COMMAND (del_stream_cli) = {
  .name = "delete",
  .function = del_stream,
  .parent = &vlib_cli_pg_command,
  .short_help = "Delete stream with given name",
};

static clib_error_t *
change_stream_parameters (vlib_main_t * vm,
			  unformat_input_t * input,
			  vlib_cli_command_t * cmd)
{
  pg_main_t * pg = &pg_main;
  pg_stream_t * s;
  u32 stream_index = ~0;

  if (unformat (input, "%U", unformat_hash_vec_string,
		pg->stream_index_by_name, &stream_index))
    ;
  else
    return clib_error_create ("unknown input `%U'",
			      format_unformat_error, input);

  s = pool_elt_at_index (pg->streams, stream_index);

  while (unformat_check_input (input) != UNFORMAT_END_OF_INPUT)
    {
      f64 x;

      if (unformat (input, "limit %f", &x))
	s->n_buffers_limit = x;

      else if (unformat (input, "rate %f", &x))
	s->rate_buffers_per_second = x;

      else
	return clib_error_create ("unknown input `%U'",
				  format_unformat_error, input);
    }

  return 0;
}

static VLIB_CLI_COMMAND (change_stream_parameters_cli) = {
  .name = "configure",
  .short_help = "Change packet generator stream parameters",
  .function = change_stream_parameters,
  .parent = &vlib_cli_pg_command,
};

/* Dummy function to get us linked in. */
void vlib_pg_cli_reference (void) {}
