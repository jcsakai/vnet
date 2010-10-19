#include <vnet/devices/xge/xge.h>

u8 * format_xge_phy_id (u8 * s, va_list * args)
{
    xge_phy_id_t id = va_arg (*args, int);
    char * t = 0;

    switch (xge_phy_id_oui (id)) {
#define _(f,x) case XGE_PHY_ID_##f: t = #f; break;
	foreach_xge_phy_id
#undef _
    }
    if (t)
	return format (s, "%s, version 0x%x", t, id & 0x3ff);
    else
	return format (s, "unknown 0x%x", id);
}

