@import "VectorIO";
@import "MidiFileIO";
if(me.args()!=2) { <<< "Supply input.svg output.mid" >>>; me.exit(); }
VectorImage image;
MidiSequence seq;
if(!image.load(me.arg(0))) { <<< "SVG load failed" >>>; me.exit(); }
int pitches[1024];
if(!image.exportPitches(32,32,48,84,128,2,pitches)||!seq.importPitches(480,120.0,0.125,0,100,pitches)||!seq.save(me.arg(1))) {
    <<< "SVG sonification failed" >>>; me.exit();
}
