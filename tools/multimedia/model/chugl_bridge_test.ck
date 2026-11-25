@import "ModelIO";
@import "ChuGL";
@import "ModelGeometryBridge.ck";

fun void check(int ok) {
    if(ok) <<< "PASS" >>>;
    else { <<< "FAIL" >>>; me.exit(); }
}
Model3D model;
ModelGeometryBridge bridge;
check(!bridge.refresh());
check(!bridge.bind(model,0));
check(model.createMesh([0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,0.0],[0,1,2]));
check(!bridge.bind(model,0));
check(model.computeNormals(0));
check(bridge.bind(model,0));
check(bridge.geometry.indices().size()==3);
check(bridge.geometry.vertexAttributeData(0).size()==9);
check(bridge.refresh());
check(model.setSkin(0,1,[1.0,1.0,1.0]));
check(!bridge.refresh());
check(bridge.bind(model,0));
check(model.setBonePose(0,0,2.0,0.0,0.0,90.0,0.0,0.0));
check(bridge.refresh());
bridge.geometry.vertexAttributeData(0) @=> float xyz[];
bridge.geometry.vertexAttributeData(1) @=> float nrm[];
check(Math.fabs(xyz[0]-2.0)<0.00001);
check(Math.fabs(nrm[1]+1.0)<0.00001);
model.clear();
check(!bridge.refresh());
