@import "TextIO";
TextDocument doc; TextStyle style; TextList items;
20.0=>style.fontSize; 2=>items.mode;
["Synthesis","Manipulation","Conversion"] @=> string labels[];
for(0=>int i;i<labels.size();i++) {
    doc.addParagraph(labels[i],style);
    i+1=>items.number; doc.setParagraphList(i,items);
}
1=>items.mode; 1=>items.level;
doc.addParagraph("Procedural list item",style); doc.setParagraphList(3,items);
if(!doc.savePNG("list.png",640,240,0xffffff)) <<< "PNG save failed" >>>;
