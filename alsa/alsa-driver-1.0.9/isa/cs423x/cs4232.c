#include "../../alsa-kernel/isa/cs423x/cs4232.c"
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#ifndef __isapnp_now__
#include "cs4232.isapnp"
#endif
#endif
