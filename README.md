# tinyedit

A lightweight text editor for AmigaOS, Linux, and Windows using ncurses

## Features

- Full UTF-8 support with charset conversion (internal UTF-8, configurable output)
- Multiple charset support: UTF-8, LATIN-1/2, CP437, CP850, CP865, CP866, CP1252
- TTF rendering with full Unicode support (including emojis) on AmigaOS
- Tab system for editing multiple files simultaneously
- Spell checker panel with Hunspell integration (optional)
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

Dictionaries are installed separately:
- Debian/Ubuntu: `sudo apt install hunspell-es hunspell-en-us` (example for Spanish and English)
- Arch Linux: `sudo pacman -S hunspell-es_es hunspell-en_us` (example for Spanish and English)
- FreeBSD: `doas pkg install es-hunspell en-hunspell` (example for Spanish and English)
- macOS: Dictionary files are included with hunspell

```bash
make -f Makefile.unix USE_HUNSPELL=1
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
into CrashEdit and rename them to freetype, zlib, and libpng.

To prepair headers:
make -f Makefile.amiga.te unprep
make -f Makefile.amiga.te prep
make -f Makefile.amiga.te clean all

To compile with Hunspell spell checker:
Hunspell source: https://github.com/hunspell/hunspell
Hypehn source: https://github.com/hunspell/hyphen.git
Mythes source: https://github.com/hunspell/mythes.git

make -f Makefile.amiga prep
make -f Makefile.amiga USE_HUNSPELL=1 all

To compile without Hunspell (default):
make -f Makefile.amiga all

For Makefile.amiga.te (with FreeType):
make -f Makefile.amiga.te prep-hunspell
make -f Makefile.amiga.te USE_HUNSPELL=1 all

Note: The prep-hunspell target applies patches to Hunspell source code to fix endianness issues on m68k (big-endian architecture). These patches are necessary because:

Vesrion 1.7.3:

**w_char.hxx (line 58):**
- Original: #if defined(_WIN32) || (defined(__BYTE_ORDER__) && (__BYTE_ORDER__==__ORDER_LITTLE_ENDIAN__)) || defined(__LITTLE_ENDIAN__)

- Patched: #if defined(_WIN32) || (defined(__BYTE_ORDER__) && (__BYTE_ORDER__==__ORDER_LITTLE_ENDIAN__)) || defined(__LITTLE_ENDIAN__) || defined(PLATFORM_AMIGA)

- Reason: Adds `PLATFORM_AMIGA` to little-endian detection so Hunspell uses memcpy conversion instead of manual shift

**csutil.cxx (u8_u16 function, lines 228-229):**
- Original:
  ```cpp
  out->h = static_cast<unsigned char>(cp >> 8);
  out->l = static_cast<unsigned char>(cp);
  ```
- Patched:
  ```cpp
  #ifdef PLATFORM_AMIGA
      out->l = static_cast<unsigned char>(cp >> 8);
  #else
      out->h = static_cast<unsigned char>(cp >> 8);
  #endif
  #ifdef PLATFORM_AMIGA
      out->h = static_cast<unsigned char>(cp);
  #else
      out->l = static_cast<unsigned char>(cp);
  #endif
  ```
- Reason: Inverts byte order for m68k big-endian compatibility (h/l swapped)

These patches are applied automatically by the Makefile and only need to be run once. If Hunspell is updated to a new version, these patches may need to be reapplied or adjusted.

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

The executable is large, but you don't need any libraries. It's optimized for RTG and also works with OCS, ECS, or AGA.
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


