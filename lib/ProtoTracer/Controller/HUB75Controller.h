/**
 * @file HUB75Controller.h
 * @brief Declares the HUB75Controller class for managing HUB75 LED matrices.
 *
 * This file defines the HUB75Controller class, which extends the Controller base class.
 * It provides functionality for controlling and displaying content on HUB75 LED matrices.
 *
 * @date 22/12/2024
 * @author Coela Can't
 */

#pragma once

#include <stdint.h> // Include for fixed-width integer types.
#include "Controller.h" // Include for base controller functionality.
#include "SmartMatrixHUB75.h" // Include for HUB75-specific matrix utilities.
#include "../Camera/CameraManager/CameraManager.h" // Include for camera management.
#include "../Camera/Pixels/PixelGroup.h" // Include for pixel group management.
#include "ScreenSource.h"

/**
 * @class HUB75Controller
 * @brief Manages HUB75 LED matrices with camera integration.
 *
 * The HUB75Controller class extends the Controller base class to provide specific
 * functionality for initializing, controlling, and displaying content on HUB75 LED matrices.
 */
class HUB75Controller : public Controller {
private:
    // When true (default) the camera output is duplicated to the lower half
    // of the panel with X mirrored. Banner-style faces (text) toggle this off
    // each frame so the lower half stays black instead of flipping the text.
    bool mirrorEnabled = true;

    // != nullptr: este frame se dibuja esa pantalla directo (texto derecho en
    // los dos paneles) en vez de la cámara. El proyecto lo resetea cada frame.
    ScreenSource* screen = nullptr;

    // Cómo está montado cada panel, visto de frente (comando k del proyecto).
    // bit0 = intercambiar izquierda/derecha, bit1 = voltear X. 0 = calibrado
    // a ojo el 2026-10-02: el panel de la mitad de ABAJO del matrix lógico es
    // el de la IZQUIERDA viéndola de frente (la derecha de la cabeza puesta).
    uint8_t panelCal = 0;

    void FrontToLogical(uint8_t panel, uint8_t x, uint8_t y, uint8_t& lx, uint8_t& ly) const;
    void Plot(uint8_t lx, uint8_t ly, const rgb24& c);

public:
    /**
     * @brief Constructs a HUB75Controller with specified parameters.
     *
     * @param cameras Pointer to the CameraManager for managing camera data.
     * @param maxBrightness Maximum brightness for the LED matrix.
     * @param maxAccentBrightness Maximum brightness for accent lighting.
     */
    HUB75Controller(CameraManager* cameras, uint8_t maxBrightness, uint8_t maxAccentBrightness);

    /**
     * @brief Initializes the HUB75Controller and sets up the LED matrix.
     */
    void Initialize() override;

    /**
     * @brief Updates and displays the content on the LED matrix.
     */
    void Display() override;

    /**
     * @brief Sets the maximum brightness of the LED matrix.
     *
     * @param maxBrightness The maximum brightness value (0-255).
     */
    void SetBrightness(uint8_t maxBrightness) override;

    /**
     * @brief Sets the maximum accent brightness of the secondary display.
     *
     * @param maxAccentBrightness The maximum accent brightness value (0-255).
     */
    void SetAccentBrightness(uint8_t maxAccentBrightness) override;

    /**
     * @brief Enable/disable the lower-half mirror. Off = lower half black.
     */
    void SetMirrorEnabled(bool enabled) { mirrorEnabled = enabled; }

    void SetScreenSource(ScreenSource* source) { screen = source; }
    void SetPanelCalibration(uint8_t cal) { panelCal = cal & 3; }
    uint8_t GetPanelCalibration() const { return panelCal; }

    // Lo último que se mandó a los paneles, en coordenadas de vista de frente
    // (pa capturas por serial): panel 0 = izquierda, x de izq a der, y de arriba.
    ScreenRGB GetFrontPixel(uint8_t panel, uint8_t x, uint8_t y) const;
};
