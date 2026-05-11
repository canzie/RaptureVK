#pragma once

#include "object_data_buffers/ObjectDataBase.h"
#include "object_data_buffers/MeshDataBuffer.h"
#include "object_data_buffers/LightDataBuffer.h"
#include "object_data_buffers/CameraDataBuffer.h"
#include "object_data_buffers/ShadowDataBuffer.h"

// This file provides convenient access to all ObjectDataBuffer types
// 
//
// Examples:
// - MeshDataBuffer: Per-object mesh data (model matrix, flags)
// - LightDataBuffer: Per-light data (position, color, type, etc.)
// - CameraDataBuffer: Camera matrices (view, projection)  
// - ShadowDataBuffer: Shadow map data (both regular and cascaded)


