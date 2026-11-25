// Explicit opt-in manual device check. Run with --silent and the installed chugin path.
// Arguments: screen OR camera and device name (quote the whole script argument).
@import "RasterIO";
RasterCapture capture;
RasterImage image;
if(me.args()<1 || me.args()>2) {
    <<< "Use screen or camera plus device name; no capture performed." >>>;
    me.exit();
}
me.arg(0) => string source;
0 => int ok;
if(source=="screen" && me.args()==1) {
    if(capture.screenWidth()>=320 && capture.screenHeight()>=240)
        capture.captureScreen(0,0,320,240) => ok;
} else if(source=="camera" && me.args()==2) {
    capture.captureCamera(me.arg(1),640,480) => ok;
}
if(!ok) { <<< "Capture unavailable or failed; check backend, device and permissions." >>>; me.exit(); }
if(!image.importCapture(capture)) { <<< "Import failed" >>>; me.exit(); }
if(!capture.save("capture-original.png")) { <<< "Save failed (file may exist)" >>>; me.exit(); }
if(!image.resize(160,120) || !image.save("capture-preview.png")) { <<< "Preview failed" >>>; me.exit(); }
<<< "Capture saved:", capture.width(), capture.height(), capture.timestamp() >>>;
