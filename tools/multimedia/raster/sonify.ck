@import "RasterIO";
@import "AudioIO";
if(me.args()!=2) { <<< "Supply input.png output.wav" >>>; me.exit(); }
RasterImage image;
AudioBuffer audio;
if(!image.load(me.arg(0))) { <<< "Image load failed" >>>; me.exit(); }
float pcm[image.width()*image.height()];
if(!image.exportAudioSamples(1,pcm) || !audio.importSamples(44100,1,pcm) ||
   !audio.highPass(20.0)) { <<< "Sonification failed" >>>; me.exit(); }
0.25=>audio.gain;
if(!audio.save(me.arg(1))) <<< "Audio save failed" >>>;
