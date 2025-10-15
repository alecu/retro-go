#pragma once
#include "doomtype.h"

void ventilastation_init();

void ventilastation_setup_projection();

void dump_vs_data(char dumpbuf[81]);

void project_angle(int angle, uint32_t row[54]);

void dump_vs_projected();

void vs_setup_projection_table();

void project_angle(int angle, uint32_t row[54]);