@import "MidiFileIO";
@import "AudioIO";
if(me.args()!=2) { <<< "Supply input.mid and output.wav" >>>; me.exit(); }
MidiSequence seq;
if(!seq.load(me.arg(0))) { <<< "MIDI load failed" >>>; me.exit(); }
44100 => int rate;
seq.renderFrames(rate) => int count;
if(count<=0) { <<< "Unsupported MIDI sequence" >>>; me.exit(); }
float pcm[count];
AudioBuffer audio;
if(!seq.renderSamples(rate,pcm)||!audio.importSamples(rate,1,pcm)||!audio.save(me.arg(1))) {
    <<< "MIDI rendering or audio save failed" >>>; me.exit();
}
<<< "Rendered mono samples:", count >>>;
