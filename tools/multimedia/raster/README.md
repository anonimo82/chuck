# RasterIO (experimental)

Optional native Chugin using ImageMagick MagickWand, called directly from ChucK. No changes to the VM or existing classes. Requires ChucK DLL API 10.2 and a C++17 compiler.

On Debian/Ubuntu install build-essential, pkg-config and libmagickwand-dev. On macOS install imagemagick and pkg-config using Homebrew. Build from this directory with make. Windows build is not yet provided.

Run with a matching ChucK binary:

    chuck --chugin-path:. example.ck:input.jpg:output.png

API:

- RasterIO.width(string path): first image width, or -1 on error.
- RasterIO.height(string path): first image height, or -1 on error.
- RasterIO.convert(string source, string destination): 1 on success, 0 on failure. Destination must not exist.

Allowed extensions: PNG, JPG/JPEG, BMP, TIFF/TIF, WebP, GIF, TGA. Actual codec availability depends on ImageMagick build and security policy. Only the first frame is exported; animated GIF, multi-page TIFF and metadata preservation are not guaranteed. Use trusted local inputs, and configure ImageMagick resource limits. Operations are synchronous and must not be used in real-time audio processing. Pixel arrays and ChuGL texture integration are not yet implemented.

Smoke test: use a known 2x3 PNG fixture, verify width and height, convert to BMP, verify dimensions again, verify attempting to overwrite the BMP fails. Compile and run checks are pending.
