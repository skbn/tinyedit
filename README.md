# tinyedit

A lightweight text editor for AmigaOS, Linux, and Windows using ncurses

## Features

- Full UTF-8 support with charset conversion (internal UTF-8, configurable output)
- Multiple charset support: UTF-8, LATIN-1/2, CP437, CP850, CP865, CP866, CP1252
- TTF rendering with full Unicode support (including emojis) on AmigaOS
- Tab system for editing multiple files simultaneously
- Spell checker with native C implementation (AmigaOS) or Hunspell integration (Linux/Windows)
- Hyphenation with native C implementation (AmigaOS) or libhyphen (Linux/Windows)
- Thesaurus with native C implementation (AmigaOS) or libmythes (Linux/Windows)
- Translator panel (placeholder for future implementation)
- Configurable colors and fonts
- Auto-wrap and hard-wrap modes
- Undo/redo support
- Search functionality
- Clipboard support
- Bracketed paste support (Unix/Linux)
- Customizable via config file

## Building

### Linux/BSD/macOS
```bash
make -f Makefile.unix
```

To compile with Hunspell spell checker (optional):
- Debian/Ubuntu: `sudo apt install libhunspell-dev`
- Arch Linux: `sudo pacman -S hunspell`
- FreeBSD: `doas pkg install hunspell`
- macOS: `brew install hunspell`

To compile with hyphenation support (optional):
- Debian/Ubuntu: `sudo apt install libhyphen-dev`
- Arch Linux: `sudo pacman -S hyphen`
- FreeBSD: `doas pkg install hyphen`

To compile with thesaurus support (optional):
- Debian/Ubuntu: `sudo apt install libmythes-dev`
- Arch Linux: `sudo pacman -S libmythes`
- FreeBSD: `doas pkg install mythes`

Dictionaries, hyphenation patterns, and thesaurus data:
- Debian/Ubuntu: `sudo apt install hunspell-es hunspell-en-us hyphen-es hyphen-en-us mythes-es mythes-en-us`
- Arch Linux: `sudo pacman -S hunspell-es_es hunspell-en_us hyphen-es hyphen-en mythes-es mythes-en`
- FreeBSD: `doas pkg install es-hunspell en-hunspell es-hyphen en-mythes es-mythes`
- macOS: Dictionary files are included with hunspell

```bash
make -f Makefile.unix USE_HUNSPELL=1
```

Or combine with hyphenation and thesaurus:
```bash
make -f Makefile.unix USE_HUNSPELL=1 USE_HYPHEN=1 USE_MYTHES=1
```

### Windows (MinGW)
```bash
make -f Makefile.win32
```

To compile with Hunspell spell checker (optional):
- Install Hunspell development package for MinGW
- Dictionary files must be placed in the program directory

```bash
make -f Makefile.win32 USE_HUNSPELL=1
```

### AmigaOS
```bash
For AmigaOS the program use ttengine or freetype with libpng and zlib

Using bebbo gcc

https://aminet.net/package/util/libs/ttengine-68k

libpng: https://www.libpng.org/
zlib: https://zlib.net/
FreeType: https://freetype.org/

To compile:

In tinyedit directory:

To ttengine.library: make -f Makefile.amiga

To static freetype with libpng and zlib:
Extract the files freetype-2.14.3.tar.xz, libpng-1.6.58.tar.xz and, zlib.tar.gz
into TinyEdit and rename them to freetype, zlib, and libpng.

To prepair headers:
make -f Makefile.amiga.te unprep
make -f Makefile.amiga.te prep
make -f Makefile.amiga.te clean all

Native spell checker, hyphenation, and thesaurus (default):
make -f Makefile.amiga all

The AmigaOS version includes native C implementations for:
- Spell checker (compatible with hunspell .aff/.dic files)
- Hyphenation (compatible with hyph_*.dic files, implements Liang algorithm)
- Thesaurus (compatible with mythes th_*.idx/dat files)

These are pure C implementations designed for AmigaOS with LRU cache, no C++ dependencies.

Freetype fonts tested:

Symbola.ttf
unifont_sample-17.0.04.otf
NotoColorEmoji-emojicompat.ttf
Symbola_hint.ttf
NotoSansCJK-Regular.ttf
NotoColorEmoji.ttf
DejaVuSansMono.ttf
LiberationMono-Regular.ttf

With ttengine:

DejaVuSansMono.ttf
LiberationMono-Regular.ttf

Dictionaries:
LibreOffice Dictionaries Collection (GitHub): https://github.com/wachin/libreoffice-dictionaries-collection - 138 dictionaries in 42 languages
wooorm/dictionaries (GitHub): https://github.com/wooorm/dictionaries/ - Normalized dictionaries
TinyMCE Spell Checker: https://www.tiny.cloud/docs/tinymce/7/self-hosting-hunspell/ - Packages hunspell-dictionaries-approved.zip and hunspell-dictionaries-all.zip

For Spanish and English, download from LibreOffice Dictionaries Collection:
- Spanish: es_ES.aff and es_ES.dic from es_ES/ directory
- English (UK): en_GB.aff and en_GB.dic from en_GB/ directory
- English (US): en_US.aff and en_US.dic from en_US/ directory

On AmigaOS, place the .aff and .dic files in the "ENVARC:dictionaries" directory.

The executable is large, but you don't need any libraries. It's optimized for RTG and also works with OCS, ECS, or AGA
```

### Windows
```bash
From msys2 with mingw x32 or x64

make -f Makefile.win32
```

To compile with Hunspell spell checker (requires libhunspell):
```bash
make -f Makefile.win32 USE_HUNSPELL=1
```

## Hyphenation and Thesaurus Support

tinyedit supports hyphenation and thesaurus functionality through optional libraries on Linux/Windows, and native C implementations on AmigaOS.

### Package Installation by Distribution

#### Arch Linux
```bash
# Hyphenation library and patterns
sudo pacman -S hyphen hyphen-en hyphen-es

# Thesaurus library and data
sudo pacman -S libmythes mythes-en mythes-es
```

#### Debian/Ubuntu
```bash
# Hyphenation library and patterns
sudo apt install libhyphen-dev libhyphen0 hyphen-en-us hyphen-es

# Thesaurus library and data
sudo apt install libmythes-dev libmythes-1.2-0 mythes-en-us mythes-es
```

#### FreeBSD
```bash
# Hyphenation library and patterns
doas pkg install hyphen es-hyphen

# Thesaurus library and data
doas pkg install mythes en-mythes es-mythes
```

### Compilation
```bash
make -f Makefile.unix USE_HYPHEN=1 USE_MYTHES=1
```

Or combine with Hunspell:
```bash
make -f Makefile.unix USE_HUNSPELL=1 USE_HYPHEN=1 USE_MYTHES=1
```

## Usage

```bash
tinyedit [filename]
```

## Configuration

Config file location:
- Linux/Unix: `~/.tinyedit.conf`
- AmigaOS: `ENVARC:tinyedit.cfg`
- Windows: `tinyedit.cfg`

## UTF-8 and Charset Support

tinyedit works internally with UTF-8 and provides flexible charset conversion:

- **Default charset**: Configurable via Setup (F4) - used when saving files
- **Per-file charset**: Temporary override via F3 for viewing/saving specific files
- **TTF encoding** (AmigaOS): UTF-8 mode supports full Unicode (0x000000-0x10FFFF) including emojis

Supported charsets for conversion:
- UTF-8 (modern standard, full Unicode)
- LATIN-1 (ISO-8859-1, Western European)
- LATIN-2 (ISO-8859-2, Central European)
- CP437 (DOS/PC original)
- CP850 (DOS Western European)
- CP865 (DOS Nordic)
- CP866 (DOS Cyrillic/Russian)
- CP1252 (Windows Western European)

=========
Screenshots
=========

![AmigaOS 3.2](img/amiga.png)

![AmigaOS 3.2](img/amiga_ttf.png)

![BSD](img/bsd.png)

![Linux](img/linux.png)

![Glyph picker](img/emojis.png)

![Glyph picker](img/amiga_glyphs.png)

## License

GPL-2.0 - see LICENSE file for details


