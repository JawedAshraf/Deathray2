/* Deathray2 - An Avisynth plug-in filter for spatial/temporal non-local means de-noising.
 *
 * version 1.00
 *
 * Copyright 2015, Jawed Ashraf - Deathray@cupidity.f9.co.uk
 */


#include "result.h"
#include "CLKernel.h"
#include "device.h"

extern cl_int       g_last_cl_error;
extern cl_context   g_context;

#include "FilterFrame.h"

result FilterFrame::CopyFrom(
    unsigned char   *dest,                              
    cl_event        *returned) {

    return g_devices[device_id_].buffers_.CopyFromPlaneAsynch(dest_plane_,
                                                              width_,
                                                              height_, 
                                                              dst_pitch_, 
                                                              NULL, 
                                                              returned,
                                                              dest);
}
