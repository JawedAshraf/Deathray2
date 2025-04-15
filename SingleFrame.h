/* Deathray2 - An Avisynth plug-in filter for spatial/temporal non-local means de-noising.
 *
 * version 1.00
 *
 * Copyright 2015, Jawed Ashraf - Deathray@cupidity.f9.co.uk
 */

#ifndef _SINGLE_FRAME_
#define _SINGLE_FRAME_

#include <CL/cl.h>
#include "CLKernel.h"
#include "FilterFrame.h"

enum result;

class SingleFrame : public FilterFrame
{
public:
    SingleFrame();
    
    // Init
    // Setup the static source and destination buffers on the device
    // and configure the kernel and its arguments, which do not
    // change over the duration of clip processing.
    result Init(
        const   int     &device_id,     // device used for filtering
        const   int     &width,         // width of frame in pixels
        const   int     &height,        // height of frame in pixels
        const   int     &src_pitch,     // length in memory of a row of pixels in source buffer
        const   int     &dst_pitch,     // length in memory of a row of pixels in destination buffer
        const   float   &h,             // NLM filtering strength
        const   int     &sample_expand, // factor of radius of 3 to use for sampling
        const   int     &linear,        // TODO delete
        const   int     &correction,    // TODO delete
        const   int     &balanced);     // TODO float for bias: shadows or highlights
                                        
    // CopyTo
    // Copy the plane from host to device.
    result CopyTo(
        const unsigned char *source);   // host buffer to be copied to device

    // Execute
    // Perform NLM computation.
    result Execute() override;

private:

    // InitBuffers
    // Create the source, destination and alpha buffers
    result InitBuffers(
        const int &sample_expand);      // factor of radius of 3 to use for sampling

    // InitKernels
    // Configure global arguments for the filter, sort and initialise kernels,
    // arguments that won't change over the duration of clip processing.
    result InitKernels(
        const int &sample_expand,       // factor of radius of 3 to use for sampling
        const int &linear,              // TODO delete
        const int &correction,          // TODO delete
        const int &balanced);           // TODO float for bias: shadows or highlights

    // InitFilterKernel
    // Configure global arguments for the filter and sort kernels,
    // arguments that won't change over the duration of clip processing.
    result InitFilterKernel(
        const int &sample_expand,       // factor of radius of 3 to use for sampling
        const int &linear,              // TODO delete
        const int &correction,          // TODO delete
        const int &balanced);           // TODO float for bias: shadows or highlights

    // InitSortKernel
    // Configure the sort kernel which takes identifies the best samples
    // in the alpha set and uses them to produce the final filtered pixel.
    result InitSortKernel(
        const int &linear);             // TODO delete

    // InitInitialiseKernel
    // Configure the kernel that zeroes the alpha set before each invocation
    // of filtering and sorting.
    result InitInitialiseKernel();

    ClKernel filter_    ;   // non local means kernel executed on device
    ClKernel sort_      ;   // sort kernel executed on device
    ClKernel initialise_;   // zeroing kernel executed on device - TODO delete
};

#endif // _SINGLE_FRAME_