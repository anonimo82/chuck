@import "VideoIO";
if(me.args()!=2) { <<< "Supply input video and output.mkv" >>>; me.exit(); }
VideoClip clip; VideoWriter writer;
Math.round(VideoIO.fps(me.arg(0)))$int=>int fps;
if(fps<1||fps>120||!clip.load(me.arg(0))) { <<< "Input failed" >>>; me.exit(); }
clip.width()=>int w; clip.height()=>int h;
if(w>4096||h>4096||w*h>4194304||!writer.open(me.arg(1),w,h,fps)) { <<< "Output failed" >>>; me.exit(); }
int pixels[w*h*4]; float matrix[12];
[0.2126,0.7152,0.0722] @=> float weights[];
while(true) {
    0.5+0.5*Math.sin(2.0*Math.pi*clip.frameIndex()/(fps*4.0))=>float mix;
    for(0=>int c;c<3;c++) for(0=>int k;k<3;k++) {
        mix*weights[k]=>matrix[c*4+k];
        if(c==k) 1.0-mix +=> matrix[c*4+k];
    }
    if(!clip.exportColorMatrix(w,h,matrix,pixels)||!writer.appendFrame(pixels)) { writer.abort(); <<< "Frame failed" >>>; me.exit(); }
    clip.next()=>int status;
    if(status==0) break;
    if(status<0) { writer.abort(); <<< "Decode failed" >>>; me.exit(); }
}
if(!writer.finish()) <<< "Video finish failed" >>>;
