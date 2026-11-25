@import "TextIO";

// Run from a writable directory with TextIO installed.
// Output files must not already exist.
fun void require(int ok) {
    if(!ok) {
        <<< "TextFlow example failed" >>>;
        me.exit();
    }
}

TextDocument doc;
TextStyle body;
TextStyle accent;
TextRun run;
TextFlow flow;
TextFrame frame;
TextContainer box;

16 => body.fontSize;
24 => accent.fontSize;
0x204080 => accent.color;
require(doc.addParagraph("A linked text frame example.\n",body));
require(run.set("Styled continuation across two pages.",accent));
require(doc.appendRun(0,run));
require(doc.exportParagraphToFlow(0,flow));

8 => box.paddingLeft;
8 => box.paddingRight;
4 => box.paddingTop;
4 => box.paddingBottom;
1 => box.borderWidth;
0xf4f4f4 => box.backgroundColor;
require(frame.setContainer(box));
require(frame.setGeometry(20,20,300,36));
require(flow.appendFrame(frame));
require(frame.setGeometry(20,20,400,180));
require(frame.page(1)==1);
require(flow.appendFrame(frame));
require(flow.layout());
require(flow.overflowBytes()==0);
for(0 => int i; i < flow.frameCount(); i++) {
    <<< "Frame", i, flow.frameStartByte(i), flow.frameEndByte(i), flow.frameText(i) >>>;
}
require(flow.savePagePNG("flow-page-0.png",0,440,220,0xffffff));
require(flow.savePagePNG("flow-page-1.png",1,440,220,0xffffff));
require(flow.saveNative("example.ckflow"));
require(flow.saveTXT("example.txt"));

TextFlow restored;
require(restored.loadNative("example.ckflow"));
require(restored.layout());
require(restored.sourceText()==flow.sourceText());
<<< "TextFlow example complete" >>>;
