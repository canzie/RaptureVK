# Mesh

**Source: `Engine/src/meshes/Mesh.h/.cpp`**

Wraps vertex and index GPU buffer data used for rendering. Creates [[VertexBuffer]] and [[IndexBuffer]] via the [[Buffer Pool System]] and exposes buffer allocations for [[MDI Batching]] grouping.
