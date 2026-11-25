@import "VideoIO";
@import "MidiFileIO";
if(me.args()!=2) { <<< "Supply input.mkv output.mid" >>>; me.exit(); }
VideoClip clip; MidiSequence seq;
VideoIO.fps(me.arg(0))=>float fps;
if(fps<=0||!clip.load(me.arg(0))) { <<< "Video load failed" >>>; me.exit(); }
int pitches[0];
while(true) {
    clip.framePitch(48,84,0.02)=>int pitch;
    if(pitch==-1||pitches.size()>=100000) { <<< "Frame conversion failed" >>>; me.exit(); }
    if(pitch==-2) -1=>pitch;
    pitches<<pitch;
    clip.next()=>int status;
    if(status==0) break;
    if(status<0) { <<< "Decode failed" >>>; me.exit(); }
}
if(!seq.importPitches(480,120.0,1.0/fps,0,100,pitches)||!seq.save(me.arg(1))) <<< "MIDI conversion failed" >>>;
