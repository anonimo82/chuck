@import "RasterIO";
@import "VectorIO";
if(me.args()!=2) { <<< "Supply input.png output.svg" >>>; me.exit(); }
RasterImage raster;
VectorImage image;
if(!raster.load(me.arg(0))) { <<< "Raster load failed" >>>; me.exit(); }
raster.width()=>int w; raster.height()=>int h;
if(w>4096||h>4096||w*h>4194304) { <<< "Image too large" >>>; me.exit(); }
int pixels[w*h*4];
if(!raster.exportPixels(pixels)||!image.importPixels(w,h,pixels)||!image.save(me.arg(1))) {
    <<< "Vectorization failed (invalid input, complexity limit or output exists)" >>>; me.exit();
}
