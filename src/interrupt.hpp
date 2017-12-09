#include <csignal>
#include "./colorize.hpp"

#ifndef INC_INTERRUPT
#define INC_INTERRUPT

static bool stop_signal_called = false;


void sig_int_handler(int) {
    std::cout << blue << "SIG INT HANDLED" << def << std::endl;
    stop_signal_called = true;
    std::exit(EXIT_SUCCESS);
}

#endif