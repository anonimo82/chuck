@import "ModelIO";
@import "MidiFileIO";
if(me.args()!=2) { <<< "Supply model file and output.mid" >>>; me.exit(); }
Model3D model;
MidiSequence seq;
if(!model.load(me.arg(0))) { <<< "Model load failed" >>>; me.exit(); }
int pitches[256];
if(!model.exportPitches(0,0,48,84,pitches)||!seq.importPitches(480,120.0,0.125,0,100,pitches)||!seq.save(me.arg(1))) {
    <<< "Geometry sonification failed" >>>; me.exit();
}
