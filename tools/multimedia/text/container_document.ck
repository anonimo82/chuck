@import "TextIO";
TextDocument doc; TextStyle style; TextParagraphStyle flow; TextContainer box;
24.0=>style.fontSize; 0x204080=>style.color;
16.0=>flow.marginLeft; 16.0=>flow.marginRight; 16.0=>flow.marginTop;
12.0=>box.paddingLeft; 12.0=>box.paddingRight; 12.0=>box.paddingTop; 12.0=>box.paddingBottom;
2.0=>box.borderWidth; 0x204080=>box.borderColor; 0xe8f0ff=>box.backgroundColor;
doc.addParagraph("Procedural text container",style); doc.setParagraphLayout(0,flow); doc.setParagraphContainer(0,box);
if(!doc.savePNG("container.png",640,160,0xffffff)) <<< "PNG save failed" >>>;
