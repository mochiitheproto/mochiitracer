#pragma once

#include <EEPROM.h>
#include "../Templates/ProtogenProjectTemplate.h"
#include "../../Assets/Models/OBJ/DeltaDisplayBackground.h"
#include "../../Assets/Models/OBJ/FullScreenPlane.h"
#include "../../Assets/Models/FBX/NukudeFlat.h"
#include "../../Assets/Textures/Static/LowBattery.h"
#include "../../Assets/Textures/Static/TestGrid.h"
#include "../../Assets/Screens/BootScreen.h"
#include "../../Assets/Screens/BsodScreen.h"
#include "../../Assets/Screens/CalScreen.h"
#include "../../Assets/Screens/BlankScreen.h"
#include "../../Assets/Textures/Animated/Kaomoji/Kaomoji.h"
#include "../../Assets/Textures/Animated/KaoPink140/KaoPink140.h"

#include "../../Camera/CameraManager/Implementations/HUB75DeltaCameras.h"
#include "../../Controller/HUB75Controller.h"
#include "BoopGesture.h"
#include "BoopScript.h"

class ProtogenHUB75Project : public ProtogenProject {
private:
    HUB75DeltaCameraManager cameras;
    HUB75Controller controller = HUB75Controller(&cameras, 50, 50);
    NukudeFace pM;
    DeltaDisplayBackground deltaDisplayBackground;
    FullScreenPlane bsodPlane;
    LowBattery lowBatImage = LowBattery(Vector2D(192.0f, 94.0f), Vector2D(96.0f, 47.0f));
    TestGrid   testGrid    = TestGrid  (Vector2D(192.0f, 94.0f), Vector2D(96.0f, 47.0f));

    // Boot animation (no slot seleccionable). Corre los primeros 3.5s del Update
    // y después se desactiva. Renderea procedural a un buffer RAM (2KB).
    // Arranque y BSOD se dibujan directo a los paneles (texto derecho en los
    // dos, ver ScreenSource.h); las caras siguen por la cámara con espejo.
    BootScreen bootScreen;
    BsodScreen bsodScreen;
    CalScreen  calScreen;
    BlankScreen blankScreen;
    uint32_t   bootStartMs = 0;
    bool       bootDone    = false;
    uint32_t   bsodStartMs = 0;
    bool       bsodActive = false, bsodWasActive = false;  // pa que el % arranque en 0 al entrar
    bool       calTest     = false;   // comando 't': patrón L/R pa calibrar
    bool       screenOff   = false;   // 'o0'/'o1': apagar/prender, NO se guarda en EEPROM
    bool       calLoaded   = false;
    static const int EEPROM_PANEL_CAL = 200;  // el menú usa 0..12
    static const int EEPROM_IDIOMA = 201;     // idioma de la OLED (l<N>); 255 = IDIOMA_DEFECTO del build
    // Boop + boooop (tap y el 2o sostenido) = siguiente cara. Ver bloque en
    // Update y BoopGesture.h.
    BoopGesture boopGesture;
    // 'g<N>': dedo de mentira que le da al gesto un preset de boop_presets.json (el mismo que
    // el simulador) pa probarlo sin tocar el sensor. No se guarda; se acaba solo.
    BoopScript boopScript;
    // Osciloscopio del boop por serial: 'p' prende, 'q' apaga. Una línea por
    // frame: "P <ms> <prox> <base> <booped> <errores_i2c>".
    bool       boopScope      = false;
    // Osciloscopio de audio 'a1'/'a0' (pa calibrar la boca con el micro): una línea
    // por frame "A <ms> <mag*1000> <temp*10> <128 bins x 3 hex>", bin = (v+1)*1000.
    bool       audioScope     = false;
    // Línea de comando por serial que se va juntando hasta el Enter (ver ReadSerialCommands).
    char       cmdBuf[8];
    uint8_t    cmdLen         = 0;
    // KAOMOJI estroboscópico: 6 kaomojis (joy/amor/jugueteo) alternados con
    // flashes blancos full-screen. 18 frames @ 18 fps → loop 1s.
    KaomojiSequence kaomojiAnim = KaomojiSequence(Vector2D(192.0f, 94.0f), Vector2D(96.0f, 47.0f), 18.0f);


    // KAOMOJIS BPM cycle: 6 caras (happy, love, laugh, sparkle, soft, teary)
    // 1 beat per kaomoji con fade. 54 frames per loop.
    // 140 BPM @ 21.0 fps. 128 BPM @ 19.2 fps. 9 frames per beat (exacto).
    // KP140: kaomojis pink fosfo 1 beat c/u con fade, 140 BPM (la única que quedó).
    KaoPink140Sequence  kaoPink140Anim  = KaoPink140Sequence (Vector2D(192.0f, 94.0f), Vector2D(96.0f, 47.0f), 14.0f);


    // Caras morph nuevas (DEAD, AMOR, OWO, HAPPY): pausan el parpadeo y callan
    // los visemes del micro cada frame (el morph Blink y los visemes deshacen sus
    // ojos/bocas). Update() lo re-arma antes de elegir cara y aplica el mute.
    bool       muteVisemes    = false;
    // DEAD: la lengua sale 150 ms después de entrar al slot (mismo patrón que bsod*).
    bool       deadActive = false, deadWasActive = false;
    uint32_t   deadSinceMs    = 0;

    // El índice es el del switch de SelectFace; faceCount (constructor) y la OLED salen de aquí.
	const __FlashStringHelper* faceArray[17] = {F("DEFAULT"), F("ANGRY"), F("DOUBT"), F("FROWN"), F("LOOKUP"), F("SAD"), F("BSOD"), F("LOWBAT"), F("AUDIO1"), F("AUDIO2"), F("TACHA"), F("KAOMOJI"), F("KP140"), F("DEAD"), F("AMOR"), F("OWO"), F("HAPPY")};

    void LinkControlParameters() override {//Called from parent
        AddParameter(NukudeFace::Anger, pM.GetMorphWeightReference(NukudeFace::Anger), 15);
        AddParameter(NukudeFace::Sadness, pM.GetMorphWeightReference(NukudeFace::Sadness), 15, IEasyEaseAnimator::InterpolationMethod::Cosine);
        AddParameter(NukudeFace::Surprised, pM.GetMorphWeightReference(NukudeFace::Surprised), 15);
        AddParameter(NukudeFace::Doubt, pM.GetMorphWeightReference(NukudeFace::Doubt), 15);
        AddParameter(NukudeFace::Frown, pM.GetMorphWeightReference(NukudeFace::Frown), 15);
        AddParameter(NukudeFace::LookUp, pM.GetMorphWeightReference(NukudeFace::LookUp), 15);
        AddParameter(NukudeFace::LookDown, pM.GetMorphWeightReference(NukudeFace::LookDown), 15);

        AddParameter(NukudeFace::HideBlush, pM.GetMorphWeightReference(NukudeFace::HideBlush), 15, IEasyEaseAnimator::InterpolationMethod::Cosine, true);

        // Caras nuevas: Cosine a fuerza. Con Overshoot el resorte llega a 1.57 y los
        // morphs grandes se salen del bbox (toda la cara se estira a medio camino).
        AddParameter(NukudeFace::DeadEye, pM.GetMorphWeightReference(NukudeFace::DeadEye), 15, IEasyEaseAnimator::InterpolationMethod::Cosine);
        AddParameter(NukudeFace::DeadMouth, pM.GetMorphWeightReference(NukudeFace::DeadMouth), 15, IEasyEaseAnimator::InterpolationMethod::Cosine);
        AddParameter(NukudeFace::LoveFace, pM.GetMorphWeightReference(NukudeFace::LoveFace), 15, IEasyEaseAnimator::InterpolationMethod::Cosine);
        AddParameter(NukudeFace::LoveBeat, pM.GetMorphWeightReference(NukudeFace::LoveBeat), 6, IEasyEaseAnimator::InterpolationMethod::Cosine);
        AddParameter(NukudeFace::OwoEye, pM.GetMorphWeightReference(NukudeFace::OwoEye), 20, IEasyEaseAnimator::InterpolationMethod::Cosine);
        AddParameter(NukudeFace::OwoMouth, pM.GetMorphWeightReference(NukudeFace::OwoMouth), 20, IEasyEaseAnimator::InterpolationMethod::Cosine);
        AddParameter(NukudeFace::OwoFix, pM.GetMorphWeightReference(NukudeFace::OwoFix), 20, IEasyEaseAnimator::InterpolationMethod::Cosine);
        AddParameter(NukudeFace::HappyEyes, pM.GetMorphWeightReference(NukudeFace::HappyEyes), 15, IEasyEaseAnimator::InterpolationMethod::Cosine);
        AddParameter(NukudeFace::HappyMouth, pM.GetMorphWeightReference(NukudeFace::HappyMouth), 15, IEasyEaseAnimator::InterpolationMethod::Cosine);

        AddViseme(Viseme::MouthShape::EE, pM.GetMorphWeightReference(NukudeFace::vrc_v_ee));
        AddViseme(Viseme::MouthShape::AH, pM.GetMorphWeightReference(NukudeFace::vrc_v_aa));
        AddViseme(Viseme::MouthShape::UH, pM.GetMorphWeightReference(NukudeFace::vrc_v_dd));
        AddViseme(Viseme::MouthShape::AR, pM.GetMorphWeightReference(NukudeFace::vrc_v_rr));
        AddViseme(Viseme::MouthShape::ER, pM.GetMorphWeightReference(NukudeFace::vrc_v_ch));
        AddViseme(Viseme::MouthShape::OO, pM.GetMorphWeightReference(NukudeFace::vrc_v_oh));
        AddViseme(Viseme::MouthShape::SS, pM.GetMorphWeightReference(NukudeFace::vrc_v_ss));

        AddBlinkParameter(pM.GetMorphWeightReference(NukudeFace::Blink));
    }

    void Default(){}

    void Angry(){
        AddParameterFrame(NukudeFace::Anger, 1.0f);
        AddMaterialFrame(Color::CRED);
    } 

    void Sad(){
        AddParameterFrame(NukudeFace::Sadness, 1.0f);
        AddParameterFrame(NukudeFace::Frown, 1.0f);
        AddMaterialFrame(Color::CBLUE);
    }

    void Surprised(){
        AddParameterFrame(NukudeFace::Surprised, 1.0f);
        AddParameterFrame(NukudeFace::HideBlush, 0.0f);
        AddMaterialFrame(Color::CRAINBOW);
    }
    
    void Doubt(){
        AddParameterFrame(NukudeFace::Doubt, 1.0f);
    }
    
    void Frown(){
        AddParameterFrame(NukudeFace::Frown, 1.0f);
    }

    void LookUp(){
        AddParameterFrame(NukudeFace::LookUp, 1.0f);
    }

    void LookDown(){
        AddParameterFrame(NukudeFace::LookDown, 1.0f);
    }

    void BsodFace(){
        // Pantallazo azul a lo ancho de los dos paneles. El % cuenta a saltos
        // desde que entras al slot; al llegar a 100 se "reinicia" (corre la
        // animación de arranque) y vuelve a empezar el pantallazo.
        static const uint32_t HOLD_MS = 1500;
        uint32_t now = millis();
        if (!bsodWasActive) bsodStartMs = now;
        bsodActive = true;

        uint32_t t = now - bsodStartMs;
        if (t >= BsodScreen::COUNT_MS + HOLD_MS + BootScreen::TOTAL_MS) {
            bsodStartMs = now;
            t = 0;
        }
        if (t >= BsodScreen::COUNT_MS + HOLD_MS) {
            bootScreen.Render(t - BsodScreen::COUNT_MS - HOLD_MS);
            controller.SetScreenSource(&bootScreen);
        } else {
            bsodScreen.Render(BsodScreen::Percent(t));
            controller.SetScreenSource(&bsodScreen);
        }
    }

    void LowBatteryFace(){
        // Low battery icon, blinking ~1.5 Hz (400ms on, 400ms off)
        pM.GetObject()->Disable();
        deltaDisplayBackground.GetObject()->Disable();
        bool blinkOn = (millis() / 400) % 2 == 0;
        if (blinkOn) {
            bsodPlane.GetObject()->SetMaterial(&lowBatImage);
            bsodPlane.GetObject()->Enable();
        }
        // else: plane stays disabled (from Update reset) → black screen during off-phase
    }


    void KaomojiFace(){
        // Kaomojis estroboscópicos: 6 caras (joy/amor/jugueteo) alternadas
        // con flashes blancos full-screen. Loop ~1s.
        pM.GetObject()->Disable();
        deltaDisplayBackground.GetObject()->Disable();
        kaomojiAnim.Update();
        bsodPlane.GetObject()->SetMaterial(&kaomojiAnim);
        bsodPlane.GetObject()->Enable();
    }


    void KaoBpmFace(ImageSequence& anim){
        pM.GetObject()->Disable();
        deltaDisplayBackground.GetObject()->Disable();
        anim.Update();
        bsodPlane.GetObject()->SetMaterial(&anim);
        bsodPlane.GetObject()->Enable();
    }

    void TachaFace(){
        // Cara arcoíris seleccionable: boca abierta + ojos alertas + cachetes visibles
        // + spiral rainbow material. Sin overlays — el quad de lengua daba bugs
        // visuales por el sistema 3D (no tenemos el offset de proyección bien).
        AddParameterFrame(NukudeFace::vrc_v_aa, 0.7f);
        AddParameterFrame(NukudeFace::Surprised, 0.4f);
        AddParameterFrame(NukudeFace::HideBlush, 0.0f);
        AddMaterialFrame(Color::CRAINBOW);
    }

    // --- Caras morph nuevas (geometría pura sobre NukudeFlat, igual de ligeras que
    // las default). Todas pausan el parpadeo y callan los visemes CADA frame; el
    // re-armado y el mute están en Update().

    void DeadFace(){
        // x.x "muerto": ojos en X; a los 150 ms de entrar la boca se vuelve línea
        // plana con la lengua de fuera (:P). Color = el del menú.
        uint32_t now = millis();
        if (!deadWasActive) deadSinceMs = now;
        deadActive = true;
        DisableBlinking();
        muteVisemes = true;
        AddParameterFrame(NukudeFace::DeadEye, 1.0f);
        // DeadMouth se resolvió encima de DeadEye=1: nunca va sin él. Al salir
        // bajan juntos (mismos frames), sólo la entrada va escalonada.
        if (now - deadSinceMs >= 150) AddParameterFrame(NukudeFace::DeadMouth, 1.0f);
    }

    void AmorFace(){
        // ♡ω♡ enamorade: corazones que laten "ba-dum" cada 0.9 s, boca ω y sonrojo,
        // en rosa mexicano.
        DisableBlinking();
        muteVisemes = true;
        AddParameterFrame(NukudeFace::LoveFace, 1.0f);
        uint16_t ph = millis() % 900;
        bool beat = ph < 80 || (ph >= 190 && ph < 270);
        AddParameterFrame(NukudeFace::LoveBeat, beat ? 0.0f : 1.0f);   // 1 = reposo (chico), 0 = latido (grande)
        AddParameterFrame(NukudeFace::HideBlush, 0.0f);                 // LoveFace acomoda las chapitas
        AddMaterialFrame(Color::CPURPLE, 1.0f);   // tapa el color del menú...
        AddMaterialFrame(Color::CRED, 0.5f);      // ...y rojo a la mitad encima = rosa
    }

    void OwoFace(){
        // OwO furry: ojos en anillo (O) y boca ω. OwoFix corrige la re-alineación
        // cuando ojo y boca van juntos (cada uno solo ya es exacto). Color del menú.
        DisableBlinking();
        muteVisemes = true;
        AddParameterFrame(NukudeFace::OwoEye, 1.0f);
        AddParameterFrame(NukudeFace::OwoMouth, 1.0f);
        AddParameterFrame(NukudeFace::OwoFix, 1.0f);
    }

    void HappyFace(){
        // ^///^ feliz: ojitos cerrados en arco, sonrisa abierta en media luna y
        // sonrojo "///" (HappyMouth también acomoda las chapitas). Color del menú.
        DisableBlinking();
        muteVisemes = true;
        AddParameterFrame(NukudeFace::HappyEyes, 1.0f);
        AddParameterFrame(NukudeFace::HappyMouth, 1.0f);
        AddParameterFrame(NukudeFace::HideBlush, 0.0f);
    }

    void SpectrumAnalyzerCallback() override {
        AddMaterialFrame(Color::CHORIZONTALRAINBOW, 0.8f);
    }

    void AudioReactiveGradientCallback() override {
        AddMaterialFrame(Color::CHORIZONTALRAINBOW, 0.8f);
    }

    void OscilloscopeCallback() override {
        AddMaterialFrame(Color::CHORIZONTALRAINBOW, 0.8f);
    }

    // Comandos por serial desde el Pi, UNO POR LÍNEA (con Enter) y la línea
    // tiene que ser exactamente el comando. Ojo: una terminal con eco prendido
    // nos regresa todo lo que imprimimos ("FPS: .. Animated in ..", capturas en
    // hex); con letras sueltas eso se volvía n/d/t/B y la cabeza se comandaba
    // sola. Línea exacta = el eco nunca coincide.
    //   p/q osciloscopio del boop · s estado · n siguiente cara
    //   d captura de la cámara (64x32) · v captura de lo que va a los paneles (vista de frente, 128x32)
    //   B repetir el arranque · t patrón L/R pa calibrar
    //   f<N> cara N · b<N> brillo N (0-9) · k<N> calibración de paneles (0-3)
    //   o0 apagar pantalla · o1 prenderla · a1/a0 osciloscopio de audio
    //   O captura de la pantallita OLED (128x64) · w<N> fingir N °C pa probar el foco de
    //   temperatura de la OLED (w0 = sensor real)
    //   g<N> dedo de mentira pal gesto de boop (sólo lo ve el gesto, SelectFace sigue con el
    //   sensor real): 1 doble, 2 sencillo, 3 doble_tap, 4 suelta, 5 abrazo, 6 seguido, 0 parar.
    //   Medio segundo sin boop, el patrón de tools/oledsim/boop_presets.json y se acaba solo.
    //   ERR con el sensor de boop apagado en el menú (o en el arranque).
    //   l<N> idioma de la pantallita, al momento: 0 español, 1 inglés, 2 chino simplificado.
    //   Mientras nunca se haya mandado, el del build (-D IDIOMA_DEFECTO=<N>; sin él, 0);
    //   l255 lo olvida (EEPROM 201 = 0xFF, como virgen) y regresa al del build
    //   f, b, k y l se guardan en EEPROM (k en la 200, l en la 201); o, w y g no (un reinicio
    //   las regresa).
    void ReadSerialCommands() {
        while (Serial.available() > 0) {
            int c = Serial.read();
            if (c == '\n' || c == '\r') {
                if (cmdLen > 0 && cmdLen < sizeof(cmdBuf)) RunCommand();
                cmdLen = 0;
            } else if (cmdLen < sizeof(cmdBuf)) {
                cmdBuf[cmdLen++] = (char)c;  // si se llena, la línea ya es inválida
            }
        }
    }

    void RunCommand() {
        cmdBuf[cmdLen] = 0;
        if (cmdLen == 1) {
            switch (cmdBuf[0]) {
                case 'p': boopScope = true;  break;
                case 'q': boopScope = false; break;
                case 's': PrintStatus();     break;
                case 'n': Menu::NextFace();  break;
                case 'd': DumpFrame();       break;
                case 'v': DumpFront();       break;
                case 'O': DumpOled();        break;
                case 'B': bootDone = false; bootStartMs = 0; break;  // repetir el arranque
                case 't': calTest = !calTest; break;
                default: break;
            }
            return;
        }
        char c = cmdBuf[0];
        if (c != 'f' && c != 'b' && c != 'k' && c != 'o' && c != 'a' && c != 'w' && c != 'g' && c != 'l') return;
        for (uint8_t i = 1; i < cmdLen; i++)
            if (cmdBuf[i] < '0' || cmdBuf[i] > '9') return;

        int v = atoi(cmdBuf + 1);
        bool ok = v <= 255;
        if (ok && cmdBuf[0] == 'f') ok = Menu::SetMenuValue(Menu::Faces, (uint8_t)v);
        else if (ok && cmdBuf[0] == 'b') ok = Menu::SetMenuValue(Menu::Bright, (uint8_t)v);
        else if (ok && cmdBuf[0] == 'k') {
            ok = v <= 3;
            if (ok) {
                controller.SetPanelCalibration((uint8_t)v);
                if (EEPROM.read(EEPROM_PANEL_CAL) != v) EEPROM.write(EEPROM_PANEL_CAL, (uint8_t)v);
            }
        }
        else if (ok && cmdBuf[0] == 'o') {
            ok = v <= 1;
            if (ok) screenOff = (v == 0);
        }
        else if (ok && cmdBuf[0] == 'a') {
            ok = v <= 1;
            if (ok) audioScope = (v == 1);
        }
        else if (ok && cmdBuf[0] == 'w') {
            ok = v <= 150;
            if (ok) hud.SetTempOverride((float)v);  // 0 = sensor real otra vez
        }
        else if (ok && cmdBuf[0] == 'l') {
            ok = v < HeadsUpDisplay::LANGUAGES || v == 255;  // 255 = olvidar: el del build
            if (ok) {
                hud.SetLanguage((uint8_t)v);  // 255 → IDIOMA_DEFECTO
                if (EEPROM.read(EEPROM_IDIOMA) != v) EEPROM.write(EEPROM_IDIOMA, (uint8_t)v);
            }
        }
        else if (ok && cmdBuf[0] == 'g') {
            // con el sensor apagado (o en el arranque) el gesto está en Reset: no habría qué ver
            ok = Menu::UseBoopSensor() && bootDone && v <= BoopPresets::COUNT;
            if (ok && v == 0) boopScript.Stop();
            else if (ok) boopScript.Start((uint8_t)v, millis());
        }
        Serial.print(ok ? "OK " : "ERR ");
        Serial.println(cmdBuf);
    }

    void PrintStatus() {
        uint8_t f = Menu::GetFaceState();
        Serial.print("S face=");
        Serial.print(f);
        if (f < sizeof(faceArray) / sizeof(faceArray[0])) {
            Serial.print(' ');
            Serial.print(faceArray[f]);
        }
        Serial.print(" bright=");
        Serial.print(Menu::GetBrightness());
        Serial.print(" fan=");
        Serial.print(Menu::GetFanSpeed());
        Serial.print(" pwm=");
        Serial.print(fanController.GetPWM());
        Serial.print(" boop=");
        Serial.print(Menu::UseBoopSensor());
        Serial.print(" mic=");
        Serial.print(Menu::UseMicrophone());
        Serial.print(" color=");
        Serial.print(Menu::GetFaceColor());
        Serial.print(" i2cerr=");
        Serial.print(APDS9960::GetI2CErrors());
        Serial.print(" cal=");
        Serial.print(controller.GetPanelCalibration());
        Serial.print(" temp=");
        Serial.print(tempmonGetTemp(), 1);  // temperatura del chip del Teensy, °C
        Serial.print(" w=");
        Serial.print((int)hud.GetTempOverride());  // temperatura fingida pa la OLED (0 = real)
        Serial.print(" off=");
        Serial.print(screenOff);
        Serial.print(" lang=");
        Serial.print(hud.GetLanguage());  // idioma de la OLED: 0 es, 1 en, 2 zh
        Serial.print(" t=");
        Serial.println(millis());
    }

    // Espectro que ven los visemes (GetFourierFiltered, ~0..1) + la magnitud que
    // usa la compuerta de voz. Sólo pa grabar sesiones y calibrar en la laptop.
    void DumpAudio() {
        static const char hexd[] = "0123456789ABCDEF";
        static const uint8_t bins = 128;  // MicrophoneFourierBase::OutputBins (protegido)
        char line[bins * 3 + 1];
        float* f = MicrophoneFourier::GetFourierFiltered();
        for (uint8_t i = 0; i < bins; i++) {
            int q = (int)lroundf((f[i] + 1.0f) * 1000.0f);
            q = q < 0 ? 0 : (q > 4095 ? 4095 : q);
            line[i * 3]     = hexd[(q >> 8) & 0xF];
            line[i * 3 + 1] = hexd[(q >> 4) & 0xF];
            line[i * 3 + 2] = hexd[q & 0xF];
        }
        line[bins * 3] = 0;
        Serial.print("A ");
        Serial.print(millis());
        Serial.print(' ');
        Serial.print((int)lroundf(MicrophoneFourier::GetCurrentMagnitude() * 1000.0f));
        Serial.print(' ');
        Serial.print((int)lroundf(tempmonGetTemp() * 10.0f));
        Serial.print(' ');
        Serial.println(line);
    }

    // Captura del último frame renderizado, como lo ve el panel "normal" (el
    // otro lo espejea en X): renglón 0 = arriba, hex RRGGBB por pixel.
    void DumpFrame() {
        static const char hexd[] = "0123456789ABCDEF";
        char line[64 * 6 + 1];
        IPixelGroup* px = cameras.GetCameras()[0]->GetPixelGroup();

        Serial.print("DUMP face=");
        Serial.print(Menu::GetFaceState());
        Serial.print(" t=");
        Serial.println(millis());
        for (uint16_t row = 0; row < 32; row++) {
            uint16_t y = 31 - row;  // la cámara cuenta y hacia arriba (ver HUB75Controller::Display)
            for (uint16_t x = 0; x < 64; x++) {
                RGBColor* c = px->GetColor(y * 64 + x);
                uint8_t v[3] = { c->R, c->G, c->B };
                for (uint8_t k = 0; k < 3; k++) {
                    line[x * 6 + k * 2]     = hexd[v[k] >> 4];
                    line[x * 6 + k * 2 + 1] = hexd[v[k] & 15];
                }
            }
            line[64 * 6] = 0;
            Serial.print('D');
            Serial.print(row < 10 ? "0" : "");
            Serial.print(row);
            Serial.print(' ');
            Serial.println(line);
        }
        Serial.println("DUMP END");
    }

    // Lo que realmente se mandó a los paneles en el último frame, visto de
    // frente: panel izquierdo y derecho lado a lado, 128 pixeles por renglón.
    void DumpFront() {
        static const char hexd[] = "0123456789ABCDEF";
        char line[128 * 6 + 1];

        Serial.print("VIEW face=");
        Serial.print(Menu::GetFaceState());
        Serial.print(" cal=");
        Serial.print(controller.GetPanelCalibration());
        Serial.print(" t=");
        Serial.println(millis());
        for (uint8_t y = 0; y < 32; y++) {
            for (uint8_t panel = 0; panel < 2; panel++) {
                for (uint8_t x = 0; x < 64; x++) {
                    ScreenRGB c = controller.GetFrontPixel(panel, x, y);
                    uint8_t v[3] = { c.r, c.g, c.b };
                    int o = (panel * 64 + x) * 6;
                    for (uint8_t k = 0; k < 3; k++) {
                        line[o + k * 2]     = hexd[v[k] >> 4];
                        line[o + k * 2 + 1] = hexd[v[k] & 15];
                    }
                }
            }
            line[128 * 6] = 0;
            Serial.print('V');
            Serial.print(y < 10 ? "0" : "");
            Serial.print(y);
            Serial.print(' ');
            Serial.println(line);
        }
        Serial.println("VIEW END");
    }

    // La pantallita OLED de adentro tal cual está en su buffer (128x64): un renglón "Lnn" por
    // página de 8 pixeles de alto, 128 bytes en hex (bit 0 = el renglón de arriba de la
    // página). inv=1 sería polaridad invertida (bit 0 = prendido); el diseño actual usa 0.
    void DumpOled() {
        static const char hexd[] = "0123456789ABCDEF";
        const uint8_t* buf = hud.GetBuffer();
        if (!buf) {  // la OLED no contestó al arrancar
            Serial.println("OLED ERR");
            return;
        }
        char line[128 * 2 + 1];

        Serial.print("OLED inv=");
        Serial.print(hud.IsInverted() ? 1 : 0);
        Serial.print(" contrast=");
        Serial.print(hud.GetContrast());
        Serial.print(" t=");
        Serial.println(millis());
        for (uint8_t page = 0; page < 8; page++) {
            for (uint8_t x = 0; x < 128; x++) {
                uint8_t b = buf[page * 128 + x];
                line[x * 2]     = hexd[b >> 4];
                line[x * 2 + 1] = hexd[b & 15];
            }
            line[128 * 2] = 0;
            Serial.print("L0");
            Serial.print(page);
            Serial.print(' ');
            Serial.println(line);
        }
        Serial.println("OLED END");
    }

public:
    // micPin=22, buttonPin=23, faceCount = las que traiga faceArray
    ProtogenHUB75Project() : ProtogenProject(&cameras, &controller, 2, Vector2D(), Vector2D(192.0f, 94.0f), 22, 23, sizeof(faceArray) / sizeof(faceArray[0])){
        scene.AddObject(pM.GetObject());
        scene.AddObject(deltaDisplayBackground.GetObject());
        scene.AddObject(bsodPlane.GetObject());

        pM.GetObject()->SetMaterial(GetFaceMaterial());
        deltaDisplayBackground.GetObject()->SetMaterial(GetFaceMaterial());
        // BSOD es BsodScreen (pantallazo dibujado aparte, sin imagen). El plano arranca
        // oculto y cada cara que lo prende le pone su material.
        bsodPlane.GetObject()->SetMaterial(&lowBatImage);
        bsodPlane.GetObject()->Disable();  // hidden by default, enabled only when BSOD face active

        hud.SetFaceArray(faceArray, sizeof(faceArray) / sizeof(faceArray[0]));

        LinkControlParameters();

        SetWiggleSpeed(5.0f);
        SetMenuWiggleSpeed(0.0f, 0.0f, 0.0f);
        SetMenuOffset(Vector2D(17.5f, -3.0f));
        SetMenuSize(Vector2D(192, 56));
    }

    void Update(float ratio) override {
        pM.Reset();

        // Reset visibility defaults: show face+background, hide BSOD plane,
        // re-enable mirror. Face methods will override these if active.
        pM.GetObject()->Enable();
        deltaDisplayBackground.GetObject()->Enable();
        bsodPlane.GetObject()->Disable();
        controller.SetMirrorEnabled(true);
        controller.SetScreenSource(nullptr);  // las pantallas directas se piden cada frame

        if (!calLoaded) {
            calLoaded = true;
            uint8_t cal = EEPROM.read(EEPROM_PANEL_CAL);
            controller.SetPanelCalibration(cal <= 3 ? cal : 0);  // EEPROM virgen = 0xFF → default
            hud.SetLanguage(EEPROM.read(EEPROM_IDIOMA));          // 0xFF o fuera de rango → IDIOMA_DEFECTO
        }

        // Arranque: BootScreen directo a los paneles los primeros ~4 s. No hace
        // early return — deja que el resto del Update corra normal pa mantener
        // consistente el state de pM/transforms (early return = bootloop).
        bool bootActive = false;
        if (!bootDone) {
            if (bootStartMs == 0) bootStartMs = millis();
            uint32_t elapsed = millis() - bootStartMs;
            if (elapsed >= BootScreen::TOTAL_MS) {
                bootDone = true;
                // Sale ya con el puerto abierto (el "Starting..." de main se pierde
                // al re-enumerar): si después de una caída del USB aparece esto,
                // fue reinicio del Teensy y no nomás el cable.
                Serial.print("[boot] listo t=");
                Serial.println(millis());
            } else {
                bootActive = true;
                bootScreen.Render(elapsed);
                controller.SetScreenSource(&bootScreen);
            }
        }

        bsodWasActive = bsodActive;  // BsodFace lo vuelve a prender si sigue en el slot
        bsodActive = false;
        deadWasActive = deadActive;  // ídem DeadFace (retraso de la lengua)
        deadActive = false;

        // Boop + boooop = siguiente cara: boop corto, suelta, y el 2o boop se
        // SOSTIENE ~1 s (dispara a los 800 ms sin esperar a que suelte, una sola
        // vez). Re-arma tras soltar + 400 ms quieto, o 2.5 s si el contacto fue
        // largo (>1.6 s después del cambio, o >1.3 s sin gesto: abrazo, mano
        // recargada): el MinFilter del APDS ya pudo "absorber" la mano y entonces
        // IsBooped() cae con la mano todavía puesta. El doble tap a secas ya no
        // hace nada: era lo que la cambiaba solita. Boop sencillo sigue siendo
        // Surprised vía SelectFace. En boot o con el sensor apagado en el menú
        // (IsBooped() se congela) se resetea y sólo arma tras silencio real.
        // Tiempos y porqués en BoopGesture.h.
        // (El selector por swipes se intentó y se descartó: los diodos U/D del
        //  APDS no distinguen arriba/abajo en este mount — ver LEARNINGS.md.)
        uint32_t boopNow = millis();

        ReadSerialCommands();
        if (boopScope) {
            // misma muestra que ve el detector (la del UpdateFace anterior), sin I2C extra
            Serial.print("P ");
            Serial.print(boopNow);
            Serial.print(' ');
            Serial.print(APDS9960::GetLastProximity());
            Serial.print(' ');
            Serial.print(APDS9960::GetBaseline(), 1);
            Serial.print(' ');
            Serial.print(IsBooped() ? 1 : 0);
            Serial.print(' ');
            Serial.println(APDS9960::GetI2CErrors());
        }
        if (audioScope) DumpAudio();

        // La OLED de adentro enseña el gesto en el renglón del nombre (manita + puntitos, y la
        // barrita con palomita cuando dispara): SetBoopGesture cada frame, antes de hud.Update().
        if (bootActive || !Menu::UseBoopSensor()) {
            boopScript.Stop();
            boopGesture.Reset(boopNow);
            hud.SetBoopGesture(BoopGesture::STAGE_WAIT, 0, BoopGesture::NONE);
        } else {
            // g<N>: el gesto ve el dedo de mentira; SelectFace sigue con el sensor de verdad
            bool boopIn = boopScript.Active() ? boopScript.Sample(boopNow) : IsBooped();
            BoopGesture::Event boopEv = boopGesture.Update(boopIn, boopNow);
            uint32_t heldMs = boopGesture.HoldMs(boopNow);
            hud.SetBoopGesture(boopGesture.GetStage(), heldMs > 65535 ? 65535 : heldMs, boopEv);
            if (boopEv == BoopGesture::FIRE) Menu::NextFace();
            if (BoopGesture::Loggable(boopEv)) {
                Serial.print("[boop] ");
                Serial.print(BoopGesture::Name(boopEv));
                Serial.print(' ');
                Serial.print(boopGesture.DurMs());
                Serial.print("ms");
                if (boopGesture.DropMs()) {
                    Serial.print(" bajones=");
                    Serial.print(boopGesture.DropMs());
                }
                if (boopEv == BoopGesture::IGNORED || boopEv == BoopGesture::IGNORED_LOCK) {
                    Serial.print(" tras ");
                    Serial.print(BoopGesture::Name(boopGesture.Cause()));
                }
                Serial.print(" t=");
                Serial.println(boopNow);
            }
        }

        uint8_t mode = Menu::GetFaceState();//change by button press

        controller.SetBrightness(Menu::GetBrightness());
        controller.SetAccentBrightness(Menu::GetAccentBrightness());

        // Parpadeo y visemes prendidos por default; DEAD/AMOR/OWO/HAPPY los apagan en
        // cada frame. Re-armarlo aquí es inocuo pa las demás caras: Reset() sólo
        // escribe Blink=0 (pM.Reset() ya lo hizo) y el BlinkTrack sigue por millis().
        EnableBlinking();
        muteVisemes = false;

#ifdef MORSEBUTTON
        if (!bootActive) SelectFaceFromMorse(mode);
#else
        if (!bootActive) SelectFace(mode);
#endif

        UpdateFace(ratio);

        // UpdateFFTVisemes() corre dentro de UpdateFace, después de la cara: aquí se
        // callan pa las caras con boca propia (si no, el micro deforma la boca).
        if (muteVisemes) {
            pM.SetMorphWeight(NukudeFace::vrc_v_ee, 0.0f);
            pM.SetMorphWeight(NukudeFace::vrc_v_aa, 0.0f);
            pM.SetMorphWeight(NukudeFace::vrc_v_dd, 0.0f);
            pM.SetMorphWeight(NukudeFace::vrc_v_rr, 0.0f);
            pM.SetMorphWeight(NukudeFace::vrc_v_ch, 0.0f);
            pM.SetMorphWeight(NukudeFace::vrc_v_oh, 0.0f);
            pM.SetMorphWeight(NukudeFace::vrc_v_ss, 0.0f);
        }

        pM.Update();

        AlignObjectFace(pM.GetObject(), -7.5f);

        pM.GetObject()->GetTransform()->SetPosition(GetWiggleOffset());
        pM.GetObject()->UpdateTransform();

        if (calTest) controller.SetScreenSource(&calScreen);  // el patrón L/R manda sobre todo
        if (screenOff) controller.SetScreenSource(&blankScreen);  // ...menos sobre apagada
    }

    void SelectFace(uint8_t code) {
        // Booping overrides most faces with Surprised, except audio modes y
        // todos los slots especiales (6+) que toman control del bsodPlane.
        // Con el sensor apagado en el menú isBooped se congela en su último valor:
        // si era true, la cara se quedaba pegada en Surprised.
        bool booped = Menu::UseBoopSensor() && IsBooped();
        if (booped && code < 6) {
            Surprised();
            return;
        }
        // Las caritas de morphs nuevas (DEAD/AMOR/OWO/HAPPY) reaccionan al boop con
        // TACHA (ojitos arcoíris). Al soltar regresan a la suya.
        if (booped && code >= 13 && code <= 16) {
            TachaFace();
            return;
        }

        switch(code) {
            case 0: Default();  break;
            case 1: Angry();    break;
            case 2: Doubt();    break;
            case 3: Frown();    break;
            case 4: LookUp();   break;
            case 5: Sad();      break;
            case 6: BsodFace();         break;  // Windows 10 blue screen
            case 7: LowBatteryFace();   break;  // blinking low-battery icon
            case 8: AudioReactiveGradientFace();    break;
            case 9: SpectrumAnalyzerFace();         break;
            case 10: TachaFace();       break;  // ojitos rainbow seleccionable
            case 11: KaomojiFace();     break;  // kaomojis estroboscópicos
            case 12: KaoBpmFace(kaoPink140Anim);  break;  // kaomojis pink fosfo 140 BPM
            case 13: DeadFace();        break;  // x.x muerto + lengua (morphs)
            case 14: AmorFace();        break;  // ♡ω♡ corazones que laten (morphs)
            case 15: OwoFace();         break;  // OwO anillos + boca ω (morphs)
            case 16: HappyFace();       break;  // ^///^ ojitos cerrados + sonrisa (morphs)
            default: Default();         break;
        }
    }

    void SelectFaceFromMorse(uint8_t code) {
        if (IsBooped() && code != 24) {
            Surprised();
            return;
        }

        switch(code) {
            case 1: Angry();        break; // [A]ngry
            case 2: Surprised();    break; // [B]lush
            case 4: Doubt();        break; // [D]oubt
            case 6: Frown();        break; // [F]rown
            case 19: Sad();         break; // [S]ad
            case 21: LookUp();      break; // Look [U]p
            case 22: LookDown();    break; // Look [V] Down
            case 24: AudioReactiveGradientFace();   break; // [X] X.X
            case 25: OscilloscopeFace();            break; // [Y] Oscilloscope
            case 26: SpectrumAnalyzerFace();        break; // [Z] Spectrum
            default: Default();     break; // [H] Happy
        }
    }
};
