if(me.args() != 2) { <<< "Supply source video and destination .ppm" >>>; me.exit(); }
me.arg(0) => string source;
<<< VideoIO.width(source), VideoIO.height(source), VideoIO.seconds(source), VideoIO.fps(source) >>>;
<<< "Frame exported:", VideoIO.firstFrame(source, me.arg(1)) >>>;
