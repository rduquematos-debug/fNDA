#ifndef gsp_main_h
#define gsp_main_h

#include "../os_compat.h"

// Initialize GSP subsystem: allocate boot args, setup queues, populate WPR meta.
// Called from GA104Device::start() after BAR mapping.
NV_STATUS gsp_init(OBJGPU *pGpu);

// Boot the GSP: SEC2 → GSP-RM → init RPCs.
NV_STATUS gsp_boot(OBJGPU *pGpu);

// Shutdown GSP subsystem.
void gsp_shutdown(OBJGPU *pGpu);

#endif
