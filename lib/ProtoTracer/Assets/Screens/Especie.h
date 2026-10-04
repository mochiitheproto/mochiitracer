#pragma once

// La especie de la cabeza. Sólo cambia textos: el arranque ("PROTOGEN OS" / "PRIMAGEN OS", en
// los paneles y en la OLED) y la pantalla azul ("YOUR PROTOGEN NEEDS A BOOP"). Los Primagen son
// la especie madre de los Protogen (abierta en 2026 por Zenith's Outer Reach): misma
// electrónica, visor más largo.
//   -D ESPECIE_PRIMAGEN   en el build (sin definir = Protogen)
// Los dos nombres tienen 8 letras: el texto ocupa lo mismo y nada se recorre.
#ifdef ESPECIE_PRIMAGEN
#define ESPECIE_NOMBRE "PRIMAGEN"
#else
#define ESPECIE_NOMBRE "PROTOGEN"
#endif

static_assert(sizeof(ESPECIE_NOMBRE) == 9, "ESPECIE_NOMBRE: 8 letras, como PROTOGEN");
