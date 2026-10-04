// OLED simulator driver: runs a design's HeadsUpDisplay (SSD1306.h/.cpp,
// compiled unmodified) the way ProtogenProject drives it, against the real
// Adafruit_GFX + Adafruit_SSD1306 and an emulated SSD1306 on the I2C bus.
//
//   oledsim <plan.txt> <out_dir>
//
// The plan (written by render.py from a scenario JSON) lists states. Each state
// runs in its own fork()ed process from t=0 (fresh statics), so states never
// leak into each other. Per frame, exactly like ProtogenHUB75Project::Update()
// -> ProtogenProject::UpdateFace() -> Engine::Render():
//   boot flag (BootScreen::TOTAL_MS) -> boop gesture: Reset or
//   Update(last frame's isBooped) -> [SetBoopGesture hook] -> FIRE: NextFace()
//   -> frameLimiter (TimeStep 120 Hz) -> events + boop edges -> isBooped sample
//   -> hud.SetEffect(Menu::GetEffect()) -> [SetBooped/SetFPS hooks] -> hud.Update()
//   -> hud.SetFaceMax(192,94) -> hud.SetFaceMin(0,0)
//   -> hud.ApplyEffect(main camera PixelGroup<2048>) -> render cost (render_ms).
// The state's language ("set lang N") goes to hud.SetLanguage(N) right after the constructor (the
// head does it in its first loop, from EEPROM 201) and again whenever an event changes it.
// Output: <out_dir>/<name>.frames with the frame visible at "millis" plus every
// change during the following "record" ms (from "rec_from" if that is earlier),
// and <out_dir>/<name>.boop: gesture events/stages, loop times, panel traffic.
#include <Arduino.h>
#include <new>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>
#include <type_traits>

#include "SSD1306.h"   // the design under test (from the staged Displays/ dir)
#include "Camera/Pixels/PixelGroup.h"   // via -I <stage>/lib/ProtoTracer
#include "Examples/Protogen/BoopGesture.h"  // the REAL gesture detector (repo copy, Arduino-free)
#include "Assets/Screens/BootScreen.h"      // BootScreen::TOTAL_MS = the head's boot (gesture held in Reset)
#include "oledsim_rt.h"

// ProtogenHUB75Project.h faceArray, same order as FACES in faces.py
static const __FlashStringHelper *simFaceArray[] = {
    F("DEFAULT"), F("ANGRY"), F("DOUBT"), F("FROWN"), F("LOOKUP"), F("SAD"), F("BSOD"),
    F("LOWBAT"), F("AUDIO1"), F("AUDIO2"), F("TACHA"), F("KAOMOJI"),
    F("KP140"),
    F("DEAD"), F("AMOR"), F("OWO"), F("HAPPY")};
static const uint8_t SIM_FACES = sizeof(simFaceArray) / sizeof(simFaceArray[0]);

// ---- optional hooks: called only if the design declares them -----------------
template <class T, class = void> struct HasSetBooped : std::false_type {};
template <class T> struct HasSetBooped<T, std::void_t<decltype(std::declval<T &>().SetBooped(true))>> : std::true_type {};
template <class T, class = void> struct HasSetFPS : std::false_type {};
template <class T> struct HasSetFPS<T, std::void_t<decltype(std::declval<T &>().SetFPS(1.0f))>> : std::true_type {};

template <class T, class = void> struct HasSetBoopGesture : std::false_type {};
template <class T> struct HasSetBoopGesture<T, std::void_t<decltype(std::declval<T &>().SetBoopGesture((uint8_t)0, (uint16_t)0, (uint8_t)0))>> : std::true_type {};

template <class T, class = void> struct HasSetLanguage : std::false_type {};
template <class T> struct HasSetLanguage<T, std::void_t<decltype(std::declval<T &>().SetLanguage((uint8_t)0))>> : std::true_type {};

// The project passes the face count (designs without the count parameter bound it themselves).
template <class T, class = void> struct HasFaceCount : std::false_type {};
template <class T> struct HasFaceCount<T, std::void_t<decltype(std::declval<T &>().SetFaceArray(simFaceArray, (uint8_t)0))>> : std::true_type {};
template <class T> static void setFaceArray(T *h) {
    if constexpr (HasFaceCount<T>::value) h->SetFaceArray(simFaceArray, SIM_FACES);
    else h->SetFaceArray(simFaceArray);
}

template <class T> static void callHooks(T *h) {
    if constexpr (HasSetBooped<T>::value) h->SetBooped(oledsim_in.booped);
    if constexpr (HasSetFPS<T>::value) h->SetFPS(oledsim_in.fps);
}

// "lang": hud.SetLanguage(N) (ProtogenHUB75Project: EEPROM 201 at boot, serial l<N>). A design
// without SetLanguage keeps its own words.
static int pendingLang = -1;
template <class T> static void applyLang(T *h) {
    if (pendingLang < 0) return;
    if constexpr (HasSetLanguage<T>::value) h->SetLanguage((uint8_t)pendingLang);
    else fprintf(stderr, "oledsim: the design has no SetLanguage(), \"lang\" %d ignored\n", pendingLang);
    pendingLang = -1;
}

// ProtogenHUB75Project::Update(): hud.SetBoopGesture(stage, heldMs, event), every frame.
template <class T> static void callBoopHook(T *h, uint8_t stage, uint16_t holdMs, uint8_t event) {
    if constexpr (HasSetBoopGesture<T>::value) h->SetBoopGesture(stage, holdMs, event);
}

struct Set { std::string key; std::string a, b; };
struct Event { uint32_t t; std::vector<Set> sets; };
struct Edge { uint32_t t; bool v; };   // boop sensor: raw isBooped() becomes v at t (ms)
struct State {
    std::string name;
    uint32_t millis = 10000;
    uint32_t record = 0;
    int64_t recFrom = -1;      // capture starts here if earlier than millis (boop strips)
    int64_t renderUs = -1;     // per-state render cost (-1 = plan default)
    std::vector<Event> events; // events[0] is t=0
    std::vector<Edge> boop;    // sorted by t
};
struct Plan {
    uint32_t initMs = 0;
    float loopHz = 120.0f;
    uint32_t renderUs = 4000;
    std::string fireMask[256];  // mask per face index, loaded on FIRE when the state follows (empty = none)
    std::vector<State> states;
};

static std::string restOfLine(const char *line, int skipWords) {
    const char *p = line;
    for (int w = 0; w < skipWords; w++) {
        while (*p == ' ') p++;
        while (*p && *p != ' ') p++;
    }
    while (*p == ' ') p++;
    return p;
}

static bool readPlan(const char *path, Plan &plan) {
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return false; }
    char line[4096];
    State *st = nullptr;
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        char k[64] = {}, a[2048] = {}, b[2048] = {};
        int c = sscanf(line, "%63s %2047s %2047s", k, a, b);
        if (c < 1 || k[0] == '#') continue;
        std::string key = k;
        if (key == "init_ms") plan.initMs = atoi(a);
        else if (key == "loop_hz") plan.loopHz = atof(a);
        else if (key == "render_ms") plan.renderUs = (uint32_t)(atof(a) * 1000.0);
        else if (key == "firemask") { int f = atoi(a); if (f >= 0 && f < 256) plan.fireMask[f] = restOfLine(line, 2); }
        else if (key == "state") { plan.states.push_back(State()); st = &plan.states.back(); st->name = a; st->events.push_back(Event{0, {}}); }
        else if (!st) continue;
        else if (key == "millis") st->millis = atoi(a);
        else if (key == "record") st->record = atoi(a);
        else if (key == "rec_from") st->recFrom = atoi(a);
        else if (key == "state_render_ms") st->renderUs = (int64_t)(atof(a) * 1000.0);
        else if (key == "boop") st->boop.push_back(Edge{(uint32_t)atoi(a), atoi(b) != 0});
        else if (key == "event") st->events.push_back(Event{(uint32_t)atoi(a), {}});
        else if (key == "set") {
            // "set mask <path>" may contain spaces: take the rest of the line
            Set s; s.key = a;
            const char *rest = line + 3;
            while (*rest == ' ') rest++;
            rest += strlen(a);
            while (*rest == ' ') rest++;
            if (s.key == "mask") s.a = rest;
            else { char x[256] = {}, y[256] = {}; sscanf(rest, "%255s %255s", x, y); s.a = x; s.b = y; }
            st->events.back().sets.push_back(s);
        }
    }
    fclose(f);
    return true;
}

static PixelGroup<2048> *pixels;
static bool maskFollow = false;   // on FIRE, load the new face's mask (Plan::fireMask)

static bool loadMask(const std::string &path) {
    for (uint16_t i = 0; i < 2048; i++) *pixels->GetColor(i) = RGBColor(0, 0, 0);
    if (path.empty() || path == "none") return true;
    FILE *f = fopen(path.c_str(), "r");
    if (!f) { fprintf(stderr, "oledsim: cannot open mask %s\n", path.c_str()); return false; }
    char line[1024];
    int r = 0;
    while (r < 32 && fgets(line, sizeof(line), f)) {
        if (strlen(line) < 64 * 6) continue;
        for (int x = 0; x < 64; x++) {
            unsigned v[3];
            for (int k = 0; k < 3; k++) { char h[3] = {line[x * 6 + k * 2], line[x * 6 + k * 2 + 1], 0}; v[k] = strtoul(h, nullptr, 16); }
            // DumpFrame(): text row r = camera row 31 - r (the camera counts y upwards)
            *pixels->GetColor((31 - r) * 64 + x) = RGBColor(v[0], v[1], v[2]);
        }
        r++;
    }
    fclose(f);
    if (r != 32) fprintf(stderr, "oledsim: mask %s has %d rows (want 32)\n", path.c_str(), r);
    return true;
}

static void applySets(const std::vector<Set> &sets) {
    for (const Set &s : sets) {
        if (s.key == "menu") Menu::SetCurrentMenu(atoi(s.a.c_str()));
        else if (s.key == "face") Menu::SetFaceState(atoi(s.a.c_str()));
        else if (s.key == "val") Menu::SetMenuValue(atoi(s.a.c_str()), atoi(s.b.c_str()));
        else if (s.key == "temp") oledsim_in.temp = atof(s.a.c_str());
        else if (s.key == "booped") oledsim_in.booped = atoi(s.a.c_str()) != 0;
        else if (s.key == "fps") oledsim_in.fps = atof(s.a.c_str());
        else if (s.key == "mask") loadMask(s.a);
        else if (s.key == "maskfollow") maskFollow = atoi(s.a.c_str()) != 0;
        else if (s.key == "lang") pendingLang = atoi(s.a.c_str());
        else fprintf(stderr, "oledsim: unknown set key '%s'\n", s.key.c_str());
    }
}

static void writeFrame(FILE *f, const OledFrame &fr, uint64_t nowUs) {
    fprintf(f, "@ %.3f %.3f %u %d %d %.3f\n", nowUs / 1000.0, fr.lastDataUs / 1000.0, fr.contrast, fr.on ? 1 : 0, fr.invert ? 1 : 0,
            fr.lastTxUs / 1000.0);
    char row[129];
    row[128] = 0;
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 128; x++) row[x] = fr.px[y][x] ? '1' : '0';
        fprintf(f, "%s\n", row);
    }
}

static const char *stageName(uint8_t s) {
    static const char *const n[5] = {"WAIT", "ARMED", "TAP", "GAP", "HOLD"};
    return s < 5 ? n[s] : "?";
}

alignas(HeadsUpDisplay) static unsigned char hudStorage[sizeof(HeadsUpDisplay)]; // zeroed, like a global project member

static int runState(const Plan &plan, const State &st, const char *outDir) {
    static PixelGroup<2048> camPixels(Vector2D(192.0f, 96.0f), Vector2D(0.0f, 0.0f), 64); // HUB75DeltaCameraManager::camPixels
    pixels = &camPixels;
    Menu::SimSetFaceCount(SIM_FACES);
    oledsim_now_us = 0;
    applySets(st.events[0].sets);

    oledsim_now_us = (uint64_t)plan.initMs * 1000ULL;
    HeadsUpDisplay *hud = new (hudStorage) HeadsUpDisplay(Vector2D(0.0f, 0.0f), Vector2D(192.0f, 96.0f));
    setFaceArray(hud);                 // ProtogenHUB75Project constructor
    applyLang(hud);                    // first Update(): the language saved in EEPROM
    hud->Initialize();                 // ProtogenProject::Initialize

    TimeStep frameLimiter(plan.loopHz);
    size_t nextEvent = 1;
    size_t nextEdge = 0;
    const uint64_t renderUs = st.renderUs >= 0 ? (uint64_t)st.renderUs : plan.renderUs;
    const uint64_t tUs = (uint64_t)st.millis * 1000ULL;
    const uint64_t endUs = tUs + (uint64_t)st.record * 1000ULL;
    const uint64_t capUs = (st.recFrom >= 0 && (uint64_t)st.recFrom * 1000ULL < tUs) ? (uint64_t)st.recFrom * 1000ULL : tUs;

    std::string path = std::string(outDir) + "/" + st.name + ".frames";
    FILE *out = fopen(path.c_str(), "w");
    if (!out) { perror(path.c_str()); return 1; }
    std::string bpath = std::string(outDir) + "/" + st.name + ".boop";
    FILE *blog = fopen(bpath.c_str(), "w");
    if (!blog) { perror(bpath.c_str()); return 1; }
    fprintf(blog, "# L <gesture-update ms>  E <ms> <event> <name> <dur_ms>  S <ms> <stage>  F <ms> <new face>\n"
                  "# B <ms> <isBooped sample>  X <first-tx ms> <bus ms> <data B> <cmd B> <bursts> <hud delay ms>\n");

    // ProtogenHUB75Project / ProtogenProject members
    BoopGesture boopGesture;
    bool isBooped = false;        // ProtogenProject::isBooped: sampled in UpdateFace, frozen while BoopSensor is off
    uint32_t bootStartMs = 0;
    bool bootDone = false;
    uint8_t lastStage = 255;

    bool captured = false;
    size_t atIndex = 0;
    uint64_t atUs = 0;
    bool atMarked = false;
    OledFrame last, cur;
    std::vector<std::pair<uint64_t, OledFrame>> frames;
    uint64_t frameCount = 0;

    while (true) {
        OledBusStats bus0, bus1;
        oledsim_bus_mark();
        oledsim_bus_stats(&bus0);
        uint64_t hudUs = 0;   // clock spent inside the design (hook + Update + ApplyEffect)

        // ---- ProtogenHUB75Project::Update(): boot flag, then the boop gesture
        uint32_t boopNow = millis();
        bool bootActive = false;
        if (!bootDone) {
            if (bootStartMs == 0) bootStartMs = millis();
            uint32_t elapsed = millis() - bootStartMs;
            if (elapsed >= BootScreen::TOTAL_MS) bootDone = true;
            else bootActive = true;
        }
        uint64_t h0 = oledsim_now_us;
        BoopGesture::Event boopEv = BoopGesture::NONE;
        if (bootActive || !Menu::UseBoopSensor()) {
            boopGesture.Reset(boopNow);
            callBoopHook(hud, BoopGesture::STAGE_WAIT, 0, BoopGesture::NONE);
        } else {
            boopEv = boopGesture.Update(isBooped, boopNow);
            uint32_t heldMs = boopGesture.HoldMs(boopNow);
            callBoopHook(hud, boopGesture.GetStage(), heldMs > 65535 ? 65535 : heldMs, boopEv);
            if (boopEv == BoopGesture::FIRE) {
                Menu::NextFace();
                // SelectFace() renders the new face in this same frame
                if (maskFollow) loadMask(plan.fireMask[Menu::GetFaceState()]);
                fprintf(blog, "F %u %u\n", boopNow, Menu::GetFaceState());
            }
        }
        hudUs += oledsim_now_us - h0;
        uint8_t stage = boopGesture.GetStage();
        fprintf(blog, "L %u\n", boopNow);
        if (boopEv != BoopGesture::NONE)
            fprintf(blog, "E %u %u %s %u\n", boopNow, (unsigned)boopEv, BoopGesture::Name(boopEv), boopGesture.DurMs());
        if (stage != lastStage) { fprintf(blog, "S %u %s\n", boopNow, stageName(stage)); lastStage = stage; }

        // ---- ProtogenProject::UpdateFace()
        while (!frameLimiter.IsReady()) delay(1);
        while (nextEvent < st.events.size() && (uint64_t)st.events[nextEvent].t * 1000ULL <= oledsim_now_us)
            applySets(st.events[nextEvent++].sets);
        applyLang(hud);                // serial l<N> mid-run
        while (nextEdge < st.boop.size() && (uint64_t)st.boop[nextEdge].t * 1000ULL <= oledsim_now_us)
            oledsim_in.booped = st.boop[nextEdge++].v;
        if (Menu::UseBoopSensor() && isBooped != oledsim_in.booped) fprintf(blog, "B %u %d\n", millis(), oledsim_in.booped ? 1 : 0);
        if (Menu::UseBoopSensor()) isBooped = oledsim_in.booped;   // boop.isBooped() (the APDS read)

        h0 = oledsim_now_us;
        hud->SetEffect(Menu::GetEffect());
        callHooks(hud);
        hud->Update();
        hud->SetFaceMax(Vector2D(192.0f, 94.0f)); // camMax of ProtogenHUB75Project
        hud->SetFaceMin(Vector2D(0.0f, 0.0f));
        hud->ApplyEffect(pixels);                  // Engine::Render -> scene effect on the main camera
        hudUs += oledsim_now_us - h0;
        oledsim_bus_stats(&bus1);
        if (bus1.tx != bus0.tx || hudUs > bus1.busUs - bus0.busUs) {
            uint64_t t0 = bus1.tx != bus0.tx ? bus1.firstTxStartUs : h0;
            fprintf(blog, "X %.3f %.3f %u %u %u %.3f\n", t0 / 1000.0, (bus1.busUs - bus0.busUs) / 1000.0,
                    bus1.dataBytes - bus0.dataBytes, bus1.cmdBytes - bus0.cmdBytes, bus1.bursts - bus0.bursts,
                    (hudUs - (bus1.busUs - bus0.busUs)) / 1000.0);
        }
        oledsim_now_us += renderUs;
        frameCount++;

        if (oledsim_now_us >= capUs) {
            oledsim_snapshot(&cur);
            if (!captured) { frames.push_back({oledsim_now_us, cur}); last = cur; captured = true; }
            else if (!oledsim_same(cur, last)) { frames.push_back({oledsim_now_us, cur}); last = cur; }
            if (!atMarked && oledsim_now_us >= tUs) { atIndex = frames.size() - 1; atUs = oledsim_now_us; atMarked = true; }
            if (oledsim_now_us >= endUs) break;
        }
    }

    std::string hooks;
    if (HasSetBooped<HeadsUpDisplay>::value) hooks += "SetBooped ";
    if (HasSetFPS<HeadsUpDisplay>::value) hooks += "SetFPS ";
    if (HasSetBoopGesture<HeadsUpDisplay>::value) hooks += "SetBoopGesture ";
    if (HasSetLanguage<HeadsUpDisplay>::value) hooks += "SetLanguage ";
    if (!hooks.empty()) hooks.pop_back();
    fprintf(out, "# name %s\n# millis %u\n# record %u\n# loops %llu\n", st.name.c_str(), st.millis, st.record, (unsigned long long)frameCount);
    fprintf(out, "# hooks %s\n", hooks.c_str());
    fprintf(out, "# warnings %s\n", oledsim_warnings().c_str());
    fprintf(out, "# at_index %zu\n# at_t %.3f\n# rec_from %.3f\n# render_ms %.3f\n", atIndex, atUs / 1000.0, capUs / 1000.0, renderUs / 1000.0);
    for (auto &p : frames) writeFrame(out, p.second, p.first);
    fclose(out);
    fclose(blog);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <plan.txt> <out_dir> [state-name-filter]\n", argv[0]); return 2; }
    Plan plan;
    if (!readPlan(argv[1], plan)) return 2;
    int failures = 0;
    for (const State &st : plan.states) {
        if (argc > 3 && st.name.find(argv[3]) == std::string::npos) continue;
        fflush(stdout); fflush(stderr);
        pid_t pid = fork();
        if (pid == 0) _exit(runState(plan, st, argv[2]));
        int status = 0;
        waitpid(pid, &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            failures++;
            if (WIFSIGNALED(status)) fprintf(stderr, "oledsim: state %s crashed (signal %d)\n", st.name.c_str(), WTERMSIG(status));
            else fprintf(stderr, "oledsim: state %s failed (exit %d)\n", st.name.c_str(), WEXITSTATUS(status));
        }
    }
    return failures ? 1 : 0;
}
