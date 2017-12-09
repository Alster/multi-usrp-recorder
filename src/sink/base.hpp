#include <iostream>

#ifndef INC_SINK_BASE
#define INC_SINK_BASE

class SinkBase {
public:
    virtual void write(const char *, std::streamsize) = 0;
    virtual std::string get_full_file_name() = 0;
    virtual bool is_open() = 0;
    virtual void close() = 0;
};

#endif