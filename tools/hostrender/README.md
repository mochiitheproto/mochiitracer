# hostrender — previews de caras del protogen con el motor REAL de ProtoTracer

Compila el motor de `lib/ProtoTracer` de este repo (Scene, Object3D,
NukudeFlat + Morph, ObjectAlign, EasyEaseAnimator, MaterialAnimator, materiales,
HUB75DeltaCameraManager, Rasterizer/QuadTree) **desde el repo, sin tocarlo**, contra un shim
mínimo de Arduino (`shim/Arduino.h`, `shim/WString.h`: reloj virtual, Serial no-op, PROGMEM/F()
vacíos). Un driver (`src/hostrender.cpp`) replica, para las caras morph, lo que hace
`ProtogenHUB75Project::Update()` + `ProtogenProject::UpdateFace()`: mismos `AddParameter`/`AddViseme`,
mismas capas de material, `AlignObjectFace(pM, -7.5)`, wiggle, `TimeStep(120)`. Lo que sale es el
mismo buffer 64x32 que `DumpFrame()`.

Validado contra las capturas reales: 5 de 7 caras salen **idénticas pixel a pixel** (IoU 1.000),
las otras dos a 0.997 y 0.990 (ver "Validación").

```
hostrender/
  build.sh              compila (objetos del motor cacheados en build/) → bin/hostrender
  protorender.py        CLI: render | transition | validate | verts | meshmap | export
  shim/                 Arduino.h, WString.h
  src/hostrender.cpp    driver (espejo de ProtogenHUB75Project para caras morph)
  recipes/              recetas de ejemplo (.json)
  morphs/               morphs nuevos de ejemplo (.json)
  objects/              objetos extra de ejemplo (.json)
  examples/gen_ojo_x.py generador del morph "ojo en X"
  out/                  salidas (val/, demo/, malla_*.png)
```

## Arranque rápido

```bash
cd tools/hostrender                                   # desde la raíz del repo
./build.sh                                            # ~25 s la primera vez, luego ~3 s
python3 protorender.py render DEFAULT ANGRY TACHA     # caras nativas → out/<NOMBRE>.png/.txt
python3 protorender.py render recipes/ejemplo_xx.json # receta propia
python3 protorender.py transition DEFAULT recipes/ejemplo_xx.json --frames 50 --every 5
python3 protorender.py validate --step 0.125          # contra tus capturas en tools/cap/ref/ (no vienen en el repo; captura.py)
python3 protorender.py meshmap DEFAULT                # malla con números de vértice encima
python3 protorender.py verts DEFAULT                  # tabla de vértices (modelo y pixel)
python3 protorender.py export recipes/ejemplo_xx.json # C++ del morph listo pa pegar
```

Requisitos: g++ (C++17), python3 + numpy + Pillow.

Caras nativas que entiende por nombre: `DEFAULT ANGRY DOUBT FROWN LOOKUP SAD LOOKDOWN SURPRISED TACHA`
(los pesos y materiales son literalmente los de sus métodos en `ProtogenHUB75Project.h`).

### Salidas

- `out/<nombre>.txt`: 32 renglones × 64 pixeles hex `RRGGBB`, renglón 0 = arriba, panel "normal".
  Mismo formato que `cap/ref/caraNN_0.txt` y que `DumpFrame()`.
- `out/<nombre>.png`: `led_render` de `captura.py` (LED 8 px, gap 2, apagado = gris 28, `[espejo | normal]`).
- `transition`: `<A>_a_<B>_tira.png` (tira vertical con t en ms) + `<A>_a_<B>.gif`.

## Settings (lo que trae la EEPROM de la cabeza)

Ajustados contra las capturas; son el default de todas las recetas:

| setting  | valor  | cómo se sacó |
|----------|--------|--------------|
| facesize | **9**  | Menu FaceSize. Con 9 el IoU es ≥0.99; con 8 o 10 se cae a 0.70-0.90 (ver abajo) |
| color    | 2      | Menu Color = CYAN (`s` del firmware reporta color=2) |
| hue      | **[5, 2]** | Menu HueF/HueB: tiñe la base `gradientMat` y `flowNoise` (opacidad mínima 0.15). Ajuste por color medio: error total 3 niveles RGB en 3 caras |
| wiggle   | `off`  | 0,0. `auto` = FunctionGenerator con el reloj virtual; `[x, y]` = offset fijo (±2 u) |
| blink    | `off`  | `auto` = BlinkTrack real (cierra a los 3, 10 y 11 s de cada ciclo de 15 s, ±0.25 s) |
| time_ms  | 20000  | reloj virtual inicial (afecta materiales animados como CRAINBOW) |
| mic_ss   | null   | si se da, emula `UpdateFFTVisemes()` con magnitud constante (viseme SS = mag/2) |

Se pueden cambiar por receta (`"settings": {...}`) o por CLI (`--facesize`, `--wiggle 1,-0.5`).

## Formato de receta (JSON)

```jsonc
{
  "name": "mi_cara",
  "base": "DEFAULT",                 // opcional: hereda pesos/material de una nativa u otra receta
  "weights": {"Anger": 1.0, "HideBlush": 0.0, "OjoX": 1.0},
                                     // = AddParameterFrame(NukudeFace::X, w) cada frame.
                                     //   OJO: sólo mueve morphs ligados en LinkControlParameters
                                     //   (Anger Sadness Surprised Doubt Frown LookUp LookDown HideBlush)
                                     //   o morphs nuevos/`link`. Los demás → WARN y el firmware los ignora.
                                     //   HideBlush está invertido: 0 = chapitas visibles, sin frame = escondidas.
  "visemes": {"AH": 0.7},            // = AddParameterFrame(Viseme::AH + 100, w)  (EE AH UH AR ER OO SS)
  "link": ["MoveEye", ["BiggerNose", 15, "Cosine"]],
                                     // simula agregar AddParameter(NukudeFace::X, ref, frames, interp)
  "material": ["CRED", 0.8],         // = AddMaterialFrame(Color::CRED, 0.8). También "CRAINBOW" o
                                     //   lista [["CRED",0.8],["CRAINBOW",0.5]]. Colores: CYELLOW CORANGE(=cyan)
                                     //   CWHITE CGREEN CPURPLE CRED CBLUE CRAINBOW CRAINBOWNOISE
                                     //   CHORIZONTALRAINBOW CBLACK. El cyan del menú se agrega siempre (color=2).
  "morphs": ["../morphs/ojo_x.json"],  // morphs nuevos (ruta relativa a la receta, o inline {...})
  "objects": ["../objects/x_ojo.json"],// objetos extra
  "objects_on": {"x_ojo": true},     // prender/apagar objetos por receta (default: los de la receta, on)
  "settings": {"facesize": 9, "wiggle": "off"}
}
```

Una receta = "lo que haría un método de cara cada frame". El render simula 250 frames de la cara
previa (DEFAULT por default, `--warmup`) y luego 1.5 s (187 frames a 125 fps) de la receta, igual
que `captura.py` (cambia de cara y espera 1.5 s). Los pesos pasan por el EasyEaseAnimator real:
Overshoot (resorte) asienta en ~0.993, no en 1.0 — es así en la cabeza también.

### Morph nuevo (JSON)

```jsonc
{
  "name": "OjoX",                    // se maneja con "weights": {"OjoX": 1}
  "frames": 15, "interp": "Overshoot",   // como AddParameter(...). Cosine/Linear/Bounce también
  // cualquiera de estas formas (se combinan):
  "indices": [1, 2], "deltas": [[dx,dy,dz], [dx,dy,dz]],   // deltas en coords de modelo (como NukudeFlat.h)
  "deltas": {"5": [dx, dy, dz]},                           // ídem como dict
  "positions": {"5": [x, y]},                              // posición absoluta de modelo (z se conserva)
  "pixel_positions": {"5": [col, renglon]},                // ★ dónde quieres el vértice en el panel normal
  "pin": "all"                                             // ★ los demás vértices se quedan donde estaban
}
```

`pixel_positions` es lo más cómodo pa diseñar: col 0..63 (izq→der), renglón 0..31 (arriba→abajo),
enteros = centro de un LED (se prende el LED si su centro cae dentro de un triángulo). El tool
resuelve los deltas de modelo **iterando contra el motor real** (≤12 iteraciones, error típico
<0.03 px), compensando la re-alineación que provoca mover vértices (ver "Gotchas: bbox y plano").
`pin: "all"` mete al morph todos los vértices 0-40 y 53 que no tengan destino, con el destino
"donde estaban sin este morph" → la boca/nariz no se mueven. Recomendado siempre.

### Objeto extra (JSON)

```jsonc
{
  "name": "x_ojo",
  "space": "pixel",                  // "pixel" (default): vértices [col, renglón(, z)] del panel normal.
                                     //   z default 0 = delante de la cara (la cara vive en z≈200-300).
                                     //   Sigue el wiggle de la cara y NO afecta su alineación.
                                     // "camera": coords de cámara crudas (X 0..189, Y 0..93), "wiggle": true opcional
                                     // "model": coords de NukudeFlat, se alinea JUNTO con la cara
                                     //   (AlignObjectsFace) → mueve el plano/bbox: inclina y re-escala la cara.
  "material": "face",                // "face" = el MaterialAnimator de la cara, o [R, G, B] fijo
  "vertices": [[36.5, 2.0], [51.5, 11.2], ...],
  "triangles": [[0, 1, 2], [0, 2, 3]]
}
```

En firmware un objeto `pixel` es un `Object3D` más en la escena con vértices `X = col*3`,
`Y = (31-renglón)*3`, `z = 0`, y cada frame `SetPosition(GetWiggleOffset())` + `UpdateTransform()`.
`export` imprime sus arrays.

## Transiciones

```bash
python3 protorender.py transition DEFAULT recipes/ejemplo_xx.json --frames 50 --every 5 [--name x]
```

Asienta A (250 frames) y luego corre B N frames a 125 fps (TimeStep(120) → 8 ms) con el
EasyEase real, guardando 1 de cada `--every`. Overshoot (resorte k=1, amortiguamiento 0.35,
dT 0.25/frame) sobre-dispara FUERTE: un 0→1 llega a **1.57 a los ~100 ms**, rebota a 0.68 a los
~190 ms y asienta (0.993) a los ~0.36 s. Cosine es suave en 15 frames; los visemes son Linear de 2 frames. Si tu morph se ve
raro a medio camino, prueba `"interp": "Cosine"` en el JSON del morph.

## Validación

`python3 protorender.py validate --step 0.125` reproduce las 7 caras con exactamente los
pesos/materiales de sus métodos, simula "venía de DEFAULT, cambio de cara, espero 1.5 s", y
compara pixeles prendidos contra `cap/ref/caraNN_0.txt`:

| cara    | IoU (wiggle subpixel, sin shift) | wiggle (u) | IoU (wiggle 0, mejor shift entero ±3) | LEDs host/real | color medio host / real |
|---------|------|-----------------|------|---------|------------------------------|
| DEFAULT | **1.000** | (-0.75, 1.375) | 0.802 | 337/337 | (18,236,215) / (17,236,216) |
| ANGRY   | **1.000** | (1.875, 1.0)   | 0.855 | 323/323 | (206,46,33) / (207,46,32) |
| DOUBT   | **0.990** | (-1.75, -1.875)| 0.787 | 302/301 | (16,236,217) / (22,236,211) |
| FROWN   | **1.000** | (-0.5, 0.625)  | 0.901 | 334/334 | (18,236,215) / (17,236,216) |
| LOOKUP  | **0.997** | (1.75, 1.625)  | 0.766 | 354/353 | (18,236,214) / (19,236,214) |
| SAD     | **1.000** | (-2.0, -1.5)   | 0.745 | 306/306 | (10,46,229) / (10,46,229) |
| TACHA   | **1.000** | (1.375, 2.0)   | 0.811 | 385/385 | (91,168,126) / (109,167,119) |

- El wiggle del firmware es `SetPosition(±2, ±2)` en unidades de cámara = **±0.67 px**: es
  subpixel, así que un shift entero no lo modela (de ahí el 0.75-0.90 de la columna "shift entero").
  Barriendo el offset dentro del rango físico (±2 u, paso 0.125) y **sin ningún shift** sale 1.000 en
  5 caras; DOUBT y LOOKUP difieren en 3 y 1 LED de borde.
- Control negativo: con facesize 8 o 10 y el mismo barrido el IoU queda en 0.70-0.90 → la búsqueda
  no "inventa" el match; sólo el modelo correcto da 1.000.
- El color de TACHA (CRAINBOW) depende del tiempo (`ratio = millis()%5000`), por eso el color medio
  no cuadra exacto aunque la forma sí.
- Salidas: `out/val/validacion.png` (real / host / diff por cara: gris = ambos, rojo = sólo host,
  azul = sólo real) y `out/val/validacion.json`.

## Malla NukudeFlat (pa diseñar morphs)

Imágenes: `out/malla_DEFAULT.png` (chapitas escondidas), `out/malla_blush_on.png` (chapitas
visibles), `out/malla_ejemplo_xx.png` (el ojo ya en X). `python3 protorender.py verts <receta>`
imprime la tabla completa. 54 vértices, 44 triángulos. Coordenadas pixel = DEFAULT, facesize 9,
wiggle 0, panel normal (col izq→der, renglón arriba→abajo).

**Mapa modelo → pixel (DEFAULT)**, afín exacto (error 0.007 px; la cámara es ortográfica):
```
col     = -0.19528·x - 0.00082·y + 0.29714·z + 21.640
renglón = -0.03823·x - 0.18319·y - 0.02563·z + 38.703
```
O sea: +x de modelo va a la IZQUIERDA (mirrorX), +y va hacia ARRIBA, y la z pesa mucho en la
columna (la malla no es plana; el firmware la "aplana" con un plano PCA girado -7.5°).

### Ojo — vértices 1-15, 16 triángulos (índices de `basisIndexes`)

```
T0 (13,8,7)   T1 (2,3,14)   T2 (8,6,5)    T3 (13,1,6)   T4 (11,7,2)   T5 (15,4,2)
T6 (7,10,3)   T7 (8,12,10)  T19 (11,13,7) T20 (9,2,14)  T21 (12,8,5)  T22 (8,13,6)
T23 (4,11,2)  T24 (9,15,2)  T25 (2,7,3)   T26 (7,8,10)
```
| v | col | renglón | papel | | v | col | renglón | papel |
|---|-----|---------|-------|-|---|-----|---------|-------|
| 11 | 36.60 | 1.98 | contorno arriba | | 12 | 51.42 | 8.44 | contorno abajo |
| 13 | 44.34 | 1.89 | contorno arriba (más alto) | | 10 | 44.02 | 8.69 | contorno abajo |
| 1  | 51.48 | 3.48 | contorno arriba-der | | 3  | 36.81 | 10.09 | contorno abajo |
| 6  | 57.38 | 6.21 | contorno der | | 14 | 28.78 | 12.36 | punta abajo-izq |
| 5  | 60.13 | 8.64 | **punta der = borde derecho del bbox** | | 9 | 27.88 | 8.41 | contorno izq |
| 2  | 33.94 | 6.33 | interior (hub izq, 7 triángulos) | | 15 | 28.19 | 5.52 | contorno izq |
| 7  | 40.78 | 5.26 | interior (hub centro, 6 tri) | | 4 | 31.90 | 3.16 | contorno arriba-izq |
| 8  | 48.16 | 5.38 | interior (hub der, 6 tri) | | | | | |

Estructura útil: si juntas **2, 7 y 8 en un punto C**, T0, T4, T25 y T26 quedan de área cero y los
otros 12 triángulos quedan como un **abanico desde C** con contorno (sentido horario)
`11 → 13 → 1 → 6 → 5 → 12 → 10 → 3 → 14 → 9 → 15 → 4 → 11`, donde (13,1,6) es el único triángulo
que no toca C. Cualquier figura "estrellada" desde C con 12 esquinas sale exacta: así está hecha
la X de `examples/gen_ojo_x.py` (13 muesca arriba; 1,6 punta arriba-der; 5 muesca der; 12,10 punta
abajo-der; 3 muesca abajo; 14,9 punta abajo-izq; 15 muesca izq; 4,11 punta arriba-izq). Mismo
truco sirve pa corazón, estrella, espiral gruesa, `>`/`<`, `^`, `T_T`, etc.

Esconder triángulos: mandar sus 3 vértices al mismo punto (área cero = invisible). Es lo que hace
`HideBlush` (las 12 chapitas colapsan en (48.54, 5.06), dentro del ojo).

### Boca — vértices 16-21 y 29-40, 16 triángulos

Tira de quads de izquierda (centro de la cara) a derecha (comisura junto al ojo). Pares
(arriba, abajo) en orden: (30,29) (32,31) (34,33) (36,35) (38,37) (40,39) (20,21) (18,19) (16,17).
Triángulos: T8 (34,36,35) T9 (19,18,16) T10 (21,20,18) T14 (37,38,40) T15 (31,29,30) T16 (32,34,33)
T17 (36,38,37) T18 (39,40,20) T27 (33,34,35) T28 (17,19,16) T29 (19,21,18) T33 (39,37,40)
T34 (32,31,30) T35 (31,32,33) T36 (35,36,37) T37 (21,39,20).

| v | col | renglón | | v | col | renglón | | v | col | renglón |
|---|-----|---------|-|---|-----|---------|-|---|-----|---------|
| 30 | 0.67 | 25.40 | | 36 | 23.78 | 26.10 | | 20 | 43.67 | 24.26 |
| 29 | 3.47 | 28.19 | | 35 | 25.71 | 28.54 | | 21 | 44.38 | 26.62 |
| 32 | 8.32 | 26.50 | | 38 | 30.28 | 24.30 | | 18 | 49.38 | 22.62 |
| 31 | 10.65 | 29.04 | | 37 | 31.66 | 27.21 | | 19 | 50.53 | 24.13 |
| 34 | 16.22 | 27.53 | | 40 | 37.61 | 25.96 | | 16 | 53.99 | 21.31 |
| 33 | 19.19 | 30.33 | | 39 | 38.05 | 28.81 | | 17 | 55.55 | 21.96 |

v30 = borde izquierdo del bbox, v33 = borde de abajo. Visemes ligados: EE→vrc_v_ee, AH→vrc_v_aa,
UH→vrc_v_dd, AR→vrc_v_rr, ER→vrc_v_ch, OO→vrc_v_oh, SS→vrc_v_ss (todos tocan 16-21 y 29-40).

### Nariz — vértices 0 y 22-28, 6 triángulos

T11 (23,24,22) T12 (25,26,27) T13 (28,27,23) T30 (0,23,22) T31 (28,25,27) T32 (0,28,23).
v0 (10.50, 14.95) v22 (9.32, 15.89) v23 (10.99, 16.10) v24 (8.59, 17.47) v25 (15.18, 14.95)
v26 (16.10, 15.86) v27 (13.00, 15.75) v28 (13.03, 14.50). Rayita curva de ~8 LEDs en renglones
15-17, cols 9-16 (en la cara completa queda junto al centro).

### Chapitas — vértices 41-52, 6 triángulos (3 barritas = 3 quads)

Quads {41,42,43,44} (T40 (42,43,44), T41 (44,41,42)), {45,46,47,48} (T39 (48,45,47), T43 (47,45,46)),
{49,50,51,52} (T38 (50,52,49), T42 (52,50,51)). Con `HideBlush` = 0 (visibles, de `blush_on`):
41 (54.14,14.89) 42 (52.88,11.72) 43 (51.04,11.20) 44 (51.37,16.37) | 45 (49.52,16.40) 46 (49.10,11.08)
47 (46.18,11.25) 48 (48.50,16.45) | 49 (46.85,16.42) 50 (44.41,11.62) 51 (43.19,11.90) 52 (43.42,15.48).
Escondidas: las 12 en (48.54, 5.06). Ojo: al mostrarlas el plano PCA cambia un poquito y toda la
cara se mueve ~0.2-0.4 px.

### v53 — ancla

(80, 180, 90) en modelo, no está en ningún triángulo: es el **borde de arriba del bbox** (renglón 0.36).

## Gotchas (importantes pa diseñar)

1. **El bbox es fijo en pantalla.** `ObjectAlign` en modo Stretch estira la malla para que su
   caja ocupe siempre cols [0.67, 60.13] × renglones [0.36, 30.33] (facesize 9). Si un morph mete el
   vértice más a la derecha (hoy v5, la punta del ojo), **toda la cara se estira** pa rellenar.
   Regla: deja algún vértice tocando 60.13 a la derecha (la X de ejemplo ajusta su punta sola) o
   usa `pin: "all"`. Lo mismo arriba (v53), izquierda (v30) y abajo (v33).
2. **El plano PCA se mueve.** Antes de alinear, el firmware calcula el plano de mejor ajuste de
   TODOS los vértices (`GetPlaneOrientation`) y gira -7.5°. Mover 15 vértices del ojo inclina ese
   plano y la boca/nariz se mueven 1-3 px. El solver de `pixel_positions` lo compensa (pin + ajuste
   de profundidad a lo largo de la dirección de vista, que no cambia pixeles). Objetos `model`
   también lo mueven; por eso el default de objetos es `pixel`.
3. **Pesos > 0 nada más**: `NukudeFace::Update()` sólo aplica morphs con peso > 0 (el undershoot
   del resorte se corta en 0).
4. **Visemes cuantizados**: ligados con rampa de 2 frames + Linear → sólo llegan a 0, 0.5 o 1.0
   (AH 0.7 se queda en 0.5).
5. **Bug en TachaFace()**: `AddParameterFrame(NukudeFace::vrc_v_aa, 0.7f)` usa el diccionario 19,
   pero vrc_v_aa está ligado como viseme AH (diccionario 105) → no hace nada; la boca de TACHA es la
   default (la captura lo confirma). Pa abrirla: `AddParameterFrame(Viseme::MouthShape::AH + 100, 0.7f)`
   (ver `recipes/ejemplo_tacha_arreglada.json`; ojo, con eso queda en 0.5 por el punto 4).
6. **Bug latente en la escena**: `ProtogenProject(..., numObjects=2, ...)` crea `Scene(3)`, pero se le
   hacen 4 `AddObject` (background, pM, deltaDisplayBackground, bsodPlane) → escribe 1 puntero fuera
   del arreglo en el heap. Hoy no truena pero si agregas objetos súbele `numObjects` (aquí el host
   usa `Scene(16)`).
7. **Transiciones**: Overshoot (default) sobre-dispara; con morphs de deltas grandes (la X mueve
   vértices ~45 u) las formas intermedias se ven raras. `"interp": "Cosine"` lo suaviza.
8. **No modelado** (no aplica a caras morph en reposo): menú/HUD visible, efectos de pantalla del
   menú (EffectS ≠ 0), boop (Surprised), visemes del micrófono (salvo `mic_ss`), cámaras laterales
   (88 px de los Delta), brillo del controller (las capturas son pre-brillo, igual que aquí).

## Llevarlo al firmware

`python3 protorender.py export <receta.json | morph.json>` imprime:
- por morph: `int XIndexes[n]` + `Vector3D XVectors[n]` (deltas ya resueltos) y los 5 pasos
  (enum, morphCount/morphs[], `Morph(n, ...)`, `AddParameter(NukudeFace::X, ...)` en
  LinkControlParameters). Lo limpio es una copia de NukudeFlat.h (p.ej. `NukudeFlatX.h`) pa no
  tocar el original.
- por objeto: `Vector3D xVertices[]` + `IndexGroup xIndexes[]` (coords de cámara pa `pixel`).
- La cara nueva es un método más: `AddParameterFrame(NukudeFace::OjoX, 1.0f); AddMaterialFrame(...)`.
  Costo: un morph = n sumas de Vector3D por frame; nada que ver con ImageSequence.

## Ejemplos incluidos

| receta | qué demuestra |
|--------|---------------|
| `recipes/ejemplo_xx.json` | x.x con morph `pixel_positions` + `pin: all` sobre los 15 vértices del ojo (`morphs/ojo_x.json`, regenerable con `examples/gen_ojo_x.py cx cy hx hy grosor`) |
| `recipes/ejemplo_xx_objeto.json` | x.x versión objeto: morph `OjoOculto` colapsa el ojo + objeto `x_ojo` (2 barras) |
| `recipes/ejemplo_objeto.json` | objeto `pixel` encima de la DEFAULT (destello) |
| `recipes/ejemplo_tacha_arreglada.json` | TACHA con la boca abierta por viseme |
| `recipes/ejemplo_parpadeo.json` | `link` de un morph no ligado (Blink) |
| `recipes/blush_on.json` | DEFAULT con chapitas visibles |

## Formato del script de bajo nivel (stdin de `bin/hostrender`)

`protorender.py` lo arma solo; por si lo quieres usar directo:
```
set facesize 9 | set color 2 | set hue 5 2 | set time_ms 20000 | set render_us 4000
set wiggle off|auto|<x> <y>  | set blink off|auto | set mic_ss <v>
link <Morph> [frames] [Overshoot|Cosine|Linear|Bounce] [invert]
morph <Nombre> <n> [frames] [interp]        + n líneas "idx dx dy dz"
object <nombre> <model|camera|camera_wiggle> <nv> <nt> <face | rgb R G B>   + nv líneas "x y z" + nt líneas "a b c"
target clear | target param <Morph|Nombre> <w> | target viseme <EE..SS> <w>
target material <CCOLOR> <opacidad> | target object <nombre> on|off
run <frames> <cada>       # cada=0: emite sólo el último; -1: nada; k: 1 de cada k
sweep x0 x1 y0 y1 paso    # re-renderiza el último estado con varios wiggles
verts                     # vértices (cámara, col/renglón, modelo con morphs)
```
Salida: `FRAME n t=ms wig=x,y`, línea `W` con los 26 pesos (+ morphs nuevos), 32 renglones hex, `END`.
