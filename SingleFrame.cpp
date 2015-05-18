/* Deathray2 - An Avisynth plug-in filter for spatial/temporal non-local means de-noising.
 *
 * version 1.00
 *
 * Copyright 2015, Jawed Ashraf - Deathray@cupidity.f9.co.uk
 */

#include "result.h"
#include "util.h"
#include "SingleFrame.h"
#include "device.h"
#include "buffer_map.h"

extern  int     g_device_count;
extern  Device  *g_devices;

extern  int     g_gaussian;

#define FILTER_ARG_TOP_LEFT 3
#define SORT_ARG_TOP_LEFT 2

SingleFrame::SingleFrame() {
    device_id_      = 0;
    width_          = 0;
    height_         = 0;
    src_pitch_      = 0;
    dst_pitch_      = 0;
    source_plane_   = 0;
    dest_plane_     = 0;
    alpha_          = 0;
    region_width_   = 0;
    region_height_  = 0;
}

result SingleFrame::Init(
    const   int     &device_id,
    const   int     &width, 
    const   int     &height,
    const   int     &src_pitch,
    const   int     &dst_pitch,
    const   float   &h,
    const   int     &sample_expand,
    const   int     &linear,        
    const   int     &correction,
    const   int     &balanced) {

    if (device_id >= g_device_count) return FILTER_ERROR;

    result status = FILTER_OK;

    device_id_      = device_id;
    width_          = width;
    height_         = height;
    src_pitch_      = src_pitch;
    dst_pitch_      = dst_pitch;
    region_width_   = width;
    region_height_  = 32;
    h_              = 1.f/h;
    cq_             = g_devices[device_id_].cq();

    if (width_ == 0 || height_ == 0 || src_pitch_ == 0 || dst_pitch_ == 0 || h == 0 ) 
        return FILTER_INVALID_PARAMETER;

    alpha_set_size_ = GetAlphaSetSize(0, sample_expand);

    status = InitBuffers(sample_expand);
    if (status != FILTER_OK) return status;

    status = InitKernels(sample_expand, linear, correction, balanced);

    return status;
}

result SingleFrame::InitBuffers(const int &sample_expand) {
    result status = FILTER_OK;

    status = g_devices[device_id_].buffers_.AllocPlane(cq_, width_, height_, &source_plane_);
    if (status != FILTER_OK) return status;

    status = g_devices[device_id_].buffers_.AllocPlane(cq_, width_, height_, &dest_plane_);
    if (status != FILTER_OK) return status;

    const int alpha_buffer_size = GetAlphaBufferSize(0,
                                                    region_width_, 
                                                    region_height_,
                                                    sample_expand)
                                * sizeof(cl_uint);

    status = g_devices[device_id_].buffers_.AllocBuffer(cq_, alpha_buffer_size, &alpha_);

    return status;
}

result SingleFrame::InitKernels(
    const int &sample_expand,
    const int &linear,
    const int &correction,
    const int &balanced) {

    result status = FILTER_OK;

    status = InitFilterKernel(sample_expand, linear, correction, balanced);
    if (status != FILTER_OK) return status;

    status = InitSortKernel(linear);
    if (status != FILTER_OK) return status;

    status = InitInitialiseKernel();

    return status;
}

result SingleFrame::InitFilterKernel(
    const int &sample_expand,
    const int &linear,
    const int &correction,
    const int &balanced) {

    filter_ = ClKernel(device_id_, "NLMSingleFrame");
    
    const int alpha_size = 16;
    const cl_int2 top_left = {0, 0};

    filter_.SetArg(sizeof(cl_mem), g_devices[device_id_].buffers_.ptr(source_plane_));
    filter_.SetArg(sizeof(int), &width_);
    filter_.SetArg(sizeof(int), &height_);
    filter_.SetArg(sizeof(cl_int2), &top_left);
    filter_.SetArg(sizeof(float), &h_);
    filter_.SetArg(sizeof(int), &sample_expand);
    filter_.SetArg(sizeof(cl_mem), g_devices[device_id_].buffers_.ptr(g_gaussian));
    filter_.SetArg(sizeof(int), &linear);
    filter_.SetArg(sizeof(int), &alpha_set_size_);
    filter_.SetArg(sizeof(cl_mem), g_devices[device_id_].buffers_.ptr(alpha_));

    if (filter_.arguments_valid()) {
        filter_.set_work_dim(2);
        const size_t set_local_work_size[2]    = {8, 16};
        // height is increased to offset the fact that 8 work items collaborate on one pixel
        const size_t set_scalar_global_size[2] = {region_width_, region_height_ << 3};
        const size_t set_scalar_item_size[2]   = {1, 1};

        filter_.set_local_work_size(set_local_work_size);
        filter_.set_scalar_global_size(set_scalar_global_size);
        filter_.set_scalar_item_size(set_scalar_item_size);

        return FILTER_OK;
    }

    return FILTER_KERNEL_ARGUMENT_ERROR;
}

result SingleFrame::InitSortKernel(const int &linear) {

    sort_ = ClKernel(device_id_, "Finalise");
    
    const int alpha_size = 16;
    const cl_int2 top_left = {0, 0};

    sort_.SetArg(sizeof(cl_mem), g_devices[device_id_].buffers_.ptr(source_plane_));
    sort_.SetArg(sizeof(int), &region_width_);
    sort_.SetArg(sizeof(cl_int2), &top_left);
    sort_.SetArg(sizeof(int), &linear);
    sort_.SetArg(sizeof(int), &alpha_size);
    sort_.SetArg(sizeof(int), &alpha_set_size_);
    sort_.SetArg(sizeof(cl_mem), g_devices[device_id_].buffers_.ptr(alpha_));
    sort_.SetArg(sizeof(cl_mem), g_devices[device_id_].buffers_.ptr(dest_plane_));

    if (sort_.arguments_valid()) {
        sort_.set_work_dim(2);
        const size_t set_local_work_size[2]        = {8, 16};
        // height is increased to offset the fact that 8 work items collaborate on one pixel
        const size_t set_scalar_global_size[2]    = {region_width_, region_height_ << 3};
        const size_t set_scalar_item_size[2]    = {1, 1};

        sort_.set_local_work_size(set_local_work_size);
        sort_.set_scalar_global_size(set_scalar_global_size);
        sort_.set_scalar_item_size(set_scalar_item_size);

        return FILTER_OK;
    }

    return FILTER_KERNEL_ARGUMENT_ERROR;
}

result SingleFrame::InitInitialiseKernel() {
    initialise_ = ClKernel(device_id_, "Initialise");

    unsigned int initialise = 0;

    initialise_.SetArg(sizeof(cl_mem), g_devices[device_id_].buffers_.ptr(alpha_));
    initialise_.SetArg(sizeof(cl_uint), &initialise);

    if (initialise_.arguments_valid()) {
        const size_t set_local_work_size[1]    = {64};
        const size_t set_scalar_global_size[1] = {region_width_ * region_height_ * alpha_set_size_};
        const size_t set_scalar_item_size[1]   = {1};

        initialise_.set_work_dim(1);
        initialise_.set_local_work_size(set_local_work_size);
        initialise_.set_scalar_global_size(set_scalar_global_size);
        initialise_.set_scalar_item_size(set_scalar_item_size);

        return FILTER_OK;
    }

    return FILTER_KERNEL_ARGUMENT_ERROR;
}

result SingleFrame::CopyTo(const unsigned char *source) {
    return g_devices[device_id_].buffers_.CopyToPlane(source_plane_,
                                                      *source, 
                                                      width_, 
                                                      height_, 
                                                      src_pitch_);

}

result SingleFrame::Execute() {
    result status = FILTER_OK;

    const int rounded_width = region_width_ * ((width_ + region_width_ - 1) / region_width_);
    const int rounded_height = region_height_ * ((height_ + region_height_ - 1) / region_height_);

    for (int region_x = 0; region_x < rounded_width; region_x += region_width_)
        for (int region_y = 0; region_y < rounded_height; region_y += region_height_) {

            const cl_int2 top_left = {region_x, region_y};
            filter_.SetNumberedArg(FILTER_ARG_TOP_LEFT, sizeof(cl_int2), &top_left);
            status = filter_.Execute(cq_, NULL);
            if (status != FILTER_OK) 
                return status;

            sort_.SetNumberedArg(SORT_ARG_TOP_LEFT, sizeof(cl_int2), &top_left);
            status = sort_.Execute(cq_, NULL);
            if (status != FILTER_OK) 
                return status;
        }
        
    return status;
}
