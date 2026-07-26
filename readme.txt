tinyedit - Lightweight text editor for AmigaOS, Linux, and Windows

Main features:
- Full UTF-8 support with charset conversion (internal UTF-8, configurable output)
- Multiple charsets: UTF-8, LATIN-1/2, CP437, CP850, CP865, CP866, CP1252
- TTF rendering with full Unicode support (including emojis) on AmigaOS
- Tabbed interface for editing multiple files
- Syntax highlighting (C/C++, x86/m68k asm, Amiga C) with bracket matching and current line highlight
- Spell checker, hyphenation, thesaurus and translator (optional, USE_HUNSPELL=1, USE_HYPHEN=1, USE_MYTHES=1, USE_TRANSLATE=1)
- Configurable colors (and TTF fonts on AmigaOS)
- Auto-wrap and hard-wrap modes
- Undo/redo support
- Search and replace functionality
- Mouse support (terminal, SSH and remote sessions)
- Clipboard support
- Bracketed paste support (Unix/Linux)
- Text-to-speech (TTS) via espeak-ng on *nix, SAPI 5 on Windows, or narrator.device on AmigaOS (optional, USE_TTS=1)
- Experimental grammar/style checker with rule packs derived from LanguageTool XML files (optional, USE_GRAMMAR=1). Grammar checks only work on UTF-8 text
- Partial rich-text support for .rtf and .wp/.wp4 files
- Experimental built-in printing (PDF, PCL, URF; optional IPP/AirPrint network printing; native RichEdit printing on Windows. Per-printer profile persistence)
- Configurable via file or menu
 
Building
AmigaOS requires FreeType, libpng and zlib. Extract freetype-2.14.3.tar.xz, libpng-1.6.58.tar.xz and zlib.tar.gz in the tinyedit directory and rename them to freetype, libpng and zlib. Use bebbo gcc

For Linux/BSD/macOS use Makefile.unix; for Windows use Makefile.win32. Optional features are enabled with USE_HUNSPELL=1, USE_HYPHEN=1, USE_MYTHES=1, USE_TRANSLATE=1, USE_STARDICT=1, USE_TTS=1, USE_GRAMMAR=1

TTS requires a backend at runtime (espeak-ng on Linux/BSD, SAPI 5 on Windows, translator.library+narrator.device on AmigaOS). Grammar uses bundled .rul rule packs; more can be generated from LanguageTool XML files with tools/lt2rul.py

Getting Started

Open tinyedit:
tinyedit

Or open a file directly:
tinyedit my_file.txt

Save
Press F2 or Ctrl+S. The first time it will ask for a filename

Exit
Press ESC or F10. If you have unsaved changes, it will ask for confirmation

Copy and paste
1. Go to the start of the text you want to copy
2. Mark the block using the editor's block selection
3. Go to the end of the text with the arrow keys
4. Press Ctrl+C to copy
5. Go to where you want to paste
6. Press Ctrl+V to paste

You can also cut with Ctrl+X, or export the block to a file with Ctrl+O

Search text
1. Press F5 or Alt+F
2. Type what you're looking for
3. Press Enter
4. Results are shown highlighted
5. Use F3 or Alt+C to go to the previous result
6. Use F4 or Alt+T to go to the next result
7. Press ESC to exit search mode

Search and replace
1. Press Ctrl+R
2. Type the text to search for
3. Type the replacement text
4. Choose if it's case-sensitive and whole-word
5. F3 or Alt+C goes to the previous result
6. F4 or Alt+T goes to the next result
7. F5 or Alt+F replaces the current occurrence
8. F6 or Alt+B replaces all occurrences

Undo mistakes
Press Ctrl+Z to undo. Alt+Z to redo

Go to a line
Press Alt+G, type the line number, and press Enter

Go to start/end of document
Ctrl+G goes to the start. Ctrl+K goes to the end

Insert another file
Press F7 or Alt+O, select the file, and it will be inserted at the cursor position

Open new file
Ctrl+N creates a new file (clears current content). Ctrl+L opens a file (also clears current content)

Configure
Press F4 or Alt+T to change colors, adjust line width, and other options
- Use arrow keys to move between fields
- Press Enter or Space to edit
- F10 or S saves the configuration
- ESC cancels

Charset conversions
tinyedit always works internally with UTF-8. There are two ways to configure charsets:

1. Default charset (Setup):
   - Press F4 or Alt+T to open setup
   - In the Editor tab, go to the "Charset" field
   - Press Enter to cycle through available charsets
   - This will be the charset used by default when saving files
   - Press F10 or S to save the configuration

2. Temporary charset for the current file (Alt+C):
   - Press Alt+C (or F3 in normal mode) to change the read/write charset for the current file
   - Select the read charset (View) and save charset (Save)
   - This does not affect the global configuration, only the current file
   - If you change the read charset (View), the file is reloaded from disk with the new encoding
   - If you have unsaved changes, it will warn you that they will be lost when reloading
   - If you only change the save charset (Save), the file is not reloaded

Practical example:
  - You want all your files to be saved in CP437 by default:
  - Press F4 or Alt+T, go to "Charset", select CP437, save with F10 or S
  - You open a DOS file (CP437) and see strange characters:
  - Press F3 or Alt+C, select CP437 as View charset
  - The file is reloaded from disk converted to UTF-8
  - Edit it normally
  - When saving, it will use the charset configured in Setup (or the one you set in F3 as Save)

Available charsets:
- UTF-8 (modern standard)
- LATIN-1 (ISO-8859-1, Western European)
- CP437 (DOS/PC original)
- CP850 (DOS Western European)
- CP865 (DOS Nordic)
- CP866 (DOS Cyrillic/Russian)
- CP1252 (Windows Western European)
- LATIN-2 (ISO-8859-2, Central European)

Rich text and legacy file formats
tinyedit can open and save .rtf and .wp/.wp4 files with partial rich-text support. When such a
file is loaded, the editor switches to rich mode and the formatting shortcuts (Ctrl+Alt+B/I/U/L/E/R/J
on Unix/Windows, Alt+Shift+B/I/U/L/E/R/J on AmigaOS) become available. RTF preserves bold/italic/underline/alignment
but drops color. WP 4.1.12 - 4.2 requires an 8-bit charset for saving and does not store font/size
For best compatibility with WordPerfect 4.1.12 - 4.2 it is recommended to set the charset to CP437 both
for reading (View) and saving (Save), either per-file with F3 / Alt+C or as the default charset in Setup


Printing
tinyedit includes experimental built-in printing support with a unified print dialog. Press
Ctrl+Alt+M (Alt+Shift+M on AmigaOS) to open the print popup

Output formats:
- PDF: default format, always available. Used for local printing on Unix (piped to lp/lpr) and
  Windows (fallback shell print verb on a temporary PDF). PDF glyph widths are now computed with
  FreeType at the configured print resolution and font size for more accurate text layout
- PCL: PCL 5/6 for legacy laser and inkjet printers
- URF (Apple Raster): required by AirPrint-compatible printers; requires FreeType (USE_URF=1)

Local printing:
- Unix: pipes the generated PDF to lp (fallback lpr); CUPS handles the rest. A specific printer
  can be set in Setup
- Windows: prints natively via the RichEdit control (Msftedit.dll, EM_FORMATRANGE) directly to the
  printer DC, exporting the document as RTF with the configured print font; falls back to the shell
  print verb on a temporary PDF if RichEdit is unavailable. Installed printers are listed via the
  Win32 spooler
- AmigaOS: writes charset-converted text directly to PRT: (printer.device); reads PrinterPrefs to
  identify the configured driver

Network printing (IPP / AirPrint):
Optional IPP/IPPS client (USE_IPP=1) sends jobs directly to network printers. Combined with
mDNS/Bonjour discovery, tinyedit can find AirPrint printers on the LAN, query their capabilities
(media sizes, duplex, color, quality, copies, orientation, number-up, sources, types, resolutions)
via Get-Printer-Attributes, and submit jobs with Print-Job. TLS (ipps://) is supported via the
platform HTTP client; on AmigaOS this requires AmiSSL (WITH_AMISSL=1)

Print options:
The print dialog exposes the media size, duplex (sides), color mode, quality, copies, orientation,
number-up, media source, media type, resolution, document format and the print font (path and size,
alternative to the screen font)
These map to IPP job attributes when printing over IPP/IPPS, and are remembered across sessions.
Print settings are saved per-printer in the persistent cache (printer_cache) as a PrinterProfile,
so each printer recalls its own media, quality, font, etc. and is restored automatically when that
printer is selected again


Other useful shortcuts
- Ctrl+W: rewrap FTN reply quote block
- Ctrl+Y: delete current line
- Ctrl+B / Home: start of line
- Ctrl+E / End: end of line
- Ctrl+U / PgUp: page up
- Ctrl+D / PgDn: page down
- Ctrl+Left/Right: move word left/right
- Tab: insert tab (default width 4)
- Ins / Alt+I: toggle insert/overwrite

Help
Press F1 or Alt+Y at any time to see keyboard shortcuts
