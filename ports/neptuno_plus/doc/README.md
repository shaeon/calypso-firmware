# Pines del Neptuno+ para los cores

Pinout del Neptuno+ (FPGA **EP4CGX150DF27**, Cyclone IV GX) para cores
estilo MiST que funcionan con este firmware.

- [`neptuno_plus_pins.qsf`](neptuno_plus_pins.qsf): plantilla de
  asignaciones lista para copiar al `.qsf` de un core.
- Este documento: de dónde sale cada pin y qué tiene que hacer el core con él.

## De dónde salen los datos

- **Pines de la FPGA:** del pinout de los cores de ZXMicroJack para
  `neptuno2`, que es el Neptuno+:
  [mist-ports](https://github.com/ZXMicroJack/mist-ports),
  `menu/neptuno2/menu.qsf` y `spectrum/neptuno2/zxspectrum.qsf`.
- **Qué GPIO del RP2040 va a cada línea:** del esquema de la middleboard
  (`edge2vgadb9midi`).
- **Comprobado con Quartus Prime Lite 17.0.2:** las 105 asignaciones de la
  plantilla, también las comentadas, son válidas en el EP4CGX150DF27, y el
  fitter coloca cada puerto en su pin.

### Cuidado con los nombres del esquema

Las señales de la middleboard se llaman `AA8_CLKBD`, `Y8_WSBD`, `V8`, `W9`…
Esos nombres llevan el pin de FPGA de la placa para la que se dibujó la
middleboard, con encapsulado de 484 pines. **No son los pines del Neptuno+.**
Por ejemplo, `Y8_WSBD` llega al pin **AF6** del Neptuno+; asignar PIN_Y8 da
`Error (171016): illegal location assignment`.

## Líneas entre el RP2040 y la FPGA

### Bus SPI de MiST

| RP2040 | Middleboard | Puerto en el core | Pin | Dirección en el core |
|---|---|---|---|---|
| GP16 | V8 | `SPI_DO` | AC14 | salida |
| GP19 | V7 | `SPI_DI` | AF12 | entrada |
| GP18 | W7 | `SPI_SCK` | AF11 | entrada |
| GP17 | W9 | `CONF_DATA0` (user_io) | AD14 | entrada |
| GP20 | W5 | `SPI_SS2` (data_io) | AC10 | entrada |
| GP21 | W6 | `SPI_SS3` (OSD) | AD10 | entrada |
| GP22 | W4 | `SPI_SS4` (modo directo SD) | AE9 | entrada |

### Audio del sintetizador MIDI

El firmware toca el MIDI con el sintetizador de Calypso y devuelve el audio
en I2S estándar: estéreo, 16 bits a 24 kHz. Los bytes MIDI van del core al
RP2040 por user_io, así que no necesitan pin.

| RP2040 | Middleboard | Puerto en el core | Pin | I2S |
|---|---|---|---|---|
| GP2 | AA8_CLKBD | `I2S_MIDI_BCK` | AA8 | BCLK |
| GP3 | Y8_WSBD | `I2S_MIDI_LRCK` | **AF6** | LRCLK |
| GP4 | AB8_DABD | `I2S_MIDI_DATA` | **AE7** | DATA |
| GP5 | AB7_OUT | `MIDI_RX` | AE5 | no se usa |

Los tres son **entradas** en el core, con el mismo receptor I2S que los cores
de Calypso y los mismos nombres de puerto.

Los cores hechos para el firmware de ZXMicroJack usan en estas mismas líneas
un formato propio que no es I2S (`not_i2s_decoder.v`), así que no oirán este
sintetizador sin cambiar el receptor.

### Joysticks DB9

En el Neptuno+, las líneas de joystick del RP2040 (`JOY_X*`) **solo llegan a
la FPGA**, no a los registros de desplazamiento de los joysticks. **El core
tiene que hacer de puente**, o el firmware no verá ningún joystick:

```verilog
assign JOY_CLK    = JOY_XCLK;
assign JOY_LOAD   = JOY_XLOAD;
assign JOY_XDATA  = JOY_DATA;
assign JOY_SELECT = 1'b1;
```

| RP2040 | Middleboard | Puerto en el core | Pin | Dirección en el core |
|---|---|---|---|---|
| GP26 | AA4_XLOAD | `JOY_XLOAD` | AE3 | entrada |
| GP27 | AB5_XSCK | `JOY_XCLK` | AF4 | entrada |
| GP28 | AA6_XDATA | `JOY_XDATA` | AF5 | salida |
| — | — | `JOY_LOAD` | AC4 | salida |
| — | — | `JOY_CLK` | AD4 | salida |
| — | — | `JOY_DATA` | AD3 | entrada |
| — | — | `JOY_SELECT` | A19 | salida |

`JOY_SELECT` es la línea de selección de los mandos de Mega Drive. Con
`1'b1` funcionan los joysticks normales y los mandos de Mega Drive de 3
botones. ZXMicroJack la conmuta así, para su lectura de mandos de 6 botones,
que este firmware todavía no hace:

```verilog
reg joy_select = 1'b1;
always @(posedge JOY_XLOAD)
    joy_select <= ~joy_select | ~JOY_XCLK;
assign JOY_SELECT = joy_select;
```

### Líneas que el RP2040 conduce en modo MiST

Están conectadas a la vez al RP2040 y a la FPGA. **En un core MiST hay que
dejarlas sin asignar**, o como entradas. Si el core las conduce, choca con el
RP2040. En la plantilla aparecen comentadas, solo como referencia.

| RP2040 | Función | Pin |
|---|---|---|
| GP6 / GP7 / GP8 / GP9 | SD: MISO / MOSI / SCK / CS | A4 / C4 / B4 / C5 |
| GP11 / GP12 | Teclado PS/2: CLK / DAT | C1 / D1 |
| GP14 / GP15 | Ratón PS/2: CLK / DAT | A2 / B1 |

## Resto de la placa

| Función | Pines |
|---|---|
| Reloj de 50 MHz | `CLOCK_50` = B14 |
| LED | A24 |
| VGA de 8 bits por color | ver la plantilla. Un core de 6 bits usa los 6 bits altos: su `VGA_x[5:0]` va a los pines de `VGA_x[7:2]` |
| Audio sigma-delta | `AUDIO_L` = AF17, `AUDIO_R` = AC17 |
| SDRAM | ver la plantilla |
| Salida I2S de audio de la placa | `I2SL` = AD15, `I2SC` = AC15, `I2SD` = AE14 (sin asignar en los cores de ZXMicroJack) |
| Entrada de cinta | `EAR` = C21 |
| RTC MCP79410 (I2C) | `SCL` = AD5, `SDA` = AD6. Va a la FPGA, no al RP2040 |
| Módulo ESP8266 | TXD = B19, RXD = B18, RTS = A12, GPIO0 = C11 |

## Ajustes globales

La plantilla incluye los ajustes de los cores de ZXMicroJack para
`neptuno2`:

- estándar de E/S 3.3-V LVTTL;
- pines no usados como entradas en alta impedancia;
- DCLK, DATA0, DATA1, nCE y nCEO como E/S normal tras la configuración;
- corriente máxima en VGA, LED y SDRAM;
- registros rápidos de E/S en la SDRAM.

## Cómo usarla

Copia en el `.qsf` del core las secciones que necesites, o inclúyela entera:

```tcl
source ../ruta/a/neptuno_plus_pins.qsf
```

Después adapta los nombres de puerto a los del top de tu core.
