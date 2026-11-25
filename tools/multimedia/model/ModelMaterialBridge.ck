@import "ModelIO";
@import "ChuGL";

// PBRMaterial exposes RGB, metallic and roughness; opacity is exported by
// ModelIO but not applied here. The optional texture is supplied by the caller.
public class ModelMaterialBridge {
    PBRMaterial material;
    float properties[6];

    fun int bind(Model3D model, int meshIndex) {
        if(model == null) return 0;
        if(!model.exportMaterial(model.materialAt(meshIndex), properties)) return 0;
        material.color(@(properties[0],properties[1],properties[2]));
        material.metallic(properties[4]);
        material.roughness(properties[5]);
        return 1;
    }

    fun void colorTexture(Texture tex) {
        material.colorMap(tex);
    }
}
