#pragma once

// Minimal zlib gzFile-API shim for MSX.c's #elif defined(ZLIB) fopen/fread/...
// remap (see MSX.c around line 2767). There's no real zlib in this build;
// gzfile_shim.c implements these on top of rg_storage_read_file() + the
// ESP32 ROM's miniz inflate, decompressing the whole file up front rather
// than streaming, since MSX cartridge/BIOS dumps are small enough to fit in
// PSRAM comfortably. Read-only: gzwrite() is a stub, fMSX's cartridge loader
// never writes through this path.

typedef void *gzFile;

gzFile gzopen(const char *path, const char *mode);
int gzread(gzFile file, void *buf, unsigned len);
int gzwrite(gzFile file, const void *buf, unsigned len);
char *gzgets(gzFile file, char *buf, int len);
long gzseek(gzFile file, long offset, int whence);
int gzrewind(gzFile file);
int gzgetc(gzFile file);
long gztell(gzFile file);
int gzeof(gzFile file);
int gzclose(gzFile file);
