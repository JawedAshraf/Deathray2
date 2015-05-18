/* Deathray2 - An Avisynth plug-in filter for spatial/temporal non-local means de-noising.
 *
 * version 1.00
 *
 * Copyright 2015, Jawed Ashraf - Deathray@cupidity.f9.co.uk
 */

#ifndef _UTIL_H_
#define _UTIL_H_

#include <string>

using namespace std;

#include "resource.h"

enum result;


// ByPowerOf2
// Rounds x up to a multiple of the power of 2 specified.
// 0 is always rounded up, i.e. return value is never 0.
// Powers of 2 more than 30 are floored at 30
int ByPowerOf2(unsigned int x, int powerOf2) ;

// FixCALBufferSizeFault
// A fault in CAL (?) means certain dimensions for a buffer
// are unusable. 
// e.g. 1024, 1280, 1536, 1792 and 2048 are OK, but 2304 is not.
// Instead 2560 is the next valid size.
int FixCALBufferSizeFault(const int &length) ;

// GetFrameDimensions
// Converts the required width and height in pixels into valid width 
// and height values.
// 
// A frame has four pixels, horizontally, packed into each element.
//
// Algorithms may also require blocked-alignment for both width 
// and height. The block alignment should be specified as a 
// power of 2. The block alignment is specified in pixels.
// e.g. a 64x8 alignment is specified as width_power_of_2 = 6
// and height_power_of_2 = 3.
//
// CAL has a bug where 2D image buffers cannot be sized freely.
// Instead buffer dimensions must have specific sizes. A workaround
// for this bug is included in the returned values.
void GetFrameDimensions(
    const int &width,                    // required width in pixels
    const int &height,                    // required height
    const int &width_power_of_2,        // power of 2 that specifies block size horizontally
    const int &height_power_of_2,        // power of 2 that specifies block size vertically
          int *device_width,            // computed width in byte4s
          int *device_height);            // computed height in rows of byte4s

// GetAlphaSize
// Converts integer alpha size to string
string GetAlphaSize(
    const int alpha_size);

// GetAlphaSetSize
// Returns the count of elements required for a single pixel's alpha buffer storage
int GetAlphaSetSize(
    const    int        &temporal_radius,
    const    int        &sample_expand);

// GetAlphaBufferSize
// Returns the size in elements for a buffer required to hold alpha weights and pixel values
// packed as pairs into single elements.
//
// The alpha buffer holds all weight/pixel pairs produced by filtering for a region of a single plane.
// On the device each element is defined as unsigned int, which is 32 bits. The weight is stored
// as the most significant 24 bits and the pixel is stored as the least significant 8 bits.
//
// The weight is a float in the range 0.f to 1.f that is scaled by 16777215.
int GetAlphaBufferSize(
    const    int        &temporal_radius,
    const    int        &region_width, 
    const    int        &region_height,
    const    int        &sample_expand);

// GetSourceFromResource
// Returns a string from a single OpenCL kernel source file.
//
// The resource is specified as one of the DEFINEd resources 
// listed in resource.h.
result GetSourceFromResource(int resource_id, string *source);

#endif // _UTIL_H_
