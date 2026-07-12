#pragma once

// Keep the Retro-Go POV driver on the exact same profile parser and encoder
// as the MicroPython user module. The wrapper makes this shared source visible
// to the legacy ESP-IDF component build without copying it.
#include "../../../../../../hardware/rotor/modules/povdisplay/color_pipeline.h"
