# AssetRef

**Source: `Engine/src/asset_manager/Asset.h`**

Reference-counted wrapper around an [[Asset]]. Increments use count on copy, decrements on destruction. The [[AssetManager]] owns the asset data; `AssetRef` only tracks usage.
