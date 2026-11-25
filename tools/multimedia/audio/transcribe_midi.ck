@import "AudioIO";
@import "MidiFileIO";
if(me.args()!=2) { <<< "Supply input.wav output.mid" >>>; me.exit(); }
AudioBuffer audio;
MidiSequence seq;
if(!audio.load(me.arg(0))) { <<< "Audio load failed" >>>; me.exit(); }
2048=>int block;
audio.frames()/block=>int count;
if(count<1||count>100000) { <<< "Unsupported clip length" >>>; me.exit(); }
int pitches[count];
if(!audio.detectPitches(0,block,0.02,0.9,pitches)||!seq.importPitches(480,120.0,block*1.0/audio.sampleRate(),0,100,pitches)||!seq.save(me.arg(1))) {
    <<< "Pitch transcription failed" >>>; me.exit();
}
