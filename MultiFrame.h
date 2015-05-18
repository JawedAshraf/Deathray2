/* Deathray2 - An Avisynth plug-in filter for spatial/temporal non-local means de-noising.
 *
 * version 1.00
 *
 * Copyright 2015, Jawed Ashraf - Deathray@cupidity.f9.co.uk
 */

#ifndef MULTI_FRAME_H_
#define MULTI_FRAME_H_

#include <vector>

#include <CL/cl.h>
#include "CLKernel.h"
#include "FilterFrame.h"

enum result;
class MultiFrameRequest;

class MultiFrame : public FilterFrame
{
public:
    MultiFrame();

    ~MultiFrame() {}

    // Init
    // One-time configuration of this object to handle all multi-frame 
    // processing for the duration of the clip
    result Init(
        const   int     &device_id,         // device used for filtering
        const   int     &temporal_radius,   // frame count both before and after frame being filtered
        const   int     &width,             // width of frame in pixels
        const   int     &height,            // height of frame in pixels
        const   int     &src_pitch,         // length in memory of a row of pixels in source buffer
        const   int     &dst_pitch,         // length in memory of a row of pixels in destination buffer
        const   float   &h,                 // NLM filtering strength
        const   int     &sample_expand,     // factor of radius of 3 to use for sampling
        const   int     &linear,            // TODO delete
        const   int     &correction,        // TODO delete
        const   int     &balanced);         // TODO float for bias: shadows or highlights

    // SupplyFrameNumbers
    // Returns a set of frame numbers, in the MultiFrameRequest
    // object, when Deathray requests which frames should be copied
    // to the device because they are missing
    void SupplyFrameNumbers(
        const   int                 &target_frame_number,   // frame being filtered
                MultiFrameRequest   *required);             // set of frame numbers that are missing from
                                                            // the device and need to be copied to it

    // CopyTo
    // Before processing each of the planes, all frames are
    // copied to the device. Usually most frames will already
    // be on the device. MultiFrameRequest defines the frame 
    // numbers and host pointers of planes that have yet to
    // be copied to the device.
    //
    // Also zeroes the intermediate buffers.
    //
    // Called once per filtered frame
    result CopyTo(
                MultiFrameRequest   *retrieved);    // set of frame numbers to be copied to device

    // Execute
    // Runs all iterations of the temporal filter
    virtual result Execute();

private:

    // InitBuffers
    // Create the intermediate averages, weights and maximum
    // weights buffers and create the destination buffer
    result InitBuffers(
        const   int         &sample_expand);        // factor of radius of 3 to use for sampling

    // InitKernels
    // Configure global arguments for the filter and sort kernels,
    // arguments that won't change over the duration of clip processing
    result InitKernels(
        const   int         &sample_expand,         // factor of radius of 3 to use for sampling
        const   int         &linear,                // TODO delete
        const   int         &correction,            // TODO delete
        const   int         &balanced);             // TODO float for bias: shadows or highlights

    // InitFilterKernel
    // Configure global arguments for the filter and sort kernels,
    // arguments that won't change over the duration of clip processing
    result InitFilterKernel(
        const   int         &sample_expand,         // factor of radius of 3 to use for sampling
        const   int         &linear,                // TODO delete
        const   int         &correction,            // TODO delete
        const   int         &balanced);             // TODO float for bias: shadows or highlights

    // InitSortKernel
    // Configure the sort kernel which takes identifies the best samples
    // in the alpha set and uses them to produce the final filtered pixel
    result InitSortKernel(
        const   int         &linear);               // TODO delete

    // InitFrames
    // Create the Frame objects, one per step of the temporal filter
    result InitFrames();

    // ExecuteFrame
    // Process a single frame in the circular buffer of frames
    result ExecuteFrame(
        const   int         &frame_id,              // frame being filtered
        const   bool        &sample_equals_target,  // frame containing sample pixels is also target of filtering
                cl_event    &copying_target,        // TODO delete
                cl_event    *filter_events);        // TODO delete


    // Frame
    // An object for each of the 2 * temporal_radius + 1 frames, all of which are processed separately.
    //
    // This allows a frame to stay in device memory without being repeatedly copied from host. 
    //
    // The usage of multiple Frame objects mimics a circular buffer. As frame number, n, progresses over
    // the lifetime of filtering a clip, each of the static set  of frame objects will take it in turns 
    // being the "target". e.g. for a cycle of 7 frames each object will do target processing once. 
    // Over this cycle of 7 frames, each Frame object only needs to perform a single copy of host 
    // data to the device
    class Frame {
    public:
        Frame();
        ~Frame() {}

        // Init
        // Tell the frame object to initialise its buffer
        result Init(
            const   int                 &device_id,     // device where buffer reside
                    cl_command_queue    *cq,            // command queue to use for the kernel
            const   ClKernel            &NLM_kernel,    // kernel object to load
            const   int                 &width,         // width in pixels of the frame
            const   int                 &height,        // height in pixels of the frame
            const   int                 &pitch);        // length of a row of pixels in memory

        // IsCopyRequired
        // Queries the Frame to discover if it needs data from the host
        // to be copied to the device, for the frame specified
        bool IsCopyRequired(
            int     &frame_number);                     // frame that may or may not be on device

        // CopyTo
        // All frame objects are given the chance to copy host data to the device, if needed.
        //
        // This handles the once-per-cycle copying of host data to the device
        result CopyTo(
                    int             &frame_number,      // frame number to copy, if required
            const   unsigned char   *const source);     // host buffer containing original pixels

        // Plane
        // Allows the parent to query the frame known to be handling the target 
        // for the plane buffer it's using, so that all the other frames can use the same plane.
        //
        // Also returns the object's event for the copy of data to the buffer
        // so that other Frame objects can use the event as an antecedent
        void Plane(
            int         *plane,                         // returned buffer id for the frame
            cl_event    *target_copied);                // copy event

        // Execute
        // Performs the NLM pass.
        //
        // During each cycle the client instructs a single frame object that it is 
        // handling the target frame. The id of the frame object handling the target frame
        // progresses circularly around the "ring" of Frame objects, as the clip is processed
        result Execute(
            const   bool        &is_sample_equal_to_target,     // specify whether frame is that being filtered
                    cl_event    *antecedent,                    // TODO delete
                    cl_event    *executed);                     // TODO delete

    private:

        int device_id_          ;   // device executing the kernels
        cl_command_queue cq_    ;   // command queue shared by all Frame objects and client object
        ClKernel filter_        ;   // each frame sets arguments for a kernel shared by all
        int frame_number_       ;   // frame being processed
        int plane_              ;   // buffer for the frame being processed
        int width_              ;   // width of plane's content
        int height_             ;   // height of plane's content
        int pitch_              ;   // host plane format allows each row to be potentially longer than width_
        cl_event copied_        ;   // tracks completion of the copy from host to device of the Frame's sample plane
        cl_event wait_list_[2]  ;   // used during execution to track completion of copying of target and sample planes
        int frame_used_         ;   // tracks count of times plane data has been copied to device - enables kludge
    };


    int temporal_radius_        ;   // count of frames either side of target frame that will be included in multi-frame filtering
    vector<Frame> frames_       ;   // set of frame planes including target
    int target_frame_number_    ;   // frame to be filtered
    int alpha_so_far_           ;   // position in alpha buffer where weight/pixel pairs will be written next
    ClKernel filter_            ;   // kernel that performs NLM computations, once per sample plane
    ClKernel sort_              ;   // single invocation of this kernel to sort all samples from all frames
    cl_event copied_            ;   // used to track the final copy to the device - at least one frame is copied to the device
    cl_event executed_          ;   // sort kernel is executed synchronously, but event is used for asynchronous copy back to host

    // Kernel needs to know whether the plane it is sampling from is the target plane
    static const int k_sample_equals_target = 1;
    static const int k_sample_is_not_target = 0;
};

#endif // MULTI_FRAME_H_