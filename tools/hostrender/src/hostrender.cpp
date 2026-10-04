// hostrender — corre el motor REAL de ProtoTracer (compilado del repo, sin
// tocarlo) en x86 para previsualizar caras de la cabeza HUB75.
//
// Replica el pipeline de ProtogenHUB75Project::Update() + ProtogenProject::UpdateFace()
// para las caras morph (todo lo que no es hardware): EasyEaseAnimator<60> con los
// mismos AddParameter/AddViseme, MaterialAnimator<20> con las mismas capas,
// BlinkTrack, ObjectAlign objA (Stretch, -7.5°, margen 2, mirrorX), wiggle con
// FunctionGenerator, frameLimiter TimeStep(120), HUB75DeltaCameraManager real y
// RenderingEngine/Rasterizer real. El reloj (millis/micros) es virtual.
//
// Entrada: script de líneas por stdin (lo arma render.py). Salida por stdout:
//   FRAME <n> t=<ms>
//   32 renglones de 64 pixeles hex RRGGBB (renglón 0 = arriba, panel "normal",
//   mismo formato que DumpFrame()/captura.py)
//   END
// Comandos del script: ver README.md (sección "formato del script").

#include <Arduino.h>
#include <iostream>
#include <sstream>
#include <map>
#include <vector>
#include <memory>
#include <set>

#include "Scene/Scene.h"
#include "Engine/Engine.h"
#include "Camera/CameraManager/Implementations/HUB75DeltaCameras.h"
#include "Assets/Models/FBX/NukudeFlat.h"
#include "Assets/Models/OBJ/Background.h"
#include "Assets/Models/OBJ/DeltaDisplayBackground.h"
#include "Animation/EasyEaseAnimator.h"
#include "Animation/AnimationTracks/BlinkTrack.h"
#include "Scene/Materials/Utils/MaterialAnimator.h"
#include "Scene/Materials/Static/SimpleMaterial.h"
#include "Scene/Materials/Static/GradientMaterial.h"
#include "Scene/Materials/Animated/FlowNoise.h"
#include "Scene/Materials/Animated/RainbowSpiral.h"
#include "Scene/Materials/Animated/HorizontalRainbow.h"
#include "Scene/Objects/ObjectAlign.h"
#include "Utils/Signals/FunctionGenerator.h"
#include "Utils/Time/TimeStep.h"
#include "ExternalDevices/Sensors/Microphone/Utils/FFTVoiceDetection.h"

// ---------------------------------------------------------------------------
// Geometría dinámica (para objetos extra definidos en la receta). Implementa
// las mismas interfaces que StaticTriangleGroup/TriangleGroup.
class DynStaticTG : public IStaticTriangleGroup {
public:
    std::vector<Vector3D> v;
    std::vector<IndexGroup> idx;
    std::vector<Triangle3D> tris;
    void Build() {
        tris.resize(idx.size());
        for (size_t i = 0; i < idx.size(); i++) {
            tris[i].p1 = &v[idx[i].A]; tris[i].p2 = &v[idx[i].B]; tris[i].p3 = &v[idx[i].C];
        }
    }
    const bool HasUV() override { return false; }
    const IndexGroup* GetIndexGroup() override { return idx.data(); }
    const int GetTriangleCount() override { return (int)idx.size(); }
    const Vector3D* GetVertices() override { return v.data(); }
    const int GetVertexCount() override { return (int)v.size(); }
    Triangle3D* GetTriangles() override { return tris.data(); }
    const Vector2D* GetUVVertices() override { return nullptr; }
    const IndexGroup* GetUVIndexGroup() override { return nullptr; }
};

class DynTG : public ITriangleGroup {
public:
    std::vector<Vector3D> v;
    const IndexGroup* idx = nullptr;
    int nt = 0;
    std::vector<Triangle3D> tris;
    explicit DynTG(DynStaticTG* s) {
        v = s->v; idx = s->idx.data(); nt = (int)s->idx.size();
        tris.resize(nt);
        for (int i = 0; i < nt; i++) {
            tris[i].p1 = &v[idx[i].A]; tris[i].p2 = &v[idx[i].B]; tris[i].p3 = &v[idx[i].C];
        }
    }
    const IndexGroup* GetIndexGroup() override { return idx; }
    int GetTriangleCount() override { return nt; }
    Vector3D* GetVertices() override { return v.data(); }
    int GetVertexCount() override { return (int)v.size(); }
    Triangle3D* GetTriangles() override { return tris.data(); }
    const Vector2D* GetUVVertices() override { return nullptr; }
    const IndexGroup* GetUVIndexGroup() override { return nullptr; }
};

struct ExtraObject {
    std::string name;
    bool modelSpace = true;   // true: coords de NukudeFlat, se alinea JUNTO con la cara
    bool followWiggle = false; // camera: aplica el mismo offset de wiggle que la cara
    bool enabled = true;
    std::unique_ptr<DynStaticTG> st;
    std::unique_ptr<DynTG> tg;
    std::unique_ptr<SimpleMaterial> ownMat;
    std::unique_ptr<Object3D> obj;
};

struct CustomMorph {
    std::string name;
    uint16_t dict;
    std::vector<int> idx;
    std::vector<Vector3D> vec;
    std::unique_ptr<Morph> morph;
};

// ---------------------------------------------------------------------------
static const char* kMorphNames[26] = {
    "Frown","Doubt","Surprised","Sadness","Anger","vrc_v_sil","vrc_v_th","vrc_v_nn",
    "vrc_v_ss","vrc_v_rr","vrc_v_dd","vrc_v_kk","vrc_v_ff","vrc_v_pp","vrc_v_ch",
    "vrc_v_ou","vrc_v_oh","vrc_v_ih","vrc_v_ee","vrc_v_aa","LookDown","LookUp","Blink",
    "BiggerNose","MoveEye","HideBlush"};

static int MorphIndex(const std::string& n) {
    for (int i = 0; i < 26; i++) if (n == kMorphNames[i]) return i;
    return -1;
}

static int VisemeIndex(const std::string& n) {
    static const std::map<std::string, int> m = {
        {"EE", Viseme::EE}, {"AE", Viseme::AE}, {"UH", Viseme::UH}, {"AR", Viseme::AR},
        {"ER", Viseme::ER}, {"AH", Viseme::AH}, {"OO", Viseme::OO}, {"SS", Viseme::SS}};
    auto it = m.find(n);
    return it == m.end() ? -1 : it->second;
}

static IEasyEaseAnimator::InterpolationMethod Interp(const std::string& s) {
    if (s == "Cosine") return IEasyEaseAnimator::Cosine;
    if (s == "Bounce") return IEasyEaseAnimator::Bounce;
    if (s == "Linear") return IEasyEaseAnimator::Linear;
    return IEasyEaseAnimator::Overshoot;
}

// ---------------------------------------------------------------------------
// Espejo de ProtogenProject (template) + ProtogenHUB75Project, sólo la parte
// de caras morph. Los nombres de miembros son los mismos que en el firmware.
class HostProject {
public:
    // ---- ProtogenHUB75Project
    HUB75DeltaCameraManager cameras;
    NukudeFace pM;
    DeltaDisplayBackground deltaDisplayBackground;

    // ---- ProtogenProject
    Background background;
    Vector2D camMin = Vector2D(0.0f, 0.0f);
    Vector2D camMax = Vector2D(192.0f, 94.0f);   // ProtogenHUB75Project ctor: Vector2D(), Vector2D(192, 94)
    Vector2D cameraSize;
    float xOffset = 0.0f, yOffset = 0.0f;

    SimpleMaterial redMaterial    = SimpleMaterial(RGBColor(255, 0, 0));
    SimpleMaterial orangeMaterial = SimpleMaterial(RGBColor(0, 255, 255)); // slot 2 = CYAN en el fork
    SimpleMaterial whiteMaterial  = SimpleMaterial(RGBColor(255, 255, 255));
    SimpleMaterial greenMaterial  = SimpleMaterial(RGBColor(0, 255, 0));
    SimpleMaterial blueMaterial   = SimpleMaterial(RGBColor(0, 0, 255));
    SimpleMaterial yellowMaterial = SimpleMaterial(RGBColor(255, 255, 0));
    SimpleMaterial purpleMaterial = SimpleMaterial(RGBColor(255, 0, 255));
    SimpleMaterial blackMaterial  = SimpleMaterial(RGBColor(0, 0, 0));
    FlowNoise flowNoise;
    RainbowSpiral rainbowSpiral;
    HorizontalRainbow hRainbow;
    RGBColor gradientSpectrum[2] = {RGBColor(255, 0, 0), RGBColor(255, 0, 0)};
    GradientMaterial<2> gradientMat = GradientMaterial<2>(gradientSpectrum, 350.0f, false);
    MaterialAnimator<20> materialAnimator;
    // El fondo en el firmware usa backgroundMaterial (base = texto del menú, fuera
    // de cuadro cuando el menú está escondido) → negro. Aquí directo negro.
    SimpleMaterial bgBlack = SimpleMaterial(RGBColor(0, 0, 0));

    BlinkTrack<2> blink;
    ObjectAlign objA = ObjectAlign(camMin, camMax, Quaternion());
    FunctionGenerator fGenMatXMove = FunctionGenerator(FunctionGenerator::Sine, -2.0f, 2.0f, 5.3f);
    FunctionGenerator fGenMatYMove = FunctionGenerator(FunctionGenerator::Sine, -2.0f, 2.0f, 6.7f);
    float offsetFace = 0.0f, offsetFaceSA = 0.0f, offsetFaceARG = 0.0f, offsetFaceOSC = 0.0f;
    EasyEaseAnimator<60> eEA = EasyEaseAnimator<60>(IEasyEaseAnimator::Overshoot, 1.0f, 0.35f);
    TimeStep frameLimiter = TimeStep(120);

    Scene scene = Scene(16);

    // ---- settings "EEPROM"/host
    uint8_t faceSize = 10;     // Menu FaceSize (0..10)
    uint8_t faceColor = 2;     // Menu Color (2 = CYAN)
    uint8_t hueF = 0, hueB = 0;
    float showMenu = 1.0f;     // Menu::ShowMenu() en reposo
    std::string blinkMode = "off"; float blinkWeight = 0.0f;
    std::string wiggleMode = "off"; float wiggleX = 0.0f, wiggleY = 0.0f;
    float micSS = -1.0f;       // si >=0: emula UpdateFFTVisemes() con magnitud constante (SS = mag/2)
    uint32_t renderCostUs = 4000; // tiempo que "tarda" render+display por frame

    // ---- extras de receta
    std::vector<std::unique_ptr<CustomMorph>> customMorphs;
    std::vector<std::unique_ptr<ExtraObject>> extras;

    // targets del frame (lo que harían los métodos de cara cada frame)
    std::vector<std::pair<uint16_t, float>> paramTargets;
    std::vector<std::pair<Material*, float>> materialTargets;

    unsigned long frameCount = 0;
    std::set<uint16_t> linkedDicts;   // diccionarios ligados al eEA (lo que AddParameterFrame sí mueve)

    HostProject() {
        // ProtogenProject ctor
        scene.AddObject(background.GetObject());
        background.GetObject()->SetMaterial(&bgBlack);
        LinkParameters();
        SetMaterialLayers();
        objA.SetCameraMax(camMax);
        objA.SetCameraMin(camMin);
        objA.SetJustification(ObjectAlign::Stretch);
        cameraSize = camMax - camMin;

        // ProtogenHUB75Project ctor
        scene.AddObject(pM.GetObject());
        scene.AddObject(deltaDisplayBackground.GetObject());
        pM.GetObject()->SetMaterial(&materialAnimator);
        deltaDisplayBackground.GetObject()->SetMaterial(&materialAnimator);
        LinkControlParameters();
        SetWiggleSpeed(5.0f);
    }

    void LinkParameters() {
        eEA.AddParameter(&offsetFace, 50, 40, 0.0f, 1.0f);
        eEA.AddParameter(&offsetFaceSA, 51, 40, 0.0f, 1.0f);
        eEA.AddParameter(&offsetFaceARG, 52, 40, 0.0f, 1.0f);
        eEA.AddParameter(&offsetFaceOSC, 53, 40, 0.0f, 1.0f);
    }

    void SetMaterialLayers() {
        materialAnimator.SetBaseMaterial(Material::Add, &gradientMat);
        materialAnimator.AddMaterial(Material::Replace, &yellowMaterial, 40, 0.0f, 1.0f);
        materialAnimator.AddMaterial(Material::Replace, &orangeMaterial, 40, 0.0f, 1.0f);
        materialAnimator.AddMaterial(Material::Replace, &whiteMaterial, 40, 0.0f, 1.0f);
        materialAnimator.AddMaterial(Material::Replace, &greenMaterial, 40, 0.0f, 1.0f);
        materialAnimator.AddMaterial(Material::Replace, &purpleMaterial, 40, 0.0f, 1.0f);
        materialAnimator.AddMaterial(Material::Replace, &redMaterial, 40, 0.0f, 1.0f);
        materialAnimator.AddMaterial(Material::Replace, &blueMaterial, 40, 0.0f, 1.0f);
        materialAnimator.AddMaterial(Material::Replace, &flowNoise, 40, 0.15f, 1.0f);
        materialAnimator.AddMaterial(Material::Replace, &rainbowSpiral, 40, 0.0f, 1.0f);
        materialAnimator.AddMaterial(Material::Replace, &hRainbow, 40, 0.0f, 1.0f);
        materialAnimator.AddMaterial(Material::Replace, &blackMaterial, 40, 0.0f, 1.0f);
        // (sA/aRG/oSC del firmware van aquí con opacidad 0: no aportan color en caras morph)
    }

    void AddParameter(uint8_t index, float* parameter, uint16_t frames,
                      IEasyEaseAnimator::InterpolationMethod m = IEasyEaseAnimator::Overshoot, bool invert = false) {
        linkedDicts.insert(index);
        if (invert) eEA.AddParameter(parameter, index, frames, 1.0f, 0.0f);
        else eEA.AddParameter(parameter, index, frames, 0.0f, 1.0f);
        eEA.SetInterpolationMethod(index, m);
    }

    void AddViseme(Viseme::MouthShape v, float* parameter) {
        linkedDicts.insert(v + 100);
        eEA.AddParameter(parameter, v + 100, 2, 0.0f, 1.0f);
        eEA.SetInterpolationMethod(v + 100, IEasyEaseAnimator::Linear);
    }

    // Copia literal de ProtogenHUB75Project::LinkControlParameters()
    void LinkControlParameters() {
        AddParameter(NukudeFace::Anger, pM.GetMorphWeightReference(NukudeFace::Anger), 15);
        AddParameter(NukudeFace::Sadness, pM.GetMorphWeightReference(NukudeFace::Sadness), 15, IEasyEaseAnimator::InterpolationMethod::Cosine);
        AddParameter(NukudeFace::Surprised, pM.GetMorphWeightReference(NukudeFace::Surprised), 15);
        AddParameter(NukudeFace::Doubt, pM.GetMorphWeightReference(NukudeFace::Doubt), 15);
        AddParameter(NukudeFace::Frown, pM.GetMorphWeightReference(NukudeFace::Frown), 15);
        AddParameter(NukudeFace::LookUp, pM.GetMorphWeightReference(NukudeFace::LookUp), 15);
        AddParameter(NukudeFace::LookDown, pM.GetMorphWeightReference(NukudeFace::LookDown), 15);
        AddParameter(NukudeFace::HideBlush, pM.GetMorphWeightReference(NukudeFace::HideBlush), 15, IEasyEaseAnimator::InterpolationMethod::Cosine, true);
        AddViseme(Viseme::MouthShape::EE, pM.GetMorphWeightReference(NukudeFace::vrc_v_ee));
        AddViseme(Viseme::MouthShape::AH, pM.GetMorphWeightReference(NukudeFace::vrc_v_aa));
        AddViseme(Viseme::MouthShape::UH, pM.GetMorphWeightReference(NukudeFace::vrc_v_dd));
        AddViseme(Viseme::MouthShape::AR, pM.GetMorphWeightReference(NukudeFace::vrc_v_rr));
        AddViseme(Viseme::MouthShape::ER, pM.GetMorphWeightReference(NukudeFace::vrc_v_ch));
        AddViseme(Viseme::MouthShape::OO, pM.GetMorphWeightReference(NukudeFace::vrc_v_oh));
        AddViseme(Viseme::MouthShape::SS, pM.GetMorphWeightReference(NukudeFace::vrc_v_ss));
    }

    void LinkBlink() { blink.AddParameter(pM.GetMorphWeightReference(NukudeFace::Blink)); }

    void SetWiggleSpeed(float m) {
        fGenMatXMove.SetPeriod(5.3f / m);
        fGenMatYMove.SetPeriod(6.7f / m);
    }

    Vector3D GetWiggleOffset() {
        if (wiggleMode == "auto") return Vector3D(fGenMatXMove.Update(), fGenMatYMove.Update(), 0);
        if (wiggleMode == "fixed") return Vector3D(wiggleX, wiggleY, 0);
        return Vector3D(0, 0, 0);
    }

    Material* ColorMaterial(const std::string& c) {
        if (c == "CYELLOW") return &yellowMaterial;
        if (c == "CORANGE" || c == "CCYAN") return &orangeMaterial;
        if (c == "CWHITE") return &whiteMaterial;
        if (c == "CGREEN") return &greenMaterial;
        if (c == "CPURPLE") return &purpleMaterial;
        if (c == "CRED") return &redMaterial;
        if (c == "CBLUE") return &blueMaterial;
        if (c == "CRAINBOW") return &rainbowSpiral;
        if (c == "CRAINBOWNOISE") return &flowNoise;
        if (c == "CHORIZONTALRAINBOW") return &hRainbow;
        if (c == "CBLACK") return &blackMaterial;
        return nullptr;
    }

    void SetMaterialColor() {
        switch (faceColor) {
            case 1: materialAnimator.AddMaterialFrame(yellowMaterial, 0.8f); break;
            case 2: materialAnimator.AddMaterialFrame(orangeMaterial, 0.8f); break;
            case 3: materialAnimator.AddMaterialFrame(whiteMaterial, 0.8f); break;
            case 4: materialAnimator.AddMaterialFrame(greenMaterial, 0.8f); break;
            case 5: materialAnimator.AddMaterialFrame(purpleMaterial, 0.8f); break;
            case 6: materialAnimator.AddMaterialFrame(redMaterial, 0.8f); break;
            case 7: materialAnimator.AddMaterialFrame(blueMaterial, 0.8f); break;
            case 8: materialAnimator.AddMaterialFrame(rainbowSpiral, 0.8f); break;
            case 9: materialAnimator.AddMaterialFrame(flowNoise, 0.8f); break;
            case 10: materialAnimator.AddMaterialFrame(hRainbow, 0.8f); break;
            case 11: materialAnimator.AddMaterialFrame(blackMaterial, 0.8f); break;
            default: break;
        }
    }

    // Espejo de ProtogenProject::UpdateFace(ratio) sin hardware (menú/mic/fan/HUD).
    void UpdateFace(float ratio) {
        while (!frameLimiter.IsReady()) delay(1);

        xOffset = fGenMatXMove.Update();
        yOffset = fGenMatYMove.Update();

        if (micSS >= 0.0f) eEA.AddParameterFrame(Viseme::SS + 100, micSS);

        SetMaterialColor();
        RGBColor hueFront = RGBColor(255, 0, 0).HueShift(hueF * 36);
        RGBColor hueBack  = RGBColor(255, 0, 0).HueShift(hueB * 36);
        gradientSpectrum[0] = hueFront;
        gradientSpectrum[1] = hueBack;
        gradientMat.UpdateGradient(gradientSpectrum);
        flowNoise.SetGradient(hueFront, 0);
        flowNoise.SetGradient(hueBack, 1);

        if (blinkMode == "auto") blink.Update();

        eEA.Update();

        flowNoise.Update(ratio);
        rainbowSpiral.Update(ratio);
        hRainbow.Update(ratio);
        materialAnimator.Update();

        float scale = showMenu * 0.6f + 0.4f;
        float faceSizeOffset = faceSize * (cameraSize.X / 20.0f);
        float faceSizeMaxX = cameraSize.X / 2.0f;
        float xMaxCamera = cameraSize.X - faceSizeMaxX + faceSizeOffset;
        objA.SetCameraMax(Vector2D(xMaxCamera, cameraSize.Y - cameraSize.Y * offsetFace).Multiply(scale));
    }

    // Espejo de ProtogenHUB75Project::Update(ratio) para caras morph.
    void Update(float ratio) {
        pM.Reset();
        pM.GetObject()->Enable();
        deltaDisplayBackground.GetObject()->Enable();

        // SelectFace(): los métodos de cara hacen AddParameterFrame/AddMaterialFrame
        for (auto& p : paramTargets) eEA.AddParameterFrame(p.first, p.second);
        for (auto& m : materialTargets) materialAnimator.AddMaterialFrame(*m.first, m.second);

        UpdateFace(ratio);

        pM.Update();
        for (auto& cm : customMorphs) {
            if (cm->morph->Weight > 0.0f) cm->morph->MorphObject3D(pM.GetObject()->GetTriangleGroup());
        }

        // AlignObjectFace(pM.GetObject(), -7.5f) — con objetos extra en espacio
        // de modelo se alinea el grupo completo (AlignObjectsFace).
        std::vector<Object3D*> group = {pM.GetObject()};
        for (auto& e : extras) {
            if (e->modelSpace && e->enabled) {
                e->obj->ResetVertices();
                group.push_back(e->obj.get());
            }
        }
        modelCoords.clear();   // coords de modelo (ya con morphs) antes de alinear, pa `verts`
        for (Object3D* o : group) {
            ITriangleGroup* tg = o->GetTriangleGroup();
            modelCoords[o] = std::vector<Vector3D>(tg->GetVertices(), tg->GetVertices() + tg->GetVertexCount());
        }
        objA.SetPlaneOffsetAngle(-7.5f);
        objA.SetEdgeMargin(2.0f);
        if (group.size() == 1) objA.AlignObject(pM.GetObject());
        else objA.AlignObjects(group.data(), (uint8_t)group.size());
        objA.SetMirrorX(true);

        // guarda los vértices alineados (antes del wiggle) pa poder barrer offsets
        lastGroup = group;
        aligned.clear();
        for (Object3D* o : group) {
            ITriangleGroup* tg = o->GetTriangleGroup();
            aligned.emplace_back(tg->GetVertices(), tg->GetVertices() + tg->GetVertexCount());
        }
        ApplyWiggle(GetWiggleOffset());
        for (auto& e : extras) {
            if (!e->modelSpace) {
                e->obj->ResetVertices();
                e->obj->GetTransform()->SetPosition(e->followWiggle ? lastWiggle : Vector3D());
                e->obj->UpdateTransform();
            }
            if (e->enabled) e->obj->Enable(); else e->obj->Disable();
        }
    }

    std::map<Object3D*, std::vector<Vector3D>> modelCoords;
    std::vector<Object3D*> lastGroup;
    std::vector<std::vector<Vector3D>> aligned;
    Vector3D lastWiggle;

    // pM.GetObject()->GetTransform()->SetPosition(GetWiggleOffset()); UpdateTransform();
    void ApplyWiggle(Vector3D wig) {
        lastWiggle = wig;
        for (auto& e : extras) {
            if (!e->modelSpace && e->followWiggle) {
                e->obj->ResetVertices();
                e->obj->GetTransform()->SetPosition(wig);
                e->obj->UpdateTransform();
            }
        }
        for (size_t k = 0; k < lastGroup.size(); k++) {
            ITriangleGroup* tg = lastGroup[k]->GetTriangleGroup();
            for (int i = 0; i < tg->GetVertexCount(); i++) tg->GetVertices()[i] = aligned[k][i];
            lastGroup[k]->GetTransform()->SetPosition(wig);
            lastGroup[k]->UpdateTransform();
        }
    }

    void Frame() {
        float ratio = (float)(millis() % 5000) / 5000.0f;  // main.cpp loop()
        Update(ratio);
        Rasterizer::Rasterize(&scene, cameras.GetCameras()[0]);  // sólo la cámara principal (64x32)
        delayMicroseconds(renderCostUs);
        frameCount++;
    }

    void Emit(std::ostream& os) {
        static const char hexd[] = "0123456789ABCDEF";
        IPixelGroup* px = cameras.GetCameras()[0]->GetPixelGroup();
        os << "FRAME " << frameCount << " t=" << millis() << " wig=" << lastWiggle.X << "," << lastWiggle.Y << "\n";
        os << "W";
        for (int k = 0; k < 26; k++) os << " " << kMorphNames[k] << "=" << *pM.GetMorphWeightReference((NukudeFace::Morphs)k);
        for (auto& cm : customMorphs) os << " " << cm->name << "=" << cm->morph->Weight;
        os << "\n";
        std::string line(64 * 6, '0');
        for (int row = 0; row < 32; row++) {
            int y = 31 - row;   // la cámara cuenta y hacia arriba (igual que DumpFrame)
            for (int x = 0; x < 64; x++) {
                RGBColor* c = px->GetColor(y * 64 + x);
                uint8_t v[3] = {c->R, c->G, c->B};
                for (int k = 0; k < 3; k++) {
                    line[x * 6 + k * 2] = hexd[v[k] >> 4];
                    line[x * 6 + k * 2 + 1] = hexd[v[k] & 15];
                }
            }
            os << line << "\n";
        }
        os << "END\n";
    }

    // Dump de vértices (después de alinear) en coordenadas de cámara / pixel.
    void EmitVerts(std::ostream& os) {
        auto dump = [&](const std::string& tag, Object3D* o) {
            ITriangleGroup* tg = o->GetTriangleGroup();
            os << tag << " " << tg->GetVertexCount() << "\n";
            for (int i = 0; i < tg->GetVertexCount(); i++) {
                Vector3D v = tg->GetVertices()[i];
                Vector3D m = modelCoords.count(o) ? modelCoords[o][i] : v;
                os << i << " " << v.X << " " << v.Y << " " << v.Z
                   << " px=" << v.X / 3.0f << " row=" << 31.0f - v.Y / 3.0f
                   << " m=" << m.X << "," << m.Y << "," << m.Z << "\n";
            }
            os << "END\n";
        };
        dump("VERTS pM", pM.GetObject());
        for (auto& e : extras) dump("VERTS " + e->name, e->obj.get());
    }
};

static std::vector<std::string> Split(const std::string& s) {
    std::istringstream is(s);
    std::vector<std::string> out; std::string t;
    while (is >> t) out.push_back(t);
    return out;
}

int main(int argc, char** argv) {
    hostclock::us() = 1000000ULL;  // arranca en t=1s (frameLimiter/blink con previousMillis=0)
    auto prj = std::make_unique<HostProject>();
    HostProject& P = *prj;

    std::istream* in = &std::cin;

    std::string line;
    int lineNo = 0;
    auto warn = [&](const std::string& m) { std::cerr << "WARN (linea " << lineNo << "): " << m << "\n"; };

    while (std::getline(*in, line)) {
        lineNo++;
        auto hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        auto t = Split(line);
        if (t.empty()) continue;
        const std::string& cmd = t[0];

        if (cmd == "set" && t.size() >= 3) {
            const std::string& k = t[1];
            if (k == "facesize") P.faceSize = (uint8_t)std::stoi(t[2]);
            else if (k == "color") P.faceColor = (uint8_t)std::stoi(t[2]);
            else if (k == "hue" && t.size() >= 4) { P.hueF = (uint8_t)std::stoi(t[2]); P.hueB = (uint8_t)std::stoi(t[3]); }
            else if (k == "time_ms") hostclock::us() = (uint64_t)std::stoull(t[2]) * 1000ULL;
            else if (k == "render_us") P.renderCostUs = (uint32_t)std::stoul(t[2]);
            else if (k == "mic_ss") P.micSS = std::stof(t[2]);
            else if (k == "blink") {
                if (t[2] == "auto") { if (P.blinkMode != "auto") P.LinkBlink(); P.blinkMode = "auto"; }
                else if (t[2] == "off") P.blinkMode = "off";
                else { P.blinkMode = "off"; warn("blink fijo: usa 'target param Blink' tras 'link Blink'"); }
            }
            else if (k == "wiggle") {
                if (t[2] == "auto") P.wiggleMode = "auto";
                else if (t[2] == "off") P.wiggleMode = "off";
                else if (t.size() >= 4) { P.wiggleMode = "fixed"; P.wiggleX = std::stof(t[2]); P.wiggleY = std::stof(t[3]); }
            }
            else warn("set desconocido: " + k);
        }
        else if (cmd == "link" && t.size() >= 2) {
            // Simula agregar AddParameter(NukudeFace::X, ...) en LinkControlParameters
            int mi = MorphIndex(t[1]);
            if (mi < 0) { warn("morph desconocido: " + t[1]); continue; }
            uint16_t frames = t.size() >= 3 ? (uint16_t)std::stoi(t[2]) : 15;
            auto m = t.size() >= 4 ? Interp(t[3]) : IEasyEaseAnimator::Overshoot;
            bool inv = t.size() >= 5 && t[4] == "invert";
            P.AddParameter((uint8_t)mi, P.pM.GetMorphWeightReference((NukudeFace::Morphs)mi), frames, m, inv);
        }
        else if (cmd == "morph" && t.size() >= 3) {
            // morph <nombre> <n> [frames] [interp]  + n líneas "idx dx dy dz"
            auto cm = std::make_unique<CustomMorph>();
            cm->name = t[1];
            int n = std::stoi(t[2]);
            uint16_t frames = t.size() >= 4 ? (uint16_t)std::stoi(t[3]) : 15;
            auto m = t.size() >= 5 ? Interp(t[4]) : IEasyEaseAnimator::Overshoot;
            for (int i = 0; i < n; i++) {
                std::getline(*in, line); lineNo++;
                auto q = Split(line);
                if (q.size() < 4) { warn("morph: renglón incompleto"); continue; }
                cm->idx.push_back(std::stoi(q[0]));
                cm->vec.push_back(Vector3D(std::stof(q[1]), std::stof(q[2]), std::stof(q[3])));
            }
            cm->dict = (uint16_t)(200 + P.customMorphs.size());
            cm->morph = std::make_unique<Morph>((int)cm->idx.size(), cm->idx.data(), cm->vec.data());
            P.eEA.AddParameter(&cm->morph->Weight, cm->dict, frames, 0.0f, 1.0f);
            P.linkedDicts.insert(cm->dict);
            P.eEA.SetInterpolationMethod(cm->dict, m);
            P.customMorphs.push_back(std::move(cm));
        }
        else if (cmd == "object" && t.size() >= 6) {
            // object <nombre> <model|camera|camera_wiggle> <nv> <nt> <face | rgb R G B>
            auto e = std::make_unique<ExtraObject>();
            e->name = t[1];
            e->modelSpace = (t[2] == "model");
            e->followWiggle = (t[2] == "camera_wiggle");
            int nv = std::stoi(t[3]), nt = std::stoi(t[4]);
            e->st = std::make_unique<DynStaticTG>();
            for (int i = 0; i < nv; i++) {
                std::getline(*in, line); lineNo++;
                auto q = Split(line);
                e->st->v.push_back(Vector3D(std::stof(q[0]), std::stof(q[1]), std::stof(q[2])));
            }
            for (int i = 0; i < nt; i++) {
                std::getline(*in, line); lineNo++;
                auto q = Split(line);
                e->st->idx.push_back(IndexGroup(std::stoi(q[0]), std::stoi(q[1]), std::stoi(q[2])));
            }
            e->st->Build();
            e->tg = std::make_unique<DynTG>(e->st.get());
            Material* mat = &P.materialAnimator;
            if (t[5] == "rgb" && t.size() >= 9) {
                e->ownMat = std::make_unique<SimpleMaterial>(RGBColor(std::stoi(t[6]), std::stoi(t[7]), std::stoi(t[8])));
                mat = e->ownMat.get();
            }
            e->obj = std::make_unique<Object3D>(e->st.get(), e->tg.get(), mat);
            P.scene.AddObject(e->obj.get());
            P.extras.push_back(std::move(e));
        }
        else if (cmd == "target") {
            if (t.size() >= 2 && t[1] == "clear") { P.paramTargets.clear(); P.materialTargets.clear(); continue; }
            if (t.size() < 4) { warn("target incompleto"); continue; }
            if (t[1] == "param") {
                // Igual que AddParameterFrame(NukudeFace::X, w): si el morph no está
                // ligado al eEA con ese índice, el firmware lo ignora en silencio.
                int mi = MorphIndex(t[2]);
                int dict = -1;
                if (mi >= 0) dict = mi;
                for (auto& cm : P.customMorphs) if (cm->name == t[2]) dict = cm->dict;
                if (dict < 0) { warn("param desconocido: " + t[2]); continue; }
                if (!P.linkedDicts.count((uint16_t)dict)) {
                    warn(t[2] + " NO está ligado en LinkControlParameters → el firmware ignora este AddParameterFrame (usa 'link' o 'viseme')");
                }
                P.paramTargets.push_back({(uint16_t)dict, std::stof(t[3])});
            } else if (t[1] == "viseme") {
                int vi = VisemeIndex(t[2]);
                if (vi < 0) { warn("viseme desconocido: " + t[2]); continue; }
                P.paramTargets.push_back({(uint16_t)(vi + 100), std::stof(t[3])});
            } else if (t[1] == "material") {
                Material* m = P.ColorMaterial(t[2]);
                if (!m) { warn("material desconocido: " + t[2]); continue; }
                P.materialTargets.push_back({m, std::stof(t[3])});
            } else if (t[1] == "object") {
                for (auto& e : P.extras) if (e->name == t[2]) e->enabled = (t[3] == "on");
            } else warn("target desconocido: " + t[1]);
        }
        else if (cmd == "run" && t.size() >= 2) {
            int frames = std::stoi(t[1]);
            int every = t.size() >= 3 ? std::stoi(t[2]) : 0;
            for (int i = 1; i <= frames; i++) {
                P.Frame();
                if ((every > 0 && i % every == 0) || (every == 0 && i == frames)) P.Emit(std::cout);
            }
        }
        else if (cmd == "sweep" && t.size() >= 6) {
            // sweep x0 x1 y0 y1 paso: re-renderiza el ÚLTIMO estado con varios offsets de
            // wiggle (unidades de cámara; el firmware oscila en ±2 por eje)
            float x0 = std::stof(t[1]), x1 = std::stof(t[2]), y0 = std::stof(t[3]), y1 = std::stof(t[4]), st = std::stof(t[5]);
            for (float wx = x0; wx <= x1 + 1e-4f; wx += st)
                for (float wy = y0; wy <= y1 + 1e-4f; wy += st) {
                    P.ApplyWiggle(Vector3D(wx, wy, 0));
                    Rasterizer::Rasterize(&P.scene, P.cameras.GetCameras()[0]);
                    P.Emit(std::cout);
                }
        }
        else if (cmd == "verts") {
            P.EmitVerts(std::cout);
        }
        else warn("comando desconocido: " + cmd);
    }
    std::cout.flush();
    return 0;
}
