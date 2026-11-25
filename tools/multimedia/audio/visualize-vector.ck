@import "AudioIO";
@import "VectorIO";
if(me.args()!=2) { <<< "Supply input.wav output.svg" >>>; me.exit(); }
AudioBuffer audio;
VectorImage wave;
float bounds[2048];
if(!audio.load(me.arg(0)) || !audio.exportEnvelope(0,1024,bounds) ||
   !wave.importWaveform(1024,256,bounds,0x000000,1.0) || !wave.save(me.arg(1))) {
    <<< "Vector waveform conversion failed" >>>; me.exit();
}
