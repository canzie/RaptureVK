

### Advanced Optimization: Multi-Draw Indirect (MDI)

We then explored a more powerful, modern technique to drastically reduce the number of draw calls issued by the CPU.

*   **Design Decision**: Shift from CPU-driven rendering to **GPU-driven rendering** using `vkCmdDrawIndexedIndirect`.
*   **Mechanism**:
    *   Instead of looping and calling `vkCmdDrawIndexed` on the CPU, we build two large GPU buffers:
        1.  A **Draw Command Buffer** (`VkDrawIndexedIndirectCommand[]`) containing all the draw parameters (index count, offsets, etc.).
        2.  An **Object Data Buffer** (SSBO) containing all per-object data like model matrices and material indices.
    *   The vertex shader uses the `gl_DrawID` built-in variable to look up the correct data from the Object Data Buffer for each mesh it's told to draw.
*   **Takeaway**: This technique reduces thousands of potential CPU draw calls into a single API call, dramatically lowering CPU overhead and driver work. It requires a "bindless" approach to materials, treating them as data in a buffer rather than state to be bound.

---

### Enabling Flexibility: Custom Shaders via Templating

To overcome the limitation of a single, hardcoded G-Buffer shader, we designed a system for user-defined materials without needing a custom shader compiler.

*   **Design Decision**: Use a **Shader Templating** or **Ubershader** approach.
*   **Mechanism**:
    *   The engine provides a fixed **G-Buffer "shell" shader** with a defined output structure and a placeholder for custom logic.
    *   A shader graph UI generates a **GLSL "core" snippet** that fills in this placeholder with the user's logic (e.g., how to calculate albedo, normals, etc.).
    *   The engine stitches these two parts together at runtime, compiles the result to a unique SPIR-V shader, and caches the resulting `VkPipeline`.
*   **Takeaway**: This provides immense creative freedom for users to define material appearances while ensuring the output always conforms to what the engine's G-Buffer expects. The trade-off is that each unique material now generates a unique pipeline, which can lead to expensive state changes if not managed correctly.

---

### The Grand Unification: MDI with Custom Shaders

Finally, we reconciled the apparent conflict between MDI (which prefers one pipeline) and Custom Shaders (which create many pipelines).

*   **Major Design Decision**: **Batch MDI calls by pipeline state.**
*   **Mechanism**:
    1.  **Gather & Sort**: On the CPU, first iterate through all visible objects and sort them into batches based on their unique `VkPipeline` (i.e., by their material).
    2.  **Pack & Upload**: Pack the object and draw data from all batches into the large, contiguous GPU buffers.
    3.  **Bind & Draw Indirect**: In the command buffer, loop through the sorted batches. For each batch, **bind its unique pipeline once**, and then issue a single `vkCmdDrawIndexedIndirect` call for all the objects in that batch.
*   **Final Takeaway**: This hybrid architecture represents the pinnacle of modern renderer design. It achieves massive performance gains by minimizing both the number of draw calls *and* the number of pipeline state changes, while retaining the full flexibility of a custom material system. It intelligently delegates work to the GPU and makes the optimal trade-offs for performance and flexibility.