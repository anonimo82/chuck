#pragma once
#include <cstdio>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#endif

namespace mediaio {
// Binary read/write descriptor permits libsndfile's descriptor-based output.
// Never truncates an existing file.
inline FILE *openNew(const char *path) {
#ifdef _WIN32
    const int fd = _open(path, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY,
                         _S_IREAD | _S_IWRITE);
    if(fd < 0) return nullptr;
    FILE *f = _fdopen(fd, "w+b");
    if(!f) { _close(fd); std::remove(path); }
    return f;
#else
    return std::fopen(path, "w+bx");
#endif
}
}
