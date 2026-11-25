// GPU-independent deterministic audiovisual recording example.
// Run in a fresh output directory with VideoIO installed.
@import "VideoIO";
VideoCapture cap;
if(!cap.openTexture(64,64) || !cap.openAudio(48000,1) ||
   !cap.startRecordingIndexed("indexed-demo.mkv",30,0)) me.exit();
float pixels[64*64*4];
float block[1600];
for(0=>int frame;frame<60;frame++) {
    for(0=>int y;y<64;y++) for(0=>int x;x<64;x++) {
        (y*64+x)*4=>int k;
        (x+frame)%64/63.0=>pixels[k];
        y/63.0=>pixels[k+1];
        0.25=>pixels[k+2];
        1.0=>pixels[k+3];
    }
    cap.nextRecordingAudioStart()=>int first;
    for(0=>int i;i<block.size();i++)
        0.2*Math.sin(2.0*pi*440.0*(first+i)/48000.0)=>block[i];
    if(!cap.ingestTexture(frame/30.0,pixels) ||
       !cap.ingestAudioAt(first,block) || !cap.indexedReady() ||
       !cap.recordIndexed(frame) || !cap.discardAudio(block.size())) {
        cap.abortRecording();
        cap.close();
        <<< "Indexed recording failed at frame",frame >>>;
        me.exit();
    }
}
cap.close();
<<< "Finalized:",cap.finishRecording(),"video frames:",cap.recordedFrames(),
    "audio frames:",cap.recordedAudioFrames() >>>;
