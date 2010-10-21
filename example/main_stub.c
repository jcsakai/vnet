#include <vlib/vlib.h>
#include <vlib/unix/unix.h>
#include <vlib/unix/pci.h>
#include <vnet/pg/pg.h>
#include <vnet/ethernet/ethernet.h>
#include <vnet/ip/ip.h>

static clib_error_t *
vnet_main_init (vlib_main_t * vm)
{
  clib_error_t * error = 0;

  if ((error = vlib_call_init_function (vm, pg_init)))
    return error;
  if ((error = vlib_call_init_function (vm, ip_main_init)))
    return error;
  if ((error = vlib_call_init_function (vm, ethernet_init)))
    return error;
  if ((error = vlib_call_init_function (vm, ethernet_arp_init)))
    return error;

  /* FIXME */
  if ((error = vlib_call_init_function (vm, ixge_init)))
    return error;

  vlib_unix_cli_set_prompt ("VNET: ");

  return error;
}

static VLIB_INIT_FUNCTION (vnet_main_init);

int main (int argc, char * argv[])
{
  return vlib_unix_main (argc, argv);
}
