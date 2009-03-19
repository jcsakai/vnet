#include <vlib/vlib.h>
#include <vlib/unix/unix.h>
#include <vnet/pg/pg.h>

int main (int argc, char * argv[])
{
  return vlib_unix_main (argc, argv);
}
