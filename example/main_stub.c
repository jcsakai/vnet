#include <vlib/vlib.h>
#include <vlib/unix/unix.h>
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

    return error;
}

static VLIB_INIT_FUNCTION (vnet_main_init);

int main (int argc, char * argv[])
{
  return vlib_unix_main (argc, argv);
}
