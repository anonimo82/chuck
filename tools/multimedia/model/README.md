# ModelIO (experimental)
Optional native Chugin using Assimp. Install libassimp-dev, pkg-config and a C++17 compiler on Linux, or assimp and pkg-config on macOS. Run make in this directory. Windows build pending. Requires matching ChucK DLL API 10.2.

- ModelIO.valid(path): 1 on successful import, otherwise 0.
- ModelIO.meshes(path): number of meshes, or -1 on failure.
- ModelIO.vertices(path): total mesh vertex count, or -1 on failure.
- ModelIO.convert(source, destination): 1 on successful export, otherwise 0.

Allowed inputs: OBJ, FBX, glTF, GLB, Collada, STL, PLY, 3DS, OFF, X3D, BLEND.
Mapped outputs: OBJ, FBX, glTF, GLB, Collada, STL, PLY, 3DS, X3D.
Actual importer/exporter availability depends on the installed Assimp version and build. Conversion may lose materials, animation, textures or other data. Destination must be in a new directory whose parent already exists (e.g. converted/model.obj); exporters can create sidecars. Failed exports may leave partial files in that directory. Only use trusted local assets. No geometry arrays or direct ChuGL scene integration yet. Synchronous offline operations, not real-time safe.

Build and runtime tests pending.

## Persistent Model3D

```chuck
@import "ModelIO";
Model3D model;
if(model.load("input.obj")) {
    2.0 => model.x;
    45.0 => model.rotationY;
    1.5 => model.scaleX;
    1.5 => model.scaleY;
    1.5 => model.scaleZ;
    <<< model.save("exported/model.glb") >>>;
}
```

Each instance retains an independent Assimp scene. load(path) replaces the scene and resets controls only on success. loaded(), meshes(), vertices(), reset(), clear(), save(path) are available; counts return -1 when unloaded. Nine float getter/setters: x/y/z (model units), rotationX/Y/Z (degrees, right-handed), scaleX/Y/Z (positive factors 0.0001–10000). Translation and angle inputs are limited to +/-1e9. Defaults are zero translation/rotation and unit scale. Invalid setters return NaN without altering previous values. Export deep-copies the scene and adds a parent node with matrix T * Rz * Ry * Rx * S, preserving the source root and its animation channels structurally; actual exporter fidelity varies. Transforms are about the scene origin, not its bounding-box center. No mesh editing, animation sampling, or ChuGL integration yet. Export requires a new directory as for ModelIO.convert; failed exports may leave partial files. Synchronous offline operations. No KeyframeIO dependency; any numeric source can drive the properties. Twenty-nine new smoke checks and independent OBJ vertex assertions await execution.
