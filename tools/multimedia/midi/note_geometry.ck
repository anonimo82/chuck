@import "MidiFileIO";
@import "ModelIO";
if(me.args()!=1) { <<< "Supply input.mid" >>>; me.exit(); }
MidiSequence seq; Model3D model;
if(!seq.load(me.arg(0))) { <<< "MIDI load failed" >>>; me.exit(); }
seq.pianoRollRectCount()=>int count;
if(count<1) { <<< "No supported note geometry" >>>; me.exit(); }
float boxes[count*6];
if(!seq.exportNoteBoxes(128.0,1.0,8.0,boxes)||!model.createBoxes(boxes)) { <<< "Geometry conversion failed" >>>; me.exit(); }
<<< "Vertices:",model.meshVertices(0),"Faces:",model.faces(0) >>>;
