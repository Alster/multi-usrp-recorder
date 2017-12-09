#include <iostream>

#ifndef INC_COLORIZE_BASE
#define INC_COLORIZE_BASE

namespace Color {
    enum Code {
        FG_RED = 31,
        FG_GREEN = 32,
        FG_BLUE = 34,
        FG_DEFAULT = 39,
        FG_YELLOW = 93,
        BG_RED = 41,
        BG_GREEN = 42,
        BG_BLUE = 44,
        BG_DEFAULT = 49,
        S_BOLD = 1,
        S_DEFAULT = 0
    };

    class Modifier {
        Code code;
    public:
        Modifier(Code pCode) : code(pCode) {}

        friend std::ostream &
        operator<<(std::ostream &os, const Modifier &mod) {
            return os << "\033[" << mod.code << "m";
        }
    };
}

Color::Modifier red(Color::FG_RED);
Color::Modifier green(Color::FG_GREEN);
Color::Modifier blue(Color::FG_BLUE);
Color::Modifier def(Color::S_DEFAULT);
Color::Modifier bold(Color::S_BOLD);
Color::Modifier yellow(Color::FG_YELLOW);
Color::Modifier bg_red(Color::BG_RED);
Color::Modifier bg_def(Color::BG_DEFAULT);

#endif