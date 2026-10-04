// fake_teensy: la cabeza "de mentira" en un pty, pa probar tools/captura.py sin hardware.
//
//   fake_teensy <sim_dir> [--face N] [--boop-off] [--temp C] [--secs S] [--eeprom ARCHIVO]
//
// Imprime la ruta del pty (pa --puerto) y luego corre en TIEMPO REAL el loop del proyecto:
// el HUD real (SSD1306.h/.cpp contra el Adafruit_SSD1306 real + el controlador emulado del
// simulador), el BoopGesture.h y BoopScript.h reales, y, COPIADOS TAL CUAL de
// ProtogenHUB75Project.h por build.sh (fake_extract.inc): ReadSerialCommands(), RunCommand(),
// DumpOled(), la lectura de la EEPROM del primer Update() y el bloque del gesto de Update().
// Lo demás (estado 's', paneles, EEPROM) son stubs con el mismo formato. La EEPROM arranca
// virgen (0xFF) como la real; con --eeprom vive en ese archivo y sobrevive un "reinicio" (otro
// fake_teensy con el mismo archivo). Nadie toca la nariz: IsBooped() = false, así que lo único
// que mueve el gesto es g<N>. La carita usa las máscaras capturadas del simulador.
#include <Arduino.h>
#include <fcntl.h>
#include <stdlib.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <new>
#include <string>

#include "SSD1306.h"
#include "Camera/Pixels/PixelGroup.h"
#include "Examples/Protogen/BoopGesture.h"
#include "Examples/Protogen/BoopScript.h"
#include "Assets/Screens/BootScreen.h"
#include "oledsim_rt.h"

// el faceArray del firmware, mismo orden que FACES en tools/oledsim/faces.py
static const __FlashStringHelper *faceArray[] = {
    F("DEFAULT"), F("ANGRY"), F("DOUBT"), F("FROWN"), F("LOOKUP"), F("SAD"), F("BSOD"),
    F("LOWBAT"), F("AUDIO1"), F("AUDIO2"), F("TACHA"), F("KAOMOJI"),
    F("KP140"),
    F("DEAD"), F("AMOR"), F("OWO"), F("HAPPY")};
static const uint8_t FAKE_FACES = sizeof(faceArray) / sizeof(faceArray[0]);

// Serial del Teensy sobre el lado maestro del pty (mismo Print que el core: println = "\r\n").
class PtySerial : public Print {
public:
    int fd = -1;
    size_t write(uint8_t b) override { return write(&b, 1); }
    size_t write(const uint8_t *buf, size_t n) override {
        size_t done = 0;
        for (int tries = 0; done < n && tries < 200; tries++) {
            ssize_t w = ::write(fd, buf + done, n - done);
            if (w > 0) done += (size_t)w;
            else usleep(1000);  // pty lleno: que lo lea captura.py
        }
        return done;
    }
    using Print::write;
    int available() {
        if (pos < len) return len - pos;
        ssize_t r = ::read(fd, in, sizeof(in));
        pos = 0;
        len = r > 0 ? (int)r : 0;
        return len;
    }
    int read() { return available() ? (uint8_t)in[pos++] : -1; }

private:
    char in[256];
    int pos = 0, len = 0;
};

struct FakeProject {
    HeadsUpDisplay &hud;
    PtySerial Serial;   // tapa al Serial global dentro de los métodos copiados
    BoopGesture boopGesture;
    BoopScript boopScript;
    bool boopScope = false, audioScope = false, calTest = false, screenOff = false;
    bool bootDone = false, calLoaded = false;
    uint32_t bootStartMs = 0;
    char cmdBuf[FAKE_CMDBUF];
    uint8_t cmdLen = 0;
    static const int EEPROM_PANEL_CAL = 200;
    static const int EEPROM_IDIOMA = 201;
    struct {
        uint8_t cal = 0;
        void SetPanelCalibration(uint8_t v) { cal = v; }
        uint8_t GetPanelCalibration() const { return cal; }
    } controller;
    struct {
        uint8_t m[256];
        const char *file = nullptr;  // --eeprom
        void load(const char *path) {
            memset(m, 0xFF, sizeof(m));  // virgen, como la del Teensy
            file = path;
            FILE *f = file ? fopen(file, "rb") : nullptr;
            if (f) {
                if (fread(m, 1, sizeof(m), f) != sizeof(m)) memset(m, 0xFF, sizeof(m));
                fclose(f);
            }
        }
        uint8_t read(int a) const { return m[a & 255]; }
        void write(int a, uint8_t v) {
            m[a & 255] = v;
            FILE *f = file ? fopen(file, "wb") : nullptr;
            if (f) {
                fwrite(m, 1, sizeof(m), f);
                fclose(f);
            }
        }
    } EEPROM;

    explicit FakeProject(HeadsUpDisplay &h) : hud(h) {}

    bool IsBooped() { return false; }  // nadie en la nariz
    void DumpFrame() {}
    void DumpFront() {}
    void PrintStatus() {  // mismo formato que el firmware, con valores de mentira donde no hay hardware
        uint8_t f = Menu::GetFaceState();
        Serial.print("S face=");
        Serial.print(f);
        if (f < FAKE_FACES) {
            Serial.print(' ');
            Serial.print(faceArray[f]);
        }
        Serial.print(" bright=");
        Serial.print(Menu::GetBrightness());
        Serial.print(" fan=9 pwm=255 boop=");
        Serial.print(Menu::UseBoopSensor());
        Serial.print(" mic=0 color=2 i2cerr=0 cal=");
        Serial.print(controller.GetPanelCalibration());
        Serial.print(" temp=");
        Serial.print(tempmonGetTemp(), 1);
        Serial.print(" w=");
        Serial.print((int)hud.GetTempOverride());
        Serial.print(" off=");
        Serial.print(screenOff);
        Serial.print(" lang=");
        Serial.print(hud.GetLanguage());
        Serial.print(" t=");
        Serial.println(millis());
    }

#include "fake_extract.inc"
};

static PixelGroup<2048> camPixels(Vector2D(192.0f, 96.0f), Vector2D(0.0f, 0.0f), 64);
static std::string simDir;

// Las mismas máscaras que usa el simulador al disparar (render.py FIRE_MASKS).
// máscara de LEDs de cada cara por NOMBRE (las mismas de faces.py): capturas de la cabeza en
// masks/ y renders de referencia en scenarios/; las de imagen/video van sin máscara
static const char *const maskFiles[][2] = {
    {"DEFAULT", "/masks/cara00_0.txt"}, {"DEAD", "/masks/cara16_0.txt"}, {"AMOR", "/masks/cara17_0.txt"},
    {"OWO", "/masks/cara18_0.txt"}, {"HAPPY", "/masks/cara19_0.txt"},
    {"ANGRY", "/scenarios/ref_cara01.txt"}, {"DOUBT", "/scenarios/ref_cara02.txt"},
    {"FROWN", "/scenarios/ref_cara03.txt"}, {"LOOKUP", "/scenarios/ref_cara04.txt"},
    {"SAD", "/scenarios/ref_cara05.txt"}, {"TACHA", "/scenarios/ref_cara10.txt"}};

static std::string maskFor(int face) {
    if (face < 0 || face >= FAKE_FACES) return "";
    const char *name = reinterpret_cast<const char *>(faceArray[face]);
    for (const auto &m : maskFiles)
        if (strcmp(m[0], name) == 0) return simDir + m[1];
    return "";
}

static void loadMask(int face) {
    for (uint16_t i = 0; i < 2048; i++) *camPixels.GetColor(i) = RGBColor(0, 0, 0);
    std::string path = maskFor(face);
    if (path.empty()) return;
    FILE *f = fopen(path.c_str(), "r");
    if (!f) return;
    char line[1024];
    int r = 0;
    while (r < 32 && fgets(line, sizeof(line), f)) {
        if (strlen(line) < 64 * 6) continue;
        for (int x = 0; x < 64; x++) {
            unsigned v[3];
            for (int k = 0; k < 3; k++) {
                char h[3] = {line[x * 6 + k * 2], line[x * 6 + k * 2 + 1], 0};
                v[k] = strtoul(h, nullptr, 16);
            }
            *camPixels.GetColor((31 - r) * 64 + x) = RGBColor(v[0], v[1], v[2]);  // DumpFrame: renglón r = fila 31-r
        }
        r++;
    }
    fclose(f);
}

static uint64_t wallUs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
}

alignas(HeadsUpDisplay) static unsigned char hudStorage[sizeof(HeadsUpDisplay)];

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "uso: %s <sim_dir> [--face N] [--boop-off] [--temp C] [--secs S] [--eeprom ARCHIVO]\n", argv[0]);
        return 2;
    }
    simDir = argv[1];
    int face = 0;
    double secs = 120;
    bool boopOff = false;
    const char *eepromFile = nullptr;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--face" && i + 1 < argc) face = atoi(argv[++i]);
        else if (a == "--boop-off") boopOff = true;
        else if (a == "--temp" && i + 1 < argc) oledsim_in.temp = atof(argv[++i]);
        else if (a == "--secs" && i + 1 < argc) secs = atof(argv[++i]);
        else if (a == "--eeprom" && i + 1 < argc) eepromFile = argv[++i];
    }

    int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || grantpt(master) || unlockpt(master)) { perror("pty"); return 1; }
    const char *slaveName = ptsname(master);
    int slave = open(slaveName, O_RDWR | O_NOCTTY);  // abierto todo el rato: el pty no cuelga entre capturas
    termios t;
    tcgetattr(slave, &t);
    cfmakeraw(&t);
    tcsetattr(slave, TCSANOW, &t);
    fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);
    printf("%s\n", slaveName);
    fflush(stdout);

    Menu::SimSetFaceCount(FAKE_FACES);
    Menu::SetFaceState(face);
    Menu::SetMenuValue(Menu::BoopSensor, boopOff ? 0 : 1);
    loadMask(face);
    int maskFace = face;

    HeadsUpDisplay *hud = new (hudStorage) HeadsUpDisplay(Vector2D(0.0f, 0.0f), Vector2D(192.0f, 96.0f));
    hud->SetFaceArray(faceArray, FAKE_FACES);
    hud->Initialize();
    FakeProject p(*hud);
    p.Serial.fd = master;
    p.EEPROM.load(eepromFile);

    TimeStep frameLimiter(120);
    uint64_t wall0 = wallUs();
    while (oledsim_now_us < (uint64_t)(secs * 1e6)) {
        // ---- ProtogenHUB75Project::Update()
        p.LoadEeprom();
        bool bootActive = false;
        if (!p.bootDone) {
            if (p.bootStartMs == 0) p.bootStartMs = millis();
            uint32_t elapsed = millis() - p.bootStartMs;
            if (elapsed >= BootScreen::TOTAL_MS) {
                p.bootDone = true;
                p.Serial.print("[boot] listo t=");
                p.Serial.println(millis());
            } else {
                bootActive = true;
            }
        }
        uint32_t boopNow = millis();
        p.ReadSerialCommands();
        p.GestureBlock(bootActive, boopNow);
        if (Menu::GetFaceState() != maskFace) {  // FIRE o f<N>: SelectFace pinta la cara nueva
            maskFace = Menu::GetFaceState();
            loadMask(maskFace);
        }

        // ---- ProtogenProject::UpdateFace()
        while (!frameLimiter.IsReady()) delay(1);
        hud->SetEffect(Menu::GetEffect());
        hud->Update();
        hud->SetFaceMax(Vector2D(192.0f, 94.0f));
        hud->SetFaceMin(Vector2D(0.0f, 0.0f));
        hud->ApplyEffect(&camPixels);
        oledsim_now_us += 12000;  // ~83 FPS como la cabeza

        uint64_t w = wallUs() - wall0;  // reloj virtual = reloj de pared
        if (oledsim_now_us > w) usleep((useconds_t)(oledsim_now_us - w));
    }
    return 0;
}
