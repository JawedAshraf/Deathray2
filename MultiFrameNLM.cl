/* Deathray2 - An Avisynth plug-in filter for spatial/temporal non-local means de-noising.
 *
 * version 1.00
 *
 * Copyright 2015, Jawed Ashraf - Deathray@cupidity.f9.co.uk
 */

__attribute__((reqd_work_group_size(8, 16, 1)))
__kernel void NLMMultiFrameFourPixel(
    read_only   image2d_t   target_plane,           // plane being filtered
    read_only   image2d_t   sample_plane,           // any other plane
    const       int         sample_equals_target,   // 1 when sample plane is target plane, 0 otherwise
    const       int         width,                  // width in pixels
    const       int         height,                 // height in pixels
    const       int2        top_left,               // coordinates of the top left corner of the region to be filtered
    const       float       h,                      // strength of denoising
    const       int         sample_expand,          // factor to expand sample radius
    constant    float       *g_gaussian,            // 49 weights of gaussian kernel
    const       int         linear,                 // process plane in linear space instead of gamma space
    const       int         alpha_set_size,         // number of weight/pixel pairs per target pixel
    const       int         alpha_so_far,           // count of alpha samples generated so far for each cooperator
    global      uint        *region_alpha) {        // region's alpha weight/pixel pairs packed as uints


    local float target_cache[128];
    local float sample_cache[1280];

    PopulateTargetCache(target_plane, top_left, linear, target_cache);

    int skip_target = sample_equals_target;
    WeightAnEighth(sample_plane,
                   h, 
                   sample_expand, 
                   width, 
                   height, 
                   top_left, 
                   skip_target, 
                   g_gaussian, 
                   linear, 
                   target_cache, 
                   sample_cache, 
                   alpha_set_size, 
                   alpha_so_far,
                   region_alpha);

}



