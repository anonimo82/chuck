@import "RasterIO";
@import "MidiFileIO";
if(me.args()!=2) { <<< "Supply input.png output.mid" >>>; me.exit(); }
RasterImage image;
MidiSequence seq;
if(!image.load(me.arg(0))) { <<< "Image load failed" >>>; me.exit(); }
if(!image.resize(32,32)) { <<< "Resize failed" >>>; me.exit(); }
int pitches[1024];
if(!image.exportPitches(48,84,128,2,pitches)||!seq.importPitches(480,120.0,0.125,0,100,pitches)||!seq.save(me.arg(1))) {
    <<< "MIDI conversion failed" >>>; me.exit();
}
