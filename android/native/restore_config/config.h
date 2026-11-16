#include "third_party/restore_config/config.h"

/* The upstream generated header selects Darwin socket extensions. */
#undef HAVE_SO_NOSIGPIPE
#undef HAVE_NET_IF_DL_H
#define HAVE_NETPACKET_PACKET_H 1
#define HAVE_PPOLL 1
