# VectorIO (experimental)

Optional native Chugin for SVG validation, saving SVG markup and rendering SVG to PNG. Uses librsvg >= 2.46 and Cairo. No VM changes.

Linux (Debian/Ubuntu): install build-essential, pkg-config, librsvg2-dev and libcairo2-dev. macOS: install librsvg, cairo and pkg-config. Run make here, then use a matching ChucK binary (DLL API 10.2):

    chuck --silent --chugin-path:. example.ck

API:

- VectorIO.valid(string path): 1 if librsvg accepts the SVG, 0 on failure.
- VectorIO.save(string svg, string path): validate and save the supplied SVG markup, returning 1 on success or 0 on failure.
- VectorIO.render(string source, string destination, int width, int height): render to PNG, returning 1 on success or 0 on failure. Dimensions must be 1–4096 pixels.

Outputs must not exist. The save operation preserves the supplied SVG text; it does not convert a ChuGL scene to vector graphics. No vector shape editing API, DOM import, EPS/PDF export or SVG animation support is provided. External relative resources may not resolve because SVG is loaded without a base URI. Use trusted local inputs; these operations are synchronous and not intended for real-time audio processing.

Build and runtime validation are pending. Windows build is not provided yet.

## Persistent VectorImage

```chuck
@import "VectorIO";
VectorImage image;
if(image.load("input.svg")) {
    100.0 => image.x;
    1.5 => image.scaleX;
    1.5 => image.scaleY;
    30.0 => image.rotation;
    0.8 => image.opacity;
    <<< image.render("output.png",1280,720) >>>;
}
```

The SVG is parsed once and retained as a native librsvg handle. Each instance has independent controls. load(path) and setSvg(markup) replace the document and reset controls only on success; input limit 16 MiB. loaded(), clear(), reset(), render(path,width,height) operate on the current document. Six float getter/setter pairs: x/y (output-pixel translations, +/-32768, default 0), scaleX/scaleY (0.001–64, default 1), rotation (clockwise degrees, +/-1e9, default 0), opacity (0–1, default 1). Scale and rotation are around the center of the output viewport, then translation is applied. Invalid setters return NaN and preserve previous values. Cairo renders the SVG into a group before applying opacity, preserving correct compositing for overlapping shapes. Output is PNG on a transparent canvas, 1–4096 pixels per axis, clipped to the canvas, exclusive creation. No SVG DOM or individual shape editing yet; transforms are global and non-destructive. No dependency on KeyframeIO; any numeric source may drive setters. Synchronous offline operations, not real-time safe. Thirty new smoke checks plus independent RGBA assertions await execution.
