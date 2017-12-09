#include "./base.hpp"

#ifndef INC_SINK_NULL
#define INC_SINK_NULL

class SinkNull: public SinkBase {
public:
    SinkNull(std::string file_name) {
    }

    void write(const char *__s, std::streamsize __n) {
    }

    bool is_open() {
        return true;
    }

    void close() {
    }

    std::string get_full_file_name() {
        return "NULL SINK";
    }
};

#endif