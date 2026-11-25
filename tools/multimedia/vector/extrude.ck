@import "VectorIO";
@import "ModelIO";
if(me.args()!=2) { <<< "Supply input.svg output.glb" >>>; me.exit(); }
VectorImage image;
Model3D model;
int alpha[128*128];
if(!image.load(me.arg(0)) || !image.exportAlpha(128,128,alpha) ||
   !model.createSilhouette(128,128,alpha,128,8.0) || !model.save(me.arg(1))) {
    <<< "Silhouette extrusion failed" >>>; me.exit();
}
