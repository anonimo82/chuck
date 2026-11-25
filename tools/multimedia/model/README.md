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

## Materials, vertex colors and bind-pose skinning

addMaterial(name,r,g,b,opacity) appends a diffuse material (RGBA in [0,1]) and returns its index or -1. materials() counts materials; materialAt(mesh) reads the material index; assignMaterial(mesh,material) assigns it to a generated mesh. setColors(mesh,float[] rgba) and exportColors(mesh,float[] rgba) exchange vertex-major normalized RGBA in channel zero. setSkin(mesh,boneCount,float[] weights) creates a flat skeleton of identity child nodes and identity inverse bind matrices. Input is vertex-major dense weights, 1–64 bones, at most four positive influences per vertex, every vertex and bone must have a positive influence, normalized per vertex. Maximum 16 million entries; one skin assignment per mesh. bones(mesh) and exportWeights(mesh,float[] weights) read mesh-local bone order and weights including imported meshes. All new mutations are generated-scene only. This is a minimal bind-pose representation, not articulated hierarchy editing, animation, automatic weighting or deformation. glTF is the intended interchange format; other exporters may discard attributes. 39 new checks plus independent glTF structure assertions await Windows compilation/testing. No new dependencies.

## Generated skeleton hierarchy and poses

boneParent(mesh,bone) returns parent bone index, -1 for scene-root parent, -2 for invalid input. setBoneParent(mesh,bone,parent) reparents within the same generated skin (-1 means scene root), rejects cycles, preserves local rather than world transforms. setBonePose(mesh,bone,x,y,z,rotationX,rotationY,rotationZ) sets local translation and Euler rotation in degrees (T*Rz*Ry*Rx). resetPose(mesh) resets local matrices to identity but retains hierarchy. exportPosedVertices(mesh,float[] xyz) evaluates linear blend skinning in mesh-local space from original vertices, current bone hierarchy and inverse bind matrices; exact preallocated array length 3*vertex count. Original vertices and normals remain unchanged; global model controls are excluded. Generated identity bind poses only; use resetPose before exporting a rest-pose skeleton, or save current node transforms as a posed skeleton where supported. No animation tracks, custom bind matrices, posed normals, imported skeleton editing or real-time guarantee. 36 new checks and independent glTF hierarchy assertions await Windows compilation/testing. No new dependencies.

## Skeletal animation clips

addAnimation(mesh,name,float[] times,float[] poses) creates an Assimp animation for a generated skinned mesh and returns its scene-wide clip index, or -1. Times are seconds, start at zero and increase strictly; 2–10000 keys. Poses are frame-major, then bone-major, with six values per bone (local XYZ translation, XYZ Euler rotation in degrees, same convention as setBonePose). All bones are keyed; scale remains identity. Duplicate names are rejected. Maximum 256 clips, 6 million pose values per clip. animations() reports scene clip count (-1 unloaded); animationDuration(index) reports seconds (-1 if invalid or unknown tick rate). sampleAnimation(index,seconds,loop) applies a generated clip to the bone nodes; loop=0 clamps, loop=1 wraps including negative times. Translation interpolates linearly and rotation uses normalized shortest-path quaternion interpolation. exportPosedVertices evaluates the sampled pose; rest vertices are unchanged. Call resetPose before saving to glTF/GLB to retain identity bind nodes; the clip channels are exported by Assimp where supported. Imported clips can be counted but not sampled. No real-time guarantee, automatic playback, scale tracks, or ChuGL integration. Build and runtime verification pending.

## Imported animation sampling

sampleAnimation also evaluates imported node animation channels using their native positive ticks-per-second rate and independent position, quaternion rotation and scale key grids. Each grid clamps to its endpoints; the clip itself clamps or loops as before. Imported transforms are held in a per-instance overlay; switching clips restores original transforms for unkeyed nodes, and resetAnimation() clears the overlay (imported scenes only). exportPosedVertices supports imported skinned meshes with a unique mesh attachment and unique bone node names, using inverse(mesh world) * bone world * inverse bind. Unweighted vertices retain rest coordinates. Ambiguous names, instanced meshes, singular mesh transforms, unknown tick rates and morph/mesh animation channels are rejected. Save exports the original imported scene and clips, not the sampled overlay. Assimp interpolation metadata and nonconstant channel extrapolation are not reproduced; no automatic playback. glTF round-trip smoke checks added; native validation pending.
