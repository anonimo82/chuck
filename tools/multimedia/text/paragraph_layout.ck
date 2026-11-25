@import "TextIO";
TextDocument doc; TextStyle style; TextParagraphStyle flow;
20.0=>style.fontSize; 3=>flow.alignment; 8.0=>flow.lineSpacing;
24.0=>flow.marginLeft; 24.0=>flow.marginRight; 12.0=>flow.marginBottom; 18.0=>flow.indent;
doc.addParagraph("Procedural paragraph layout with independent typography and margins.",style);
doc.setParagraphLayout(0,flow);
int dimensions[2];
if(doc.measureDocument(320,dimensions)) <<< dimensions[0],dimensions[1] >>>;
