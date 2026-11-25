@import "MidiFileIO";
@import "VideoIO";
if(me.args()!=2) { <<< "Supply input.mid output.mkv" >>>; me.exit(); }
MidiSequence seq;
VideoWriter writer;
if(!seq.load(me.arg(0))) { <<< "MIDI load failed" >>>; me.exit(); }
48000=>int rate; 25=>int fps; 640=>int w; 384=>int h;
seq.renderFrames(rate)=>int total;
if(total<1) { <<< "Unsupported MIDI sequence" >>>; me.exit(); }
float pcm[total];
if(!seq.renderSamples(rate,pcm)||!writer.openAV(me.arg(1),w,h,fps,rate,1)) { <<< "Video initialization failed" >>>; me.exit(); }
int pixels[w*h*4];
0=>int first;
while(first<total) {
    writer.nextAudioFrames()=>int count;
    float samples[count];
    for(0=>int i;i<count&&first+i<total;i++) pcm[first+i]=>samples[i];
    if(!seq.exportPianoRoll(rate,first,w,h,pixels)||!writer.appendAV(pixels,samples)) { writer.abort(); <<< "Video creation failed" >>>; me.exit(); }
    count +=> first;
}
if(!writer.finish()) <<< "Video finish failed" >>>;
