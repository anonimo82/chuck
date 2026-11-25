@import "VectorIO";
@import "AudioIO";
if(me.args()!=2) { <<< "Supply input.svg output.wav" >>>; me.exit(); }
VectorImage image; AudioBuffer audio;
512=>int w; 256=>int h;
float samples[w*h*2];
if(!image.load(me.arg(0))||!image.exportAudioSamples(w,h,2,samples)||!audio.importSamples(44100,2,samples)) { <<< "Conversion failed" >>>; me.exit(); }
0.2=>audio.gain;
if(!audio.save(me.arg(1))) <<< "WAV save failed" >>>;
