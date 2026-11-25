@import "TextIO";
TextDocument doc; TextStyle style; TextParagraphStyle flow; TextPage sheet;
320=>sheet.width; 240=>sheet.height; 20=>sheet.marginLeft; 20=>sheet.marginRight; 20=>sheet.marginTop; 20=>sheet.marginBottom;
20.0=>style.fontSize; 24.0=>flow.marginBottom;
for(0=>int i;i<12;i++) { doc.addParagraph("Procedural document paragraph",style); doc.setParagraphLayout(i,flow); }
doc.pageCount(sheet)=>int pages;
for(0=>int i;i<pages;i++) if(!doc.savePagePNG("page-"+Std.itoa(i+1)+".png",sheet,i)) <<< "PNG save failed" >>>;
