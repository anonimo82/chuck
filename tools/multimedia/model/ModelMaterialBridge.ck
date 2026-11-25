@import "ModelIO";
@import "ChuGL";

// PBR for opaque materials, Phong fallback for scalar opacity.
// Keep the original PBR field for callers that explicitly choose it.
public class ModelMaterialBridge {
    PBRMaterial material;
    PhongMaterial transparentMaterial;
    float properties[6];
    float lighting[8];
    int transparent;

    fun Material activeMaterial() {
        if(transparent) return transparentMaterial;
        return material;
    }

    fun int bind(Model3D model, int meshIndex) {
        if(model == null) return 0;
        model.materialAt(meshIndex) => int mat;
        if(!model.exportMaterial(mat, properties) || !model.exportMaterialLighting(mat, lighting)) return 0;
        properties[3] < 1.0 => transparent;
        material.color(@(properties[0],properties[1],properties[2]));
        material.metallic(properties[4]);
        material.roughness(properties[5]);
        material.emissive(@(lighting[0],lighting[1],lighting[2]));
        transparentMaterial.color(@(properties[0],properties[1],properties[2]));
        transparentMaterial.alpha(properties[3]);
        transparentMaterial.emission(@(lighting[0],lighting[1],lighting[2]));
        transparentMaterial.specular(@(lighting[3],lighting[4],lighting[5]));
        transparentMaterial.shine(Math.log2(Math.max(1.0,lighting[6])));
        if(lighting[7] > 0.0) {
            material.cullMode(Material.Cull_None);
            transparentMaterial.cullMode(Material.Cull_None);
        } else {
            material.cullMode(Material.Cull_Back);
            transparentMaterial.cullMode(Material.Cull_Back);
        }
        material.colorMap(null);
        transparentMaterial.colorMap(null);
        return 1;
    }

    // Explicit opt-in: load the first external base-color texture if available.
    fun int loadExternalTexture(Model3D model, int meshIndex) {
        if(model == null) return 0;
        model.baseColorTexturePath(model.materialAt(meshIndex),0) => string path;
        if(path == "") return 0;
        Texture.load(path) @=> Texture tex;
        if(tex == null) return 0;
        colorTexture(tex);
        return 1;
    }

    // Explicit destination: caller owns the extracted file and its cleanup.
    fun int loadEmbeddedTexture(Model3D model, int meshIndex, string path) {
        if(model == null) return 0;
        if(!model.exportEmbeddedBaseColor(model.materialAt(meshIndex),0,path)) return 0;
        Texture.load(path) @=> Texture tex;
        if(tex == null) return 0;
        colorTexture(tex);
        return 1;
    }

    fun void colorTexture(Texture tex) {
        material.colorMap(tex);
        transparentMaterial.colorMap(tex);
    }
}
