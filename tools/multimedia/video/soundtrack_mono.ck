@import "VideoIO";
@import "AudioIO";
if(me.args()!=2) { <<< "Supply input.mkv output.wav" >>>; me.exit(); }
VideoAudio source; AudioBuffer audio;
if(!source.load(me.arg(0),0)) { <<< "Track load failed" >>>; me.exit(); }
float samples[source.frames()]; float stats[4];
if(!source.readChannel(-1,0,samples)||!source.analyze(-1,0,source.frames(),stats)||!audio.importSamples(source.sampleRate(),1,samples)||!audio.save(me.arg(1))) { <<< "Audio conversion failed" >>>; me.exit(); }
<<< "Min",stats[0],"Max",stats[1],"Mean",stats[2],"RMS",stats[3] >>>;
