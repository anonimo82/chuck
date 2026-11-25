@import "ModelIO";
@import "AudioIO";
if(me.args()!=2) { <<< "Supply model file and output.wav" >>>; me.exit(); }
Model3D model;
AudioBuffer audio;
if(!model.load(me.arg(0))) { <<< "Model load failed" >>>; me.exit(); }
float pcm[44100];
if(!model.exportAudioSamples(0,0,pcm)||!audio.importSamples(44100,1,pcm)||!audio.highPass(20.0)||!audio.fade(0,220,0.0,1.0)||!audio.fade(43880,220,1.0,0.0)) {
    <<< "Sonification failed" >>>; me.exit();
}
0.25=>audio.gain;
if(!audio.save(me.arg(1))) <<< "Audio save failed" >>>;
