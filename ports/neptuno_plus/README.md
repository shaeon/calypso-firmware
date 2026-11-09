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
| `src/mist/tap_processor.cxx` | `src/stubs.cxx` | No hay cinta en esta placa |
| `src/mist/FirmwareUpdate.cxx` | se compila tal cual | Sus 3 funciones se renombran y `src/FirmwareGuard.cxx` las envuelve |
| `src/mist/compat.c` | se compila tal cual | `mist_loop` se renombra y `src/CoreWatch.cxx` lo envuelve (ver más abajo) |
| `src/main.cxx` | `src/main.cxx` | Objetos y pines de Neptuno+ |
| `src/core/Configuration.h` | `include/Configuration.h` | Mapa de pines (tiene prioridad en el include path) |

Además, las librerías originales enlazan el driver de UART de stdio. Este
`CMakeLists.txt` lo cambia por stdio sin UART, porque sus pines por defecto
(GP0/GP1) son DCLK/DATA0.

## Carga de cores por JTAG (USB Blaster)

**Carga directa.** Basta con cargar el core desde Quartus con el menú (o
cualquier core) en marcha. El firmware nota el cambio y adopta el core nuevo:
lo detecta, carga su ARC de desarrollo si existe e inicializa OSD y ROM.

El firmware original reinicia el RP2040 (`MCUReset`) para adoptar el core. En
el Neptuno+ eso no sirve: durante el reinicio todos los pines del RP2040
vuelven a su estado por defecto (entrada con pull-down), nCONFIG incluido, y
la FPGA se resetea. Por eso la primera carga por JTAG funcionaba y la segunda
dejaba la FPGA sin core. `src/CoreWatch.cxx` sustituye `MCUReset()`: cuando
se trata de un core cargado por JTAG, vuelve al bucle principal y repite la
secuencia de adopción del arranque **sin reiniciar el RP2040**. Cualquier
otro `MCUReset()` (por ejemplo la combinación de teclas de reset de MiST)
reinicia como siempre.

Además, si en algún momento el firmware no reconoce el core, sigue
vigilándolo y lo adopta en cuanto responde de forma estable. El original se
quedaría en `LEGACY_MODE` para siempre, sin OSD.

**`JTAGMODE.RBF`** (como en el firmware de ZXMicroJack). Sigue disponible por
si hiciera falta:

1. Crea en la SD un fichero vacío (0 bytes) llamado `JTAGMODE.RBF`. En
   Windows: `type nul > JTAGMODE.RBF`.
2. Selecciónalo en el menú como si fuera un core.
3. El firmware resetea la FPGA, suelta DCLK/DATA0 y espera hasta 60 segundos
   a que cargues el core desde Quartus.

En la consola de depuración aparecen los mensajes `MCUReset:`, `Adopting`,
`CoreWatch:` y `JTAG mode:`.

## Sintetizador MIDI

El mismo de Calypso (wavetable con reverb). El core envía los bytes MIDI por
user_io y el RP2040 devuelve audio estéreo en **I2S estándar**: 16 bits a
24 kHz, por los pines que la middleboard tiene para audio hacia la FPGA.

| GPIO | Señal | Pin FPGA | I2S |
|---|---|---|---|
| GP2 | AA8_CLKBD | AA8 | BCLK |
| GP3 | Y8_WSBD | Y8 | LRCLK |
| GP4 | AB8_DABD | AB8 | DATA |

En el core se usa el mismo receptor I2S que en los cores de Calypso,
asignado a AA8, Y8 y AB8. **Los cores hechos para el firmware de
ZXMicroJack no lo oirán:** su sintetizador usa en esos pines otro formato que
no es I2S (`audio_not_i2s.pio`). GP5 (AB7_OUT) no se usa, porque la entrada
MIDI va por user_io.

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
- Carga de cores por JTAG (USB Blaster): funciona, ver más arriba.
- nCONFIG se mantiene a nivel alto desde el RP2040 en todo momento: el
  pull-up de la placa no basta por sí solo.
- Sintetizador MIDI: activado, sin probar en la placa.
- Pendiente:
  - Cinta: no hay ninguna línea libre hacia la FPGA.
  - Cores nativos (.NP1).
- El menú muestra la versión como `CLP-x.xx`, la del firmware original.
