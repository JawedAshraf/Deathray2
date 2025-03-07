/* Deathray2 - An Avisynth plug-in filter for spatial/temporal non-local means de-noising.
 *
 * version 1.00
 *
 * Copyright 2015, Jawed Ashraf - Deathray@cupidity.f9.co.uk
 */

#ifndef _DEATHRAY_
#define _DEATHRAY_

#include "avisynth.h"

enum result;

class Deathray : public GenericVideoFilter {
public:

    Deathray(
        PClip _child, 
        double h_Y, 
        double h_UV, 
        int t_Y, 
        int t_UV, 
        double sigma, 
        int sample_expand, 
        int linear, 
        int correction, 
        int balanced, 
        int alpha_size, 
        IScriptEnvironment* env);

    ~Deathray();

    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);

private:
    // Init
    // Verifies that OpenCL is ready to go and that at least
    // one device is ready.
    result Init();

    // SetupFilters
    // Configure the global classes for single frame and multi
    // frame filtering.
    result SetupFilters(
        const int &device_id);  // single GPU device used for filtering

    // InitPointers
    // Get the pointers for single frame filtering
    void InitPointers();
    
    // InitDimensions
    // Get the dimensions for filtering
    void InitDimensions();

    // PassThroughLuma
    // Puts unfiltered luma in destination
    void PassThroughLuma();
    
    // PassThroughChroma
    // Puts unfiltered chroma in destination
    void PassThroughChroma();

    // SingleFrameInit
    // Configure the plane-specific objects
    // for single frame filtering
    result SingleFrameInit(
        const int &device_id);  // single GPU device used for filtering

    // SingleFrameCopy
    // Copies the plane types that require
    // single frame filtering
    void SingleFrameCopy();

    // MultiFrameInit
    // Configure the plane-type specific objects
    // for multi frame filtering
    result MultiFrameInit(
        const int &device_id);  // single GPU device used for filtering

    // MultiFrameCopy
    // Queries each plane type for the frame numbers
    // it requires, then provides the relevant host pointers
    // and activates the copy of host data to the device.
    void MultiFrameCopy(
        const int &n);          // frame number being filtered

    // Execute
    // Filter all applicable frames and copy the
    // result back to the host
    void Execute();

    float h_Y_              ;   // strength of luma noise reduction
    float h_UV_             ;   // strength of chroma noise reduction
    int temporal_radius_Y_  ;   // luma temporal radius
    int temporal_radius_UV_ ;   // chroma temporal radius
    float sigma_            ;   // gaussian weights are computed based upon sigma
    int sample_expand_      ;   // factor by which the sample radius is expanded, e.g. 2 means sample radius of 6, since kernel has radius 3
    int linear_             ;   // process plane in linear space instead of gamma space when set to 1
    int correction_         ;   // apply a post-filtering correction
    int balanced_           ;   // balanced tonal range de-noising
    int alpha_size_         ;   // 1/8th count of sorted samples used for filtering

    // Following are standard Avisynth properties of environment, source and destination: frames and planes
    IScriptEnvironment *env_;

    PVideoFrame src_;
    PVideoFrame dst_;

    const unsigned char *srcpY_;
    const unsigned char *srcpU_;
    const unsigned char *srcpV_;

    unsigned char *dstpY_;
    unsigned char *dstpU_;
    unsigned char *dstpV_;    

    int src_pitchY_;
    int src_pitchUV_;

    int dst_pitchY_;
    int dst_pitchUV_;
                    
    int row_sizeY_; 
    int row_sizeUV_; 
                  
    int heightY_;
    int heightUV_;

};

#endif // _DEATHRAY_