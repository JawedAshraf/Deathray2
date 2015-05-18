/* Deathray2 - An Avisynth plug-in filter for spatial/temporal non-local means de-noising.
 *
 * version 1.00
 *
 * Copyright 2015, Jawed Ashraf - Deathray@cupidity.f9.co.uk
 */

// ResetAlpha
// Zeroes the alpha set
void ResetAlpha(
    const       int     alpha_size, // TODO delete
                uint    *alpha) {   // work item's subset of best weights

    for (int i = 0; i < ALPHASIZE; ++i) {
        alpha[i] = 0;
    }
}

// UpdateAlpha
// All eight cooperating work items evaluate eight sample weights
// to determine which of them can join the alpha set
void UpdateAlpha(
    const       uint    cooperator_weights[8],  // 8 weights to be evaluated
    local       uint    *pixel_swap,            // swap buffer for weight/pixel pairs
                uint    *alpha) {               // an eighth of the best weights and samples to be used to filter the pixel 

    // The alpha set is spread equally across all eight cooperating work items, in descending
    // order, with work item 0 having the highest weights and 7 having the lowest weights. 
    // Each work item ends up doing one of three things:
    // 
    // 1. nothing, the new weight is lower than its lowest weight in alpha[15]
    // 2. finding the correct slot to insert the new weight, shuffling all others down by 1
    // 3. inserting at alpha[0] the weight passed down to it by its superior and shuffling
    //      all others down by 1

    const int cooperator_id = get_local_id(0);
    const int pixel_id = get_local_id(1);
    const int swap_base = pixel_id << 3;
    const int swap_home = swap_base + cooperator_id;

    // Each cooperator will take the weight supplied by its superior
    // and insert it somewhere in its own section of the entire sorted list
    int superior_id = swap_base + ((cooperator_id - 1) & 7);
        barrier(CLK_LOCAL_MEM_FENCE);    

    for (int sample_id = 0; sample_id < 8; ++sample_id) {
        // Each cooperator decides which weight to send to its inferior
        pixel_swap[swap_home] = min(alpha[ALPHASIZE - 1], cooperator_weights[sample_id]);
        barrier(CLK_LOCAL_MEM_FENCE);    

        // Each cooperator starts with its superior's handed-down weight
        uint candidate = pixel_swap[superior_id];

        // top-most cooperator has no superior, so starts with the weight being evaluated
        candidate = (cooperator_id == 0) ? cooperator_weights[sample_id] : candidate;

        uint next_candidate;

        // Determine where to put the new weight. For most work items, this is simply going to
        // be alpha[0]. The work item which holds the weight that's immediately higher up the 
        // sorted list than the new weight has to search all of its list in order to insert the
        // new weight in the ordered position. All work items then shuffle downwards the remainder
        // of their lists (after point of insertion), hence all 16 slots have to be processed
        for (int i = 0; i < ALPHASIZE; ++i) {
            next_candidate = min(candidate, alpha[i]);
            alpha[i] = max(alpha[i], candidate);
            candidate = next_candidate;
        }
    }
}

// SortAlpha
// Each work item writes an alpha weight/pixel pair to the region's alpha buffer
void SortAlpha(
    const       int     region_base,    // base address within the region_alpha buffer for all alpha samples
    local       uint    *pixel_swap,    // swap buffer for weight/pixel pairs
    const       int     alpha_set_size, // number of weight/pixel pairs per target pixel
    global      uint    *region_alpha,  // region's alpha weight/pixel pairs packed as uints
                uint    *alpha) {       // an eighth of the best weights and samples to be used to filter the pixel 

    // The region's alpha weights are organised as sets per pixel, with 8 weights per
    // pixel being generated concurrently by 8 cooperating work items.
    //
    // The sets are numbered according to the linearised coordinates of the pixel
    // within the region

    for (int i = 0; i < alpha_set_size; i += 8) {
        int linear_address = region_base + i;
        uint cooperator_weights[8];

        for (int j = 0; j < 8; ++j)
            cooperator_weights[j] = region_alpha[linear_address + j];

        UpdateAlpha(cooperator_weights, pixel_swap, alpha);
    }
}

// ReduceAlpha
// Returns the target weight and sums the weighted pixels and weights based
// upon the final alpha weights and samples
float ReduceAlpha(
    const       int     alpha_size,             // TODO delete
                uint    *alpha,                 // an eighth of the best weights and samples to be used to filter the pixel
    local       uint    *weight_swap,           // swap buffer for running averages/sums
    local       uint    *pixel_swap,            // swap buffer for weight/pixel pairs
                float   *all_samples_average,   // sum of weighted pixel values
                float   *all_samples_weight) {  // sum of weights

    float own_average = 0.f;
    float own_weight = 0.f;
    uint min_weight = UINT_MAX;
    for (int i = 0; i < ALPHASIZE; ++i) {
        float weight = (float)(alpha[i] >> 8) * 0.000000059604648f;
        float pixel = (float)(alpha[i] & 255) * 0.0039215686f;
        own_average += weight * pixel;
        own_weight += weight;
        min_weight = (weight == 0.f) ? min_weight : min(min_weight, alpha[i]);
    }

    const int pixel_id = get_local_id(1) << 3;
    const int cooperator_id = get_local_id(0);

    pixel_swap[pixel_id] = 1.f;
    barrier(CLK_LOCAL_MEM_FENCE);

    for (int swap_id = 0; swap_id < 8; ++swap_id) {
        if (cooperator_id == swap_id) {
            weight_swap[(pixel_id << 1)    ] = as_uint(own_average);
            weight_swap[(pixel_id << 1) + 1] = as_uint(own_weight);

            pixel_swap[pixel_id] = min(pixel_swap[pixel_id], min_weight);
        }

        barrier(CLK_LOCAL_MEM_FENCE);

        *all_samples_average += as_float(weight_swap[(pixel_id << 1)    ]);
        *all_samples_weight  += as_float(weight_swap[(pixel_id << 1) + 1]);
    }
    return as_float(pixel_swap[pixel_id]);
}

// FilterPixel
// Uses the weights to compute a filtered pixel
float FilterPixel (
    local       float   *target_cache,      // caches pixels around the 8x2 tile of target pixels
                float   *average,           // running sum of weighted pixel values
                float   *weight,            // running sum of weights
                float   *target_weight) {   // weight chosen from across all samples that will be used for target pixel

    *target_weight = max(*target_weight, 0.004f);
    *weight += *target_weight;

    float target_pixel = target_cache[51 + (get_local_id(1) & 7) + ((get_local_id(1) >> 3) << 4)];
    *average +=  *target_weight * target_pixel;
    
    return *average / *weight;
}

// SwapAndWriteFilteredPixels
// Filtered pixels are swapped in to the 4 master work items
// which then write them to destination
void SwapAndWriteFilteredPixels(
    write_only  image2d_t   destination_plane,  // plane containing filtered pixels
    const       int2        top_left,           // coordinates of the top left corner of the region to be filtered
    local       uint        *pixel_swap,        // buffer for slaves to send pixels to masters
    const       int         linear,             // process plane in linear space instead of gamma space
    const       float       filtered_pixel) {   // one of 16 pixels to be swapped before writing

    int offset = get_local_id(1);
    if (get_local_id(0) == 0)
        pixel_swap[offset] = as_uint(filtered_pixel);

    barrier(CLK_LOCAL_MEM_FENCE);

    if (IsMaster()) {
        float4 target_pixel = 0.f;
        if (offset == 0) {
            target_pixel.x = as_float(pixel_swap[0]);
            target_pixel.y = as_float(pixel_swap[1]);
            target_pixel.z = as_float(pixel_swap[2]);
            target_pixel.w = as_float(pixel_swap[3]);
        } else if (offset == 4) {
            target_pixel.x = as_float(pixel_swap[4]);
            target_pixel.y = as_float(pixel_swap[5]);
            target_pixel.z = as_float(pixel_swap[6]);
            target_pixel.w = as_float(pixel_swap[7]);
        } else if (offset == 8) {
            target_pixel.x = as_float(pixel_swap[8]);
            target_pixel.y = as_float(pixel_swap[9]);
            target_pixel.z = as_float(pixel_swap[10]);
            target_pixel.w = as_float(pixel_swap[11]);
        } else if (offset == 12) {
            target_pixel.x = as_float(pixel_swap[12]);
            target_pixel.y = as_float(pixel_swap[13]);
            target_pixel.z = as_float(pixel_swap[14]);
            target_pixel.w = as_float(pixel_swap[15]);
        }
        int2 target = GetTargetCoordinates(top_left);
        WritePixel4(target_pixel, GetCoord4(target), linear, destination_plane);
    }
}

__attribute__((reqd_work_group_size(8, 16, 1)))
__kernel void Finalise(
    read_only   image2d_t   input_plane,        // input plane
    const       int         width,              // region width in pixels
    const       int2        top_left,           // coordinates of the top left corner of the region to be filtered
    const       int         linear,             // process plane in linear space instead of gamma space
    const       int         alpha_size,         // TODO delete          
    const       int         alpha_set_size,     // number of weight/pixel pairs per target pixel
    global      uint        *region_alpha,      // region's alpha weight/pixel pairs packed as uints
    write_only  image2d_t   destination_plane) {// filtered result

    // Each work group produces 16 filtered pixels derived from the best
    // 128 weights in the alpha set of weight/pixel pairs.
    //
    // The 16 filtered pixels are organised as a tile that's 8 wide and 2 high.
    //
    // Each set of 8 work items within the work group cooperates to sort the 
    // alpha set for the pixel.
    //
    // There are 4 master work items (0, 32, 64, 96) and the rest are slaves. Each 
    // master is responsible for writing the filtered result for 4 target 
    // pixels. The slaves send their results through pixel_swap in local memory.
    // 
    // Destination plane is formatted as UNORM8 uchar. The device 
    // automatically converts a pixel in range 0.f to 1.f into 0 to 255.

    local uint weight_swap[256];
    local uint pixel_swap[128];

    // Sort
    uint alpha[ALPHASIZE];    // an eighth of the best weights and samples to be used to filter the pixel
    ResetAlpha(alpha_size, alpha);

    // Determine base address in region_alpha buffer
    const int region_base = GetRegionBaseAddress(width, alpha_set_size);

    SortAlpha(region_base, pixel_swap, alpha_set_size, region_alpha, alpha);

    // Reduce
    float average = 0.f;    // Weights are kept as running average and running weight ... 
    float weight = 0.f;        // ... which simplifies final reduction into a weighted-average pixel.
    float target_weight = ReduceAlpha(alpha_size, alpha, weight_swap, pixel_swap, &average, &weight);

    // Filter
    local float target_cache[128];
    // TODO no need to read so many target pixels
    PopulateTargetCache(input_plane, top_left, linear, target_cache);
    float filtered_pixel = FilterPixel(target_cache, &average, &weight, &target_weight);
    
    // Write
    SwapAndWriteFilteredPixels(destination_plane, top_left, pixel_swap, linear, filtered_pixel);
}
