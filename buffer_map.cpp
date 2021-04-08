/* Deathray2 - An Avisynth plug-in filter for spatial/temporal non-local means de-noising.
 *
 * version 1.00
 *
 * Copyright 2015, Jawed Ashraf - Deathray@cupidity.f9.co.uk
 */

#include "result.h"
#include "buffer.h"
#include "buffer_map.h"

int BufferMap::NewIndex() {

    if (buffer_map_.size() != 0)
        return buffer_map_.rbegin()->first + 1;
    else
        return 1;
}

result BufferMap::Append(
    Mem     **new_mem, 
    int     *new_index) {

    pair<map<int, Mem*>::iterator, bool> insertionStatus;

    *new_index = NewIndex();
    insertionStatus = buffer_map_.insert(pair<int, Mem*>(*new_index, *new_mem));

    if (insertionStatus.second)
        return FILTER_OK; 
    else
        return FILTER_ERROR;
}

result BufferMap::AllocBuffer(
    const   cl_command_queue    &cq,        
    const   size_t              &bytes,        
            int                 *new_index) {

    result status = FILTER_OK;

    Buffer *new_float_buffer = new Buffer;
    new_float_buffer->Init(cq, bytes);
    if (new_float_buffer->valid()) {
        Mem *new_mem = new_float_buffer;
        status = Append(&new_mem, new_index);
        return status;
    } else {
        return FILTER_BUFFER_ALLOCATION_FAILED;
    }
}

result BufferMap::CopyToBuffer(
    const   int     &index,            
    const   void    *host_buffer,     
    const   size_t  &bytes) {

    Buffer *destination;
    destination = static_cast<Buffer*>(buffer_map_[index]);
    return destination->CopyTo(host_buffer, bytes);
}

result BufferMap::CopyFromBuffer(
    const   int     &index,            
    const   size_t  &bytes,             
            void    *host_buffer) {

    Buffer *source;
    source = static_cast<Buffer*>(buffer_map_[index]);
    return source->CopyFrom(bytes, host_buffer);
}

result BufferMap::AllocPlane(
    const   cl_command_queue    &cq,    
    const   int                 &width, 
    const   int                 &height,
            int                 *new_index) {

    result status = FILTER_OK;

    Plane *new_plane = new Plane;
    new_plane->Init(cq, width, height, 2, 0);
    if (new_plane->valid()) {
        Mem *new_mem = new_plane;
        status = Append(&new_mem, new_index);
        return status;
    } else {
        return FILTER_PLANE_ALLOCATION_FAILED;
    }
}

result BufferMap::CopyToPlane(
    const   int     &index,
    const   byte    &host_buffer,             
    const   int     &host_cols,                 
    const   int     &host_rows,                 
    const   int     &host_pitch) {

    Plane *destination;
    destination = static_cast<Plane*>(buffer_map_[index]);
    return destination->CopyTo(host_buffer, host_cols, host_rows, host_pitch);
}

result BufferMap::CopyToPlaneAsynch(
    const   int         &index,
    const   byte        &host_buffer,             
    const   int         &host_cols,                 
    const   int         &host_rows,                 
    const   int         &host_pitch,
            cl_event    *event) {

    Plane *destination;
    destination = static_cast<Plane*>(buffer_map_[index]);
    return destination->CopyToAsynch(host_buffer, host_cols, host_rows, host_pitch, event);
}

result BufferMap::CopyFromPlane(
    const   int     &index,
    const   int     &host_cols,                 
    const   int     &host_rows,                 
    const   int     &host_pitch,
            byte    *host_buffer) {

    Plane *source;
    source = static_cast<Plane*>(buffer_map_[index]);
    return source->CopyFrom(host_cols, host_rows, host_pitch, host_buffer);
}

result BufferMap::CopyFromPlaneAsynch(
    const   int         &index,
    const   int         &host_cols,                 
    const   int         &host_rows,                 
    const   int         &host_pitch,
    const   cl_event    *antecedent,
            cl_event    *event,
            byte        *host_buffer) {

    Plane *source;
    source = static_cast<Plane*>(buffer_map_[index]);
    return source->CopyFromAsynch(host_cols, host_rows, host_pitch, antecedent, event, host_buffer);
}

void BufferMap::Destroy(
    const int &index) { 

    if (! ValidIndex(index)) return;    
        
    // Check reference count
    cl_uint reference_count = 0;
    clGetMemObjectInfo(buffer_map_[index]->obj(),
                       CL_MEM_REFERENCE_COUNT,
                       sizeof(cl_uint),
                       &reference_count,
                       0);

    // Repeatedly release it to ensure it will be destroyed
    for (cl_uint i = 0; i < reference_count; ++i) {
        clReleaseMemObject(buffer_map_[index]->obj());
    }

    delete buffer_map_[index];
    buffer_map_.erase(index);
}

void BufferMap::DestroyAll() {
    if (buffer_map_.size() == 0) return;

    map<int, Mem*>::iterator each_buffer;
    map<int, Mem*>::iterator next_buffer;
    each_buffer = buffer_map_.begin();
    while (each_buffer != buffer_map_.end()) {
        next_buffer = each_buffer;
        ++next_buffer;
        Destroy(each_buffer->first);
        each_buffer = next_buffer;
    }
    buffer_map_.clear();
}

bool BufferMap::ValidIndex(
    const int &index) {

    return buffer_map_.count(index) > 0;
}

cl_mem* BufferMap::ptr(
    const int &index) {

    return buffer_map_[index]->ptr();
}
