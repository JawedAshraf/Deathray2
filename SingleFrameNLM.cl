/* Deathray2 - An Avisynth plug-in filter for spatial/temporal non-local means de-noising.
 *
 * version 1.00
 *
 * Copyright 2015, Jawed Ashraf - Deathray@cupidity.f9.co.uk
 */

__attribute__((reqd_work_group_size(8, 16, 1)))
__kernel void NLMSingleFrame(
    read_only   image2d_t   input_plane,    // input plane
    const       int         width,          // width in pixels
    const       int         height,         // height in pixels
    const       int2        top_left,       // coordinates of the top left corner of the region to be filtered
    const       float       h,              // strength of denoising
    const       int         sample_expand,  // factor to expand sample radius
    constant    float       *g_gaussian,    // 49 weights of gaussian kernel
    const       int         linear,         // process plane in linear space instead of gamma space
    const       int         alpha_set_size, // number of weight/pixel pairs per target pixel
    global      uint        *region_alpha) {// region's alpha weight/pixel pairs packed as uints

    // Each work group produces a set of alpha weight/pixel pairs for 
    // 16 filtered pixels, organised as a tile that's 8 wide and 2 high.
    //
    // Each work item computes multiple weights, in an 8-stride sequence, 
    // for a single pixel. 8 work items, together, compute the entire set 
    // of weights used to produce the filtered result. 
    // 
    // For a set of 7x7 samples' weights, work item 0 computes samples:
    // 0, 8, 16, 24, 33, 41
    //
    // while work item 1 computes:
    // 1, 9, 17, 26, 34, 42
    //
    // and work item 7 computes:
    // 7, 15, 23, 32, 40, 48
    //
    // Sample 24 (the centre of the 7x7 set) is the pixel being filtered.
    // It does not need to have a weight computed, since its weight is
    // the minimum of the other samples' weights.
    //
    // When the sample set is larger, e.g. 19x19, then each work item
    // iterates 45 times, for a total of 360 weights.
    //
    // Input plane contains pixels as uchars. UNORM8 format is defined,
    // so a read converts uchar into a normalised float of range 0.f to 1.f. 

    local float target_cache[128];
    local float sample_cache[1280];

    PopulateTargetCache(input_plane, top_left, linear, target_cache);

    int skip_target = 1;
    WeightAnEighth(input_plane,
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
                   0,
                   region_alpha);
}

