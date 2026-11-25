@import "RasterIO";
@import "VideoIO";
if(me.args()!=3) { <<< "Supply first.png second.png output.mkv" >>>; me.exit(); }
RasterImage first; RasterImage second; VideoWriter writer;
640=>int w; 360=>int h; 25=>int fps; 50=>int count;
if(!first.load(me.arg(0))||!second.load(me.arg(1))||!first.resize(w,h)||!second.resize(w,h)) { <<< "Image preparation failed" >>>; me.exit(); }
int target[w*h*4]; int pixels[w*h*4];
if(!second.exportPixels(target)||!writer.open(me.arg(2),w,h,fps)) { <<< "Initialization failed" >>>; me.exit(); }
for(0=>int i;i<count;i++) {
    if(!first.exportBlend(target,i*1.0/(count-1),0,pixels)||!writer.appendFrame(pixels)) { writer.abort(); <<< "Frame generation failed" >>>; me.exit(); }
}
if(!writer.finish()) <<< "Video finish failed" >>>;
