@import "ModelIO";
@import "ChuGL";
@import "ModelGeometryBridge.ck";
@import "ModelMaterialBridge.ck";

Model3D model;
if(me.args() > 0) {
    if(!model.load(me.arg(0))) { <<< "Cannot load model" >>>; me.exit(); }
} else {
    // Two-bone strip, with a bending upper half.
    [-0.5,-1.0,0.0, 0.5,-1.0,0.0,
     -0.5,0.0,0.0, 0.5,0.0,0.0,
     -0.5,1.0,0.0, 0.5,1.0,0.0] @=> float xyz[];
    [0,1,2, 1,3,2, 2,3,4, 3,5,4] @=> int ids[];
    [1.0,0.0, 1.0,0.0, 0.5,0.5, 0.5,0.5, 0.0,1.0, 0.0,1.0] @=> float weights[];
    [0.0,1.0,2.0] @=> float times[];
    [0.0,0.0,0.0,0.0,0.0,0.0, 0.0,0.0,0.0,0.0,0.0,0.0,
     0.0,0.0,0.0,0.0,0.0,0.0, 0.0,0.0,0.0,45.0,0.0,30.0,
     0.0,0.0,0.0,0.0,0.0,0.0, 0.0,0.0,0.0,0.0,0.0,0.0] @=> float poses[];
    if(!model.createMesh(xyz,ids) || !model.computeNormals(0) ||
       !model.setSkin(0,2,weights) || !model.setBoneParent(0,1,0) ||
       model.addAnimation(0,"bend",times,poses) < 0) {
        <<< "Cannot create demo mesh" >>>; me.exit();
    }
}
ModelGeometryBridge bridge;
if(!bridge.bind(model,0)) { <<< "Mesh zero requires triangles and normals" >>>; me.exit(); }
ModelMaterialBridge appearance;
if(!appearance.bind(model,0)) { <<< "Cannot read material" >>>; me.exit(); }
// Optional caller-selected image; use a trusted local texture with matching UVs.
if(me.args()>1) appearance.colorTexture(Texture.load(me.arg(1)));
else appearance.loadExternalTexture(model,0);
GMesh mesh(bridge.geometry, appearance.activeMaterial()) --> GG.scene();
GG.windowTitle("ModelIO / ChuGL animated mesh");
now => time start;
while(true) {
    GG.nextFrame() => now;
    if(model.animations() > 0) {
        if(!model.sampleAnimation(0,(now-start)/second,1) || !bridge.refresh()) {
            <<< "Animation or geometry update failed" >>>; me.exit();
        }
    }
}
