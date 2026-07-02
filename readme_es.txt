tinyedit - Editor de texto ligero para AmigaOS, Linux y Windows

Características principales:
- Soporte completo UTF-8 con conversión de charset (interno UTF-8, salida configurable)
- Múltiples charsets: UTF-8, LATIN-1/2, CP437, CP850, CP865, CP866, CP1252
- Renderizado TTF con soporte Unicode completo (incluyendo emojis) en AmigaOS
- Colores y fuentes configurables
- Modos auto-wrap y hard-wrap
- Soporte deshacer/rehacer
- Búsqueda de texto
- Portapapeles
- Pegado con corchetes (Unix/Linux)
- Configurable vía archivo o menú

Compilación
AmigaOS requiere ttengine.library de https://aminet.net/package/util/libs/ttengine-68k

Empezar

Abre tinyedit:
tinyedit

O abre un archivo directamente:
tinyedit mi_archivo.txt

Guardar
Presiona F2 o Ctrl+S. La primera vez te pedirá un nombre de archivo

Salir
Presiona ESC o F10. Si tienes cambios sin guardar, te preguntará

Copiar y pegar
1. Ve al inicio del texto que quieres copiar
2. Presiona F6 o Alt+B (marca el inicio del bloque)
3. Ve al final del texto con las flechas
4. Presiona Ctrl+C para copiar
5. Ve a donde quieres pegar
6. Presiona Ctrl+V para pegar

También puedes cortar con Ctrl+X, o exportar el bloque a un archivo con Ctrl+O

Buscar texto
1. Presiona F5 o Alt+F
2. Escribe lo que buscas
3. Presiona Enter
4. Los resultados se muestran resaltados
5. Usa F3 o Alt+C para ir al resultado anterior
6. Usa F4 o Alt+T para ir al siguiente resultado
7. Presiona ESC para salir del modo de búsqueda
8. Presiona Alt+G para quitar los resaltados y salir del modo buscar

Buscar y reemplazar
1. Presiona Ctrl+R
2. Escribe el texto a buscar
3. Escribe el texto de reemplazo
4. Elige si es case-sensitive y whole-word
5. F3 o Alt+C va al resultado anterior
6. F4 o Alt+T va al siguiente resultado
7. F5 o Alt+F reemplaza la ocurrencia actual
8. F6 o Alt+B reemplaza todas las ocurrencias

Deshacer errores
Presiona Ctrl+Z para deshacer. Alt+Z para rehacer

Ir a una línea
Presiona Ctrl+Q, escribe el número de línea, y Enter

Ir al inicio/final del documento
Ctrl+G va al inicio. Ctrl+K va al final.

Insertar otro archivo
Presiona F7 o Alt+O, selecciona el archivo, y se inserta donde está el cursor

Abrir archivo nuevo
Ctrl+N crea un archivo nuevo (borra el contenido actual). Ctrl+L abre un archivo (también borra el contenido actual)

Configurar
Presiona F4 o Alt+T para cambiar colores, ajustar el ancho de línea, y otras opciones
- Usa las flechas para moverte entre campos
- Presiona Enter o Espacio para editar
- F10 o S guarda la configuración
- ESC cancela

Conversiones de charset
tinyedit trabaja siempre con UTF-8 internamente. Hay dos formas de configurar charsets:

1. Charset por defecto (Setup):
   - Presiona F4 o Alt+T para abrir el setup
   - En la pestaña Editor, ve al campo "Charset"
   - Presiona Enter para ciclar entre los charsets disponibles
   - Este será el charset usado por defecto al guardar archivos
   - Presiona F10 o S para guardar la configuración

2. Charset temporal para el archivo actual (Alt+C):
   - Presiona Alt+C (o F3 en modo normal) para cambiar el charset de lectura/escritura del archivo actual
   - Selecciona el charset de lectura (View) y de salida (Save)
   - Esto no afecta la configuración global, solo el archivo actual
   - Si cambias el charset de lectura (View), el archivo se recarga desde disco con la nueva codificación
   - Si tienes cambios sin guardar, te avisará que se perderán al recargar
   - Si solo cambias el charset de guardado (Save), no se recarga el archivo

Ejemplo práctico:
  - Quieres que todos tus archivos se guarden en CP437 por defecto:
  - Presiona F4 o Alt+S, ve a "Charset", selecciona CP437, guarda con F10 o S
  - Abres un archivo de DOS (CP437) y ves caracteres raros:
  - Presiona F3 o Alt+C, selecciona CP437 como View charset
  - El archivo se recarga desde disco convertido a UTF-8
  - Lo editas normalmente
  - Al guardar, usará el charset configurado en Setup (o el que hayas puesto en F3 como Save)

Charsets disponibles:
- UTF-8 (estándar moderno)
- LATIN-1 (ISO-8859-1, Europeo occidental)
- CP437 (DOS/PC original)
- CP850 (DOS Europeo occidental)
- CP865 (DOS Nórdico)
- CP866 (DOS Cirílico/Ruso)
- CP1252 (Windows Europeo occidental)
- LATIN-2 (ISO-8859-2, Europeo central)

Otros atajos útiles
- Ctrl+W: rewrap párrafo (ajusta el ancho de línea)
- Ctrl+Y: borrar línea actual
- Ctrl+T: borrar palabra a la derecha
- Ctrl+_: borrar palabra a la izquierda
- Ctrl+B / Home: inicio de línea
- Ctrl+E / End: fin de línea
- Ctrl+U / PgUp: página arriba
- Ctrl+D / PgDn: página abajo
- Ctrl+Left/Right: mover palabra izquierda/derecha
- Tab: insertar tab (4 espacios)
- Ins / Alt+I: toggle insert/overwrite

Ayuda
Presiona F1 o ? en cualquier momento para ver los atajos de teclado

