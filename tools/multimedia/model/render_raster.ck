@import "ModelIO";
@import "RasterIO";
if(me.args()!=2) { <<< "Supply model file and output.png" >>>; me.exit(); }
Model3D model;
RasterImage image;
if(!model.load(me.arg(0))) { <<< "Model load failed" >>>; me.exit(); }
512=>int w; 512=>int h;
int pixels[w*h*4];
25.0=>model.rotationX; 30.0=>model.rotationY;
if(!model.renderOrthographic(0,w,h,4.0,0x80b0ff,pixels)||!image.importPixels(w,h,pixels)||!image.save(me.arg(1))) {
    <<< "Raster render failed" >>>; me.exit();
}
