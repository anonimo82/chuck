@import "AudioIO";
@import "ModelIO";
if(me.args()!=1) { <<< "Supply input audio" >>>; me.exit(); }
AudioBuffer audio; Model3D model;
128=>int w; 256=>int n;
float heights[w*(n/2+1)];
if(!audio.load(me.arg(0))||!audio.exportSpectralHeightmap(0,w,n,-80.0,heights)||!model.createHeightmap(w,n/2+1,heights,1.0,20.0)) { <<< "Conversion failed" >>>; me.exit(); }
<<< "Vertices:",model.meshVertices(0),"Faces:",model.faces(0) >>>;
