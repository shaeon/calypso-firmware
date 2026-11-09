# calypso-firmware para Neptuno+

Port del firmware de Calypso ([teiram/calypso-firmware](https://github.com/teiram/calypso-firmware))
al Neptuno+ con la middleboard RP2040 que hoy usa
[mist-firmware-rp2040](https://github.com/ZXMicroJack/mist-firmware-rp2040) de ZXMicroJack.

**Ningún fichero del firmware original está modificado.** Todo lo de Neptuno+
está en este directorio, así que los cambios de teiram se traen sin conflictos.

## Compilar

Necesita el mismo entorno que Calypso (Pico SDK, pico-extras y el submódulo
`libs/mist-firmware`):

```
git submodule update --init
mkdir build-neptuno && cd build-neptuno
cmake ../ports/neptuno_plus
make
```

Salen dos ficheros:

- `neptuno-plus-firmware.uf2`: para grabar por BOOTSEL.
- `FIRMWARE.UPG`: para actualizar desde la SD. Se copia tal cual a la raíz de
  la tarjeta y se lanza desde el menú (`Update FIRMWARE`).

## Actualizar desde la SD

Se usa el actualizador original de Calypso (`src/mist/FirmwareUpdate.cxx`) sin
cambios. `src/FirmwareGuard.cxx` hace dos comprobaciones antes de cederle el
control:

- **Tamaño de la flash.** El actualizador original guarda la imagen nueva en
  0x10800000, así que necesita una flash de 16 MB, que es la que lleva la
  middleboard. Aun así se lee el tamaño real del chip y, si fuera más pequeño,
  la actualización se rechaza, porque esa dirección caería encima del firmware
  en ejecución. El tamaño aparece en la consola de depuración al arrancar, en
  la línea `Flash:`.
- **Placa.** Solo acepta ficheros cuya versión empieza por `NPP-`. Un
  `FIRMWARE.UPG` de Calypso se rechaza.

Al revés no hay protección: el firmware original de Calypso aceptaría un
`FIRMWARE.UPG` de Neptuno+, porque no comprueba el prefijo. No copies el de
Neptuno+ en la SD de una Calypso.

Consola de depuración opcional, solo transmisión, 115200 8N1:

```
cmake -DNEPTUNO_DEBUG_UART_PIN=23 ../ports/neptuno_plus
```

GP23 va al pin Y6 de la FPGA: el core debe dejarlo sin usar. **No hay UART en
GP0/GP1**: en esta placa son DCLK y DATA0 de la FPGA.

## Traer cambios del firmware original

```
git remote add upstream https://github.com/teiram/calypso-firmware   # una vez
git fetch upstream
git merge upstream/master
git submodule update --init
```

Como este directorio no existe en el original, el merge no da conflictos. Si
teiram renombra o elimina alguno de los ficheros que el port sustituye, `cmake`
se para con un mensaje que lo indica, en vez de compilar una mezcla.

Después de cada merge conviene mirar si ha cambiado `src/main.cxx`, porque
`src/main.cxx` de este directorio sigue su misma estructura.

## Cómo está hecho

Las librerías originales (`src/core`, `src/mist` y mist-firmware) se compilan
tal cual. Solo se sustituyen unos pocos `.cxx` por versiones de este directorio
que implementan **las mismas clases y funciones**, con las cabeceras originales
sin tocar:

| Original | Aquí | Motivo |
|---|---|---|
| `src/core/JTAG.cxx` | `src/JTAG.cxx` | El RP2040 carga la FPGA por Passive Serial, no por JTAG |
| `src/core/SPIDevice.cxx` | `src/SPIDevice.cxx` | Igual que el original para el bus MiST; con `nullptr` usa PIO para la SD |
| `src/mist/db9.cxx` | `src/db9.cxx` | Dos joysticks por el registro de desplazamiento |
| `src/mist/buttons.cxx` | `src/buttons.cxx` | No hay botón en el lado del RP2040 |
| `midireceiver.cxx`, `midi_processor.cxx`, `tap_processor.cxx` | `src/stubs.cxx` | Funciones no activadas en esta placa |
| `src/mist/FirmwareUpdate.cxx` | se compila tal cual | Sus 3 funciones se renombran y `src/FirmwareGuard.cxx` las envuelve |
| `src/mist/compat.c` | se compila tal cual | `mist_loop` se renombra y `src/CoreWatch.cxx` lo envuelve (ver más abajo) |
| `src/main.cxx` | `src/main.cxx` | Objetos y pines de Neptuno+ |
| `src/core/Configuration.h` | `include/Configuration.h` | Mapa de pines (tiene prioridad en el include path) |

Además, las librerías originales enlazan el driver de UART de stdio. Este
`CMakeLists.txt` lo cambia por stdio sin UART, porque sus pines por defecto
(GP0/GP1) son DCLK/DATA0.

## Carga de cores por JTAG (USB Blaster)

**Forma recomendada: `JTAGMODE.RBF`**, igual que en el firmware de ZXMicroJack.

1. Crea en la SD un fichero vacío (0 bytes) llamado `JTAGMODE.RBF`. En
   Windows: `type nul > JTAGMODE.RBF`.
2. Antes de cada carga por JTAG, selecciónalo en el menú como si fuera un core.
3. El firmware resetea la FPGA, suelta DCLK/DATA0 y espera hasta 60 segundos.
   Mientras tanto, carga el core desde Quartus.
4. Cuando el core responde, el firmware lo detecta y lo inicializa como si lo
   hubiera cargado él. Para cargar otro, vuelve a seleccionar `JTAGMODE.RBF`.

Así la carga por JTAG empieza siempre con la FPGA recién reseteada y con el
firmware quieto. Cualquier `.RBF` vacío sirve: un fichero de 0 bytes nunca es
un core válido. En la consola de depuración aparecen los mensajes `JTAG mode:`.

**Carga directa, sin `JTAGMODE.RBF`.** También funciona el mecanismo del MiST
original:

Al cargar un core por JTAG, `user_io_poll()` nota el cambio, marca "conservar
el core" (`USB_LOAD_VAR`) y reinicia el RP2040 (`MCUReset`) en cuanto lee un
identificador de core válido. Tras el reinicio, el firmware adopta el core que
hay en la FPGA en vez de cargar el menú.

**Fallo del firmware original que se corrige aquí.** Si al arrancar el tipo de
core sale desconocido, `mist_loop()` pasa a `LEGACY_MODE` y deja de llamar a
`user_io_poll()`. Desde ese momento nunca vuelve a leer el tipo de core, y el
firmware se queda sin OSD y sin reaccionar. Al cargar por JTAG ocurre cuando
el reinicio llega antes de que termine la carga: basta una lectura con ruido
que parezca un identificador válido. Por eso unas veces funciona y otras no.

`src/CoreWatch.cxx` envuelve `mist_loop()`. Mientras el core sea desconocido,
lee el identificador cada 50 ms. Cuando ve un core MiST válido y estable
durante medio segundo, hace lo mismo que el original: conserva el core y
reinicia. En la consola de depuración aparecen los mensajes `CoreWatch:`.

## Pines

El esquema de la middleboard (`edge2vgadb9midi`) confirma el mapa. GP25 va a
`R19_PAL`, que en esta placa base es CONF_DONE.

El mapa completo está en `include/Configuration.h`. Se ha sacado de
mist-firmware-rp2040 (`drivers/pins.h`, objetivo `mistusb`).
El RTC (MCP79410) cuelga de la FPGA (Y7/AB6), no del RP2040.

Líneas compartidas con la FPGA: SD (GP6–9), PS/2 (GP11/12 y GP14/15),
registro de joysticks (GP26–28) y bus MiST. Los cores MiST para Neptuno+ deben
dejar los pines de SD y PS/2 como entradas.

## Estado

- Funciona: carga de cores por Passive Serial, SD (con modo directo), PS/2,
  USB y OSD.
- Joysticks DB9: implementados, sin probar en la placa. Sin mandos de Mega
  Drive de 6 botones.
- Carga de cores por JTAG (USB Blaster): ver la sección siguiente.
- Pendiente:
  - MIDI: faltan los pines de audio y el tamaño de la flash.
  - Cinta: no hay ninguna línea libre hacia la FPGA.
  - Cores nativos (.NP1).
  - Soltar nCONFIG tras la configuración: es también la línea del reset.
- El menú muestra la versión como `CLP-x.xx`, la del firmware original.
