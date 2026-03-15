# PxTone Playback Plugin

Plays PxTone music files using Wohlstand's libpxtone fork.
PxTone is a simple music creation tool by Pixel (creator of Cave Story).

## Supported Formats

| Format | Extension | Description |
|--------|-----------|-------------|
| PxTone Collage | .ptcop | Full PxTone project (magic "PTCOLLAGE") |
| PxTone Tune | .pttune | Simplified tune format (magic "PTTUNE") |

## Library Source

| Library | Source | Commit |
|---------|--------|--------|
| libpxtone | https://github.com/Wohlstand/libpxtone | dec7b6015a3f2736603d02bfb5e4b9bd453fc41c |

Original PxTone by Pixel: https://pxtone.org/

Wohlstand's fork provides a standalone build without platform-specific
dependencies (the original pxtone source is designed for Windows).
OGG Vorbis support is enabled via bundled stb_vorbis for files with
embedded Vorbis samples.

## License

See [LICENSE](LICENSE) for the libpxtone license (custom permissive, from pxtone.org).
