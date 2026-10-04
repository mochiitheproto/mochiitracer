#include "APDS9960.h"

Adafruit_APDS9960 APDS9960::apds;
uint16_t APDS9960::proximity;
uint16_t APDS9960::threshold;

MinFilter<10> APDS9960::minF = MinFilter<10>(false);
TimeStep APDS9960::timeStep = TimeStep(5);
float APDS9960::minimum = 0.0f;
bool APDS9960::didBegin = false;
bool APDS9960::isBright = false;
bool APDS9960::isProx = false;
bool APDS9960::isGesture = false;
uint32_t APDS9960::i2cErrors = 0;

bool APDS9960::Initialize(uint8_t threshold) {
    APDS9960::threshold = threshold;

    Wire.setClock(100000); // for longer-range transmissions
    Wire.begin();

#ifdef WS35
    Wire.setSDA(19);
    Wire.setSCL(18);
#else
    Wire.setSDA(18);
    Wire.setSCL(19);
#endif

    Wire.beginTransmission(0x39);
    uint8_t error = Wire.endTransmission();

    if (error == 0) { // SSD1306 Found
        didBegin = apds.begin();

        // (gesture engine disabled — caused render loop to stall when FIFO
        //  had partial data. ReadGesture() in the lib has a blocking while-loop
        //  that delay(30)s while reading, which freezes animation.
        //  2026-06-11: se intentó un lector no-bloqueante con clasificador por
        //  orden de saturación — funcionó, pero los diodos U/D no distinguen
        //  arriba/abajo en este mount (visor) y se decidió mejor UX: doble-boop
        //  = siguiente cara (ver ProtogenHUB75Project::Update). Detalles en
        //  LEARNINGS.md. Del selector por gestos sólo quedó un hex respaldado;
        //  el fuente no se guardó.)
    } else {
        didBegin = false;
    }

    return didBegin;
}

uint8_t APDS9960::ReadGesture() {
    if (!didBegin || !isGesture) return 0;
    return apds.readGesture();
}

bool APDS9960::isBooped() {
    GetValue();

    if (timeStep.IsReady()) {
        minimum = minF.Filter(proximity);
    }

    return proximity > minimum + threshold;
}

void APDS9960::ResetI2CBus() {
    Wire.end(); // Disable the I2C hardware
    delay(10);  // Wait a bit
    Wire.begin(); // Re-enable the I2C hardware
}

uint8_t APDS9960::GetValue() {
    unsigned long cmdTime = millis();

    if (didBegin) {
        if (!isProx) {
            apds.enableProximity(true);
            isProx = true;
        }
        // readProximity() de Adafruit no avisa si falla el I2C: regresa 156
        // (la dirección de PDATA que quedó en el buffer) = boop fantasma.
        // Leemos a mano y si falla nos quedamos con la lectura anterior.
        Wire.beginTransmission((uint8_t)0x39);
        Wire.write((uint8_t)0x9C); // PDATA
        if (Wire.endTransmission((uint8_t)0) == 0 && Wire.requestFrom((uint8_t)0x39, (uint8_t)1) == 1) {
            proximity = Wire.read();
        } else {
            i2cErrors++;
        }
    }

    if (millis() - cmdTime > 100) {
        // Timeout occurred
        ResetI2CBus();
    }

    return proximity;
}

uint16_t APDS9960::GetBrightness() {
    uint16_t brightness;
    uint16_t r, g, b, c;

    if (didBegin) {
        if (!isBright) {
            apds.enableColor();
            isBright = true;
        }

        apds.getColorData(&r, &g, &b, &c);

        brightness = r + g + b + c;
    }
    else{
        brightness = 0;
    }

    return brightness;
}
