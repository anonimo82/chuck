@import "TextIO";
@import "KeyframeIO";
TextStyle normal; TextStyle accent; TextRun run; TextDocument doc;
accent.inherit(normal); 1=>accent.bold; 0x204080=>accent.color;
doc.addParagraph("ChucK supports ",normal);
run.set("procedural typography",accent); doc.appendRun(0,run);
KeyframeCurve curve; curve.add(0.0,16.0,"linear"); curve.add(2.0,32.0,"smooth");
int dimensions[2]; now=>time start;
repeat(121) {
    curve.valueAt((now-start)/second)=>accent.fontSize;
    doc.setRunStyle(0,0,accent);
    if(doc.measureParagraph(0,640,dimensions)) <<< dimensions[0],dimensions[1] >>>;
    (second/60)=>now;
}
