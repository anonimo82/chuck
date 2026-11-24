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
