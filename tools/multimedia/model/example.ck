if(me.args() != 2) { <<< "Supply source and destination in a new directory" >>>; me.exit(); }
me.arg(0) => string source;
<<< ModelIO.valid(source), ModelIO.meshes(source), ModelIO.vertices(source) >>>;
<<< ModelIO.convert(source, me.arg(1)) >>>;
