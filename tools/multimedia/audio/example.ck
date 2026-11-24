if(me.args() != 2) { <<< "Supply input and output file" >>>; me.exit(); }
me.arg(0) => string source;
<<< AudioIO.sampleRate(source), AudioIO.channels(source), AudioIO.seconds(source) >>>;
<<< "Conversion success:", AudioIO.convert(source, me.arg(1)) >>>;
