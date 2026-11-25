@import "TextIO";
TextStyle style; TextNode root; TextNode group; TextNode leaf; TextNode saved;
leaf.setParagraph("A procedural tree",style); group.appendChild(leaf);
leaf.setParagraph("Independent snapshot",style); group.appendChild(leaf);
root.appendChild(group); root.getChild(0,saved);
<<< root.depth(), root.nodeCount(), saved.childCount() >>>;
