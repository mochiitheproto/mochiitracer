"""Every word the OLED HUD ("visorcito") shows, in each language. gen_assets.py turns this into
the text tables of SoftAssets.* and stops with an error if a character has no glyph (font5x7.py
or the CJK subset in fonts/) or if a string does not fit the screen.

The position in LANGS is the language number: -D IDIOMA_DEFECTO=<N> at build time, the serial
command l<N> at run time (saved in EEPROM 201). Spanish is the original design (do not change
a letter of it: the renders must stay identical); English and Simplified Chinese are translations.

Latin text is caps only (font5x7.py). Private glyphs of font5x7.py: \\x01 <-> (horizontal),
\\x02 up/down, \\x03 radial / rings, \\x05 rising bars. Chinese characters come from the CJK
subset (fonts/fusion-pixel-12px-zh_hans-subset.bdf): after adding new ones here, run
cjk_subset.py on the full font (see fonts/README.md), then gen_assets.py.
"""

LANGS = ["es", "en", "zh"]

# Name under the visor, by the project's faceArray NAME. A face missing here (OWO, the private
# image slots) or None in a language shows its faceArray name as is.
FACES = {
    #            es              en             zh
    "DEFAULT": ("NORMAL",       "NORMAL",      "默认"),
    "ANGRY":   ("ENOJO",        "ANGRY",       "生气"),
    "DOUBT":   ("DUDA",         "HMM?",        "疑惑"),
    "FROWN":   ("PUCHERO",      "POUT",        "委屈"),
    "LOOKUP":  ("ARRIBA",       "LOOK UP",     "抬头"),
    "SAD":     ("TRISTE",       "SAD",         "难过"),
    "BSOD":    ("PANTALLAZO",   "BLUESCREEN",  "蓝屏"),
    "LOWBAT":  ("PILA BAJA",    "LOW BATT",    "没电了"),
    "AUDIO1":  ("AUDIO \x03",   "AUDIO \x03",  "音频 \x03"),
    "AUDIO2":  ("AUDIO \x05",   "AUDIO \x05",  "音频 \x05"),
    "TACHA":   (None,           "RAVE",        "蹦迪"),
    "KAOMOJI": (None,           None,          "颜文字"),
    "KP140":   ("KAO ROSA",     "PINK KAO",    "粉色颜文字"),
    "DEAD":    ("MUERTITO",     "DEAD",        "挂了"),
    "AMOR":    (None,           "LOVE",        "心动"),
    "HAPPY":   ("FELIZ",        "HAPPY",       "开心"),
}

# Label of each setting while it is edited. Index = Menu::MenuState (0 = Faces is normal use and
# never shows an edit screen).
SETTINGS = [
    ("",             "",             ""),
    ("BRILLO",       "BRIGHTNESS",   "亮度"),
    ("LADOS",        "SIDES",        "侧灯"),
    ("MICRO",        "MIC",          "麦克风"),
    ("SENSIBILIDAD", "SENSITIVITY",  "灵敏度"),
    ("BOOP",         "BOOP",         "戳鼻子"),
    ("ESPEJO",       "MIRROR",       "镜像"),
    ("TAMAÑO",       "SIZE",         "大小"),
    ("COLOR",        "COLOR",        "颜色"),
    ("TONO FRENTE",  "FRONT HUE",    "前景色"),
    ("TONO FONDO",   "BACK HUE",     "背景色"),
    ("EFECTO",       "EFFECT",       "特效"),
    ("ABANICOS",     "FANS",         "风扇"),
]

# Menu::GetFaceColor(): GRADIENT, YELLOW, CYAN, WHITE, GREEN, PURPLE, RED, BLUE, RAINBOW, NOISE
COLORS = [
    ("DEGRADADO",  "GRADIENT", "渐变"),
    ("AMARILLO",   "YELLOW",   "黄色"),
    ("CIAN",       "CYAN",     "青色"),
    ("BLANCO",     "WHITE",    "白色"),
    ("VERDE",      "GREEN",    "绿色"),
    ("MORADO",     "PURPLE",   "紫色"),
    ("ROJO",       "RED",      "红色"),
    ("AZUL",       "BLUE",     "蓝色"),
    ("ARCOÍRIS",   "RAINBOW",  "彩虹"),
    ("NEBULOSA",   "NEBULA",   "星云"),
]

# HueF / HueB: red hue-shifted by v*36 deg (RGBColor::HueShift in UpdateFace) -> the colour you get
HUES = [
    ("ROJO",     "RED",    "红色"),
    ("NARANJA",  "ORANGE", "橙色"),
    ("AMARILLO", "YELLOW", "黄色"),
    ("LIMA",     "LIME",   "黄绿"),
    ("VERDE",    "GREEN",  "绿色"),
    ("CIAN",     "CYAN",   "青色"),
    ("AZUL",     "BLUE",   "蓝色"),
    ("ÍNDIGO",   "INDIGO", "靛蓝"),
    ("MORADO",   "PURPLE", "紫色"),
    ("ROSA",     "PINK",   "粉色"),
]

# Menu::GetEffectS(): NONE, PHASEY, PHASEX, PHASER, GLITCHX, MAGNET, FISHEYE, BLURH, BLURV, BLURR
EFFECTS = [
    ("NINGUNO",       "NONE",       "无"),
    ("ONDA \x02",     "WAVE \x02",  "波浪 \x02"),
    ("ONDA \x01",     "WAVE \x01",  "波浪 \x01"),
    ("ONDA \x03",     "WAVE \x03",  "波浪 \x03"),
    ("GLITCH",        "GLITCH",     "故障风"),
    ("IMÁN",          "MAGNET",     "磁铁"),
    ("LUPA",          "FISHEYE",    "鱼眼"),
    ("BORROSO \x01",  "BLUR \x01",  "模糊 \x01"),
    ("BORROSO \x02",  "BLUR \x02",  "模糊 \x02"),
    ("BORROSO \x03",  "BLUR \x03",  "模糊 \x03"),
]

# Loose words: C++ enum SoftWord = WORD_<key>, in this order.
WORDS = {
    "HELLO": ("¡HOLA!", "HELLO!", "你好！"),  # end of the boot animation (zh: full-width ！ of the CJK font)
    "FACE":  ("CARA",   "FACE",   "表情"),    # a face number out of range (old EEPROM value)
    "OFF":   ("NO",     "OFF",    "关"),      # switch, left of the pill
    "ON":    ("SÍ",     "ON",     "开"),      # switch, right of the pill
}
