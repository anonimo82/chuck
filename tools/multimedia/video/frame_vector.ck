@import "VideoIO";
@import "VectorIO";
if(me.args()!=2) { <<< "Supply input.mkv output.svg" >>>; me.exit(); }
VideoClip clip; VectorImage image;
64=>int w; 64=>int h;
int pixels[w*h*4];
if(!clip.load(me.arg(0))||!clip.exportQuantized(w,h,4,pixels)||!image.importPixels(w,h,pixels)||!image.save(me.arg(1))) { <<< "SVG conversion failed" >>>; me.exit(); }
<<< "Editable rectangles:",image.shapes() >>>;
