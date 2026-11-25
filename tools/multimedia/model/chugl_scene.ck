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
if(model.animations()>0 && !model.playAnimation(0,1)) { <<< "Animation start failed" >>>; me.exit(); }
now => time previous;
while(true) {
    GG.nextFrame() => now;
    if(model.playbackClip()>=0) {
        if(!model.advanceAnimation((now-previous)/second) || !sceneBridge.refresh()) {
            <<< "Scene animation failed" >>>; me.exit();
        }
    }
    now => previous;
}
