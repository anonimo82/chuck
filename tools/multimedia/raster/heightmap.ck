@import "RasterIO";
@import "ModelIO";
if(me.args()!=2) { <<< "Supply input.png output.glb" >>>; me.exit(); }
RasterImage image;
Model3D model;
if(!image.load(me.arg(0))) { <<< "Image load failed" >>>; me.exit(); }
image.width()=>int w; image.height()=>int h;
if(w<2||h<2||w>512||h>512) { <<< "Heightmap dimensions must be 2–512" >>>; me.exit(); }
float heights[w*h];
if(!image.exportHeightmap(heights)||!model.createHeightmap(w,h,heights,1.0,20.0)||!model.save(me.arg(1))) {
    <<< "Heightmap conversion failed" >>>; me.exit();
}
