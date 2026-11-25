@import "ModelIO";
@import "ChuGL";
@import "ModelSceneBridge.ck";

if(me.args()!=1) { <<< "Supply a trusted model path" >>>; me.exit(); }
Model3D model;
if(!model.load(me.arg(0))) { <<< "Model load failed" >>>; me.exit(); }
ModelSceneBridge sceneBridge;
if(!sceneBridge.bind(model)) { <<< "Scene requires triangle meshes, normals and decomposable transforms" >>>; me.exit(); }
sceneBridge.root --> GG.scene();
GG.windowTitle("ModelIO / ChuGL scene");
now => time start;
while(true) {
    GG.nextFrame() => now;
    if(model.animations()>0) {
        if(!model.sampleAnimation(0,(now-start)/second,1) || !sceneBridge.refresh()) {
            <<< "Scene animation failed" >>>; me.exit();
        }
    }
}
