@import "VectorIO";
@import "VideoIO";
if(me.args()!=2) { <<< "Supply input.svg output.mkv" >>>; me.exit(); }
VectorImage image; VideoWriter writer;
640=>int w; 360=>int h; 25=>int fps; 100=>int count;
if(!image.load(me.arg(0))||!writer.open(me.arg(1),w,h,fps)) { <<< "Initialization failed" >>>; me.exit(); }
int pixels[w*h*4];
for(0=>int i;i<count;i++) {
    i*1.0/(count-1)=>float t;
    360.0*t=>image.rotation;
    0.6+0.3*Math.sin(Math.pi*t)=>image.scaleX=>image.scaleY;
    60.0*Math.sin(2.0*Math.pi*t)=>image.x;
    20.0*Math.sin(2.0*Math.pi*t)=>image.y;
    0.5+0.5*Math.sin(Math.pi*t)=>image.opacity;
    if(!image.exportFrame(w,h,0x101820,pixels)||!writer.appendFrame(pixels)) { writer.abort(); <<< "Frame generation failed" >>>; me.exit(); }
}
if(!writer.finish()) <<< "Video finish failed" >>>;
