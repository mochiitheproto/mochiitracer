"""The firmware's faceArray (ProtogenHUB75Project.h), in the same order: a face number in a
scenario, the simulator's simFaceArray (src/oledsim_main.cpp) and the fake head
(tests/fake_teensy/fake_teensy.cpp) all go by this index. Keep the four in step."""

FACES = ["DEFAULT", "ANGRY", "DOUBT", "FROWN", "LOOKUP", "SAD", "BSOD", "LOWBAT", "AUDIO1",
         "AUDIO2", "TACHA", "KAOMOJI",
         "KP140",
         "DEAD", "AMOR", "OWO", "HAPPY"]

# LED mask of a face (what EnableBitFaceRender sees), by name. masks/caraNN_0.txt are captures
# of the real head (NN = the slot it had then), scenarios/ref_caraNN.txt reference renders.
# Image/video slots have none.
CAPTURED_MASKS = {"DEFAULT": "cara00_0.txt", "DEAD": "cara16_0.txt", "AMOR": "cara17_0.txt",
                  "OWO": "cara18_0.txt", "HAPPY": "cara19_0.txt"}
REFERENCE_MASKS = {"ANGRY": "ref_cara01.txt", "DOUBT": "ref_cara02.txt", "FROWN": "ref_cara03.txt",
                   "LOOKUP": "ref_cara04.txt", "SAD": "ref_cara05.txt", "TACHA": "ref_cara10.txt"}
