@import "AudioIO";
@import "VideoIO";
if(me.args()!=2) { <<< "Supply input.wav output.mkv" >>>; me.exit(); }
AudioBuffer audio;
VideoWriter writer;
if(!audio.load(me.arg(0))) { <<< "Audio load failed" >>>; me.exit(); }
640=>int w; 360=>int h; 25=>int fps;
if(!writer.openAV(me.arg(1),w,h,fps,audio.sampleRate(),audio.channels())) { <<< "Unsupported audio or destination" >>>; me.exit(); }
int pixels[w*h*4];
0=>int first;
while(first<audio.frames()) {
    writer.nextAudioFrames()=>int count;
    Math.min(count,audio.frames()-first)$int=>int available;
    float samples[count*audio.channels()];
    for(0=>int i;i<available;i++) for(0=>int c;c<audio.channels();c++) audio.valueAt(first+i,c)=>samples[i*audio.channels()+c];
    if(!audio.exportWaveform(0,first,available,w,h,pixels)||!writer.appendAV(pixels,samples)) { writer.abort(); <<< "Video creation failed" >>>; me.exit(); }
    count +=> first;
}
if(!writer.finish()) <<< "Video finish failed" >>>;
