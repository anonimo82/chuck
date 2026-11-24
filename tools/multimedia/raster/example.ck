if(me.args() != 2) {
    <<< "Usage: example.ck:input.jpg:output.png" >>>;
    me.exit();
}
me.arg(0) => string source;
me.arg(1) => string destination;
<<< RasterIO.width(source), RasterIO.height(source) >>>;
<<< "Conversion success (1=yes):", RasterIO.convert(source, destination) >>>;
