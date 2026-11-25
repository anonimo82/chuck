@import "ModelIO";
@import "ChuGL";

// One mesh-local view. Rebind after replacing the scene or editing topology.
public class ModelGeometryBridge {
    Model3D @ source;
    Geometry geometry;
    int meshIndex;
    int vertexCount;
    int triangleCount;
    int ready;
    int skinned;
    float positions[0];
    float normals[0];
    int triangles[0];
    float uv[0];

    fun int bind(Model3D model, int index) {
        0 => ready;
        if(model == null) return 0;
        model.meshVertices(index) => int nv;
        model.faces(index) => int nf;
        if(nv <= 0 || nf <= 0 || nv > 1000000 || nf > 1000000) return 0;
        if(!model.hasNormals(index)) return 0;
        model.bones(index) => int nb;
        if(nb < 0) return 0;
        float xyz[nv*3];
        float nrm[nv*3];
        int ids[nf*3];
        float tex[nv*2];
        if(!model.exportTriangles(index, ids)) return 0;
        if(nb > 0) {
            if(!model.exportPosedVertices(index, xyz) ||
               !model.exportPosedNormals(index, nrm)) return 0;
        } else {
            if(!model.exportVertices(index, xyz) ||
               !model.exportNormals(index, nrm)) return 0;
        }
        // Missing UVs are represented by zero coordinates.
        model.exportUV(index, tex);
        model @=> source;
        index => meshIndex;
        nv => vertexCount;
        nf => triangleCount;
        nb > 0 => skinned;
        xyz @=> positions;
        nrm @=> normals;
        ids @=> triangles;
        tex @=> uv;
        geometry.vertexAttribute(0, 3, positions);
        geometry.vertexAttribute(1, 3, normals);
        geometry.vertexAttribute(2, 2, uv);
        geometry.indices(triangles);
        1 => ready;
        return 1;
    }

    fun int refresh() {
        if(!ready || source == null) return 0;
        if(source.meshVertices(meshIndex) != vertexCount ||
           source.faces(meshIndex) != triangleCount ||
           (source.bones(meshIndex) > 0) != skinned) return 0;
        if(skinned) {
            if(!source.exportPosedVertices(meshIndex, positions) ||
               !source.exportPosedNormals(meshIndex, normals)) return 0;
        } else {
            if(!source.exportVertices(meshIndex, positions) ||
               !source.exportNormals(meshIndex, normals)) return 0;
        }
        // Publish only after both exports succeed. Indices and UVs are static.
        geometry.vertexAttribute(0, 3, positions);
        geometry.vertexAttribute(1, 3, normals);
        return 1;
    }
}
