tinyedit - Lightweight text editor for AmigaOS, Linux, and Windows

Main features:
- Full UTF-8 support with charset conversion (internal UTF-8, configurable output)
- Multiple charsets: UTF-8, LATIN-1/2, CP437, CP850, CP865, CP866, CP1252
- TTF rendering with full Unicode support (including emojis) on AmigaOS
- Configurable colors and fonts
- Auto-wrap and hard-wrap modes
- Undo/redo support
- Search functionality
- Clipboard support
- Bracketed paste support (Unix/Linux)
- Configurable via file or menu

Building
AmigaOS requires ttengine.library from https://aminet.net/package/util/libs/ttengine-68k

Getting Started

Open tinyedit:
tinyedit

Or open a file directly:
tinyedit my_file.txt

Save
Press F2 or Ctrl+S. The first time it will ask for a filename.

Exit
Press ESC or F10. If you have unsaved changes, it will ask for confirmation.

Copy and paste
1. Go to the start of the text you want to copy
2. Press F6 or Alt+B (marks the start of the block)
3. Go to the end of the text with the arrow keys
4. Press Ctrl+C to copy
5. Go to where you want to paste
6. Press Ctrl+V to paste

You can also cut with Ctrl+X, or export the block to a file with Ctrl+O.

Search text
1. Press F5 or Alt+F
2. Type what you're looking for
3. Press Enter
4. Results are shown highlighted
5. Use F3 or Alt+C to go to the previous result
6. Use F4 or Alt+S to go to the next result
7. Press ESC to exit search mode
8. Press Alt+G to clear highlights and exit search mode

Search and replace
1. Press Ctrl+R
2. Type the text to search for
3. Type the replacement text
4. Choose if it's case-sensitive and whole-word
5. F3 or Alt+C goes to the previous result
6. F4 or Alt+S goes to the next result
7. F5 or Alt+F replaces the current occurrence
8. F6 or Alt+B replaces all occurrences

Undo mistakes
Press Ctrl+Z to undo. Alt+Z to redo.

Go to a line
Press Ctrl+Q, type the line number, and press Enter.

Go to start/end of document
Ctrl+G goes to the start. Ctrl+K goes to the end.

Insert another file
Press F7 or Alt+O, select the file, and it will be inserted at the cursor position.

Open new file
Ctrl+N creates a new file (clears current content). Ctrl+L opens a file (also clears current content).

Configure
Press F4 or Alt+S to change colors, adjust line width, and other options.
- Use arrow keys to move between fields
- Press Enter or Space to edit
- F10 or S saves the configuration
- ESC cancels

Charset conversions
tinyedit always works internally with UTF-8. There are two ways to configure charsets:

1. Default charset (Setup):
   - Press F4 or Alt+S to open setup
   - In the Editor tab, go to the "Charset" field
   - Press Enter to cycle through available charsets
   - This will be the charset used by default when saving files
   - Press F10 or S to save the configuration

2. Temporary charset for the current file (F3):
   - Press F3 or Alt+C to change the read/write charset for the current file
   - Select the read charset (View) and save charset (Save)
   - This does not affect the global configuration, only the current file
   - If you change the read charset (View), the file is reloaded from disk with the new encoding
   - If you have unsaved changes, it will warn you that they will be lost when reloading
   - If you only change the save charset (Save), the file is not reloaded

Practical example:
  - You want all your files to be saved in CP437 by default:
  - Press F4 or Alt+S, go to "Charset", select CP437, save with F10 or S
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

Other useful shortcuts
- Ctrl+W: rewrap paragraph (adjusts line width)
- Ctrl+Y: delete current line
- Ctrl+T: delete word to the right
- Ctrl+_: delete word to the left
- Ctrl+B / Home: start of line
- Ctrl+E / End: end of line
- Ctrl+U / PgUp: page up
- Ctrl+D / PgDn: page down
- Ctrl+Left/Right: move word left/right
- Tab: insert tab (4 spaces)
- Ins / Alt+I: toggle insert/overwrite

Help
Press F1 or ? at any time to see keyboard shortcuts.

