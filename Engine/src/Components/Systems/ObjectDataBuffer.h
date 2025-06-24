#pragma once

#include "ObjectDataBuffers/ObjectDataBase.h"
#include "ObjectDataBuffers/MeshDataBuffer.h"
#include "ObjectDataBuffers/LightDataBuffer.h"
#include "ObjectDataBuffers/CameraDataBuffer.h"
#include "ObjectDataBuffers/ShadowDataBuffer.h"

// This file provides convenient access to all ObjectDataBuffer types
// 
//
// Examples:
// - MeshDataBuffer: Per-object mesh data (model matrix, flags)
// - LightDataBuffer: Per-light data (position, color, type, etc.)
// - CameraDataBuffer: Camera matrices (view, projection)  
// - ShadowDataBuffer: Shadow map data (both regular and cascaded)


