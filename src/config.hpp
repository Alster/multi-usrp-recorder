#ifndef INC_CONFIG
#define INC_CONFIG

#define LLRECORDER_VERSION "1.0"

#define DEBUG false

typedef float DATA_TYPE;
uint PARTS_PER_SECOND = 1;
uint WRITE_SLEEP_TIME = 100 / PARTS_PER_SECOND;
uint RECV_FRAME_SIZE = 6000;//4000
//default 16
uint NUM_RECV_FRAMES = 128;//500
bool VERBOSE = false;
uint QUEUE_SIZE = 4;

#endif