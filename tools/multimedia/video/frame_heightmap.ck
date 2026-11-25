@import "VideoIO";
@import "ModelIO";
if(me.args()!=1) { <<< "Supply input video" >>>; me.exit(); }
VideoClip clip; Model3D model;
64=>int w; 64=>int h;
float heights[w*h];
if(!clip.load(me.arg(0))||!clip.exportHeightmap(w,h,heights)||!model.createHeightmap(w,h,heights,1.0,10.0)) { <<< "Conversion failed" >>>; me.exit(); }
<<< "Vertices:",model.meshVertices(0),"Faces:",model.faces(0) >>>;
