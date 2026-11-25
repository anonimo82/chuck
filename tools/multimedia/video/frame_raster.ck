@import "VideoIO";
@import "RasterIO";
if(me.args()!=2) { <<< "Supply input.mkv output.png" >>>; me.exit(); }
VideoClip clip;
RasterImage image;
if(!clip.load(me.arg(0))) { <<< "Video load failed" >>>; me.exit(); }
320=>int w; 240=>int h;
int pixels[w*h*4];
if(!clip.exportPixels(w,h,pixels)||!image.importPixels(w,h,pixels)||!image.save(me.arg(1))) {
    <<< "Frame conversion failed" >>>; me.exit();
}
