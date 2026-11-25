@import "TextIO";
TextNode root; TextNode panel; TextNode leaf; TextStyle style; TextContainer box;
12.0=>box.paddingLeft; 12.0=>box.paddingRight; 0xe8f0ff=>box.backgroundColor;
root.setContainer(box); root.gap(10.0);
0xffffff=>box.backgroundColor; panel.setContainer(box);
leaf.setParagraph("Nested content",style); panel.appendChild(leaf); root.appendChild(panel);
leaf.setParagraph("Another paragraph",style); root.appendChild(leaf);
<<< root.depth(), root.nodeCount(), root.childCount() >>>;
