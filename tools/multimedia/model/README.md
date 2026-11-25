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

## Procedural meshes

Model3D.createMesh(float[] xyz,int[] triangles) replaces the model with one indexed triangle mesh, default material and identity transform. XYZ triples contain finite coordinates within +/-1e9; indices are zero-based and distinct within each face. Limits: one million vertices and triangles; geometric degeneracy is not tested. createBox(width,height,depth) creates a centered box with eight vertices and twelve outward-facing triangles. Both operations are transactional and reset controls on success. meshVertices(mesh), faces(mesh), vertexAt(mesh,index,axis), faceSize(mesh,face), faceIndex(mesh,face,index) inspect local mesh geometry. setVertex(index,x,y,z) edits generated meshes only. exportVertices(mesh,float[]) and exportTriangles(mesh,int[]) copy to exact-sized preallocated arrays; triangle export rejects nontriangular faces. Existing save applies global transforms and requires a new output directory. No generated normals, UVs, skin weights or multiple meshes yet. No new dependencies. 31 new checks and independent OBJ assertions await Windows testing.

## Procedural mesh editing

addMesh(float[] xyz,int[] triangles) appends an indexed triangle mesh to a generated scene and returns its zero-based index or -1. Maximum 256 meshes, one million total vertices and one million total triangles. setVertex(mesh,index,x,y,z) edits a specific generated mesh; existing setVertex(index,x,y,z) still edits mesh zero. transformMesh(mesh,float[] matrix) applies a row-major 3x4 affine matrix to local vertex positions. Both invalidate normals and tangents. computeNormals(mesh) calculates area-weighted smooth unit vertex normals from triangle winding; fails without changing existing normals if any vertex has zero net normal. hasNormals(mesh) and exportNormals(mesh,float[] xyz) support imported and generated meshes. setUV(mesh,float[] uv) assigns channel-zero 2D coordinates (two values per vertex, no wrapping/clamping); exportUV(mesh,float[] uv) reads that channel in either scene type. UV changes invalidate tangents. Arrays must have exact lengths; all geometry edits apply to generated scenes only, preserving existing global model controls. No material editing, skeletal weighting or automatic UV unwrapping yet. No new dependencies. 43 new checks plus independent OBJ assertions await compilation and Windows testing.
