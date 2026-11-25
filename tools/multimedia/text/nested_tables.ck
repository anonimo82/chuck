@import "TextIO";
TextNode outer; TextNode inner; TextNode cell; TextNode leaf; TextStyle style;
outer.createTable(2,2); inner.createTable(1,2);
leaf.setParagraph("Nested table cell",style); inner.setCellNode(0,0,leaf);
outer.getCellNode(1,0,cell); cell.appendChild(inner); outer.setCellNode(1,0,cell);
<<< outer.depth(), outer.nodeCount(), outer.textBytes() >>>;
