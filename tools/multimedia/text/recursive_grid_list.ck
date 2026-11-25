@import "TextIO";
TextNode panels; TextNode items; TextNode leaf; TextStyle style; TextList numbering;
2=>numbering.mode; items.createList(numbering);
leaf.setParagraph("First item",style); items.appendChild(leaf);
leaf.setParagraph("Second item",style); items.appendChild(leaf);
panels.createGrid(1,2); panels.columnGap(12); panels.setCellNode(0,0,items);
<<< panels.depth(), items.itemLabel(0), items.itemLabel(1) >>>;
