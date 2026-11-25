@import "VideoIO";
@import "AudioIO";
@import "MidiFileIO";
if(me.args()!=2) { <<< "Supply input.mkv output.mid" >>>; me.exit(); }
VideoAudio source; AudioBuffer audio; MidiSequence seq;
if(!source.load(me.arg(0),0)) { <<< "Track load failed" >>>; me.exit(); }
1024=>int block;
float samples[source.frames()];
int pitches[source.frames()/block];
if(!source.readChannel(0,0,samples)||!audio.importSamples(source.sampleRate(),1,samples)||!audio.detectPitches(0,block,0.02,0.9,pitches)||!seq.importPitches(480,120.0,block*1.0/source.sampleRate(),0,100,pitches)||!seq.save(me.arg(1))) <<< "Transcription failed" >>>;
