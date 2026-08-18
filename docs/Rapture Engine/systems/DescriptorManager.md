# DescriptorManager

**Source: `Engine/src/gpu/descriptors/DescriptorManager.h/.cpp`**

Manages descriptor sets for the bindless rendering pipeline. Binding locations encode the set and binding: `XYZ` means `SET=X BIND=YZ`.

- **Set 0** (000-099): Common scene data — camera, lights, shadows, DDGI probes, MDI info
- **Set 1** (100-199): Material resources
- **Set 2** (200-299): Object/mesh data
- **Set 3** (300-399): Bindless resources — textures, SSBOs, acceleration structures
- **Set 4** (400-499): Custom per-system bindings — storage images, system-specific resources
