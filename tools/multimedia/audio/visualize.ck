@import "AudioIO";
@import "RasterIO";
if(me.args()!=3) { <<< "Supply input.wav waveform.png spectrogram.png" >>>; me.exit(); }
AudioBuffer audio;
RasterImage wave;
RasterImage spectrum;
if(!audio.load(me.arg(0))) { <<< "Audio load failed" >>>; me.exit(); }
int pixels[1024*256*4];
int spec[1024*513*4];
if(!audio.exportWaveform(0,1024,256,pixels) ||
   !wave.importPixels(1024,256,pixels) || !wave.save(me.arg(1)) ||
   !audio.exportSpectrogram(0,1024,1024,-80.0,spec) ||
   !spectrum.importPixels(1024,513,spec) || !spectrum.save(me.arg(2))) {
    <<< "Visualization failed" >>>; me.exit();
}
