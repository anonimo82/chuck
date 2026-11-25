// Optional ChuGL v0.3 texture readback smoke example; requires both chugins.
// Run in a fresh output directory, with normal graphics scheduling.
@import "ChuGL";
@import "VideoIO";
TextureDesc desc;
2 => desc.width;
2 => desc.height;
Texture.FORMAT_RGBA8UNORM => desc.format;
Texture tex(desc);
TextureWriteDesc region;
2 => region.width;
2 => region.height;
[1.0,0.0,0.0,1.0, 0.0,1.0,0.0,1.0,
 0.0,0.0,1.0,1.0, 1.0,1.0,1.0,1.0] @=> float rgba[];
tex.write(rgba,region);
GG.nextFrame() => now;
VideoCapture cap;
if(!cap.openTexture(tex.width(),tex.height())) me.exit();
if(!cap.startRecording("chugl-texture.mkv",25)) me.exit();
// Only one readback in flight. Timestamp belongs to the requested snapshot.
tex.read() => now;
if(!cap.ingestTexture(0.0,tex.data()) || !cap.recordFrame()) {
    cap.abortRecording();
    cap.close();
    <<< "Texture readback or recording failed" >>>;
    me.exit();
}
cap.close();
<<< "Texture recording finished:",cap.finishRecording() >>>;
