#include "opencl_deathray2.h"

const char *rc_util_cl =       "\n\
__kernel void Initialise( // TODO delete\n\
    __global    uint    *A,\n\
    const       uint    x) {\n\
\n\
    int pos = get_global_id(0);\n\
    A[pos] = x;\n\
}\n\
\n\
// ReadPixel4\n\
// Reads 4 pixels from an image texture.\n\
// For unorm8 formatted pixels, a 4-pixel read is the smallest\n\
// that can be done\n\
float4 ReadPixel4(\n\
    read_only   image2d_t   plane,          // input plane\n\
    const       int2        coordinates) {  // coordinates of the left-most of the four pixels to be read\n\
\n\
    const sampler_t frame = CLK_NORMALIZED_COORDS_FALSE |\n\
                            CLK_ADDRESS_CLAMP | // TODO try CLK ADDRESS REPEAT\n\
                            CLK_FILTER_NEAREST;\n\
\n\
    return read_imagef(plane, frame, coordinates);\n\
}\n\
\n\
void WritePixel4(\n\
    const       float4      pixel,          // four contiguous pixels to be written\n\
    const       int2        coordinates,    // coordinates of the left-most pixel (as vec4 coordinates)\n\
    const       int         linear,         // 1 means treat the pixel as being in linear space and convert back to gamma space\n\
    write_only  image2d_t   plane) {        // output plane\n\
\n\
    write_imagef(plane, coordinates, pixel);\n\
}\n\
\n\
\n\
// GetLocalID\n\
// Returns the coordinates of the work item within the work group\n\
int2 GetLocalID() {\n\
    return (int2)(get_local_id(0), get_local_id(1));\n\
}\n\
\n\
// GetRegionCoordinates8x2\n\
// The 8x2 tile's top-left position in the region\n\
int2 GetRegionCoordinates8x2() {\n\
    return (int2)(get_group_id(0) << 3, get_group_id(1) << 1);\n\
}\n\
\n\
// GetCoordinates8x2\n\
// The 8x2 tile's top-left position in the plane\n\
int2 GetCoordinates8x2(\n\
    const int2 top_left) {  // coordinates of the top left corner of the region to be filtered\n\
\n\
    return top_left + GetRegionCoordinates8x2();\n\
}\n\
\n\
// GetLocalCoordinates\n\
// Coordinates within the workgroup of the pixel being filtered\n\
int2 GetLocalCoordinates() {\n\
    return (int2)(get_local_id(1) & 7, get_local_id(1) >> 3);\n\
}\n\
\n\
// GetTargetCoordinates\n\
// Coordinates of the pixel being filtered\n\
int2 GetTargetCoordinates(\n\
    const int2 top_left) {  // coordinates of the top left corner of the region to be filtered\n\
\n\
    return GetCoordinates8x2(top_left) + GetLocalCoordinates();\n\
}\n\
\n\
// GetRegionBaseAddress\n\
// Base address within the region_alpha buffer for all alpha samples\n\
int GetRegionBaseAddress(\n\
    const int width,            // width in pixels\n\
    const int alpha_set_size) { // number of weight/pixel pairs per target pixel\n\
\n\
    const int2 region_coordinates = GetRegionCoordinates8x2() + GetLocalCoordinates();\n\
    const int region_pixel_base = mad24(region_coordinates.y, width, region_coordinates.x);\n\
    return mul24(region_pixel_base, alpha_set_size);\n\
}\n\
\n\
// GetFilterRegionBaseAddress\n\
// Base address within the region_alpha buffer for all alpha samples\n\
// with an offset for the cooperator\n\
int GetFilterRegionBaseAddress(\n\
    const int width,            // width in pixels\n\
    const int alpha_set_size) { // number of weight/pixel pairs per target pixel\n\
\n\
    const int cooperator_id = get_local_id(0);\n\
    return GetRegionBaseAddress(width, alpha_set_size) + cooperator_id;\n\
}\n\
\n\
// GetCoord4\n\
// Converts coordinates for a scalar into coordinates for a vec4\n\
int2 GetCoord4(\n\
    int2 coordinates) {\n\
    return (int2)(coordinates.x >> 2, coordinates.y);\n\
}\n\
\n\
// ReadPixel\n\
// Returns a single pixel\n\
float ReadPixel( // TODO delete?\n\
    read_only   image2d_t   plane,          // input plane\n\
    const       int2        coordinates,    // scalar coordinates of pixel\n\
    const       int         linear) {       // TODO delete\n\
\n\
    int2 coordinates4 = GetCoord4(coordinates);\n\
    float4 input_pixel = ReadPixel4(plane, coordinates4);\n\
\n\
    float chosen_pixel;\n\
    int select = coordinates.x & 3;\n\
    switch (select) {\n\
        case 0:\n\
            chosen_pixel = input_pixel.x;\n\
            break;\n\
        case 1:\n\
            chosen_pixel = input_pixel.y;\n\
            break;\n\
        case 2:\n\
            chosen_pixel = input_pixel.z;\n\
            break;\n\
        case 3:\n\
            chosen_pixel = input_pixel.w;\n\
    }\n\
    return chosen_pixel;\n\
}\n\
\n\
// ReadPixel1x2\n\
// Returns a column of 2 pixels given the coordinates of the top\n\
float2 ReadPixel1x2(\n\
    read_only   image2d_t   plane,          // input plane\n\
    const       int2        coordinates,    // scalar coordinates of pixel\n\
    const       int         linear) {       // TODO delete\n\
\n\
    int2 coordinates4 = GetCoord4(coordinates);\n\
    float4 input_pixel_0 = ReadPixel4(plane, coordinates4);\n\
    coordinates4.y += 1;\n\
    float4 input_pixel_1 = ReadPixel4(plane, coordinates4);\n\
\n\
    float2 column;\n\
    int select = coordinates.x & 3;\n\
    switch (select) {\n\
        case 0:\n\
            column.x = input_pixel_0.x;\n\
            column.y = input_pixel_1.x;\n\
            break;\n\
        case 1:\n\
            column.x = input_pixel_0.y;\n\
            column.y = input_pixel_1.y;\n\
            break;\n\
        case 2:\n\
            column.x = input_pixel_0.z;\n\
            column.y = input_pixel_1.z;\n\
            break;\n\
        case 3:\n\
            column.x = input_pixel_0.w;\n\
            column.y = input_pixel_1.w;\n\
    }\n\
\n\
    return column;\n\
}\n\
\n\
// ReadPixel4x2\n\
// Returns a 4x2 block of pixels given the coordinates of the top-left\n\
float8 ReadPixel4x2(\n\
    read_only   image2d_t   plane,          // input plane\n\
    const       int2        coordinates,    // scalar coordinates of pixel\n\
    const       int         linear) {       // TODO delete\n\
\n\
    // Since pixels are not aligned with the vec4 read arrangement, each work item\n\
    // reads two vec4s and picks the subset of 4 pixels that are required.\n\
\n\
    int2 coordinates4 = GetCoord4(coordinates);\n\
    float4 input_pixel_00 = ReadPixel4(plane, coordinates4);\n\
    coordinates4.y += 1;\n\
    float4 input_pixel_10 = ReadPixel4(plane, coordinates4);\n\
\n\
    coordinates4.x += 1;\n\
    float4 input_pixel_11 = ReadPixel4(plane, coordinates4);\n\
    coordinates4.y -= 1;\n\
    float4 input_pixel_01 = ReadPixel4(plane, coordinates4);\n\
\n\
    float8 pixel_block;\n\
\n\
    int select = coordinates.x & 3;\n\
    switch (select) {\n\
        case 0: // TODO separate case 0 for a faster function that only does 2x ReadPixel4\n\
            pixel_block.s0123 = input_pixel_00;\n\
            pixel_block.s4567 = input_pixel_10;\n\
            break;\n\
        case 1:\n\
            pixel_block.s012 = input_pixel_00.yzw;\n\
            pixel_block.s456 = input_pixel_10.yzw;\n\
\n\
            pixel_block.s3 = input_pixel_01.x;\n\
            pixel_block.s7 = input_pixel_11.x;\n\
            break;\n\
        case 2:\n\
            pixel_block.s01 = input_pixel_00.zw;\n\
            pixel_block.s45 = input_pixel_10.zw;\n\
\n\
            pixel_block.s23 = input_pixel_01.xy;\n\
            pixel_block.s67 = input_pixel_11.xy;\n\
            break;\n\
        case 3:\n\
            pixel_block.s0 = input_pixel_00.w;\n\
            pixel_block.s4 = input_pixel_10.w;\n\
\n\
            pixel_block.s123 = input_pixel_01.xyz;\n\
            pixel_block.s567 = input_pixel_11.xyz;\n\
    }\n\
    return pixel_block;\n\
}\n\
\n\
// GetRadius\n\
// Returns a radius that determines the size of the set of samples.\n\
// Radius is 3, or a multiple of 3.\n\
int GetRadius(\n\
    int sample_expand) { // must be 1 or more\n\
\n\
    return (sample_expand << 1) + sample_expand;\n\
}\n\
\n\
// GetSetSide\n\
// Returns the length of the side of a square that defines the\n\
// set of sample pixels.\n\
int GetSetSide(\n\
    int radius) {\n\
    return 1 + (radius << 1);\n\
}\n\
\n\
// GetEighthSequenceNumber\n\
// Returns the starting sequence number for a work item\n\
// which is one of the 8 work items that weights a sample pixel\n\
int GetEighthSequenceNumber() {\n\
\n\
    // There are 8 sequences within a work group, each having\n\
    // work items with start numbers of between 0 and 7.\n\
\n\
    return get_local_id(0);\n\
}\n\
\n\
// WrapSetCoordinate\n\
// Adjusts the coordinates so that they remain within the bounds\n\
// of the set. Also, if the target pixel coordinate should be skipped\n\
// then do the skip\n\
int2 WrapSetCoordinate (\n\
    int2    target,         // target pixel coordinates\n\
    int2    sample,         // sample pixel coordinates\n\
    int     radius,         // spatial sampling radius\n\
    int2    set_max,        // coordinates of bottom-right corner of sample cache\n\
    int     skip_target) {  // 1 if target pixel should be skipped\n\
\n\
    while (true) {\n\
        if (sample.x > set_max.x) {\n\
            sample.x -= GetSetSide(radius);\n\
            ++sample.y;\n\
        } else {\n\
            if ((sample.x == target.x) && (sample.y == target.y) && skip_target) {\n\
                sample.x += 8;\n\
            } else {\n\
                break;\n\
            }\n\
        }\n\
    }\n\
    if (sample.y > set_max.y) {\n\
        sample = set_max;\n\
    }\n\
    return sample;\n\
}\n\
\n\
// GetSampleStartCoordinates\n\
// Returns the coordinates of the initial sample. All 8 collaborating work items\n\
// have distinct starting positions near the top-left of the set of samples\n\
int2 GetSampleStartCoordinates(\n\
    int2    target,             // target pixel coordinates\n\
    int     radius,             // spatial sampling radius\n\
    int2    set_max,            // coordinates of bottom-right corner of sample cache\n\
    int     sequence_number) {  // per work item linear id in collaborating set of work items\n\
\n\
    const int2 start = set_max - (int2)(GetSetSide(radius) - sequence_number - 1, GetSetSide(radius) - 1);\n\
    return WrapSetCoordinate(target, start, radius, set_max, 0);\n\
}\n\
\n\
// GetSetMax\n\
// Returns the bottom-right coordinates for the sample set.\n\
// Bottom-right is adjusted to stay within image bounds. This adjustment\n\
// caters both for the window size (radius 3) and the sample set size\n\
// (radius defined by the radius parameter)\n\
int2 GetSetMax(\n\
    int2    target,     // coordinates of pixel being filtered\n\
    int2    image_max,  // width,height of image (1-based)\n\
    int     radius) {   // radius of the set of samples\n\
\n\
\n\
    // set max starts without adjustment\n\
    int2 set_max = target + (int2)(radius, radius);\n\
\n\
    // bottom right adjusted for image size\n\
    int2 bottom_right = image_max - (int2)(4, 4);\n\
    set_max.x = (set_max.x > bottom_right.x) ? bottom_right.x : set_max.x;\n\
    set_max.y = (set_max.y > bottom_right.y) ? bottom_right.y : set_max.y;\n\
\n\
    // top left is adjusted for window size & length of the side of the set\n\
    int set_side = GetSetSide(radius);\n\
    int2 top_left = (int2)(2, 2) + (int2)(set_side, set_side);\n\
\n\
    set_max.x = (set_max.x < top_left.x) ? top_left.x: set_max.x;\n\
    set_max.y = (set_max.y < top_left.y) ? top_left.y: set_max.y;\n\
\n\
    return set_max;\n\
}\n\
\n\
// GetSampleCacheBaseCoordinates\n\
// Returns the input plane coordinates at O, in the following picture of the top\n\
// half + 1 row of the sample cache:\n\
//\n\
//    Ooooooo.................................\n\
//    ooooooo.................................\n\
//    ooooooo.................................\n\
//    oooxooo.................................\n\
//    ooooooo.................................\n\
//    ooooooo.................................\n\
//    ooooooo.................................\n\
//    ........................................\n\
//    ........................................\n\
//    ........................................\n\
//    ........................................\n\
//    ........................................\n\
//    ........................................\n\
//    ........................................\n\
//    ........................................\n\
//    ...............Tttttttt.................\n\
//    0123456789012345678901234567890123456789\n\
//\n\
// O = coordinates returned\n\
// o = sample window\n\
// x = sample pixel\n\
// T = target pixel\n\
// t = 7 other target pixels in the 8x2 tile\n\
int2 GetSampleCacheBaseCoordinates(\n\
    const   int2    top_left,   // coordinates of the top left corner of the region to be filtered\n\
            int2    image_max) {// width,height of image (1-based)\n\
\n\
    // Due to the borders of the input plane, the relative positions of O and T\n\
    // vary (T may not even lie within the bounds of the cache).\n\
    //\n\
    // Regardless of the value of x specified by the user, the cache size is\n\
    // fixed, and based on x == 4.\n\
    //\n\
    // Additionally, the base is the same for all work items - it does not\n\
    // vary with target pixel position within the 8x2 tile.\n\
    //\n\
    // TODO need to be careful about what happens when x != 4 and T is near bottom-right\n\
\n\
    int radius = GetRadius(4);\n\
    int2 target = GetCoordinates8x2(top_left);\n\
    int2 set_max = GetSetMax(target, image_max, radius);\n\
    int2 base = set_max - (int2)(GetSetSide(radius) - 1, GetSetSide(radius) - 1);\n\
    base -= (int2)(3, 3);\n\
    return base;\n\
}\n\
\n\
// GetSampleOffset\n\
// Returns the coordinate offset required to translate coordinates\n\
// in the image plane into coordinates within the sample cache\n\
int2 GetSampleOffset(\n\
    int2 sample,                            // coordinates of sample\n\
    int2 sample_cache_base_coordinates) {   // coordinates of sample cache's top left corner\n\
\n\
    return sample - sample_cache_base_coordinates;\n\
}\n\
\n\
// GetStrideCount\n\
// Returns the count of strides that each work item\n\
// takes to process the entire set within a plane\n\
int GetStrideCount(\n\
    int radius) {    // radius of the set of samples\n\
\n\
    return (GetSetSide(radius) * GetSetSide(radius)) >> 3;\n\
}\n\
\n\
// NextStride\n\
// Work item takes its next stride, respecting the right edge of the set of\n\
// samples (e.g. for a 19x19 set of samples there are 45 strides of 8)\n\
int2 NextStride(\n\
    int2    target,         // coordinates of pixel being filtered\n\
    int2    sample,         // coordinates of the sample that has just been processed\n\
    int     radius,         // radius of the set of samples\n\
    int2    set_max,        // coordinates of the bottom-right of the set of samples\n\
    int     skip_target) {  // when set do not sample at the target pixel\n\
\n\
    int2 next = (int2)(sample.x + 8, sample.y);\n\
    next = WrapSetCoordinate(target, next, radius, set_max, skip_target);\n\
    return next;\n\
}\n\
\n\
// IsMaster\n\
// Returns 1 if work item is a master, or 0 for slave.\n\
//\n\
// A master work item is defined as one of the 4 work items in a work group\n\
// that is responsible for reading/writing the 16 target pixels processed\n\
// by the work group. Each of the master work items handles a contiguous\n\
// subset of 4 of these target pixels.\n\
int IsMaster() {\n\
    int2 local_id = GetLocalID();\n\
    return ((local_id.x == 0)\n\
            && ((local_id.y == 0)\n\
                || (local_id.y == 4)\n\
                || (local_id.y == 8)\n\
                || (local_id.y == 12)\n\
                )\n\
            )\n\
            ? 1\n\
            : 0;\n\
}\n\
\n\
// PopulateSampleCache32x32\n\
// 32x32 subset of the sample cache is constructed based on pixels with origin\n\
// at (-15, -15) from the work-group's left-most pixel.\n\
void PopulateSampleCache32x32(\n\
    read_only   image2d_t   plane,              // input plane\n\
    const       int         linear,             // process plane in linear space instead of gamma space - TODO delete\n\
    const       int2        sample_cache_base,  // coordinates in plane for top left of the cache\n\
    local       float       *sample_cache) {    // caches pixels around the 8x2 tile of target pixels\n\
\n\
    // Each work item is responsible for 8 pixels, arranged as a 4x2 block.\n\
\n\
    int2 coordinates = sample_cache_base;\n\
    int2 local_pos = GetLocalID();\n\
    int2 offset = (int2)(local_pos.x << 2, local_pos.y << 1);\n\
    coordinates += offset;\n\
\n\
    float8 pixel_block = ReadPixel4x2(plane, coordinates, linear);\n\
\n\
    int cache_address = mul24(local_pos.y, 20) + local_pos.x;\n\
    vstore4(pixel_block.s0123, cache_address, sample_cache);\n\
    cache_address += 10;\n\
    vstore4(pixel_block.s4567, cache_address, sample_cache);\n\
    barrier(CLK_LOCAL_MEM_FENCE);\n\
}\n\
\n\
// PopulateSampleCache8x32\n\
// 8x32 subset of the sample cache is constructed based on pixels with origin\n\
// at (16, -15) from the work-group's left-most pixel.\n\
void PopulateSampleCache8x32(\n\
    read_only   image2d_t   plane,              // input plane\n\
    const       int         linear,             // process plane in linear space instead of gamma space\n\
    const       int2        sample_cache_base,  // coordinates in plane for top left of the cache\n\
    local       float       *sample_cache) {    // caches pixels around the 8x2 tile of target pixels\n\
\n\
    // Each work item is responsible for vertical strip of 4 pixels, arranged as a 1x4 block.\n\
\n\
    int2 coordinates = sample_cache_base;\n\
    int2 local_pos = GetLocalID();\n\
    int2 offset = (int2)(local_pos.x + 32, local_pos.y << 1);\n\
    coordinates += offset;\n\
\n\
    float2 column = ReadPixel1x2(plane, coordinates, linear);\n\
\n\
    int cache_address = mul24(offset.y, 40) + offset.x;\n\
    sample_cache[cache_address] = column.x;\n\
    cache_address += 40;\n\
    sample_cache[cache_address] = column.y;\n\
    barrier(CLK_LOCAL_MEM_FENCE);\n\
}\n\
\n\
// PopulateSampleCache\n\
// 40x32 cache of pixels with origin at (-15, -15) from the work-group's\n\
// left-most target pixel.\n\
void PopulateSampleCache(\n\
    read_only   image2d_t   plane,                          // input plane\n\
    const       int         linear,                         // process plane in linear space instead of gamma space\n\
                int2        sample_cache_base_coordinates,  // coordinates in plane for top left of the cache\n\
    local       float       *sample_cache) {                // caches pixels around the 8x2 tile of target pixels\n\
\n\
    // The width of the cache accounts for the 8-wide strip of pixels processed\n\
    // by the work group. The cache only needs to be 39 wide, but\n\
    // it's simpler to schedule 128 work items across a size of 40x32.\n\
    //\n\
    // The cache is populated in two sections:\n\
    // - 32x32 with each work item handling 8 pixels\n\
    PopulateSampleCache32x32(plane, linear, sample_cache_base_coordinates, sample_cache);\n\
\n\
    // - 8x32 with each work item handling 2 pixels.\n\
    PopulateSampleCache8x32(plane, linear, sample_cache_base_coordinates, sample_cache);\n\
}\n\
\n\
// PopulateTargetCache\n\
// 16x8 window of pixels centred upon the 8-wide strip of target pixels\n\
// is read from memory into local memory\n\
//\n\
// Each work item reads two pixels. y, ordinate, corresponds with the row\n\
// of target pixels.\n\
//\n\
// The cache is 2 too wide.\n\
void PopulateTargetCache(\n\
    read_only   image2d_t   plane,          // input plane\n\
    const       int2        top_left,       // coordinates of the top left corner of the region to be filtered\n\
    const       int         linear,         // process plane in linear space instead of gamma space\n\
    local       float       *target_cache) {// caches pixels around the 8x2 tile of target pixels\n\
\n\
    const int2 local_pos = GetLocalID();\n\
    const int2 offset_top_left = GetCoordinates8x2(top_left) + (int2)(-3, -3);\n\
    const int2 coordinates = offset_top_left + (int2)(get_local_id(1), get_local_id(0));\n\
\n\
    float pixel = ReadPixel(plane, coordinates, linear);\n\
\n\
    target_cache[(get_local_id(0) << 4) + get_local_id(1)] = pixel;\n\
\n\
    barrier(CLK_LOCAL_MEM_FENCE);\n\
}";

const char *rc_nlm_cl =       "\n\
float GetWindowDistance(\n\
    local       float   *target_cache,  // caches pixels around the 8x2 tile of target pixels\n\
    local       float   *sample_cache,  // caches pixels around the 8x2 tile of sample pixels\n\
    const       int2    sample,         // centre coordinates of sample window\n\
    const       int     target,         // linear address of top-left of window in target cache\n\
    constant    float   *g_gaussian) {  // 49 weights of gaussian kernel\n\
\n\
    float distance = 0.f;\n\
    int s_linear = mul24(sample.y - 3, 40) + sample.x - 3;\n\
    int t_linear = target;\n\
    int gaussian_position = 0;\n\
\n\
    for (int y = -3; y < 4; ++y) {\n\
        for (int x = -3; x < 4; ++x) {\n\
             float diff = target_cache[t_linear++] - sample_cache[s_linear++];\n\
            distance += g_gaussian[gaussian_position++] * (diff * diff);\n\
        }\n\
        t_linear += 9;\n\
        s_linear += 33;\n\
    }\n\
    return distance;\n\
}\n\
\n\
// WriteAlpha\n\
// Each work item writes an alpha weight/pixel pair to the region's alpha buffer\n\
void WriteAlpha(\n\
    const       uint    weight,         // weight/pixel pair to be written to region's alpha buffer\n\
    const       int     region_base,    // base address within the region_alpha buffer for all alpha samples\n\
    const       int     alpha_index,    // counter of weights generated by cooperator\n\
    global      uint    *region_alpha) {// region's alpha weight/pixel pairs packed as uints\n\
\n\
    // The region's alpha weights are organised as sets per pixel, with 8 weights per\n\
    // pixel being generated concurrently by 8 cooperating work items.\n\
    //\n\
    // The sets are numbered according to the linearised coordinates of the pixel\n\
    // within the region\n\
\n\
    const int linear_address = (alpha_index << 3) + region_base;\n\
    region_alpha[linear_address] = weight;\n\
}\n\
\n\
// WeightAnEighth\n\
// Process one-eighth of the samples.\n\
//\n\
// The weight that will be assigned to the target pixel is also tracked.\n\
void WeightAnEighth(\n\
    read_only   image2d_t   plane,          // input plane\n\
    const       float       h,              // strength of denoising\n\
    const       int         sample_expand,  // factor to expand sample radius\n\
    const       int         width,          // width in pixels\n\
    const       int         height,         // height in pixels\n\
    const       int2        top_left,       // coordinates of the top left corner of the region to be filtered\n\
    const       int         skip_target,    // when set do not sample at the target pixel\n\
    constant    float       *g_gaussian,    // 49 weights of gaussian kernel\n\
    const       int         linear,         // process plane in linear space instead of gamma space\n\
    local       float       *target_cache,  // caches pixels around the 8x1 strip of target pixels\n\
    local       float       *sample_cache,  // caches pixels around the 8x1 strip of sample pixels\n\
    const       int         alpha_set_size, // number of weight/pixel pairs per target pixel\n\
    const       int         alpha_so_far,   // count of alpha samples generated so far (multi-frame support)\n\
    global      uint        *region_alpha) {// region's alpha weight/pixel pairs packed as uints\n\
\n\
    int2 target = GetTargetCoordinates(top_left);\n\
    int radius = GetRadius(sample_expand);\n\
    int eighth = GetEighthSequenceNumber();\n\
\n\
    int2 set_max = GetSetMax(target, (int2)(width, height), radius);\n\
    int2 sample = GetSampleStartCoordinates(target, radius, set_max, eighth);\n\
    int2 sample_cache_base = GetSampleCacheBaseCoordinates(top_left, (int2)(width, height));\n\
    PopulateSampleCache(plane, linear, sample_cache_base, sample_cache);\n\
\n\
    int stride_count = GetStrideCount(radius);\n\
    int stride = 0;\n\
\n\
    // Determine linear address in target cache\n\
    const int target_col_offset = get_local_id(1) & 7;\n\
    const int target_row_offset = (get_local_id(1) >> 3) << 4;\n\
    const int target_offset = target_col_offset + target_row_offset;\n\
\n\
    // Determine base address in region_alpha buffer\n\
    const int region_base = GetFilterRegionBaseAddress(width, alpha_set_size);\n\
\n\
    int alpha_index = alpha_so_far;\n\
\n\
    while (true) {\n\
        int2 sample_offset = GetSampleOffset(sample, sample_cache_base);\n\
        float euclidean_distance = GetWindowDistance(target_cache, sample_cache, sample_offset, target_offset, g_gaussian);\n\
        uint sample_weight = (uint)(floor(16777215.f * exp(-euclidean_distance * h))) << 8;\n\
        uint sample_pixel = floor(255.f * sample_cache[mul24(sample_offset.y, 40) + sample_offset.x]);\n\
\n\
        sample_weight |= sample_pixel;\n\
\n\
        WriteAlpha(sample_weight, region_base, alpha_index, region_alpha);\n\
\n\
        if (++stride == stride_count) {\n\
            break;\n\
        } else {\n\
            sample = NextStride(target, sample, radius, set_max, skip_target);\n\
            ++alpha_index;\n\
        }\n\
    }\n\
}";

const char *rc_nlm_single_cl =       "\n\
__attribute__((reqd_work_group_size(8, 16, 1)))\n\
__kernel void NLMSingleFrame(\n\
    read_only   image2d_t   input_plane,    // input plane\n\
    const       int         width,          // width in pixels\n\
    const       int         height,         // height in pixels\n\
    const       int2        top_left,       // coordinates of the top left corner of the region to be filtered\n\
    const       float       h,              // strength of denoising\n\
    const       int         sample_expand,  // factor to expand sample radius\n\
    constant    float       *g_gaussian,    // 49 weights of gaussian kernel\n\
    const       int         linear,         // process plane in linear space instead of gamma space\n\
    const       int         alpha_set_size, // number of weight/pixel pairs per target pixel\n\
    global      uint        *region_alpha) {// region's alpha weight/pixel pairs packed as uints\n\
\n\
    // Each work group produces a set of alpha weight/pixel pairs for\n\
    // 16 filtered pixels, organised as a tile that's 8 wide and 2 high.\n\
    //\n\
    // Each work item computes multiple weights, in an 8-stride sequence,\n\
    // for a single pixel. 8 work items, together, compute the entire set\n\
    // of weights used to produce the filtered result.\n\
    //\n\
    // For a set of 7x7 samples' weights, work item 0 computes samples:\n\
    // 0, 8, 16, 24, 33, 41\n\
    //\n\
    // while work item 1 computes:\n\
    // 1, 9, 17, 26, 34, 42\n\
    //\n\
    // and work item 7 computes:\n\
    // 7, 15, 23, 32, 40, 48\n\
    //\n\
    // Sample 24 (the centre of the 7x7 set) is the pixel being filtered.\n\
    // It does not need to have a weight computed, since its weight is\n\
    // the minimum of the other samples' weights.\n\
    //\n\
    // When the sample set is larger, e.g. 19x19, then each work item\n\
    // iterates 45 times, for a total of 360 weights.\n\
    //\n\
    // Input plane contains pixels as uchars. UNORM8 format is defined,\n\
    // so a read converts uchar into a normalised float of range 0.f to 1.f.\n\
\n\
    local float target_cache[128];\n\
    local float sample_cache[1280];\n\
\n\
    PopulateTargetCache(input_plane, top_left, linear, target_cache);\n\
\n\
    int skip_target = 1;\n\
    WeightAnEighth(input_plane,\n\
                   h,\n\
                   sample_expand,\n\
                   width,\n\
                   height,\n\
                   top_left,\n\
                   skip_target,\n\
                   g_gaussian,\n\
                   linear,\n\
                   target_cache,\n\
                   sample_cache,\n\
                   alpha_set_size,\n\
                   0,\n\
                   region_alpha);\n\
}";

const char *rc_sort_cl =       "\n\
// ResetAlpha\n\
// Zeroes the alpha set\n\
void ResetAlpha(\n\
    const       int     alpha_size, // TODO delete\n\
                uint    *alpha) {   // work item's subset of best weights\n\
\n\
    for (int i = 0; i < ALPHASIZE; ++i) {\n\
        alpha[i] = 0;\n\
    }\n\
}\n\
\n\
// UpdateAlpha\n\
// All eight cooperating work items evaluate eight sample weights\n\
// to determine which of them can join the alpha set\n\
void UpdateAlpha(\n\
    const       uint    cooperator_weights[8],  // 8 weights to be evaluated\n\
    local       uint    *pixel_swap,            // swap buffer for weight/pixel pairs\n\
                uint    *alpha) {               // an eighth of the best weights and samples to be used to filter the pixel\n\
\n\
    // The alpha set is spread equally across all eight cooperating work items, in descending\n\
    // order, with work item 0 having the highest weights and 7 having the lowest weights.\n\
    // Each work item ends up doing one of three things:\n\
    //\n\
    // 1. nothing, the new weight is lower than its lowest weight in alpha[15]\n\
    // 2. finding the correct slot to insert the new weight, shuffling all others down by 1\n\
    // 3. inserting at alpha[0] the weight passed down to it by its superior and shuffling\n\
    //      all others down by 1\n\
\n\
    const int cooperator_id = get_local_id(0);\n\
    const int pixel_id = get_local_id(1);\n\
    const int swap_base = pixel_id << 3;\n\
    const int swap_home = swap_base + cooperator_id;\n\
\n\
    // Each cooperator will take the weight supplied by its superior\n\
    // and insert it somewhere in its own section of the entire sorted list\n\
    int superior_id = swap_base + ((cooperator_id - 1) & 7);\n\
        barrier(CLK_LOCAL_MEM_FENCE);\n\
\n\
    for (int sample_id = 0; sample_id < 8; ++sample_id) {\n\
        // Each cooperator decides which weight to send to its inferior\n\
        pixel_swap[swap_home] = min(alpha[ALPHASIZE - 1], cooperator_weights[sample_id]);\n\
        barrier(CLK_LOCAL_MEM_FENCE);\n\
\n\
        // Each cooperator starts with its superior's handed-down weight\n\
        uint candidate = pixel_swap[superior_id];\n\
\n\
        // top-most cooperator has no superior, so starts with the weight being evaluated\n\
        candidate = (cooperator_id == 0) ? cooperator_weights[sample_id] : candidate;\n\
\n\
        uint next_candidate;\n\
\n\
        // Determine where to put the new weight. For most work items, this is simply going to\n\
        // be alpha[0]. The work item which holds the weight that's immediately higher up the\n\
        // sorted list than the new weight has to search all of its list in order to insert the\n\
        // new weight in the ordered position. All work items then shuffle downwards the remainder\n\
        // of their lists (after point of insertion), hence all 16 slots have to be processed\n\
        for (int i = 0; i < ALPHASIZE; ++i) {\n\
            next_candidate = min(candidate, alpha[i]);\n\
            alpha[i] = max(alpha[i], candidate);\n\
            candidate = next_candidate;\n\
        }\n\
    }\n\
}\n\
\n\
// SortAlpha\n\
// Each work item writes an alpha weight/pixel pair to the region's alpha buffer\n\
void SortAlpha(\n\
    const       int     region_base,    // base address within the region_alpha buffer for all alpha samples\n\
    local       uint    *pixel_swap,    // swap buffer for weight/pixel pairs\n\
    const       int     alpha_set_size, // number of weight/pixel pairs per target pixel\n\
    global      uint    *region_alpha,  // region's alpha weight/pixel pairs packed as uints\n\
                uint    *alpha) {       // an eighth of the best weights and samples to be used to filter the pixel\n\
\n\
    // The region's alpha weights are organised as sets per pixel, with 8 weights per\n\
    // pixel being generated concurrently by 8 cooperating work items.\n\
    //\n\
    // The sets are numbered according to the linearised coordinates of the pixel\n\
    // within the region\n\
\n\
    for (int i = 0; i < alpha_set_size; i += 8) {\n\
        int linear_address = region_base + i;\n\
        uint cooperator_weights[8];\n\
\n\
        for (int j = 0; j < 8; ++j)\n\
            cooperator_weights[j] = region_alpha[linear_address + j];\n\
\n\
        UpdateAlpha(cooperator_weights, pixel_swap, alpha);\n\
    }\n\
}\n\
\n\
// ReduceAlpha\n\
// Returns the target weight and sums the weighted pixels and weights based\n\
// upon the final alpha weights and samples\n\
float ReduceAlpha(\n\
    const       int     alpha_size,             // TODO delete\n\
                uint    *alpha,                 // an eighth of the best weights and samples to be used to filter the pixel\n\
    local       uint    *weight_swap,           // swap buffer for running averages/sums\n\
    local       uint    *pixel_swap,            // swap buffer for weight/pixel pairs\n\
                float   *all_samples_average,   // sum of weighted pixel values\n\
                float   *all_samples_weight) {  // sum of weights\n\
\n\
    float own_average = 0.f;\n\
    float own_weight = 0.f;\n\
    uint min_weight = UINT_MAX;\n\
    for (int i = 0; i < ALPHASIZE; ++i) {\n\
        float weight = (float)(alpha[i] >> 8) * 0.000000059604648f;\n\
        float pixel = (float)(alpha[i] & 255) * 0.0039215686f;\n\
        own_average += weight * pixel;\n\
        own_weight += weight;\n\
        min_weight = (weight == 0.f) ? min_weight : min(min_weight, alpha[i]);\n\
    }\n\
\n\
    const int pixel_id = get_local_id(1) << 3;\n\
    const int cooperator_id = get_local_id(0);\n\
\n\
    pixel_swap[pixel_id] = 1.f;\n\
    barrier(CLK_LOCAL_MEM_FENCE);\n\
\n\
    for (int swap_id = 0; swap_id < 8; ++swap_id) {\n\
        if (cooperator_id == swap_id) {\n\
            weight_swap[(pixel_id << 1)    ] = as_uint(own_average);\n\
            weight_swap[(pixel_id << 1) + 1] = as_uint(own_weight);\n\
\n\
            pixel_swap[pixel_id] = min(pixel_swap[pixel_id], min_weight);\n\
        }\n\
\n\
        barrier(CLK_LOCAL_MEM_FENCE);\n\
\n\
        *all_samples_average += as_float(weight_swap[(pixel_id << 1)    ]);\n\
        *all_samples_weight  += as_float(weight_swap[(pixel_id << 1) + 1]);\n\
    }\n\
    return as_float(pixel_swap[pixel_id]);\n\
}\n\
\n\
// FilterPixel\n\
// Uses the weights to compute a filtered pixel\n\
float FilterPixel (\n\
    local       float   *target_cache,      // caches pixels around the 8x2 tile of target pixels\n\
                float   *average,           // running sum of weighted pixel values\n\
                float   *weight,            // running sum of weights\n\
                float   *target_weight) {   // weight chosen from across all samples that will be used for target pixel\n\
\n\
    *target_weight = max(*target_weight, 0.004f);\n\
    *weight += *target_weight;\n\
\n\
    float target_pixel = target_cache[51 + (get_local_id(1) & 7) + ((get_local_id(1) >> 3) << 4)];\n\
    *average +=  *target_weight * target_pixel;\n\
\n\
    return *average / *weight;\n\
}\n\
\n\
// SwapAndWriteFilteredPixels\n\
// Filtered pixels are swapped in to the 4 master work items\n\
// which then write them to destination\n\
void SwapAndWriteFilteredPixels(\n\
    write_only  image2d_t   destination_plane,  // plane containing filtered pixels\n\
    const       int2        top_left,           // coordinates of the top left corner of the region to be filtered\n\
    local       uint        *pixel_swap,        // buffer for slaves to send pixels to masters\n\
    const       int         linear,             // process plane in linear space instead of gamma space\n\
    const       float       filtered_pixel) {   // one of 16 pixels to be swapped before writing\n\
\n\
    int offset = get_local_id(1);\n\
    if (get_local_id(0) == 0)\n\
        pixel_swap[offset] = as_uint(filtered_pixel);\n\
\n\
    barrier(CLK_LOCAL_MEM_FENCE);\n\
\n\
    if (IsMaster()) {\n\
        float4 target_pixel = 0.f;\n\
        if (offset == 0) {\n\
            target_pixel.x = as_float(pixel_swap[0]);\n\
            target_pixel.y = as_float(pixel_swap[1]);\n\
            target_pixel.z = as_float(pixel_swap[2]);\n\
            target_pixel.w = as_float(pixel_swap[3]);\n\
        } else if (offset == 4) {\n\
            target_pixel.x = as_float(pixel_swap[4]);\n\
            target_pixel.y = as_float(pixel_swap[5]);\n\
            target_pixel.z = as_float(pixel_swap[6]);\n\
            target_pixel.w = as_float(pixel_swap[7]);\n\
        } else if (offset == 8) {\n\
            target_pixel.x = as_float(pixel_swap[8]);\n\
            target_pixel.y = as_float(pixel_swap[9]);\n\
            target_pixel.z = as_float(pixel_swap[10]);\n\
            target_pixel.w = as_float(pixel_swap[11]);\n\
        } else if (offset == 12) {\n\
            target_pixel.x = as_float(pixel_swap[12]);\n\
            target_pixel.y = as_float(pixel_swap[13]);\n\
            target_pixel.z = as_float(pixel_swap[14]);\n\
            target_pixel.w = as_float(pixel_swap[15]);\n\
        }\n\
        int2 target = GetTargetCoordinates(top_left);\n\
        WritePixel4(target_pixel, GetCoord4(target), linear, destination_plane);\n\
    }\n\
}\n\
\n\
__attribute__((reqd_work_group_size(8, 16, 1)))\n\
__kernel void Finalise(\n\
    read_only   image2d_t   input_plane,        // input plane\n\
    const       int         width,              // region width in pixels\n\
    const       int2        top_left,           // coordinates of the top left corner of the region to be filtered\n\
    const       int         linear,             // process plane in linear space instead of gamma space\n\
    const       int         alpha_size,         // TODO delete\n\
    const       int         alpha_set_size,     // number of weight/pixel pairs per target pixel\n\
    global      uint        *region_alpha,      // region's alpha weight/pixel pairs packed as uints\n\
    write_only  image2d_t   destination_plane) {// filtered result\n\
\n\
    // Each work group produces 16 filtered pixels derived from the best\n\
    // 128 weights in the alpha set of weight/pixel pairs.\n\
    //\n\
    // The 16 filtered pixels are organised as a tile that's 8 wide and 2 high.\n\
    //\n\
    // Each set of 8 work items within the work group cooperates to sort the\n\
    // alpha set for the pixel.\n\
    //\n\
    // There are 4 master work items (0, 32, 64, 96) and the rest are slaves. Each\n\
    // master is responsible for writing the filtered result for 4 target\n\
    // pixels. The slaves send their results through pixel_swap in local memory.\n\
    //\n\
    // Destination plane is formatted as UNORM8 uchar. The device\n\
    // automatically converts a pixel in range 0.f to 1.f into 0 to 255.\n\
\n\
    local uint weight_swap[256];\n\
    local uint pixel_swap[128];\n\
\n\
    // Sort\n\
    uint alpha[ALPHASIZE];    // an eighth of the best weights and samples to be used to filter the pixel\n\
    ResetAlpha(alpha_size, alpha);\n\
\n\
    // Determine base address in region_alpha buffer\n\
    const int region_base = GetRegionBaseAddress(width, alpha_set_size);\n\
\n\
    SortAlpha(region_base, pixel_swap, alpha_set_size, region_alpha, alpha);\n\
\n\
    // Reduce\n\
    float average = 0.f;    // Weights are kept as running average and running weight ...\n\
    float weight = 0.f;        // ... which simplifies final reduction into a weighted-average pixel.\n\
    float target_weight = ReduceAlpha(alpha_size, alpha, weight_swap, pixel_swap, &average, &weight);\n\
\n\
    // Filter\n\
    local float target_cache[128];\n\
    // TODO no need to read so many target pixels\n\
    PopulateTargetCache(input_plane, top_left, linear, target_cache);\n\
    float filtered_pixel = FilterPixel(target_cache, &average, &weight, &target_weight);\n\
\n\
    // Write\n\
    SwapAndWriteFilteredPixels(destination_plane, top_left, pixel_swap, linear, filtered_pixel);\n\
}";

const char *rc_nlm_multi_cl =       "\n\
__attribute__((reqd_work_group_size(8, 16, 1)))\n\
__kernel void NLMMultiFrameFourPixel(\n\
    read_only   image2d_t   target_plane,           // plane being filtered\n\
    read_only   image2d_t   sample_plane,           // any other plane\n\
    const       int         sample_equals_target,   // 1 when sample plane is target plane, 0 otherwise\n\
    const       int         width,                  // width in pixels\n\
    const       int         height,                 // height in pixels\n\
    const       int2        top_left,               // coordinates of the top left corner of the region to be filtered\n\
    const       float       h,                      // strength of denoising\n\
    const       int         sample_expand,          // factor to expand sample radius\n\
    constant    float       *g_gaussian,            // 49 weights of gaussian kernel\n\
    const       int         linear,                 // process plane in linear space instead of gamma space\n\
    const       int         alpha_set_size,         // number of weight/pixel pairs per target pixel\n\
    const       int         alpha_so_far,           // count of alpha samples generated so far for each cooperator\n\
    global      uint        *region_alpha) {        // region's alpha weight/pixel pairs packed as uints\n\
\n\
    local float target_cache[128];\n\
    local float sample_cache[1280];\n\
\n\
    PopulateTargetCache(target_plane, top_left, linear, target_cache);\n\
\n\
    int skip_target = sample_equals_target;\n\
    WeightAnEighth(sample_plane,\n\
                   h,\n\
                   sample_expand,\n\
                   width,\n\
                   height,\n\
                   top_left,\n\
                   skip_target,\n\
                   g_gaussian,\n\
                   linear,\n\
                   target_cache,\n\
                   sample_cache,\n\
                   alpha_set_size,\n\
                   alpha_so_far,\n\
                   region_alpha);\n\
}";
