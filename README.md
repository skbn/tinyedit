# tinyedit

Lightweight text editor for AmigaOS, Linux and Windows using ncurses

## Features

- Full UTF-8 support with charset conversion (internal UTF-8, configurable output)
- Multiple charsets: UTF-8, LATIN-1/2, CP437, CP850, CP865, CP866, CP1252
- TTF rendering with full Unicode support (including emojis) on AmigaOS
- Tab system for editing multiple files simultaneously
- Spell checker with native implementation (AmigaOS/Windows) or Hunspell integration (*nix)
- Hyphenation with native implementation (AmigaOS/Windows) or libhyphen (*nix)
- Thesaurus with native implementation (AmigaOS/Windows) or libmythes (*nix)
- Translator panel (placeholder for future implementation)
- Configurable colors and fonts
- Auto-wrap and hard-wrap modes
- Undo/redo support
- Text search
- Clipboard
- Bracketed paste (Unix/*nix)
- Configurable via configuration file

## Compilation

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

Dictionaries, hyphenation patterns and thesaurus data:
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

To compile with native spell checker (spellchecker/ like AmigaOS):
```bash
make -f Makefile.win32 USE_HUNSPELL=1
```

The Windows version includes the same native implementation as AmigaOS for:
- Spell checker (compatible with hunspell .aff/.dic files)
- Hyphenation (compatible with hyph_*.dic files, implements Liang algorithm)
- Thesaurus (compatible with mythes th_*.idx/dat files)

### AmigaOS
```bash
For AmigaOS the program uses ttengine or freetype with libpng and zlib

Using bebbo gcc

https://aminet.net/package/util/libs/ttengine-68k

libpng: https://www.libpng.org/
zlib: https://zlib.net/
FreeType: https://freetype.org/

To compile:

In the tinyedit directory:

For ttengine.library: make -f Makefile.amiga

For static freetype with libpng and zlib:
Extract freetype-2.14.3.tar.xz, libpng-1.6.58.tar.xz, and zlib.tar.gz
in TinyEdit and rename them to freetype, zlib, and libpng.

To prepare headers:
make -f Makefile.amiga.te unprep
make -f Makefile.amiga.te prep
make -f Makefile.amiga.te clean all

Native spell checker, hyphenation and thesaurus (default):
make -f Makefile.amiga all

The AmigaOS version includes native implementations for:
- Spell checker (compatible with hunspell .aff/.dic files)
- Hyphenation (compatible with hyph_*.dic files, implements Liang algorithm)
- Thesaurus (compatible with mythes th_*.idx/dat files)

These are pure implementations designed for AmigaOS with LRU cache, no C++ dependencies

Tested Freetype fonts:

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
TinyMCE Spell Checker: https://www.tiny.cloud/docs/tinymce/7/self-hosting-hunspell/ - hunspell-dictionaries-approved.zip and hunspell-dictionaries-all.zip packages

For Spanish and English, download from LibreOffice Dictionaries Collection:
- Spanish: es_ES.aff and es_ES.dic from es_ES/ directory
- English (UK): en_GB.aff and en_GB.dic from en_GB/ directory
- English (US): en_US.aff and en_US.dic from en_US/ directory

Dictionary location:
- **AmigaOS**: Place .aff and .dic files in `ENVARC:tinyedit/dictionaries` directory
- **Windows**: Place files in the directory configured in Setup (default: program directory)
- **Unix/*nix**: Place files in the directory configured in Setup (default: `/usr/share/hunspell` or similar)

The executable is large, but you don't need any libraries. It's optimized for RTG and also works with OCS, ECS, or AGA
```

### Windows
```bash
From msys2 with mingw x32 or x64

make -f Makefile.win32
```

To compile with native spell checker (spellchecker/ like AmigaOS):
```bash
make -f Makefile.win32 USE_HUNSPELL=1
```

## Hyphenation and Thesaurus Support

tinyedit supports hyphenation and thesaurus functionality through optional libraries on *nix, and native implementations on AmigaOS/Windows

### Package Installation by Distribution

#### Arch Linux
```bash
# Hyphen library and patterns
sudo pacman -S hyphen hyphen-en hyphen-es

# Thesaurus library and data
sudo pacman -S libmythes mythes-en mythes-es
```

#### Debian/Ubuntu
```bash
# Hyphen library and patterns
sudo apt install libhyphen-dev libhyphen0 hyphen-en-us hyphen-es

# Thesaurus library and data
sudo apt install libmythes-dev libmythes-1.2-0 mythes-en-us mythes-es
```

#### FreeBSD
```bash
# Hyphen library and patterns
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

Configuration file location:
- *nix: `~/.tinyedit/config`
- AmigaOS: `ENVARC:tinyedit/config`
- Windows: `%APPDATA%\tinyedit\config` (or `tinyedit\config` if APPDATA is not defined)

### Configurable Options

From Setup (F4) you can configure:

- **Charset**: Default charset for reading/writing
- **Undo levels**: Undo history depth (default 50)
- **Auto-wrap column**: Column to wrap text to (0 = disabled, default 75)
- **Hard wrap**: Enable/disable hard-wrap mode
- **Line numbers**: Show/hide line numbers
- **Font**: Interface font (AmigaOS)
- **TTF settings**: TTF font configuration (AmigaOS)
- **Colors**: Interface colors
- **Spell checker**: Spell checker configuration (if compiled with support)
- **Hyphenation**: Hyphenation configuration (if compiled with support)
- **Thesaurus**: Thesaurus configuration (if compiled with support)

## UTF-8 and Charset Support

tinyedit works internally with UTF-8 and provides flexible charset conversion:

- **Default charset**: Configurable via Setup (F4) - used when saving files
- **Per-file charset**: Temporary override via F3 to view/save specific files
- **TTF encoding** (AmigaOS): UTF-8 mode supports full Unicode (0x000000-0x10FFFF) including emojis

Supported charsets for conversion:
- UTF-8 (modern standard, full Unicode)
- LATIN-1 (ISO-8859-1, Western European)
- LATIN-2 (ISO-8859-2, Central European)
- CP437 (original DOS/PC)
- CP850 (DOS Western European)
- CP865 (DOS Nordic)
- CP866 (DOS Cyrillic/Russian)
- CP1252 (Windows Western European)

## Wrap Modes

tinyedit supports two line wrapping modes:

- **Soft-wrap (visual)**: Text wraps visually at the specified column, but no actual line breaks are inserted. Useful for viewing text without modifying the file
- **Hard-wrap**: Line breaks (CR) are inserted when reaching the specified column. Useful for formatting text for sending or files that require specific widths

Configuration:
- **Auto-wrap column**: Column to wrap text to (0 = disabled, default 75)
- **Hard wrap**: Enable/disable hard-wrap mode

## Line Numbers

tinyedit can show or hide line numbers in the left margin:

- Enable/disable from Setup
- Useful for navigation and reference in large files
- Each tab can have its own line number configuration

## Clipboard

tinyedit supports system clipboard:

- **AmigaOS/Windows**: Uses clipboard.device or Windows clipboard automatically
- **Unix/*nix**: Uses xclip or wl-clipboard if available
- **SSH**: In SSH sessions, uses internal clipboard if no external backend is available
- Copy/cut blocks to system clipboard automatically

## Bracketed Paste

tinyedit supports bracketed paste in compatible terminals:

- Automatically detects when text is pasted from clipboard
- Prevents paste from triggering keyboard shortcuts accidentally
- Fallback to rapid paste detection for terminals without support

## Search and Replace

tinyedit includes search and replace functionality:

- **Search**: Search text in document with case-sensitive and whole-word options
- **Replace**: Replace individual occurrences or all occurrences
- **Result navigation**: Go to previous/next result
- **Highlighting**: Search results are highlighted in text

## Rewrap Paragraph

tinyedit can rewrap paragraphs to a specific column:

- **Rewrap**: Adjusts current paragraph to the configured auto-wrap column
- Uses hyphenation if enabled and dictionary is loaded
- Useful for reformatting text after edits

## Undo/Redo

tinyedit supports undo and redo changes:

- **Undo levels**: Configurable from Setup (default 50)
- **Undo**: Revert changes one by one
- **Redo**: Reapply undone changes
- Each tab maintains its own undo history

## Insert File

tinyedit allows inserting the content of another file at the cursor position:

- Opens a file picker
- The content of the selected file is inserted where the cursor is
- Useful for combining multiple files

## Text Editing

tinyedit includes text editing commands:

- **Delete line**: Delete current line
- **Delete word**: Delete word to the left or right of cursor
- **Insert/overwrite mode**: Toggle between inserting and overwriting text

## Navigation

tinyedit provides various navigation options:

- **Go to line**: Jump to a specific line number
- **Go to start/end of document**: Navigate quickly to beginning or end
- **Word navigation**: Move word by word
- **Paragraph navigation**: Select paragraphs with triple click

## Colors and Appearance

tinyedit allows customizing interface colors:

- **Color pairs**: Configure colors for different elements (normal, status, popup, etc)
- **Cursor color**: Custom cursor color
- **Colormap** (AmigaOS): Mapping of logical colors to physical pens
- Configuration from Setup

## TTF Fonts (AmigaOS)

On AmigaOS, tinyedit supports TTF rendering with advanced options:

- **TTF enabled**: Enable/disable TTF rendering
- **TTF font**: Path to TTF font
- **TTF size**: Font size in points
- **TTF antialias**: Antialiasing configuration
- **TTF UTF-8 mode**: UTF-8 for full Unicode (including emojis) or UTF-16 BE (BMP only)
- **TTF fallbacks**: Alternative fonts for characters not available in main font

## Help

tinyedit includes an integrated help system:

- Press F1 or ? to view available keyboard shortcuts
- Help shows all commands and their key combinations

## Mouse Support

tinyedit supports mouse in compatible terminals:

- **Single click**: Place cursor and anchor block start
- **Double click**: Select word under pointer
- **Triple click**: Select entire paragraph
- **Drag**: Extend selection
- **Mouse wheel**: Vertical scrolling

## Status Bar

The status bar at the bottom shows:

- Current line and column number
- Total line count
- Wrap mode (HARD/SOFT)
- Spell checker indicator (SP)
- Insert mode (INS/OVR)
- Program status messages

## Title Bar

The title bar at the top shows:

- Current filename
- Cursor information
- Configured charset

## File Picker

tinyedit includes an integrated file picker:

- **Open file**: Browse and select file to open
- **Save as**: Specify name and path to save
- **Select directory**: Browse directories for configuration

## Tab System

tinyedit supports editing multiple files simultaneously using a tab system:

- **Open new tab**: Open a new file or create a new document
- **Switch between tabs**: Navigate between open files
- **Close tab**: Close current tab
- **Toggle tabs panel**: Show/hide tabs panel

Each tab maintains its own editing state, undo history and cursor position

## Spell Checker and Translator Panel

tinyedit includes a spell checker panel and a translator panel (placeholder for future implementation):

- **Spell Checker**: Verifies spelling of words in document using Hunspell (*nix) or native implementation (AmigaOS/Windows)
- **Panel information**: Shows if the current word is correct or incorrect, and provides suggestions for misspelled words
- **Custom dictionary**: Allows adding custom words that are saved to a custom dict file
- **Translator Panel**: Placeholder for future translation functionality (not yet implemented)

The panel can be toggled to show spell checker or translator interface

## Glyph Picker

The glyph picker allows you to insert Unicode characters and symbols not available on your keyboard:

- Opens a Unicode character grid
- Supports full Unicode range including emojis
- Useful for inserting special symbols, mathematical characters or emojis

## Hyphenation

tinyedit supports automatic hyphenation when using hard-wrap mode:

- **Native implementation** (AmigaOS): Implementation compatible with hyph_*.dic files, implements Liang algorithm
- **libhyphen** (*nix): Uses libhyphen library
- Dictionary path and name configurable
- Can be enabled/disabled for hard-wrap mode

## Thesaurus

tinyedit includes thesaurus functionality to find synonyms:

- **Native implementation** (AmigaOS): Pure implementation compatible with mythes th_*.idx/dat files
- **libmythes** (*nix): Uses libmythes library
- Search synonyms of word under cursor
- Integrates with spell checker for stemming fallback

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
