#include "HUB75Controller.h"

//Macro calls from SmartMatrix library included in SmartMatrixHUB75
SMARTMATRIX_ALLOCATE_BUFFERS(matrix, kMatrixWidth, kMatrixHeight, kRefreshDepth, kDmaBufferRows, kPanelType, kMatrixOptions);
SMARTMATRIX_ALLOCATE_BACKGROUND_LAYER(backgroundLayer, kMatrixWidth, kMatrixHeight, COLOR_DEPTH, kBackgroundLayerOptions);

SMARTMATRIX_APA_ALLOCATE_BUFFERS(apamatrix, kApaMatrixWidth, kApaMatrixHeight, kApaRefreshDepth, kApaDmaBufferRows, kApaPanelType, kApaMatrixOptions);
SMARTMATRIX_ALLOCATE_BACKGROUND_LAYER(apaBackgroundLayer, kApaMatrixWidth, kApaMatrixHeight, COLOR_DEPTH, kApaBackgroundLayerOptions);

HUB75Controller::HUB75Controller(CameraManager* cameras, uint8_t maxBrightness, uint8_t maxAccentBrightness) : Controller(cameras, maxBrightness, maxAccentBrightness){}

void HUB75Controller::Initialize(){
    //HUB75
    matrix.addLayer(&backgroundLayer);
    matrix.begin();
    matrix.setRefreshRate(240);

    backgroundLayer.swapBuffers();//for ESP32 - first is ignored

    //APA102
    apamatrix.addLayer(&apaBackgroundLayer);
    apamatrix.begin();
    apamatrix.setRefreshRate(240);
}

void HUB75Controller::Display(){
    UpdateBrightness();

    matrix.setBrightness(brightness);
    apamatrix.setBrightness(accentBrightness);

    IPixelGroup* camPixels = cameras->GetCameras()[0]->GetPixelGroup();
    IPixelGroup* camSidePixelsL = cameras->GetCameras()[1]->GetPixelGroup();
    IPixelGroup* camSidePixelsR = cameras->GetCameras()[2]->GetPixelGroup();

    if (screen) {
        // pantalla directa (arranque, BSOD): ya viene en vista de frente
        for (uint8_t panel = 0; panel < 2; panel++) {
            for (uint8_t y = 0; y < 32; y++) {
                for (uint8_t x = 0; x < 64; x++) {
                    ScreenRGB c = screen->GetPixel(panel, x, y);
                    uint8_t lx, ly;
                    FrontToLogical(panel, x, y, lx, ly);
                    Plot(lx, ly, rgb24(c.r, c.g, c.b));
                }
            }
        }
    } else {
        // cara: la cámara en un panel y su espejo en el otro (simetría)
        for (uint16_t y = 0; y < 32; y++) {
            for (uint16_t x = 0; x < 64; x++){
                uint16_t pixelNum = y * 64 + x;

                rgb24 rgbColor = rgb24((uint16_t)camPixels->GetColor(pixelNum)->R, (uint16_t)camPixels->GetColor(pixelNum)->G, (uint16_t)camPixels->GetColor(pixelNum)->B);

                Plot(x, (31 - y), rgbColor);
                if (mirrorEnabled) {
                    Plot(63 - x, (31 - y) + 32, rgbColor);
                } else {
                    // Black out the lower half so banner-style faces don't flip horizontally.
                    Plot(63 - x, (31 - y) + 32, rgb24(0, 0, 0));
                }
            }
        }
    }

    backgroundLayer.swapBuffers(false);

    for (uint16_t x = 0; x < kApaMatrixWidth / 2; x++){
        rgb24 rgbColorL = rgb24((uint16_t)camSidePixelsL->GetColor(x)->R, (uint16_t)camSidePixelsL->GetColor(x)->G, (uint16_t)camSidePixelsL->GetColor(x)->B);
        rgb24 rgbColorR = rgb24((uint16_t)camSidePixelsR->GetColor(x)->R, (uint16_t)camSidePixelsR->GetColor(x)->G, (uint16_t)camSidePixelsR->GetColor(x)->B);
        
        apaBackgroundLayer.drawPixel(x, 0, rgbColorL);
        apaBackgroundLayer.drawPixel(x + 88, 0, rgbColorR);
    }
    
    apaBackgroundLayer.swapBuffers(false);
}

// Panel en vista de frente → coordenada del matrix lógico 64x64 de SmartMatrix
// (mitad de arriba = un panel, mitad de abajo = el otro). Ver panelCal.
void HUB75Controller::FrontToLogical(uint8_t panel, uint8_t x, uint8_t y, uint8_t& lx, uint8_t& ly) const {
    bool left = (panel == 0) != ((panelCal & 1) != 0);
    lx = (panelCal & 2) ? (uint8_t)(63 - x) : x;
    ly = left ? (uint8_t)(y + 32) : y;
}

// Copia de lo que se mandó a los paneles, pa GetFrontPixel (capturas). En
// DMAMEM (OCRAM) pa no comerse RAM1; se escribe completa cada frame antes de leerse.
DMAMEM static rgb24 sentPixels[64][64];

void HUB75Controller::Plot(uint8_t lx, uint8_t ly, const rgb24& c) {
    backgroundLayer.drawPixel(lx, ly, c);
    sentPixels[ly][lx] = c;
}

ScreenRGB HUB75Controller::GetFrontPixel(uint8_t panel, uint8_t x, uint8_t y) const {
    uint8_t lx, ly;
    FrontToLogical(panel, x, y, lx, ly);
    const rgb24& c = sentPixels[ly][lx];
    return {c.red, c.green, c.blue};
}

void HUB75Controller::SetBrightness(uint8_t maxBrightness){
    this->maxBrightness = maxBrightness * 25 + 5;
    
    if(isOn){//past soft start
        this->brightness = maxBrightness * 25 + 5;
    }
}

void HUB75Controller::SetAccentBrightness(uint8_t maxAccentBrightness){
    this->maxAccentBrightness = maxAccentBrightness * 12 + 5;
    
    if(isOn){//past soft start
        this->accentBrightness = maxAccentBrightness * 12 + 5;
    }
}