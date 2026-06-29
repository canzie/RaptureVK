# Asset

**Source: `Engine/src/asset_manager/Asset.h`**

Variant-based asset container holding one of: [[Shader]], [[Texture]], [[MaterialInstance]], [[Mesh]], or `SceneFileData`. Identified by `AssetHandle` (UUID). Metadata tracks type, storage mode (disk/virtual), file path, and use count.
