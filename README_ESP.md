# tinyedit

Editor de texto ligero para AmigaOS, Linux y Windows usando ncurses

## Características

- Soporte completo UTF-8 con conversión de charset (interno UTF-8, salida configurable)
- Múltiples charsets: UTF-8, LATIN-1/2, CP437, CP850, CP865, CP866, CP1252
- Renderizado TTF con soporte Unicode completo (incluyendo emojis) en AmigaOS
- Sistema de pestañas para editar múltiples archivos simultáneamente
- Resaltado de sintaxis (C, C++, asm x86/m68k, Amiga C) con emparejamiento de corchetes y resaltado de línea actual
- Corrector ortográfico con implementación nativa (AmigaOS/Windows) o integración Hunspell (*nix) (opcional, USE_HUNSPELL=1)
- Guiones (hyphenation) con implementación nativa (AmigaOS/Windows) o libhyphen (*nix) (opcional, USE_HYPHEN=1)
- Tesauro (thesaurus) con implementación nativa (AmigaOS/Windows) o libmythes (*nix) (opcional, USE_MYTHES=1)
- Texto a voz (TTS) vía espeak-ng en *nix, SAPI 5 en Windows, o narrator.device en AmigaOS (opcional, USE_TTS=1)
- Corrector gramatical/estilístico experimental usando packs de reglas derivados de los XML de LanguageTool (opcional, USE_GRAMMAR=1)
- Panel de traductor con soporte online y diccionario offline compatible con StarDict
- Soporte de ratón (funciona en terminal, SSH y sesiones remotas)
- Colores configurables (y fuentes TTF en AmigaOS)
- Modos auto-wrap y hard-wrap
- Soporte deshacer/rehacer
- Búsqueda de texto
- Portapapeles
- Pegado con corchetes (Unix/*nix)
- Configurable vía archivo de configuración

## Compilación

### Linux/BSD/macOS

Para compilar la versión base (sin librerías externas opcionales):
```bash
make -f Makefile.unix
```

Para compilar con librerías externas (Hunspell, libhyphen, libmythes, libcurl), activa las flags deseadas:
```bash
make -f Makefile.unix USE_HUNSPELL=1
make -f Makefile.unix USE_HUNSPELL=1 USE_HYPHEN=1 USE_MYTHES=1
make -f Makefile.unix USE_TRANSLATE=1 USE_STARDICT=1
make -f Makefile.unix USE_HUNSPELL=1 USE_HYPHEN=1 USE_MYTHES=1 USE_TRANSLATE=1 USE_STARDICT=1
```

Para compilar con las implementaciones propias (sin instalar librerías externas):
```bash
make -f Makefile.unix.static
```

Las implementaciones propias incluyen corrector ortográfico, guiones, tesauro y diccionario offline (compatible con StarDict), pero requieren libcurl para el traductor online.

Para compilar con corrector ortográfico Hunspell (opcional):
- Debian/Ubuntu: `sudo apt install libhunspell-dev`
- Arch Linux: `sudo pacman -S hunspell`
- FreeBSD: `doas pkg install hunspell`
- macOS: `brew install hunspell`

Para compilar con soporte de guiones (opcional):
- Debian/Ubuntu: `sudo apt install libhyphen-dev`
- Arch Linux: `sudo pacman -S hyphen`
- FreeBSD: `doas pkg install hyphen`

Para compilar con soporte de tesauro (opcional):
- Debian/Ubuntu: `sudo apt install libmythes-dev`
- Arch Linux: `sudo pacman -S libmythes`
- FreeBSD: `doas pkg install mythes`

Para compilar con soporte de traductor (opcional):
- Debian/Ubuntu: `sudo apt install libcurl4-openssl-dev`
- Arch Linux: `sudo pacman -S curl`
- FreeBSD: `doas pkg install curl`
- macOS: `brew install curl`

Para compilar con soporte de texto a voz (opcional):
```bash
make -f Makefile.unix USE_TTS=1
```
- Debian/Ubuntu: `sudo apt install espeak-ng` (instala también `espeak-ng-data`)
- Arch Linux: `sudo pacman -S espeak-ng`
- FreeBSD: `doas pkg install espeak-ng`
- NetBSD: `pkgin install espeak-ng`
- OpenBSD: `pkg_add espeak`
- macOS: `brew install espeak-ng` (o usa el comando `say` nativo)

No requiere dependencia en tiempo de compilación: tinyedit ejecuta el backend TTS como subproceso en tiempo de ejecución

Para compilar con soporte de corrector gramatical (opcional):
```bash
make -f Makefile.unix USE_GRAMMAR=1
```
- Sin dependencia de librería externa; usa un módulo C autocontenido y packs de reglas `.rul`
- Los packs de reglas pueden generarse desde los XML de LanguageTool usando `tools/lt2rul.py`

Diccionarios, patrones de guiones y datos de tesauro:
- Debian/Ubuntu: `sudo apt install hunspell-es hunspell-en-us hyphen-es hyphen-en-us mythes-es mythes-en-us`
- Arch Linux: `sudo pacman -S hunspell-es_es hunspell-en_us hyphen-es hyphen-en mythes-es mythes-en`
- FreeBSD: `doas pkg install es-hunspell en-hunspell es-hyphen en-mythes es-mythes`
- macOS: Los archivos de diccionario se incluyen con hunspell

Para compilar con corrector ortográfico nativo (spellchecker/ como AmigaOS):
```bash
make -f Makefile.unix.static
```

### Windows (MinGW)
```bash
make -f Makefile.win32
```

Para compilar con corrector ortográfico nativo (spellchecker/ como AmigaOS):
```bash
make -f Makefile.win32 USE_HUNSPELL=1
```

Compilando con `USE_HUNSPELL=1`, la versión Windows incluye la misma implementación nativa que AmigaOS para:
- Corrector ortográfico (compatible con archivos .aff/.dic de hunspell)
- Guiones (compatible con archivos hyph_*.dic, implementa algoritmo de Liang) (requiere `USE_HYPHEN=1`)
- Tesauro (compatible con archivos th_*.idx/dat de mythes) (requiere `USE_MYTHES=1`)

### AmigaOS

Para AmigaOS el programa usa FreeType con libpng y zlib para el renderizado TTF

Usando bebbo gcc:

- libpng: https://www.libpng.org/
- zlib: https://www.zlib.net/
- FreeType: https://freetype.org/

Para compilar, en el directorio tinyedit extrae freetype-2.14.3.tar.xz,
libpng-1.6.58.tar.xz y zlib.tar.gz y renómbralos a `freetype`, `zlib` y `libpng`

Para preparar headers y compilar:
```bash
make -f Makefile.amiga unprep
make -f Makefile.amiga prep
make -f Makefile.amiga clean all
```

Para compilar con corrector ortográfico, guiones y tesauro nativos (opcional):
```bash
make -f Makefile.amiga USE_HUNSPELL=1 clean all
make -f Makefile.amiga USE_HUNSPELL=1 USE_HYPHEN=1 USE_MYTHES=1 clean all
```

La versión AmigaOS incluye implementaciones nativas (se habilitan en tiempo de compilación):
- Corrector ortográfico (compatible con archivos .aff/.dic de hunspell) (`USE_HUNSPELL=1`)
- Guiones (compatible con archivos hyph_*.dic, implementa algoritmo de Liang) (`USE_HUNSPELL=1 USE_HYPHEN=1`)
- Tesauro (compatible con archivos th_*.idx/dat de mythes) (`USE_HUNSPELL=1 USE_MYTHES=1`)

Estas son implementaciones puras diseñadas para AmigaOS con caché LRU, sin dependencias de C++

**Nota**: El soporte de traductor online en AmigaOS requiere AmiSSL para conexiones HTTPS

Fuentes Freetype probadas:

Symbola.ttf
unifont_sample-17.0.04.otf
NotoColorEmoji-emojicompat.ttf
Symbola_hint.ttf
NotoSansCJK-Regular.ttf
NotoColorEmoji.ttf
DejaVuSansMono.ttf
LiberationMono-Regular.ttf

Diccionarios:
LibreOffice Dictionaries Collection (GitHub): https://github.com/wachin/libreoffice-dictionaries-collection - 138 diccionarios en 42 idiomas
wooorm/dictionaries (GitHub): https://github.com/wooorm/dictionaries/ - Diccionarios normalizados
TinyMCE Spell Checker: https://www.tiny.cloud/docs/tinymce/7/self-hosting-hunspell/ - Paquetes hunspell-dictionaries-approved.zip y hunspell-dictionaries-all.zip

Para español e inglés, descarga de LibreOffice Dictionaries Collection:
- Español: es_ES.aff y es_ES.dic del directorio es_ES/
- Inglés (UK): en_GB.aff y en_GB.dic del directorio en_GB/
- Inglés (US): en_US.aff y en_US.dic del directorio en_US/

Ubicación de diccionarios:
- **AmigaOS**: Coloca los archivos .aff y .dic en el directorio `ENVARC:dictionaries` (o configura SPELL_DICT_PATH)
- **Windows**: Coloca los archivos en el directorio configurado en Setup (por defecto: directorio del programa)
- **Unix/*nix**: Coloca los archivos en el directorio configurado en Setup (por defecto: `/usr/share/hunspell` o similar)

Diccionarios compatibles con StarDict para búsqueda offline:
- Descarga de https://stardict.uber.space/ (mirror Wayback del archivo original)
- O de http://download.huzheng.org/ (archivo original)
- O de https://freedict.org/downloads/ (bilingües con licencia CC)
- Coloca los archivos .ifo, .idx y .dict en el directorio de diccionarios configurado en Setup

El ejecutable es grande, pero no necesitas ninguna librería. Está optimizado para RTG y también funciona con OCS, ECS, o AGA

## Soporte de Guiones y Tesauro

tinyedit soporta funcionalidad de guiones y tesauro a través de librerías opcionales en *nix, e implementaciones nativas en AmigaOS/Windows

### Instalación de Paquetes por Distribución

#### Arch Linux
```bash
# Librería de guiones y patrones
sudo pacman -S hyphen hyphen-en hyphen-es

# Librería de tesauro y datos
sudo pacman -S libmythes mythes-en mythes-es
```

#### Debian/Ubuntu
```bash
# Librería de guiones y patrones
sudo apt install libhyphen-dev libhyphen0 hyphen-en-us hyphen-es

# Librería de tesauro y datos
sudo apt install libmythes-dev libmythes-1.2-0 mythes-en-us mythes-es
```

#### FreeBSD
```bash
# Librería de guiones y patrones
doas pkg install hyphen es-hyphen

# Librería de tesauro y datos
doas pkg install mythes en-mythes es-mythes
```

### Compilación
```bash
make -f Makefile.unix USE_HYPHEN=1 USE_MYTHES=1
```

O combinar con Hunspell:
```bash
make -f Makefile.unix USE_HUNSPELL=1 USE_HYPHEN=1 USE_MYTHES=1
```

## Uso

```bash
tinyedit [nombre_archivo]
```

## Configuración

Ubicación del archivo de configuración:
- *nix: `~/.tinyedit/config`
- AmigaOS: `ENVARC:tinyedit/config`
- Windows: `%APPDATA%\tinyedit\config` (o `tinyedit\config` si APPDATA no está definido)

### Opciones Configurables

Desde el Setup (F4) puedes configurar:

- **Charset**: Charset por defecto para lectura/escritura
- **Undo levels**: Profundidad del historial de deshacer (por defecto 50)
- **Auto-wrap column**: Columna de ajuste automático (0 = deshabilitado, por defecto 75)
- **Hard wrap**: Habilitar/deshabilitar modo hard-wrap
- **Line numbers**: Mostrar/ocultar números de línea
- **TTF settings**: Configuración de fuentes TTF (AmigaOS)
- **Colors**: Colores de la interfaz
- **Spell checker**: Configuración del corrector ortográfico (si compilado con soporte)
- **Hyphenation**: Configuración de guiones (si compilado con soporte)
- **Thesaurus**: Configuración del tesauro (si compilado con soporte)

## Soporte UTF-8 y Charset

tinyedit trabaja internamente con UTF-8 y proporciona conversión flexible de charset:

- **Charset por defecto**: Configurable vía Setup (F4) - usado como valor por defecto para leer y guardar archivos
- **Charset por archivo**: Override temporal vía F3 para ver/guardar archivos específicos
- **Codificación TTF** (AmigaOS): El modo UTF-8 soporta Unicode completo (0x000000-0x10FFFF) incluyendo emojis

Charsets soportados para conversión:
- UTF-8 (estándar moderno, Unicode completo)
- LATIN-1 (ISO-8859-1, Europeo occidental)
- LATIN-2 (ISO-8859-2, Europeo central)
- CP437 (DOS/PC original)
- CP850 (DOS Europeo occidental)
- CP865 (DOS Nórdico)
- CP866 (DOS Cirílico/Ruso)
- CP1252 (Windows Europeo occidental)

## Modos de Wrap

tinyedit soporta dos modos de ajuste de línea:

- **Soft-wrap (visual)**: El texto se ajusta visualmente a la columna especificada, pero no se insertan saltos de línea reales. Útil para ver texto sin modificar el archivo
- **Hard-wrap**: Se insertan saltos de línea (CR) al alcanzar la columna especificada. Útil para formatear texto para envío o archivos que requieren anchos específicos

Configuración:
- **Auto-wrap column**: Columna a la que se ajusta el texto (0 = deshabilitado, por defecto 75)
- **Hard wrap**: Habilitar/deshabilitar modo hard-wrap

## Números de Línea

tinyedit puede mostrar u ocultar números de línea en el margen izquierdo:

- Útil para navegación y referencia en archivos grandes
- Alternar números de línea globalmente desde el Setup (Alt+D)

## Portapapeles

tinyedit soporta portapapeles del sistema:

- **AmigaOS/Windows**: Usa clipboard.device o portapapeles de Windows automáticamente
- **Unix/*nix**: Usa xclip, xsel, wl-paste o pbpaste si están disponibles; usa fallback OSC 52 en sesiones SSH
- **SSH**: En sesiones SSH, usa portapapeles interno si no hay backend externo disponible
- Copiar/cortar bloques al portapapeles del sistema automáticamente

## Pegado con Corchetes (Bracketed Paste)

tinyedit soporta pegado con corchetes en terminales compatibles:

- Detecta automáticamente cuando se pega texto del portapapeles
- Evita que el pegado active atajos de teclado accidentalmente
- Fallback a detección rápida de pegado para terminales sin soporte

## Búsqueda y Reemplazo

tinyedit incluye funcionalidad de búsqueda y reemplazo:

- **Búsqueda**: Buscar texto en el documento con opciones de case-sensitive y whole-word
- **Reemplazo**: Reemplazar ocurrencias individuales o todas las ocurrencias
- **Navegación de resultados**: Ir a resultado anterior/siguiente
- **Resaltado**: Los resultados de búsqueda se resaltan en el texto

## Ajustar Párrafo (Rewrap)

tinyedit puede reajustar párrafos a una columna específica:

- **Rewrap**: Ajusta el párrafo actual a la columna de auto-wrap configurada
- Usa hyphenation si está habilitado y el diccionario está cargado
- Útil para reformatear texto después de ediciones

## Deshacer/Rehacer

tinyedit soporta deshacer y rehacer cambios:

- **Undo levels**: Configurable desde el Setup (por defecto 50)
- **Deshacer**: Revertir cambios uno por uno
- **Rehacer**: Reaplicar cambios deshechos
- Cada pestaña mantiene su propio historial de deshacer

## Insertar Archivo

tinyedit permite insertar el contenido de otro archivo en la posición del cursor:

- Abre un selector de archivos
- El contenido del archivo seleccionado se inserta donde está el cursor
- Útil para combinar múltiples archivos

## Edición de Texto

tinyedit incluye comandos de edición de texto:

- **Borrar línea**: Eliminar la línea actual
- **Modo insert/overwrite**: Alternar entre insertar y sobrescribir texto

## Navegación

tinyedit proporciona varias opciones de navegación:

- **Ir a línea**: Saltar a un número de línea específico
- **Ir al inicio/final del documento**: Navegar rápidamente al principio o final
- **Navegación por palabras**: Moverse palabra por palabra
- **Navegación por párrafos**: Seleccionar párrafos con triple clic

## Colores y Apariencia

tinyedit permite personalizar los colores de la interfaz:

- **Color pairs**: Configurar colores para diferentes elementos (normal, status, popup, etc)
- **Cursor color**: Color personalizado del cursor
- **Colormap** (AmigaOS): Mapeo de colores lógicos a plumas físicas
- Configuración desde el Setup

## Fuentes TTF (AmigaOS)

En AmigaOS, tinyedit soporta renderizado TTF con opciones avanzadas:

- **TTF enabled**: Habilitar/deshabilitar renderizado TTF
- **TTF font**: Ruta de la fuente TTF
- **TTF size**: Tamaño de la fuente en puntos
- **TTF antialias**: Configuración de antialiasing
- **TTF UTF-8 mode**: UTF-8 para Unicode completo (incluyendo emojis) o UTF-16 BE (BMP only)
- **TTF fallbacks**: Fuentes alternativas para caracteres no disponibles en la fuente principal

## Ayuda

tinyedit incluye un sistema de ayuda integrado:

- Presiona F1 o Alt+Y para ver los atajos de teclado disponibles
- La ayuda muestra todos los comandos y sus combinaciones de teclas

## Soporte de Ratón

tinyedit soporta ratón en terminales compatibles:

- **Clic simple**: Colocar cursor (iniciar arrastre para extender selección)
- **Doble clic**: Seleccionar palabra bajo el puntero
- **Triple clic**: Seleccionar párrafo completo
- **Arrastrar**: Extender selección
- **Rueda del ratón**: Desplazamiento vertical

## Barra de Estado

La barra de estado en la parte inferior muestra:

- Número de línea y columna actual
- Número total de líneas
- Modo de wrap (HARD/SOFT)
- Indicador de corrector ortográfico (SP)
- Modo de inserción (INS/OVR)
- Mensajes de estado del programa

## Barra de Título

La barra de título en la parte superior muestra:

- Nombre del archivo actual
- Indicador de modificado
- Identificador del wrapper/proceso en Unix/*nix

## Selector de Archivos

tinyedit incluye un selector de archivos integrado:

- **Abrir archivo**: Navegar y seleccionar archivo para abrir
- **Guardar como**: Especificar nombre y ruta para guardar
- **Seleccionar directorio**: Navegar directorios

## Sistema de Pestañas

tinyedit soporta editar múltiples archivos simultáneamente usando un sistema de pestañas:

- **Abrir nueva pestaña**: Abrir un nuevo archivo o crear un nuevo documento
- **Cambiar entre pestañas**: Navegar entre archivos abiertos
- **Cerrar pestaña**: Cerrar la pestaña actual
- **Alternar panel de pestañas**: Mostrar/ocultar el panel de pestañas

Cada pestaña mantiene su propio estado de edición, historial de deshacer y posición del cursor

## Panel de Corrector Ortográfico y Traductor

tinyedit incluye un panel de corrector ortográfico y un panel de traductor:

- **Corrector Ortográfico**: Verifica la ortografía de palabras en el documento usando Hunspell (*nix) o implementación nativa (AmigaOS/Windows)
- **Información del panel**: Muestra si la palabra actual es correcta o incorrecta, y proporciona sugerencias para palabras mal escritas
- **Diccionario personalizado**: Permite añadir palabras personalizadas que se guardan en un archivo custom dict
- **Traductor**: Soporta traducción online vía backends HTTP (MyMemory, LibreTranslate, Lingva, DeepL) y búsqueda de diccionario offline
- **Diccionario offline compatible con StarDict**: Implementación pura en C compatible con el formato de diccionario StarDict (archivos .ifo, .idx, .dict), sin dependencias externas

El panel puede alternarse para mostrar la interfaz del corrector ortográfico, traductor o diccionario (Alt+S cicla entre ellos)

## Selector de Caracteres (Glyph Picker)

El selector de caracteres te permite insertar caracteres Unicode y símbolos que no están disponibles en tu teclado:

- Abre una cuadrícula de caracteres Unicode
- Soporta el rango completo de Unicode incluyendo emojis
- Útil para insertar símbolos especiales, caracteres matemáticos o emojis

## Guiones (Hyphenation)

tinyedit soporta guiones automáticos al usar el modo hard-wrap:

- **Implementación nativa** (AmigaOS): Implementación compatible con archivos hyph_*.dic, implementa algoritmo de Liang
- **libhyphen** (*nix): Usa la librería libhyphen
- Ruta de diccionario y nombre configurables
- Puede habilitarse/deshabilitarse para el modo hard-wrap

## Tesauro (Thesaurus)

tinyedit incluye una funcionalidad de tesauro para encontrar sinónimos:

- **Implementación nativa** (AmigaOS): Implementación pura compatible con archivos th_*.idx/dat de mythes
- **libmythes** (*nix): Usa la librería libmythes
- Buscar sinónimos de palabras bajo el cursor
- Se integra con el corrector ortográfico para fallback de stemming

## Texto a Voz (TTS)

tinyedit soporta texto a voz al compilar con `USE_TTS=1`:

- **Unix/*nix**: Lanza `espeak-ng` (preferido), `espeak`, `festival` o `say` de macOS como subproceso
- **Windows**: Usa SAPI 5 (no requiere paquete externo)
- **AmigaOS**: Usa `translator.library` v43 y `narrator.device` v34/v37

Atajos:
- **Hablar selección / párrafo**: `Ctrl+Alt+L` (Unix/Windows), `Alt+Shift+L` (AmigaOS)
- **Hablar documento completo**: `Ctrl+Alt+K` (Unix/Windows), `Alt+Shift+K` (AmigaOS)
- **Pausar / reanudar voz**: `Ctrl+Alt+P` (Unix/Windows), `Alt+Shift+P` (AmigaOS)
- **Detener voz**: `Ctrl+Alt+O` (Unix/Windows), `Alt+Shift+O` (AmigaOS)
- **Popup de ajustes de voz**: `Ctrl+Alt+J` (Unix/Windows), `Alt+Shift+J` (AmigaOS)

Configura voz, velocidad, tono y volumen desde Configuración (F4)

## Corrector Gramatical (Experimental)

tinyedit incluye un corrector gramatical/estilístico experimental al compilar con `USE_GRAMMAR=1`:

- Módulo C autocontenido, sin librería externa
- Carga packs de reglas `.rul` desde el directorio configurado (por defecto: `/usr/share/gramcheck` en Linux, `/usr/local/share/gramcheck` en BSD, `C:\gramcheck\rules` en Windows, `PROGDIR:rules` en AmigaOS)
- El directorio `rules/` incluye packs para inglés, español, alemán, francés, italiano y portugués
- Los packs de reglas se derivan de los XML de LanguageTool usando `tools/lt2rul.py`
- Repositorio fuente de LanguageTool: https://github.com/languagetool-org/languagetool
- Solo se extrae un subconjunto de reglas de LanguageTool (pares literales simples, puntuación, espacios, mayúsculas, repeticiones, emparejamiento de paréntesis y sugerencias de estilo)
- **No es un corrector ortográfico/gramatical completo por idioma**: no sabe dónde poner acentos ni elegir palabras según el contexto de la frase o la morfología. Es un asistente ligero para problemas superficiales comunes.

Habilita y configura el pack de reglas desde Configuración (F4) en el panel de diccionario

## Capturas de Pantalla

![AmigaOS 3.2](img/amiga.png)

![AmigaOS 3.2](img/amiga_ttf.png)

![BSD](img/bsd.png)

![Linux](img/linux.png)

![Selector de caracteres](img/emojis.png)

![Selector de caracteres](img/amiga_glyphs.png)

## Licencia

GPL-2.0 - ver archivo LICENSE para detalles
