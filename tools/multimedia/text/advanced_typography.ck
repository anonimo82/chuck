@import "TextIO";
TextStyle base; TextStyle title; TextStyle annotation; TextDocument doc;
"Sans"=>base.font; 20.0=>base.fontSize;
title.inherit(base); 36.0=>title.fontSize; 800=>title.weight; 2.0=>title.letterSpacing; 0x204080=>title.color;
annotation.inherit(base); 1=>annotation.script; 0xffe8a0=>annotation.backgroundColor; 0.8=>annotation.opacity;
doc.addParagraph("Procedural typography",title);
doc.addParagraph("Superscript annotation",annotation);
int dimensions[2];
for(0=>int i;i<doc.paragraphCount();i++) {
    if(doc.measureParagraph(i,640,dimensions)) <<< doc.paragraphText(i),dimensions[0],dimensions[1] >>>;
}
