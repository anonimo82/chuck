@import "TextIO";
TextDocument doc; TextGrid panels; TextStyle style; TextContainer box;
panels.create(2,2); panels.columnWeight(0,2.0); 12=>panels.columnGap; 8=>panels.rowGap;
0xe8f0ff=>box.backgroundColor; 8.0=>box.paddingLeft; 8.0=>box.paddingTop; 8.0=>box.paddingBottom;
panels.setPanel(0,0,"Main panel",style); panels.setPanel(0,1,"Sidebar",style);
panels.setPanel(1,0,"Details",style); panels.setPanel(1,1,"Notes",style);
for(0=>int r;r<2;r++) for(0=>int c;c<2;c++) panels.setPanelContainer(r,c,box);
doc.appendGrid(panels);
if(!doc.savePNG("grid.png",640,240,0xffffff)) <<< "PNG save failed" >>>;
