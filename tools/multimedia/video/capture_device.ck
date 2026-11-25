// Opt-in physical device check. Use a fresh directory and authorized sources.
// Arguments: screen OR camera plus exact device name (quote entire argument).
@import "VideoIO";
VideoCapture cap;
if(me.args()<1 || me.args()>2) {
    <<< "Use screen or camera plus device name; no capture performed." >>>;
    me.exit();
}
0 => int opened;
if(me.arg(0)=="screen" && me.args()==1) {
    if(cap.screenWidth()>=320 && cap.screenHeight()>=240)
        cap.openScreen(0,0,320,240) => opened;
} else if(me.arg(0)=="camera" && me.args()==2) {
    cap.openCamera(me.arg(1),640,480,25) => opened;
}
if(!opened) { <<< "Device unavailable or unsupported format." >>>; me.exit(); }
if(!cap.startRecording("device-capture.mkv",25)) {
    cap.close();
    <<< "Cannot start recording; use a fresh directory." >>>;
    me.exit();
}
for(0=>int i;i<50;i++) {
    if(!cap.next() || !cap.recordFrame()) {
        cap.abortRecording();
        cap.close();
        <<< "Device capture or encode failed." >>>;
        me.exit();
    }
    40::ms => now;
}
cap.close();
if(cap.finishRecording()) <<< "Recorded frames:",cap.recordedFrames() >>>;
else <<< "Finalization failed." >>>;
