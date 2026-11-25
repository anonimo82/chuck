@import "TextIO";
TextStyle normal; TextStyle title; TextDocument doc;
"Sans"=>normal.font; 16.0=>normal.fontSize;
title.inherit(normal); 32.0=>title.fontSize; 1=>title.bold;
doc.addParagraph("ChucK Multimedia",title);
doc.addParagraph("Procedural text, no markup.",normal);
int dimensions[2];
for(0=>int i;i<doc.paragraphCount();i++) {
    if(doc.measureParagraph(i,640,dimensions)) <<< doc.paragraphText(i),dimensions[0],dimensions[1] >>>;
}
