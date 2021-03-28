/* Deathray2 - An Avisynth plug-in filter for spatial/temporal non-local means de-noising.
 *
 * version 1.00
 *
 * Copyright 2015, Jawed Ashraf - Deathray@cupidity.f9.co.uk
 */

#include "result.h"
#include "util.h"
#include "device.h"
#include "buffer_map.h"
#include "CLKernel.h"
#include "MultiFrame.h"
#include "MultiFrameRequest.h"

extern  int         g_device_count;
extern  Device      *g_devices;
extern  cl_context  g_context;
extern  int         g_gaussian;

#define FILTER_ARG_TARGET_PLANE 0
#define FILTER_ARG_SAMPLE_PLANE 1
#define FILTER_ARG_SAMPLE_EQUALS_TARGET 2
#define FILTER_ARG_WIDTH 3
#define FILTER_ARG_HEIGHT 4
#define FILTER_ARG_TOP_LEFT 5
#define FILTER_ARG_H 6
#define FILTER_ARG_SAMPLE_EXPAND 7
#define FILTER_ARG_G_GAUSSIAN 8
#define FILTER_ARG_LINEAR 9
#define FILTER_ARG_ALPHA_SET_SIZE 10
#define FILTER_ARG_ALPHA_SO_FAR 11
#define FILTER_ARG_REGION_ALPHA 12

MultiFrame::MultiFrame() {
    device_id_          = 0;
    temporal_radius_    = 0;
    frames_.clear();
    dest_plane_         = 0;
    width_              = 0;
    height_             = 0;
    src_pitch_          = 0;
    dst_pitch_          = 0;
}

result MultiFrame::Init(
    const   int     &device_id,
    const   int     &temporal_radius,
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

    device_id_          = device_id;
    temporal_radius_    = temporal_radius;
    width_              = width;    
    height_             = height;    
    src_pitch_          = src_pitch;
    dst_pitch_          = dst_pitch;
    region_width_       = width;
    region_height_      = 8;
    h_                  = 1.f/h;
    cq_                 = g_devices[device_id_].cq();

    if (width_ == 0 || height_ == 0 || src_pitch_ == 0 || dst_pitch_ == 0 || h == 0 ) 
        return FILTER_INVALID_PARAMETER;

    alpha_set_size_ = GetAlphaSetSize(temporal_radius, sample_expand);

    status = InitBuffers(sample_expand);
    if (status != FILTER_OK) return status;
    status = InitKernels(sample_expand, linear, correction, balanced);
    if (status != FILTER_OK) return status;
    status = InitFrames();

    return status;                        
}

result MultiFrame::InitBuffers(const int &sample_expand) {
    result status = FILTER_OK;

    status = g_devices[device_id_].buffers_.AllocPlane(cq_, width_, height_, &dest_plane_);
    if (status != FILTER_OK) return status;

    const int alpha_buffer_size = GetAlphaBufferSize(temporal_radius_,
                                                    region_width_, 
                                                    region_height_,
                                                    sample_expand)
                                * sizeof(cl_uint);

    status = g_devices[device_id_].buffers_.AllocBuffer(cq_, alpha_buffer_size, &alpha_);

    return status;
}

result MultiFrame::InitKernels(
    const int &sample_expand,
    const int &linear,
    const int &correction,
    const int &balanced) {

    result status = FILTER_OK;

    status = InitFilterKernel(sample_expand, linear, correction, balanced);
    if (status != FILTER_OK) return status;

    status = InitSortKernel(linear);
    if (status != FILTER_OK) return status;

    return status;
}

result MultiFrame::InitFilterKernel(
    const int &sample_expand,
    const int &linear,
    const int &correction,
    const int &balanced) {

    filter_ = ClKernel(device_id_, "NLMMultiFrameFourPixel");
    filter_.SetNumberedArg(FILTER_ARG_WIDTH, sizeof(int), &width_);
    filter_.SetNumberedArg(FILTER_ARG_HEIGHT, sizeof(int), &height_);
    filter_.SetNumberedArg(FILTER_ARG_H, sizeof(float), &h_);
    filter_.SetNumberedArg(FILTER_ARG_SAMPLE_EXPAND, sizeof(int), &sample_expand);
    filter_.SetNumberedArg(FILTER_ARG_G_GAUSSIAN, sizeof(cl_mem), g_devices[device_id_].buffers_.ptr(g_gaussian));
    filter_.SetNumberedArg(FILTER_ARG_LINEAR, sizeof(int), &linear);
    filter_.SetNumberedArg(FILTER_ARG_ALPHA_SET_SIZE, sizeof(int), &alpha_set_size_);
    filter_.SetNumberedArg(FILTER_ARG_REGION_ALPHA, sizeof(cl_mem), g_devices[device_id_].buffers_.ptr(alpha_));

    if (filter_.arguments_valid()) {
        filter_.set_work_dim(2);
        const size_t set_local_work_size[2]        = {8, 16};
        // height is increased to offset the fact that 8 work items collaborate on one pixel
        const size_t set_scalar_global_size[2]    = {region_width_, region_height_ << 3};
        const size_t set_scalar_item_size[2]    = {1, 1};

        filter_.set_local_work_size(set_local_work_size);
        filter_.set_scalar_global_size(set_scalar_global_size);
        filter_.set_scalar_item_size(set_scalar_item_size);

        return FILTER_OK;                        
    }

    return FILTER_KERNEL_ARGUMENT_ERROR;
}

result MultiFrame::InitSortKernel(const int &linear) {

    sort_ = ClKernel(device_id_, "Finalise");
    
    const int alpha_size = 16;
    const cl_int2 top_left = {0, 0};

    sort_.SetNumberedArg(1, sizeof(int), &region_width_);
    sort_.SetNumberedArg(2, sizeof(cl_int2), &top_left);
    sort_.SetNumberedArg(3, sizeof(int), &linear);
    sort_.SetNumberedArg(4, sizeof(int), &alpha_size);
    sort_.SetNumberedArg(5, sizeof(int), &alpha_set_size_);
    sort_.SetNumberedArg(6, sizeof(cl_mem), g_devices[device_id_].buffers_.ptr(alpha_));
    sort_.SetNumberedArg(7, sizeof(cl_mem), g_devices[device_id_].buffers_.ptr(dest_plane_));

    if (sort_.arguments_valid()) {
        sort_.set_work_dim(2);
        const size_t set_local_work_size[2]     = {8, 16};
        // height is increased to offset the fact that 8 work items collaborate on one pixel
        const size_t set_scalar_global_size[2]  = {region_width_, region_height_ << 3};
        const size_t set_scalar_item_size[2]    = {1, 1};

        sort_.set_local_work_size(set_local_work_size);
        sort_.set_scalar_global_size(set_scalar_global_size);
        sort_.set_scalar_item_size(set_scalar_item_size);

        return FILTER_OK;
    }

    return FILTER_KERNEL_ARGUMENT_ERROR;
}

result MultiFrame::InitFrames() {    
    const int frame_count = 2 * temporal_radius_ + 1;
    frames_.reserve(frame_count);
    for (int i = 0; i < frame_count; ++i) {
        Frame new_frame;
        frames_.push_back(new_frame);
        frames_[i].Init(device_id_, &cq_, filter_, width_, height_, src_pitch_);
    }

    if (frames_.size() != frame_count)
        return FILTER_MULTI_FRAME_INITIALISATION_FAILED;

    return FILTER_OK;
}

void MultiFrame::SupplyFrameNumbers(
    const   int                 &target_frame_number, 
            MultiFrameRequest   *required) {

    target_frame_number_ = target_frame_number;

    for (int i = -temporal_radius_, frame_id = 0; i <= temporal_radius_; ++i, ++frame_id) {
        int frame_number = target_frame_number_ - ((target_frame_number_ - i + temporal_radius_) % frames_.size()) + temporal_radius_;
        if (frames_[frame_id].IsCopyRequired(frame_number))
            required->Request(frame_number);    
    }
}

result MultiFrame::CopyTo(MultiFrameRequest *retrieved) {
    result status = FILTER_OK;

    for (int i = -temporal_radius_, frame_id = 0; i <= temporal_radius_; ++i, ++frame_id) {
        int frame_number = target_frame_number_ - ((target_frame_number_ - i + temporal_radius_) % frames_.size()) + temporal_radius_;
        status = frames_[frame_id].CopyTo(frame_number, retrieved->Retrieve(frame_number));
        if (status != FILTER_OK) return status;
    }
    clFinish(cq_);
    return status;
}

result MultiFrame::ExecuteFrame(
    const   int         &frame_id,
    const   bool        &sample_equals_target) {

    filter_.SetNumberedArg(FILTER_ARG_ALPHA_SO_FAR, sizeof(int), &alpha_so_far_);
    result status = frames_[frame_id].Execute(sample_equals_target);
    alpha_so_far_ += alpha_set_size_ / (8 * (2 * temporal_radius_ + 1)); // TODO make this a function
    return status;
}

result MultiFrame::Execute() {
    result status = FILTER_OK;

    // Query the Frame object handling the target frame to get the plane for the other Frames to use
    int target_frame_id = (target_frame_number_ + temporal_radius_) % frames_.size();

    int target_frame_plane;         
    cl_event copying_target;
    frames_[target_frame_id].Plane(&target_frame_plane, &copying_target);
    filter_.SetNumberedArg(FILTER_ARG_TARGET_PLANE, sizeof(cl_mem), g_devices[device_id_].buffers_.ptr(target_frame_plane));
    
    const int rounded_width = region_width_ * ((width_ + region_width_ - 1) / region_width_);
    const int rounded_height = region_height_ * ((height_ + region_height_ - 1) / region_height_);

    for (int region_x = 0; region_x < rounded_width; region_x += region_width_)
        for (int region_y = 0; region_y < rounded_height; region_y += region_height_) {
            const cl_int2 top_left = {region_x, region_y};
            alpha_so_far_ = 0;
            filter_.SetNumberedArg(FILTER_ARG_TOP_LEFT, sizeof(cl_int2), &top_left);

            for (int i = 0; i < 2 * temporal_radius_ + 1; ++i) {
                bool sample_equals_target = i == target_frame_id;
                if (!sample_equals_target) { // exclude the target frame so that it is processed last - TODO unnecessary
                    status = ExecuteFrame(i, sample_equals_target);
                    if (status != FILTER_OK) return status;
                }
            }
            status = ExecuteFrame(target_frame_id, true);
            if (status != FILTER_OK) return status;

            sort_.SetNumberedArg(0, sizeof(cl_mem), g_devices[device_id_].buffers_.ptr(target_frame_plane));
            sort_.SetNumberedArg(2, sizeof(cl_int2), &top_left);
            status = sort_.Execute(cq_, NULL);
            if (status != FILTER_OK) return status;
        }

    return status;
}

// Frame
MultiFrame::Frame::Frame() {
    frame_number_   = 0;
    plane_          = 0;    
    width_          = 0;
    height_         = 0;
    pitch_          = 0;
}

result MultiFrame::Frame::Init(
    const   int                 &device_id,
            cl_command_queue    *cq, 
    const   ClKernel            &filter,
    const   int                 &width, 
    const   int                 &height, 
    const   int                 &pitch) {

    // Setting this frame's kernel object to the client's kernel object means all frames share the 
    // same instance, and therefore each Frame object only needs to do minimal argument
    // setup.
                        
    device_id_  = device_id;
    cq_         = *cq;
    filter_     = filter;
    width_      = width;
    height_     = height;
    pitch_      = pitch;
    frame_used_ = 0;

    return g_devices[device_id_].buffers_.AllocPlane(cq_, width_, height_, &plane_);
}

bool MultiFrame::Frame::IsCopyRequired(int &frame_number) {
    // BUG: erroneous frames will appear early in clip
    // FIX: frame_used_ < 3 is a kludge.
    if (frame_used_ < 3 || (frame_number != frame_number_)) 
        return true;
    else
        return false;
}

result MultiFrame::Frame::CopyTo(
            int             &frame_number, 
    const   unsigned char   *const source) {

    result status = FILTER_OK;

    if (IsCopyRequired(frame_number)) {
        frame_number_ = frame_number;
        status = g_devices[device_id_].buffers_.CopyToPlane(plane_,
                                                           *source, 
                                                           width_, 
                                                           height_, 
                                                           pitch_);
    }
    copied_ = NULL;
    ++frame_used_;
    return status;
}

void MultiFrame::Frame::Plane(
    int         *plane,
    cl_event    *target_copied) {

    *plane = plane_;
    *target_copied = copied_;
}

result MultiFrame::Frame::Execute(
    const   bool        &is_sample_equal_to_target) {

    result status = FILTER_OK;

    int sample_equals_target = is_sample_equal_to_target ? k_sample_equals_target : k_sample_is_not_target;

    filter_.SetNumberedArg(FILTER_ARG_SAMPLE_PLANE, sizeof(cl_mem), g_devices[device_id_].buffers_.ptr(plane_));
    filter_.SetNumberedArg(FILTER_ARG_SAMPLE_EQUALS_TARGET, sizeof(int), &sample_equals_target);

    status = filter_.Execute(cq_, NULL);
    return status;
}
