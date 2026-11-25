@import "MidiFileIO";
@import "RasterIO";
if(me.args()!=2) { <<< "Supply input.mid output.png" >>>; me.exit(); }
MidiSequence seq; RasterImage image;
1024=>int w; 512=>int h;
int pixels[w*h*4];
if(!seq.load(me.arg(0))||!seq.exportStaticPianoRoll(w,h,0x55ccff,0x101820,pixels)||!image.importPixels(w,h,pixels)||!image.save(me.arg(1))) { <<< "Conversion failed" >>>; me.exit(); }
