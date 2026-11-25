@import "MidiFileIO";
@import "VectorIO";
if(me.args()!=2) { <<< "Supply input.mid output.svg" >>>; me.exit(); }
MidiSequence seq; VectorImage image;
if(!seq.load(me.arg(0))) { <<< "MIDI load failed" >>>; me.exit(); }
seq.pianoRollRectCount()=>int count;
if(count<0) { <<< "Invalid note geometry" >>>; me.exit(); }
float rects[count*6];
if(!seq.exportPianoRollRects(1024,512,0x55ccff,rects)||!image.importRects(1024,512,rects)||!image.save(me.arg(1))) { <<< "SVG conversion failed" >>>; me.exit(); }
<<< "Editable notes:",image.shapes() >>>;
