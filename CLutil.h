/* Deathray2 - An Avisynth plug-in filter for spatial/temporal non-local means de-noising.
 *
 * version 1.00
 *
 * Copyright 2015, Jawed Ashraf - Deathray@cupidity.f9.co.uk
 */

#ifndef _CL_UTIL_H_
#define _CL_UTIL_H_

#include <string>
using namespace std;

#include <CL/cl.h>

enum result;


// GetCLErrorString
// Returns error message in English
char* GetCLErrorString(
    const cl_int &err);                                 // Error number

// GetPlatform
// Finds the first available OpenCL platform
result GetPlatform(
    cl_platform_id *platform);                          // Platform found by OpenCL

// SetContext
// Creates a context solely for GPU devices, setting the 
// global variable g_context
result SetContext(
    const cl_platform_id &platform);                    // Platform that requires the new context

// GetDeviceCount
// Requires that g_context is valid
result GetDeviceCount(
    int* device_count);                                 // Count of devices found by OpenCL

// GetDeviceList 
// Requires that g_context is valid
result GetDeviceList(
    cl_device_id** devices);                            // Array of devices found by OpenCL

#ifndef LIBDEATHRAY2_STATIC
// AssembleSources
// Produces a single string containing the text of all the resources
// specified in the input array
result AssembleSources(
    const   int             *resources,                 // array of resources declared to exist in DLL
    const   int             &resource_count,            // count of resources defined within DLL
            string          *entire_program_source);    // source code concatenated from all resources
#endif

// CompileAll
// All kernels are compiled, for all available devices.
// Requires g_context and g_devices.
// "include" definitions using the OpenCL compiler's 
// -D syntax are applied to the entire source that's compiled
result CompileAll(
    const   int             &device_count,              // count of devices in the ...
    const   cl_device_id    &devices,                   // ... array of devices
    const   string          cl_include);                // "include" definitions

// StartOpenCL
// Get OpenCL running, if possible.
// The global array g_devices is configured based on the 
// count of devices supplied and all of them are
// targetted for compilation
result StartOpenCL(
            int             *device_count,              // count of devices to configure
    const   string          cl_include);                // "include" definitions

#endif  // _CL_UTIL_H_

