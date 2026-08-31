#ifndef SAS_OODLE_H
#define SAS_OODLE_H

#include <cstddef>

bool Oodle_init();
size_t Oodle_decompress(const void* src, size_t srcLen, void* dst, size_t dstLen);

#endif // SAS_OODLE_H
