/* Deathray2 - An Avisynth plug-in filter for spatial/temporal non-local means de-noising.
 *
 * version 1.00
 *
 * Copyright 2015, Jawed Ashraf - Deathray@cupidity.f9.co.uk
 */

__kernel void Initialise( // TODO delete
    __global    uint    *A,
    const       uint    x) {

    int pos = get_global_id(0);
    A[pos] = x;
}

// ReadPixel4
// Reads 4 pixels from an image texture. 
// For unorm8 formatted pixels, a 4-pixel read is the smallest
// that can be done
float4 ReadPixel4(
    read_only   image2d_t   plane,          // input plane
    const       int2        coordinates) {  // coordinates of the left-most of the four pixels to be read

    const sampler_t frame = CLK_NORMALIZED_COORDS_FALSE |
                            CLK_ADDRESS_CLAMP | // TODO try CLK ADDRESS REPEAT
                            CLK_FILTER_NEAREST;

    return read_imagef(plane, frame, coordinates);
}

void WritePixel4(
    const       float4      pixel,          // four contiguous pixels to be written            
    const       int2        coordinates,    // coordinates of the left-most pixel (as vec4 coordinates)
    const       int         linear,         // 1 means treat the pixel as being in linear space and convert back to gamma space
    write_only  image2d_t   plane) {        // output plane

    write_imagef(plane, coordinates, pixel);
}


// GetLocalID
// Returns the coordinates of the work item within the work group
int2 GetLocalID() {
    return (int2)(get_local_id(0), get_local_id(1));
}

// GetRegionCoordinates8x2
// The 8x2 tile's top-left position in the region
int2 GetRegionCoordinates8x2() {
    return (int2)(get_group_id(0) << 3, get_group_id(1) << 1);
}

// GetCoordinates8x2
// The 8x2 tile's top-left position in the plane
int2 GetCoordinates8x2(
    const int2 top_left) {  // coordinates of the top left corner of the region to be filtered

    return top_left + GetRegionCoordinates8x2();
}

// GetLocalCoordinates
// Coordinates within the workgroup of the pixel being filtered
int2 GetLocalCoordinates() {
    return (int2)(get_local_id(1) & 7, get_local_id(1) >> 3);
}

// GetTargetCoordinates
// Coordinates of the pixel being filtered
int2 GetTargetCoordinates(
    const int2 top_left) {  // coordinates of the top left corner of the region to be filtered

    return GetCoordinates8x2(top_left) + GetLocalCoordinates();
}

// GetRegionBaseAddress
// Base address within the region_alpha buffer for all alpha samples
int GetRegionBaseAddress(
    const int width,            // width in pixels
    const int alpha_set_size) { // number of weight/pixel pairs per target pixel

    const int2 region_coordinates = GetRegionCoordinates8x2() + GetLocalCoordinates();
    const int region_pixel_base = mad24(region_coordinates.y, width, region_coordinates.x);
    return mul24(region_pixel_base, alpha_set_size);
}

// GetFilterRegionBaseAddress
// Base address within the region_alpha buffer for all alpha samples
// with an offset for the cooperator
int GetFilterRegionBaseAddress(
    const int width,            // width in pixels
    const int alpha_set_size) { // number of weight/pixel pairs per target pixel

    const int cooperator_id = get_local_id(0);
    return GetRegionBaseAddress(width, alpha_set_size) + cooperator_id;
}

// GetCoord4
// Converts coordinates for a scalar into coordinates for a vec4
int2 GetCoord4(
    int2 coordinates) {
    return (int2)(coordinates.x >> 2, coordinates.y);
}

// ReadPixel
// Returns a single pixel
float ReadPixel( // TODO delete?
    read_only   image2d_t   plane,          // input plane
    const       int2        coordinates,    // scalar coordinates of pixel
    const       int         linear) {       // TODO delete

    int2 coordinates4 = GetCoord4(coordinates);
    float4 input_pixel = ReadPixel4(plane, coordinates4);

    float chosen_pixel;
    int select = coordinates.x & 3;
    switch (select) {
        case 0: 
            chosen_pixel = input_pixel.x;
            break;
        case 1:
            chosen_pixel = input_pixel.y;
            break;
        case 2:
            chosen_pixel = input_pixel.z;
            break;
        case 3:
            chosen_pixel = input_pixel.w;
    }
    return chosen_pixel;
}

// ReadPixel1x2
// Returns a column of 2 pixels given the coordinates of the top
float2 ReadPixel1x2(
    read_only   image2d_t   plane,          // input plane
    const       int2        coordinates,    // scalar coordinates of pixel
    const       int         linear) {       // TODO delete

    int2 coordinates4 = GetCoord4(coordinates);
    float4 input_pixel_0 = ReadPixel4(plane, coordinates4);
    coordinates4.y += 1;
    float4 input_pixel_1 = ReadPixel4(plane, coordinates4);

    float2 column;
    int select = coordinates.x & 3;
    switch (select) {
        case 0: 
            column.x = input_pixel_0.x;
            column.y = input_pixel_1.x;
            break;
        case 1:
            column.x = input_pixel_0.y;
            column.y = input_pixel_1.y;
            break;
        case 2:
            column.x = input_pixel_0.z;
            column.y = input_pixel_1.z;
            break;
        case 3:
            column.x = input_pixel_0.w;
            column.y = input_pixel_1.w;
    }

    return column;
}

// ReadPixel4x2
// Returns a 4x2 block of pixels given the coordinates of the top-left
float8 ReadPixel4x2(
    read_only   image2d_t   plane,          // input plane
    const       int2        coordinates,    // scalar coordinates of pixel
    const       int         linear) {       // TODO delete

    // Since pixels are not aligned with the vec4 read arrangement, each work item
    // reads two vec4s and picks the subset of 4 pixels that are required.

    int2 coordinates4 = GetCoord4(coordinates);
    float4 input_pixel_00 = ReadPixel4(plane, coordinates4);
    coordinates4.y += 1;
    float4 input_pixel_10 = ReadPixel4(plane, coordinates4);

    coordinates4.x += 1;
    float4 input_pixel_11 = ReadPixel4(plane, coordinates4);
    coordinates4.y -= 1;
    float4 input_pixel_01 = ReadPixel4(plane, coordinates4);

    float8 pixel_block;

    int select = coordinates.x & 3;
    switch (select) {
        case 0: // TODO separate case 0 for a faster function that only does 2x ReadPixel4
            pixel_block.s0123 = input_pixel_00;
            pixel_block.s4567 = input_pixel_10;
            break;
        case 1:
            pixel_block.s012 = input_pixel_00.yzw;
            pixel_block.s456 = input_pixel_10.yzw;

            pixel_block.s3 = input_pixel_01.x;
            pixel_block.s7 = input_pixel_11.x;
            break;
        case 2:
            pixel_block.s01 = input_pixel_00.zw;
            pixel_block.s45 = input_pixel_10.zw;

            pixel_block.s23 = input_pixel_01.xy;
            pixel_block.s67 = input_pixel_11.xy;
            break;
        case 3:
            pixel_block.s0 = input_pixel_00.w;
            pixel_block.s4 = input_pixel_10.w;

            pixel_block.s123 = input_pixel_01.xyz;
            pixel_block.s567 = input_pixel_11.xyz;
    }
    return pixel_block;
}

// GetRadius
// Returns a radius that determines the size of the set of samples.
// Radius is 3, or a multiple of 3.
int GetRadius(
    int sample_expand) { // must be 1 or more

    return (sample_expand << 1) + sample_expand;
}

// GetSetSide
// Returns the length of the side of a square that defines the 
// set of sample pixels.
int GetSetSide(
    int radius) {    
    return 1 + (radius << 1);
}

// GetEighthSequenceNumber
// Returns the starting sequence number for a work item
// which is one of the 8 work items that weights a sample pixel
int GetEighthSequenceNumber() {

    // There are 8 sequences within a work group, each having
    // work items with start numbers of between 0 and 7.

    return get_local_id(0);
}

// WrapSetCoordinate
// Adjusts the coordinates so that they remain within the bounds
// of the set. Also, if the target pixel coordinate should be skipped
// then do the skip
int2 WrapSetCoordinate (
    int2    target,         // target pixel coordinates
    int2    sample,         // sample pixel coordinates
    int     radius,         // spatial sampling radius
    int2    set_max,        // coordinates of bottom-right corner of sample cache
    int     skip_target) {  // 1 if target pixel should be skipped  

    while (true) {
        if (sample.x > set_max.x) {
            sample.x -= GetSetSide(radius);
            ++sample.y;
        } else {
            if ((sample.x == target.x) && (sample.y == target.y) && skip_target) {
                sample.x += 8;
            } else {
                break;
            }
        }
    }
    if (sample.y > set_max.y) {
        sample = set_max;
    }
    return sample;
}

// GetSampleStartCoordinates
// Returns the coordinates of the initial sample. All 8 collaborating work items 
// have distinct starting positions near the top-left of the set of samples
int2 GetSampleStartCoordinates(
    int2    target,             // target pixel coordinates
    int     radius,             // spatial sampling radius
    int2    set_max,            // coordinates of bottom-right corner of sample cache
    int     sequence_number) {  // per work item linear id in collaborating set of work items

    const int2 start = set_max - (int2)(GetSetSide(radius) - sequence_number - 1, GetSetSide(radius) - 1);
    return WrapSetCoordinate(target, start, radius, set_max, 0);
}

// GetSetMax
// Returns the bottom-right coordinates for the sample set.
// Bottom-right is adjusted to stay within image bounds. This adjustment
// caters both for the window size (radius 3) and the sample set size
// (radius defined by the radius parameter)
int2 GetSetMax(
    int2    target,     // coordinates of pixel being filtered
    int2    image_max,  // width,height of image (1-based)
    int     radius) {   // radius of the set of samples


    // set max starts without adjustment
    int2 set_max = target + (int2)(radius, radius);

    // bottom right adjusted for image size
    int2 bottom_right = image_max - (int2)(4, 4);
    set_max.x = (set_max.x > bottom_right.x) ? bottom_right.x : set_max.x;
    set_max.y = (set_max.y > bottom_right.y) ? bottom_right.y : set_max.y;

    // top left is adjusted for window size & length of the side of the set
    int set_side = GetSetSide(radius);
    int2 top_left = (int2)(2, 2) + (int2)(set_side, set_side);

    set_max.x = (set_max.x < top_left.x) ? top_left.x: set_max.x;
    set_max.y = (set_max.y < top_left.y) ? top_left.y: set_max.y;

    return set_max;
}

// GetSampleCacheBaseCoordinates
// Returns the input plane coordinates at O, in the following picture of the top
// half + 1 row of the sample cache:
//
//    Ooooooo.................................
//    ooooooo.................................
//    ooooooo.................................
//    oooxooo.................................
//    ooooooo.................................
//    ooooooo.................................
//    ooooooo.................................
//    ........................................
//    ........................................
//    ........................................
//    ........................................
//    ........................................
//    ........................................
//    ........................................
//    ........................................
//    ...............Tttttttt.................
//    0123456789012345678901234567890123456789
//
// O = coordinates returned
// o = sample window
// x = sample pixel
// T = target pixel
// t = 7 other target pixels in the 8x2 tile
int2 GetSampleCacheBaseCoordinates(
    const   int2    top_left,   // coordinates of the top left corner of the region to be filtered
            int2    image_max) {// width,height of image (1-based)

    // Due to the borders of the input plane, the relative positions of O and T
    // vary (T may not even lie within the bounds of the cache). 
    //
    // Regardless of the value of x specified by the user, the cache size is 
    // fixed, and based on x == 4.
    //
    // Additionally, the base is the same for all work items - it does not
    // vary with target pixel position within the 8x2 tile.
    //
    // TODO need to be careful about what happens when x != 4 and T is near bottom-right

    int radius = GetRadius(4);
    int2 target = GetCoordinates8x2(top_left);
    int2 set_max = GetSetMax(target, image_max, radius);
    int2 base = set_max - (int2)(GetSetSide(radius) - 1, GetSetSide(radius) - 1);
    base -= (int2)(3, 3);
    return base;
}

// GetSampleOffset
// Returns the coordinate offset required to translate coordinates
// in the image plane into coordinates within the sample cache
int2 GetSampleOffset(
    int2 sample,                            // coordinates of sample
    int2 sample_cache_base_coordinates) {   // coordinates of sample cache's top left corner

    return sample - sample_cache_base_coordinates;
}

// GetStrideCount
// Returns the count of strides that each work item
// takes to process the entire set within a plane
int GetStrideCount(
    int radius) {    // radius of the set of samples

    return (GetSetSide(radius) * GetSetSide(radius)) >> 3;
}

// NextStride
// Work item takes its next stride, respecting the right edge of the set of
// samples (e.g. for a 19x19 set of samples there are 45 strides of 8)
int2 NextStride(
    int2    target,         // coordinates of pixel being filtered
    int2    sample,         // coordinates of the sample that has just been processed
    int     radius,         // radius of the set of samples
    int2    set_max,        // coordinates of the bottom-right of the set of samples
    int     skip_target) {  // when set do not sample at the target pixel

    int2 next = (int2)(sample.x + 8, sample.y);
    next = WrapSetCoordinate(target, next, radius, set_max, skip_target);
    return next;
}

// IsMaster
// Returns 1 if work item is a master, or 0 for slave.
//
// A master work item is defined as one of the 4 work items in a work group
// that is responsible for reading/writing the 16 target pixels processed
// by the work group. Each of the master work items handles a contiguous
// subset of 4 of these target pixels.
int IsMaster() {
    int2 local_id = GetLocalID();
    return ((local_id.x == 0) 
            && ((local_id.y == 0) 
                || (local_id.y == 4)
                || (local_id.y == 8)
                || (local_id.y == 12)
                )
            )
            ? 1 
            : 0;
}

// PopulateSampleCache32x32
// 32x32 subset of the sample cache is constructed based on pixels with origin 
// at (-15, -15) from the work-group's left-most pixel.
void PopulateSampleCache32x32(
    read_only   image2d_t   plane,              // input plane
    const       int         linear,             // process plane in linear space instead of gamma space - TODO delete
    const       int2        sample_cache_base,  // coordinates in plane for top left of the cache
    local       float       *sample_cache) {    // caches pixels around the 8x2 tile of target pixels

    // Each work item is responsible for 8 pixels, arranged as a 4x2 block.

    int2 coordinates = sample_cache_base;
    int2 local_pos = GetLocalID();
    int2 offset = (int2)(local_pos.x << 2, local_pos.y << 1);
    coordinates += offset;

    float8 pixel_block = ReadPixel4x2(plane, coordinates, linear);

    int cache_address = mul24(local_pos.y, 20) + local_pos.x;
    vstore4(pixel_block.s0123, cache_address, sample_cache);
    cache_address += 10;
    vstore4(pixel_block.s4567, cache_address, sample_cache);
    barrier(CLK_LOCAL_MEM_FENCE);
}

// PopulateSampleCache8x32
// 8x32 subset of the sample cache is constructed based on pixels with origin 
// at (16, -15) from the work-group's left-most pixel.
void PopulateSampleCache8x32(
    read_only   image2d_t   plane,              // input plane
    const       int         linear,             // process plane in linear space instead of gamma space
    const       int2        sample_cache_base,  // coordinates in plane for top left of the cache
    local       float       *sample_cache) {    // caches pixels around the 8x2 tile of target pixels

    // Each work item is responsible for vertical strip of 4 pixels, arranged as a 1x4 block.

    int2 coordinates = sample_cache_base;
    int2 local_pos = GetLocalID();
    int2 offset = (int2)(local_pos.x + 32, local_pos.y << 1);
    coordinates += offset;

    float2 column = ReadPixel1x2(plane, coordinates, linear);

    int cache_address = mul24(offset.y, 40) + offset.x;
    sample_cache[cache_address] = column.x;
    cache_address += 40;
    sample_cache[cache_address] = column.y;
    barrier(CLK_LOCAL_MEM_FENCE);
}

// PopulateSampleCache
// 40x32 cache of pixels with origin at (-15, -15) from the work-group's
// left-most target pixel.
void PopulateSampleCache(
    read_only   image2d_t   plane,                          // input plane
    const       int         linear,                         // process plane in linear space instead of gamma space
                int2        sample_cache_base_coordinates,  // coordinates in plane for top left of the cache
    local       float       *sample_cache) {                // caches pixels around the 8x2 tile of target pixels

    // The width of the cache accounts for the 8-wide strip of pixels processed 
    // by the work group. The cache only needs to be 39 wide, but 
    // it's simpler to schedule 128 work items across a size of 40x32.
    //
    // The cache is populated in two sections: 
    // - 32x32 with each work item handling 8 pixels
    PopulateSampleCache32x32(plane, linear, sample_cache_base_coordinates, sample_cache);

    // - 8x32 with each work item handling 2 pixels.
    PopulateSampleCache8x32(plane, linear, sample_cache_base_coordinates, sample_cache);
}

// PopulateTargetCache
// 16x8 window of pixels centred upon the 8-wide strip of target pixels
// is read from memory into local memory
//
// Each work item reads two pixels. y, ordinate, corresponds with the row
// of target pixels.
//
// The cache is 2 too wide.
void PopulateTargetCache(
    read_only   image2d_t   plane,          // input plane
    const       int2        top_left,       // coordinates of the top left corner of the region to be filtered
    const       int         linear,         // process plane in linear space instead of gamma space
    local       float       *target_cache) {// caches pixels around the 8x2 tile of target pixels

    const int2 local_pos = GetLocalID();
    const int2 offset_top_left = GetCoordinates8x2(top_left) + (int2)(-3, -3);
    const int2 coordinates = offset_top_left + (int2)(get_local_id(1), get_local_id(0));

    float pixel = ReadPixel(plane, coordinates, linear);

    target_cache[(get_local_id(0) << 4) + get_local_id(1)] = pixel;

    barrier(CLK_LOCAL_MEM_FENCE);
}