# Protogen dev — qué jala y qué no

Notas acumuladas de la primera sesión (2026-04-23). Todo aquí es específico de:
- Teensy 4.0 (IMXRT1062) en Rhasky Workshops PROTO CONTROL V3
- Firmware base: coelacant1/ProtoTracer upstream
- Host: Linux Mint 22.3, PlatformIO vía pipx

---

## Setup que funciona

### Toolchain
```bash
pipx install platformio                     # pio 6.1.19
pipx inject platformio pyserial              # para scripts de probe serial
pip3 install --user --break-system-packages pyserial  # para main.py scripts
sudo cp 99-platformio-udev.rules /etc/udev/rules.d/  # acceso sin root
sudo usermod -aG dialout,plugdev $USER       # requiere logout/login
```

### Build
```bash
# en la raíz del repo
pio run -e teensy40hub75            # producción (220KB flash ~11% usage)
pio run -e teensy40verifyhardware   # diagnóstico I2C (32KB)
pio run -t clean                    # limpieza forzada si cache actúa raro
```

### Flash (la vía que jala)
```bash
sudo ~/.platformio/packages/tool-teensy/teensy_loader_cli \
    --mcu=TEENSY40 -w -s -v \
    .pio/build/teensy40hub75/firmware.hex
```

Flags importantes:
- `-s` soft reboot via HID — esencial, sin esto necesitas botón físico
- `-w` wait for device — por si el Teensy tarda en enumerar
- `-v` verbose — vital para ver "Programming..." progress o "error writing"

---

## Gotchas que nos mordieron

### 1. PlatformIO usa `teensy-gui` por default
```
CURRENT: upload_protocol = teensy-gui
```
El GUI falla silenciosamente en sesión headless y reporta SUCCESS aunque no haya flasheado nada. Override con `PLATFORMIO_UPLOAD_PROTOCOL=teensy-cli` NO funciona (pio la ignora).

**Solución:** llamar `teensy_loader_cli` directo en vez de `pio run -t upload`.

### 2. Primer intento de flash SIEMPRE falla
```
Programming...error writing to Teensy
```
Empíricamente: el segundo intento inmediato funciona perfecto. No sé por qué, probablemente el Teensy todavía enumerando después del soft reboot. Workaround:

```bash
for i in 1 2 3; do
  out=$(sudo teensy_loader_cli ... 2>&1 | tail -1)
  [ "$out" = "Booting" ] && break
  sleep 1
done
```

### 3. Permisos — sudo requerido hasta logout
Aunque agregamos al grupo `dialout` y `plugdev`, la sesión actual no los tiene hasta logout/login. Mientras tanto: todo con sudo. Alternativa: `newgrp dialout` en la terminal donde flasheas.

### 4. USB nativo del Teensy 4.x NO resetea al reconectar
Cuando desconectas/reconectas USB, el firmware SIGUE corriendo. Para reset real:
- Botón físico del Teensy (entra a bootloader), o
- Cortar power total (USB-C + microUSB), esperar 10s, reconectar

Esto afectó captura de boot logs — soluciónes:
- `teensy_reboot` binary en el tool-teensy folder (HID reset)
- Corte físico de power

### 5. EEPROM sobrevive el flash
Cuando sobreescribes el firmware, las variables en EEPROM del Teensy se mantienen. Util para: la posición del menú (FaceColor, etc) queda donde Rhasky la dejó.

Consecuencia: cambios a `colorArray[1]` pueden no verse si la EEPROM apunta a otro slot.

Para debug: `Serial.println(Menu::GetFaceColor())` al boot imprime el slot actual.

### 6. Build cache puede ignorar cambios a headers
Un `.h` editado a veces no triggerea recompile de los .cpp que lo incluyen. Cuando pasa cosas raras:
```bash
pio run -t clean
pio run -e teensy40hub75
```

---

## Sistema de faces

### Arquitectura upstream (NO tocar)
- `Menu::GetFaceState()` → int 0-N (se selecciona por botón físico)
- `SelectFace(state)` → llama método específico (Default/Angry/Sad/etc)
- Cada face method agrega MorphWeights + MaterialFrame
- `materialAnimator.Update()` blendea todo y renderiza

### Slots del menú
Por botón físico en barbilla. En orden actual:
```
0: DEFAULT      3: FROWN        6: BSOD (custom)
1: ANGRY        4: LOOKUP       7: AUDIO1 (spectrum analyzer)
2: DOUBT        5: SAD          8: AUDIO2 (audio reactive gradient)
```

### Colores del menú (EEPROM-backed)
```
0: GRDIENT    4: GREEN    8: RAINBOW
1: YELLOW     5: PURPLE   9: NOISE
2: CYAN*      6: RED
3: WHITE      7: BLUE
```
`* originalmente "ORANGE" en upstream, lo cambié a CYAN RGB(0,255,255) porque
  el EEPROM de la cabeza apunta al slot 2 y Rhasky lo tenía cyan.`

---

## Patterns que funcionan

### Agregar una nueva expresión de cara
1. Método nuevo en `ProtogenHUB75Project.h`:
   ```cpp
   void Excited() {
       AddParameterFrame(NukudeFace::Surprised, 1.0f);
       AddMaterialFrame(Color::CRAINBOW);
   }
   ```
2. Registrar en `SelectFace()` switch
3. Agregar al `faceArray[]` pa' el menú OLED
4. Compilar + flashear

### Agregar imagen estática como cara (patron BSOD)
Image material NO puede ir al 3D face mesh directamente — el mesh tiene UVs para ojos/boca solamente, así que solo muestra la imagen en esos fragmentos. **Necesitas un quad dedicado fullscreen.**

1. Crear una clase quad nueva en `Assets/Models/OBJ/FullScreenPlane.h` o similar — 4 vértices cubriendo (0,0) a (192,94) a z=0
2. Convertir imagen con Python+PIL → header con palette indexed
3. Registrar quad + image en `ProtogenHUB75Project` constructor:
   ```cpp
   scene.AddObject(fullScreenQuad.GetObject());
   fullScreenQuad.GetObject()->SetMaterial(&myImage);
   fullScreenQuad.GetObject()->Disable();
   ```
4. En `Update()`: resetear visibilidad (enable face, disable plane) al inicio
5. En método de cara custom: `pM.Disable()` + `fullScreenQuad.Enable()`

### Exponer members de ProtogenProject a clases derivadas
Por default `materialAnimator` es `private`. Para que la clase hija lo use:
```cpp
class ProtogenProject : public Project {
protected:  // era 'private' en upstream
    ...
```
Es un cambio de un token que abre toda la flexibilidad.

### Resolución de includes en assets
Convención del repo — headers en `Assets/Textures/Static/` tienen que usar:
```cpp
#include "../../../Scene/Materials/Static/Image.h"
```
Tres `../` para salir a `lib/ProtoTracer/`, no dos como sugieren algunos ejemplos (`CoelaToot.h` upstream tiene el path mal, no compila tal cual).

---

## Reglas de oro para imágenes

### Panel físico real y safe zone (CALIBRADO 2026-05-07)

Calibrado con TestGrid en hardware. Datos confirmados con foto + conteo manual:

- **Panel físico = 64×32 LEDs** (no 128x32 ni 64x64 — UN panel HUB75 estándar 64x32)
- **Source 64x32 mapea 1:1 con el LED panel** (cada source pixel = 1 LED, sin scaling)
- El "mirror vertical" del `HUB75Controller::Display()` dibuja en `(63-x, y+32)` que cae OFF-SCREEN en panel 32-tall — no produce kaleidoscope visible. Es legacy del template, ignorable.

**Safe zone: X=2..61, Y=2..29 → 60×28 LEDs usables** (incluye los pixels de los bordes, pero margen de 2 px de cada lado se corta o queda inestable). Los pixels en X=0,1 / X=62,63 / Y=0,1 / Y=30,31 NO son confiables — pueden no llegar al panel.

**Convención para nuevas caritas pixel-art**: todo el contenido (border, esquinas, gridlines, glyphs) debe quedar adentro de X=2..61, Y=2..29. Si se usa el límite (e.g., border en X=2..4) jala. Si se sale (X=0,1) se corta.

### Resolución nativa > downsampling
El pipeline renderiza **64x32 pixels** por cámara. Una imagen 1920x1080 convertida a 64x32 queda ilegible. Reglas:

- **Texto / detalles finos**: diseña directo en 64x32 o 128x64 (2x oversample) con pixel-art primitivas. Olvídate de fuentes TTF.
- **Colores sólidos / gradientes**: cualquier resolución sirve.
- **Screenshots**: solo funcionan si son cartoons/pixel-art nativamente.

### Fuentes TTF en LEDs = sufrimiento
A <12px las fuentes se embarran. Draw manual:
- Rectángulos para letras "bloque" tipo 3x5 o 5x7 bitmap
- Hand-pixel cada caracter en un diccionario tipo:
  ```python
  letters = {'A': ['010','101','111','101','101'], ...}
  ```
- Ventaja extra: los caracteres quedan idénticos al mirror (siempre que el font sea simétrico) — útil por el HUB75 mirror.

### Paleta adaptativa puede devolver menos colores
```python
img.convert("P", palette=Image.ADAPTIVE, colors=4)
# Si la imagen real solo tiene 2 colores, getpalette() regresa array de 6 bytes, no 12
```
**Siempre padear** antes de escribir al header:
```python
while len(pal) < numColors * 3:
    pal.extend([0, 0, 0])
```

### Mirror horizontal en la mitad inferior
El `HUB75Controller::Display()` renderiza 64x32 y lo pinta DOS VECES en el 64x64 logical matrix — la segunda con X flipped. Si tu cabeza es un panel wide (128x32), se ven lado a lado.

Consecuencia: **texto asimétrico se ve al revés en la mitad mirror**. Soluciones:
- Diseñar contenido simétrico (ej: `:(` puesto al centro, líneas horizontales)
- Aceptar y embrace el estilo "kaleidoscope"
- Refactorear `HUB75Controller::Display()` para renderear 64x64 sin mirror (no probado, rompería caras normales)

---

## Archivos clave modificados (diff vs upstream)

Histórico (primera sesión). Hoy el BSOD lo dibuja `Assets/Screens/BsodScreen.h` y la imagen
`Static/Bsod.h` ya no se usa.

```
lib/ProtoTracer/Examples/Templates/ProtogenProjectTemplate.h
  L55  private: → protected:  (abrir acceso a derived)
  L106 orangeMaterial RGB: 255,165,0 → 0,255,255 (cyan por EEPROM de la cabeza)

lib/ProtoTracer/ExternalDevices/Displays/SSD1306.h
  colorArray[2] "ORANGE" → "CYAN"

lib/ProtoTracer/Examples/Protogen/ProtogenHUB75Project.h
  +#include para Bsod.h y FullScreenPlane.h
  + Bsod bsodImage + FullScreenPlane bsodPlane members
  + scene.AddObject(bsodPlane.GetObject())
  + BsodFace() method
  + case 6: BsodFace() en SelectFace (BSOD en lugar de AudioReactiveGradient)
  + faceArray[6] = "BSOD"

lib/ProtoTracer/Assets/Textures/Static/Bsod.h        (NEW)
lib/ProtoTracer/Assets/Models/OBJ/FullScreenPlane.h  (NEW)
```

---

## Siguiente iteración — TODO

- [ ] Restaurar AUDIO1/AUDIO2/AUDIO3 que perdimos al meter BSOD en slot 6
- [ ] git init + commit los mods como branch personal para pull de upstream
- [ ] Experimentar con más caras custom (windows xp derpy, ramiel, etc)
- [ ] Probar modificar `HUB75Controller::Display()` para render sin mirror
- [ ] Agregar easter eggs al boop sensor (cambiar a cara specific en boop)
- [ ] Investigar lectura del micrófono para lip-sync custom

---

## Gotchas adicionales (sesión 2026-05-07/08)

### Image::GetRGB tiene un guard buggy con el arg `colors`
En `lib/ProtoTracer/Scene/Materials/Static/Image.cpp:49`:
```cpp
unsigned int pos = data[x + y * xPixels] * 3;
if (pos > colors - (unsigned int)1) return RGBColor();
```

El `colors` arg debería conceptualmente ser "cantidad de colores", pero el check (con pos = idx*3) descarta indices ≥ 1 si pasas `colors = n_palette_entries - 1`.

**Convención correcta**: pasar `colors = n_palette_entries` (el número de RGB triplets en la paleta padded a 4). Bsod pasa `colors = 4` con 4 triplets. Replicar ese pattern en cualquier nuevo Image / ImageSequence.

Para palette grandes (e.g. 32 colors en ImageSequence con paleta compartida), pasar `colors = 32` permite indices 0..10 funcionar (`pos > 31` falla, indices 11..31 dan negro). Está bug-tolerable pa imágenes que mayoritariamente usan colores bajos.

Si querés palette completa accesible, pasar `colors = (n_palette_entries-1) * 3 + 1` — pero el mejor fix sería arreglar el upstream.

### Límite del `teensy_loader_cli` = 65,536 líneas hex
Cualquier hex con más líneas falla con "HEX parse error line 65553+". El binary cabe en flash (Teensy 4.0 tiene 1.94 MB) pero el loader tiene un buffer interno limitado.

A la fecha 2026-05-08 estamos a ~426 líneas del límite con 23 slots. Optimizaciones posibles:
- Reducir frame counts de los assets más grandes (los videos y galerías de muchos frames)
- Source size más chico (32×16 en vez de 64×32 → 4× menos PROGMEM por frame)
- Compartir paletas entre slots (no soportado actualmente)
- Bypass del cli usando teensy_reboot + USB raw HID (no probado)

### Boot animation procedural
Histórico: la primera versión vivía en `Animated/BootAnim/BootAnim.h`; hoy es
`Assets/Screens/BootScreen.h` (directo a los paneles). La lección del early return sigue igual.
Implementación vieja:
- Class `BootAnim : public Image` con buffer mutable 64×32 en RAM (2 KB) + font 3×5 inline
- `RenderFrame(elapsed_ms)` reescribe el buffer cada frame
- Llamado desde `ProtogenHUB75Project::Update()` durante los primeros ~3.5s

**CRITICAL: NO hacer early return** desde Update durante boot phase. Saltar `UpdateFace/pM.Update/AlignObjectFace/UpdateTransform` causa bootloop (probable HardFault — fans arrancan y se apagan en loop). En su lugar, usar un boolean `bootActive` y skipear solo `SelectFace()`:

```cpp
bool bootActive = false;
if (!bootDone) {
    // ... activate bootAnim on bsodPlane
    bootActive = true;
}
// ... rest of Update including UpdateFace, pM.Update, etc.
if (!bootActive) SelectFace(mode);
// ... continue normally
```

### Brightness en boot inicial = 0
El `Controller::brightness` member queda uninitalized hasta que `Display()` corre por primera vez (`UpdateBrightness()` hace soft-start de 5s desde 0 a `maxBrightness`). 

Si necesitas brightness durante init/setup (antes de Display), hardcodear con `matrix.setBrightness(maxBrightness)` directamente.

### Acceso al backgroundLayer desde código custom NO funciona pa boot
Intenté primero dibujar la boot animation directo al `backgroundLayer` (escribir pixels antes del main loop). Resultado: el contenido solo se vio en uno de los 2 paneles físicos (la cabeza tiene 2 paneles 64×32 en config C-shape stacking del SmartMatrix, mapeados a logical 64×64). Aún duplicando al top + bottom half del backgroundLayer no se mostró en ambos paneles.

**Solución que funcionó**: usar el render pipeline normal — la BootAnim como Image se monta en `bsodPlane` (FullScreenPlane que ya forma parte del scene). El scene render automáticamente envía a ambos paneles físicos vía el camera + Display() que ya maneja el mirror.

### Convención de paths para nuevos headers
Los `.h` generados por scripts Python van a `lib/ProtoTracer/Assets/Textures/Animated/<Name>/<Name>.h`. El include desde el header (subiendo 4 niveles) debe ser:
```cpp
#include "../../../../Scene/Materials/Static/Image.h"
```
(no 3 dots como sugiere el GIF Converter upstream que está mal).

### Slots actuales y stats
La lista de caras al día está en el README del repo.

---

## Workflow recomendado para nuevos cambios

```bash
# 1. Editar código (todo desde la raíz del repo)
$EDITOR lib/ProtoTracer/Examples/Protogen/ProtogenHUB75Project.h

# 2. Compilar (limpio si el cache jode)
pio run -e teensy40hub75

# 3. Flashear (retry loop incluido)
for i in 1 2 3; do
  out=$(sudo ~/.platformio/packages/tool-teensy/teensy_loader_cli \
    --mcu=TEENSY40 -w -s -v \
    .pio/build/teensy40hub75/firmware.hex 2>&1 | tail -1)
  [ "$out" = "Booting" ] && break
  sleep 1
done

# 4. Ver en la cabeza, iterar
```

Tiempo típico del ciclo edit→compile→flash: ~25 segundos (10s compile incremental + 12s flash + retry).

---

## Gestos APDS-9960: hallazgos de hardware (2026-06-11)

Se intentó un selector de caras por swipes y se descartó por física del mount,
NO por software. Si alguien lo reintenta, esto es lo que hay que saber:

### Lo que NO jala y por qué
- `Adafruit_APDS9960::readGesture()` — while(1) + delay(30): cuesta 30-300ms
  por llamada AUNQUE no haya mano. Congela el render. Jamás llamarlo.
- **Eje vertical muerto en este mount**: los diodos U/D están a mm; una mano
  vertical los cubre con <3ms de diferencia (< resolución de 2.78ms/sample del
  chip). Arriba y abajo son INDISTINGUIBLES. El eje horizontal sí discrimina
  (derecha física llegaba como chip-DOWN: módulo girado).
- **Saturación**: swipes cercanos clavan los 4 canales del FIFO a 255 incluso
  a ganancia 2x. Las diferencias absolutas estilo Adafruit no sirven; lo único
  útil es el ORDEN en que los canales se elevan.
- **Reflexión estática del visor**: a ganancia 2x los diodos leen U~33 D~118
  L~63 R~67 con GMODE activo SIN mano. Consecuencias: GEXTH es inútil (nunca
  baja de ahí → GMODE jamás sale solo → se necesita watchdog que escriba
  GCONF4=0x04) y cualquier baseline debe ser por-canal.
- **PDATA se congela durante GMODE=1** → isBooped() se pega si GMODE se atasca.
- Niveles de ESTE sensor: prox idle = 2, boop dispara ~7. El GPENTH de entrada
  al modo gesto debe ser ~10 (el default 50 de la lib jamás se alcanza).

### Gotchas de debugging que costaron horas
- **Serial por USB tras reflashear**: el Teensy re-enumera y si algún proceso
  tiene el FD viejo de /dev/ttyACM0 abierto, el kernel lo monta como ttyACM1 →
  el monitor oye un puerto muerto. SIEMPRE leer de
  `/dev/serial/by-id/usb-Teensyduino_USB_Serial_<número de serie>-if00` (symlink estable;
  `tools/flash.sh` y `tools/captura.py` lo encuentran solos).
- TEENSY_OPT_FASTEST_LTO NO rompe el USB serial (pista falsa — era lo del puerto).
- El firmware spamea "FPS: .." por serial siempre; filtrar al monitorear.
- `pkill -f` con un patrón que aparece en tu propio cmdline (heredocs incluidos)
  se suicida. Usar kill por PID.

### Lo que quedó
Doble-boop = siguiente cara (detector de flancos sobre IsBooped() con ventana
120-600ms, en ProtogenHUB75Project::Update). El firmware del selector por
gestos (funcional, con lector no-bloqueante + clasificador por orden de
saturación + watchdogs) quedó en firmware_gestos_v2_2026-06-11.hex por si se
quiere resucitar con otro mount/visor.
El fuente de ese selector NO se guardó (no hay stash): hay que reescribirlo.

2026-10-03 se re-estudió (veredicto: izquierda/derecha probablemente sí, arriba/abajo ≤20 %).
Ojo con la explicación de arriba del eje vertical: con el módulo girado la vertical física cae
en el eje L/R del chip. En junio sólo se movieron GGAIN 2x, GPENTH, GEXTH y GFIFOTH; nunca se
probó bajar la energía (GCONF2 0xA3=0x10: 1x/25 mA; GPULSE 0xA6=0x43) pa salir de saturación,
los GOFFSET (0xA4/0xA5/0xA7/0xA9, signo-magnitud) contra el reflejo del visor, ni un
clasificador por razón (a−b)/(a+b) en vez de orden. Si se retoma: sólo dentro de una ventana
que abra el doble-boop (GEN apagado fuera, así el boop no se entera de GMODE).
