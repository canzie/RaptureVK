#pragma once


#include <string>
#include <vector>

#include "Buffers/IndexBuffers/IndexBuffer.h"
#include "Buffers/VertexBuffers/VertexBuffer.h"

namespace Rapture
{


    struct AllocatorParams {
        void* vertexData = nullptr;
        size_t vertexDataSize = 0;
        const void* indexData = nullptr;
        size_t indexDataSize = 0;
        size_t indexCount = 0;
        unsigned int indexType = 0;
    };

	class Mesh
	{

	public:
		Mesh(std::string filepath);
        //Mesh(std::string filepath, bool useGLTF2=false);
		Mesh() = default;
		~Mesh();

        void setMeshData(const AllocatorParams& params);





	private:
		// indices in the IBO that draw this sub mesh
		//size_t m_indexCount;
		//size_t m_offsetBytes;

        std::shared_ptr<VertexBuffer> m_vertexBuffer;
        std::shared_ptr<IndexBuffer> m_indexBuffer;

	};



}