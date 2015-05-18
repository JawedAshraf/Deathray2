/* Deathray2 - An Avisynth plug-in filter for spatial/temporal non-local means de-noising.
 *
 * version 1.00
 *
 * Copyright 2015, Jawed Ashraf - Deathray@cupidity.f9.co.uk
 */

#ifndef _FILTERFRAME_H_
#define _FILTERFRAME_H_

#include <CL/cl.h>

enum result;

// FilterFrame
// Base class for SingleFrame and MultiFrame
class FilterFrame
{
public:
    FilterFrame() {}
    ~FilterFrame() {}
    
    // Execute
    // Perform NLM computation.
    virtual result Execute() = 0;

    // CopyFrom
    // Copy the plane of filtered pixels from the device
    // to the destination buffer on the host.
    result CopyFrom(
        unsigned char   *dest,
        cl_event        *returned);

protected:
    int device_id_      ;   // device used to execute the filter kernels
    int width_          ;   // width of plane's content
    int height_         ;   // height of plane's content
    int src_pitch_      ;   // host plane format allows each row to be potentially longer than width_
    int dst_pitch_      ;   // host plane format allows each row to be potentially longer than width_
    float h_            ;   // strength of noise reduction
    int source_plane_   ;   // dedicated buffer for source plane
    int dest_plane_     ;   // dedicated buffer for destination plane
    int alpha_          ;   // alpha weight/pixel pairs packed as single uints
    int region_width_   ;   // width of region to be filtered by a single kernel invocation
    int region_height_  ;   // height of region to be filtered by a single kernel invocation
    int alpha_set_size_ ;   // count of all weight/pixel pairs that will be generated during filtering
    cl_command_queue cq_;   // synchronous queue of device commands

};

#endif // _FILTERFRAME_H_

