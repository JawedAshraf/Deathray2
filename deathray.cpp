/* Deathray2 - An Avisynth plug-in filter for spatial/temporal non-local means de-noising.
 *
 * version 1.00
 *
 * Copyright 2015, Jawed Ashraf - Deathray@cupidity.f9.co.uk
 */

#include "result.h"
#include "util.h"
#include "clutil.h"
#include "device.h"
#include "deathray.h"
#include "SingleFrame.h"
#include "MultiFrame.h"
#include "MultiFrameRequest.h"

#define DEVICE 0 // Filter currently only uses a single device


// Globals are used because the cost of re-initialisation per frame is prohibitive

// OpenCL devices
Device  *g_devices      = NULL;
int     g_device_count  = 0;

// OpenCL usage
bool        g_opencl_available              = false;
bool        g_opencl_failed_to_initialise   = false;
cl_context  g_context                       = NULL;
cl_int      g_last_cl_error                 = CL_SUCCESS;

// Buffer containing the gaussian weights
int g_gaussian = 0;

// Frame processing requires 3 planes 
FilterFrame *g_Y;
FilterFrame *g_U;
FilterFrame *g_V;

void GaussianGenerator(const float &sigma, const int &device_id) {
    float two_sigma_squared = 2 * sigma * sigma;

    float gaussian[49]; 
    float gaussian_sum = 0;

    for (int y = -3; y < 4; ++y) {
        for (int x = -3; x < 4; ++x) {
            int index = 7 * (y + 3) + x + 3;
            gaussian[index] = exp(-(x * x + y * y) / two_sigma_squared) / (3.14159265f * two_sigma_squared);
            gaussian_sum += gaussian[index];
        }
    }

    for (int i = 0; i < 49; ++i)
        gaussian[i] /= gaussian_sum;

    g_devices[device_id].buffers_.AllocBuffer(g_devices[device_id].cq(), 49 * sizeof(float), &g_gaussian);
    g_devices[device_id].buffers_.CopyToBuffer(g_gaussian, gaussian, 49 * sizeof(float));
}

Deathray::Deathray(PClip child, 
                   double h_Y, 
                   double h_UV, 
                   int temporal_radius_Y, 
                   int temporal_radius_UV,
                   double sigma,
                   int sample_expand, 
                   int linear,
                   int correction,
                   int balanced,
                   int alpha_size,
                   IScriptEnvironment *env) : GenericVideoFilter(child),
                                              h_Y_(static_cast<float>(h_Y/10000.)), 
                                              h_UV_(static_cast<float>(h_UV/10000.)), 
                                              temporal_radius_Y_(temporal_radius_Y),
                                              temporal_radius_UV_(temporal_radius_UV),
                                              sigma_(static_cast<float>(sigma)),
                                              sample_expand_(sample_expand),
                                              linear_(linear),
                                              correction_(correction),
                                              balanced_(balanced),
                                              alpha_size_(alpha_size / 8),
                                              env_(env) {
}

Deathray::~Deathray() {
    if (g_opencl_available) g_devices[DEVICE].buffers_.DestroyAll();
}

result Deathray::Init() {
    if (g_devices != NULL) return FILTER_OK;

    // No point continuing, as prior attempt failed
    if (g_opencl_failed_to_initialise) return FILTER_ERROR;

    int device_count = 0;
    const string cl_include = "-D ALPHASIZE=" +  GetAlphaSize(alpha_size_);
    result status = StartOpenCL(&device_count, cl_include);
    if (status != FILTER_OK) env_->ThrowError("OpenCL could not start, status=%d and OpenCL status=%d", status, g_last_cl_error);    
    if (device_count != 0) {
        g_opencl_available = true;
        GaussianGenerator(sigma_, DEVICE);
        status = SetupFilters(DEVICE);
    } else {
        g_opencl_failed_to_initialise = true;
    }

    return status;
}

result Deathray::SetupFilters(const int &device_id) {
    result status = FILTER_OK;

    if ((temporal_radius_Y_ == 0 && h_Y_ > 0.f) || (temporal_radius_UV_ == 0 && h_UV_ > 0.f)) {
        status = SingleFrameInit(device_id);
        if (status != FILTER_OK) env_->ThrowError("Single-frame initialisation failed, status=%d and OpenCL status=%d", status, g_last_cl_error);    
    }
    if ((temporal_radius_Y_ > 0 && h_Y_ > 0.f) || (temporal_radius_UV_ > 0 && h_UV_ > 0.f)) {
        status = MultiFrameInit(device_id);
        if (status != FILTER_OK) env_->ThrowError("Multi-frame initialisation failed, status=%d and OpenCL status=%d", status, g_last_cl_error);    
    }    

    return status;
}

PVideoFrame __stdcall Deathray::GetFrame(int n, IScriptEnvironment *env) {
    src_ = child->GetFrame(n, env);
    dst_ = env->NewVideoFrame(vi);

    InitPointers();
    InitDimensions();

    if (h_Y_ == 0.f)                    PassThroughLuma();
    if (h_UV_ == 0.f)                   PassThroughChroma();
    if (h_Y_ == 0.f && h_UV_ == 0.f)    return dst_;

    result status = FILTER_OK;
    status = Init();
    if (status != FILTER_OK || !(vi.IsPlanar())) { 
        if (g_opencl_failed_to_initialise) {
            env->ThrowError("Deathray2: Error in OpenCL status=%d frame %d and OpenCL status=%d", status, n, g_last_cl_error);
        } else {
            env->ThrowError("Deathray2: Check that clip is planar format - status=%d frame %d", status, n);
        }
    }

    if ((temporal_radius_Y_ == 0 && h_Y_ > 0.f) || (temporal_radius_UV_ == 0 && h_UV_ > 0.f))
        SingleFrameCopy();

    if ((temporal_radius_Y_ > 0 && h_Y_ > 0.f) || (temporal_radius_UV_ > 0 && h_UV_ > 0.f))
        MultiFrameCopy(n);

    Execute();

    return dst_;
}

void Deathray::InitPointers() {
    srcpY_ = src_->GetReadPtr(PLANAR_Y);
    srcpU_ = src_->GetReadPtr(PLANAR_U);
    srcpV_ = src_->GetReadPtr(PLANAR_V);    

    dstpY_ = dst_->GetWritePtr(PLANAR_Y);
    dstpU_ = dst_->GetWritePtr(PLANAR_U);
    dstpV_ = dst_->GetWritePtr(PLANAR_V);    
}

void Deathray::InitDimensions() {
    src_pitchY_ = src_->GetPitch(PLANAR_Y);
    src_pitchUV_ = src_->GetPitch(PLANAR_V);

    dst_pitchY_ = dst_->GetPitch(PLANAR_Y);
    dst_pitchUV_ = dst_->GetPitch(PLANAR_V);
                
    row_sizeY_ = src_->GetRowSize(PLANAR_Y); 
    row_sizeUV_ = src_->GetRowSize(PLANAR_V); 
              
    heightY_ = src_->GetHeight(PLANAR_Y);
    heightUV_ = src_->GetHeight(PLANAR_V);
}

void Deathray::PassThroughLuma() {
    env_->BitBlt(dstpY_, dst_pitchY_, srcpY_, src_pitchY_, row_sizeY_, heightY_);
}

void Deathray::PassThroughChroma() {
    env_->BitBlt(dstpV_, dst_pitchUV_, srcpV_, src_pitchUV_, row_sizeUV_, heightUV_);
    env_->BitBlt(dstpU_, dst_pitchUV_, srcpU_, src_pitchUV_, row_sizeUV_, heightUV_);
}

result Deathray::SingleFrameInit(const int &device_id) {
    result status = FILTER_OK;
            
    if (temporal_radius_Y_ == 0 && h_Y_ > 0.f) {
        g_Y = new SingleFrame();
        status = static_cast<SingleFrame*>(g_Y)->Init(device_id, row_sizeY_, heightY_, src_pitchY_, dst_pitchY_, h_Y_, sample_expand_, linear_, correction_, balanced_);
        if (status != FILTER_OK) return status;
    }

    if (temporal_radius_UV_ == 0 && h_UV_ > 0.f) {
        g_U = new SingleFrame();
        g_V = new SingleFrame();

        status = static_cast<SingleFrame*>(g_U)->Init(device_id, row_sizeUV_, heightUV_, src_pitchUV_, dst_pitchUV_, h_UV_, sample_expand_, 0, correction_, 0);
        if (status != FILTER_OK) return status;

        status = static_cast<SingleFrame*>(g_V)->Init(device_id, row_sizeUV_, heightUV_, src_pitchUV_, dst_pitchUV_, h_UV_, sample_expand_, 0, correction_, 0);
        if (status != FILTER_OK) return status;
    }

    return status;
}

void Deathray::SingleFrameCopy() {    
    result status;

    if (temporal_radius_Y_ == 0 && h_Y_ > 0.f) {
        status = static_cast<SingleFrame*>(g_Y)->CopyTo(srcpY_);
        if (status != FILTER_OK) env_->ThrowError("Deathray2: Copy Y to device status=%d and OpenCL status=%d", status, g_last_cl_error);
    }

    if (temporal_radius_UV_ == 0 && h_UV_ > 0.f) {
        status = static_cast<SingleFrame*>(g_U)->CopyTo(srcpU_);
        if (status != FILTER_OK) env_->ThrowError("Deathray2: Copy U to device status=%d and OpenCL status=%d", status, g_last_cl_error);

        status = static_cast<SingleFrame*>(g_V)->CopyTo(srcpV_);
        if (status != FILTER_OK) env_->ThrowError("Deathray2: Copy V to device status=%d and OpenCL status=%d", status, g_last_cl_error);
    }
}

result Deathray::MultiFrameInit(const int &device_id) {
    result status = FILTER_OK;

    if (temporal_radius_Y_ > 0 && h_Y_ > 0.f) {
        g_Y = new MultiFrame();
        status = static_cast<MultiFrame*>(g_Y)->Init(device_id, temporal_radius_Y_, row_sizeY_, heightY_, src_pitchY_, dst_pitchY_, h_Y_, sample_expand_, linear_, correction_, balanced_);
        if (status != FILTER_OK) return status;
    }

    if (temporal_radius_UV_ > 0 && h_UV_ > 0.f) {
        g_U = new MultiFrame();
        status = static_cast<MultiFrame*>(g_U)->Init(device_id, temporal_radius_UV_, row_sizeUV_, heightUV_, src_pitchUV_, dst_pitchUV_, h_UV_, sample_expand_, 0, correction_, 0);
        if (status != FILTER_OK) return status;

        g_V = new MultiFrame();
        status = static_cast<MultiFrame*>(g_V)->Init(device_id, temporal_radius_UV_, row_sizeUV_, heightUV_, src_pitchUV_, dst_pitchUV_, h_UV_, sample_expand_, 0, correction_, 0);
        if (status != FILTER_OK) return status;
    }

    return status;
}

void Deathray::MultiFrameCopy(const int &n) {
    result status = FILTER_OK;

    int frame_number;
    if (temporal_radius_Y_ > 0 && h_Y_ > 0.f) {
        MultiFrameRequest frames_Y;
        static_cast<MultiFrame*>(g_Y)->SupplyFrameNumbers(n, &frames_Y);
        while (frames_Y.GetFrameNumber(&frame_number)) {
            PVideoFrame Y = child->GetFrame(frame_number, env_);
            const unsigned char* ptr_Y = Y->GetReadPtr(PLANAR_Y);
            frames_Y.Supply(frame_number, ptr_Y);
        }
        status = static_cast<MultiFrame*>(g_Y)->CopyTo(&frames_Y);
        if (status != FILTER_OK ) env_->ThrowError("Deathray2: Copy Y to device, status=%d and OpenCL status=%d", status, g_last_cl_error);
    }

    if (temporal_radius_UV_ > 0 && h_UV_ > 0.f) {
        MultiFrameRequest frames_U;
        MultiFrameRequest frames_V;
        static_cast<MultiFrame*>(g_U)->SupplyFrameNumbers(n, &frames_U);
        static_cast<MultiFrame*>(g_V)->SupplyFrameNumbers(n, &frames_V);
        while (frames_U.GetFrameNumber(&frame_number)) {
            PVideoFrame UV = child->GetFrame(frame_number, env_);
            const unsigned char* ptr_U = UV->GetReadPtr(PLANAR_U);
            const unsigned char* ptr_V = UV->GetReadPtr(PLANAR_V);
            frames_U.Supply(frame_number, ptr_U);
            frames_V.Supply(frame_number, ptr_V);
        }
        status = static_cast<MultiFrame*>(g_U)->CopyTo(&frames_U);
        if (status != FILTER_OK ) env_->ThrowError("Deathray2: Copy U to device, status=%d and OpenCL status=%d", status, g_last_cl_error);
        status = static_cast<MultiFrame*>(g_V)->CopyTo(&frames_V);
        if (status != FILTER_OK ) env_->ThrowError("Deathray2: Copy V to device, status=%d and OpenCL status=%d", status, g_last_cl_error);
    }
}

void Deathray::Execute() {    
    cl_uint wait_list_length = 0;
    cl_event wait_list[3];
    result status = FILTER_OK;

    if (h_Y_ > 0.f) {
        status = g_Y->Execute();
        if (status != FILTER_OK) env_->ThrowError("Deathray2: Execute Y kernel status=%d and OpenCL status=%d", status, g_last_cl_error);
        status = g_Y->CopyFrom(dstpY_, wait_list);
        if (status != FILTER_OK) env_->ThrowError("Deathray2: Copy Y to host status=%d and OpenCL status=%d", status, g_last_cl_error);
        ++wait_list_length;
    }

    if (h_UV_ > 0.f) {
        g_U->Execute();
        if (status != FILTER_OK) env_->ThrowError("Deathray2: Execute U kernel status=%d and OpenCL status=%d", status, g_last_cl_error);
        g_U->CopyFrom(dstpU_, wait_list + wait_list_length++);
        if (status != FILTER_OK) env_->ThrowError("Deathray2: Copy U to host status=%d and OpenCL status=%d", status, g_last_cl_error);

        g_V->Execute();
        if (status != FILTER_OK) env_->ThrowError("Deathray2: Execute V kernel status=%d and OpenCL status=%d", status, g_last_cl_error);
        g_V->CopyFrom(dstpV_, wait_list + wait_list_length++);
        if (status != FILTER_OK) env_->ThrowError("Deathray2: Copy V to host status=%d and OpenCL status=%d", status, g_last_cl_error);
    }

    clWaitForEvents(wait_list_length, wait_list);
}

AVSValue __cdecl CreateDeathray(AVSValue args, void *user_data, IScriptEnvironment *env) {

    double h_Y = args[1].AsFloat(1.);
    if (h_Y < 0.) h_Y = 0.;

    double h_UV = args[2].AsFloat(1.);
    if (h_UV < 0.) h_UV = 0.;

    int temporal_radius_Y = args[3].AsInt(0);
    if (temporal_radius_Y < 0) temporal_radius_Y = 0;
    if (temporal_radius_Y > 64) temporal_radius_Y = 64;

    int temporal_radius_UV = args[4].AsInt(0);
    if (temporal_radius_UV < 0) temporal_radius_UV = 0;
    if (temporal_radius_UV > 64) temporal_radius_UV = 64;

    double sigma = args[5].AsFloat(1.);
    if (sigma < 0.1) sigma = 0.1;

    int sample_expand = args[6].AsInt(1);    
    if (sample_expand <= 0) sample_expand = 1;
    if (sample_expand > 4) sample_expand = 4;

    int linear = args[7].AsBool(false) ? 1 : 0;

    int correction = args[8].AsBool(true) ? 1 : 0;

    int balanced = args[9].AsBool(false) ? 1 : 0;

    int alpha_size = args[10].AsInt(128);
    if (alpha_size < 8) alpha_size = 8;
    if (alpha_size > 128) alpha_size = 128;

    return new Deathray(args[0].AsClip(),
                        h_Y, 
                        h_UV, 
                        temporal_radius_Y, 
                        temporal_radius_UV, 
                        sigma,
                        sample_expand, 
                        linear,
                        correction,
                        balanced,
                        alpha_size,
                        env);
}

extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit2(IScriptEnvironment *env) {

    env->AddFunction("deathray2", "c[hY]f[hUV]f[tY]i[tUV]i[s]f[x]i[l]b[c]b[b]b[a]i", CreateDeathray, 0);
    return "Deathray2";
}
