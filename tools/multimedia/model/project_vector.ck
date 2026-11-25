@import "ModelIO";
@import "VectorIO";
if(me.args()!=2) { <<< "Supply model file and output.svg" >>>; me.exit(); }
Model3D model;
VectorImage image;
if(!model.load(me.arg(0))) { <<< "Model load failed" >>>; me.exit(); }
model.faces(0)=>int count;
if(count<1||count>4096) { <<< "Triangle count outside limit" >>>; me.exit(); }
float triangles[count*7];
25.0=>model.rotationX; 30.0=>model.rotationY;
if(!model.projectTriangles(0,512,512,4.0,0x80b0ff,triangles)||!image.importTriangles(512,512,triangles)||!image.save(me.arg(1))) {
    <<< "Vector projection failed" >>>; me.exit();
}
