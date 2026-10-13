# lamepng2ico

**A dependency-free, freestanding Linux utility that converts supported images into Windows `.ico` files or Android launcher-icon resources.** The executable is written in C and uses Linux x86-64 system calls directly: no libc, dynamic linker, or third-party runtime libraries are required.

## Usage

### Windows ICO

```sh
./lamepng2ico picture.png
# Creates picture.ico

./lamepng2ico picture.png app-icon.ico
# Writes the chosen output path
```

The ICO contains six 32-bit RGBA/DIB icon images: **16, 32, 48, 64, 128, and 256 pixels**. Images keep their aspect ratio, are centered on a transparent square canvas, and are bilinearly resampled. Alpha is preserved.

### Android launcher resources

```sh
./lamepng2ico --android picture.png
# Creates picture-android/ by default

./lamepng2ico --android picture.png ./android-icon-resources
# Writes to a chosen directory
```

Android mode creates an Android-style resource tree:

```text
android-icon-resources/
├── play_store_icon.png                  # 512 × 512
└── res/
    ├── drawable-nodpi/
    │   └── ic_launcher_foreground.png   # 432 × 432 adaptive foreground canvas
    ├── mipmap-anydpi-v26/
    │   └── ic_launcher.xml              # adaptive icon definition
    ├── mipmap-mdpi/ic_launcher.png      # 48 × 48
    ├── mipmap-hdpi/ic_launcher.png      # 72 × 72
    ├── mipmap-xhdpi/ic_launcher.png     # 96 × 96
    ├── mipmap-xxhdpi/ic_launcher.png    # 144 × 144
    ├── mipmap-xxxhdpi/ic_launcher.png   # 192 × 192
    └── values/ic_launcher_background.xml
```

The five density PNGs are legacy/compatibility launcher icons. The adaptive icon foreground is placed within a centered 288 × 288 safe-content box on the 432 × 432 transparent canvas; the generated adaptive-icon XML uses a white background. The Play Store artwork is a separate 512 × 512 PNG.

**This mode creates resource files; it does not patch or re-sign an APK.** To apply the resources to an existing Android app, you generally need to replace the app's corresponding resources, rebuild/package it with Android build tools, and sign the resulting APK. Updating an installed app normally requires its original signing key.

## Supported input formats

- **PNG:** non-interlaced, 8-bit grayscale, RGB, indexed/palette, grayscale-alpha, and RGBA. Standard PNG scanline filters and zlib/DEFLATE blocks are decoded internally; indexed transparency (`tRNS`) is preserved.
- **BMP:** uncompressed 24-bit or 32-bit `BI_RGB`, including top-down images.

JPEG, GIF, WebP, interlaced PNG, and non-8-bit-depth PNG are not supported. Input PNG chunk CRCs are not currently validated. Inputs are limited to 128 MiB, dimensions to 4096 × 4096, and decompressed scanline data to 96 MiB.

## Runtime dependencies

**None beyond a compatible Linux x86-64 kernel.** The executable has no dynamic section and does not require libc, libpng, zlib, ImageMagick, FFmpeg, Python, or a dynamic loader at runtime. The build step requires GCC/binutils on the development machine.

## Build

```sh
make
```

Install system-wide:

```sh
sudo install -m 0755 lamepng2ico /usr/local/bin/lamepng2ico
```

## Technical notes

- Native ELF64, x86-64 Linux; the source intentionally refuses non-x86-64 builds.
- Uses direct Linux syscalls for file I/O and anonymous memory mappings.
- Own PNG/zlib/DEFLATE decoder, ICO DIB writer, and minimal PNG encoder (RGBA PNGs use valid, uncompressed DEFLATE blocks).
- The format decoder is separate from output generation so new formats and output modes can be added without rewriting the parser.

## Credits

- **Project direction, requirements, testing, and release ownership:** Lamer (`lamersparky`).
- **AI-assisted engineering:** ChatGPT by OpenAI (Buddy) assisted with architecture, implementation, debugging, validation, and documentation.

This attribution describes the development process; ChatGPT is an AI system, not a human co-author or legal entity.
