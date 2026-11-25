@import "ModelIO";
@import "VideoIO";
if(me.args()!=1) { <<< "Supply output.mkv" >>>; me.exit(); }
Model3D model; VideoWriter writer;
640=>int w; 360=>int h; 25=>int fps; 100=>int count;
if(!model.createBox(1.0,1.0,1.0)||!writer.open(me.arg(0),w,h,fps)) { <<< "Initialization failed" >>>; me.exit(); }
int pixels[w*h*4];
20.0=>model.rotationX;
for(0=>int i;i<count;i++) {
    360.0*i/count=>model.rotationY;
    if(!model.renderOrthographic(0,w,h,3.0,0x55bbaa,0x101820,pixels)||!writer.appendFrame(pixels)) { writer.abort(); <<< "Frame generation failed" >>>; me.exit(); }
}
if(!writer.finish()) <<< "Video finish failed" >>>;
