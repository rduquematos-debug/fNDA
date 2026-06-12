#ifndef gsp_obj_h
#define gsp_obj_h

#include "../os_compat.h"

// Create OBJGPU from GA104Device and allocate sub-objects.
// Returns 0 on success, NV_ERR_NO_MEMORY on failure.
NV_STATUS gsp_obj_init(OBJGPU *pGpu, GA104Device *device);

// Destroy OBJGPU and free all sub-objects.
void gsp_obj_destroy(OBJGPU *pGpu);

#endif
