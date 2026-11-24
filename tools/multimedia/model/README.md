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
