#define HAVE_PTHREAD_SETNAME

#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <string>
#include <csignal>
#include <future>
#include <chrono>
#include <fcntl.h>
#include <atomic>
#include <uhd/utils/thread.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/variant/detail/substitute.hpp>
#include <sys/stat.h>
#include <gnuradio/types.h>

#include "src/colorize.hpp"
#include "src/config.hpp"
#include "src/buffer_wrapper.hpp"
#include "src/usrp_controller.hpp"

//region Settings
#define USE_NULL_SINK false
#define CAPTURE_ONE_SECOND false
//endregion

namespace po = boost::program_options;
namespace fs = boost::filesystem;

//void uhd::set_thread_name(
//        boost::thread *thrd,
//        const std::string &name
//) {
//#ifdef HAVE_PTHREAD_SETNAME
//    pthread_setname_np(thrd->native_handle(), name.substr(0,16).c_str());
//#endif /* HAVE_PTHREAD_SETNAME */
//#ifdef HAVE_THREAD_SETNAME_DUMMY
//    UHD_LOG_DEBUG("UHD", "Setting thread name is not implemented; wanted to set to " << name);
//#endif /* HAVE_THREAD_SETNAME_DUMMY */
//}

std::vector<std::string> device_serials = {};

std::vector<USRPController *> devices;

void create_device(
    std::string serial,
    double rate,
    double freq,
    double gain,
    std::string file_name
) {
    std::cout << "createdStart creating..." << std::endl;
    USRPController *device = new USRPController(serial, rate, freq, gain, file_name);
    devices.push_back(device);
    std::cout << "created!" << std::endl;
}

void watcher(
) {
    std::cout << "Watcher started" << std::endl;
    bool received_chunks_enough;
    while (not stop_signal_called){
        received_chunks_enough = true;
        for (USRPController *device : devices) {
            if (device->writes_count < RECEIVE_AND_DIE) received_chunks_enough = false;
        }
        if (RECEIVE_AND_DIE != 0 && received_chunks_enough){
            stop_signal_called = true;
            std::cout << "Written enough" << std::endl;
        }
        boost::this_thread::sleep(boost::posix_time::milliseconds(500));
    }
}

struct BandOpt {
    float sample_rate;
    float freq;
    fs::path targetFile;
};

int UHD_SAFE_MAIN(int argc, char *argv[]) {

    //БЛЯДЬ! Заебала эта буферизация
    std::cout.setf(std::ios::unitbuf);

    std::cerr << "v" << LLRECORDER_VERSION << std::endl;
    ////////////////////////////////////////////////////////////////////
    // Hi
    ////////////////////////////////////////////////////////////////////
    std::ostringstream string_stream;
    string_stream << bold << blue << "[LL Recorder" << def << "] ";
    std::string log_prepend = string_stream.str();

    uhd::set_thread_priority_safe();

//    std::cout << "gr complex " << sizeof(gr_complex) << std::endl;
//    std::cout << "std complex " << sizeof(std::complex<float>) << std::endl;
//    std::cout << "float " << sizeof(float) << std::endl;
    uhd::device_addrs_t device_addrs = uhd::device::find(std::string());
    for (auto it = device_addrs.begin(); it != device_addrs.end(); ++it) {
        std::string serial = (*it)["serial"];
        std::cout << log_prepend << "FOUND device " << serial << std::endl;
        device_serials.push_back(serial);
    }

    //region Play with gsm_header
//// uint8_t version;	/* version, set to GSMTAP_VERSION */
//    std::cout << sizeof(uint8_t) << std::endl;
//// uint8_t hdr_len;	/* length in number of 32bit words */
//    std::cout << sizeof(uint8_t) << std::endl;
//// uint8_t type;		/* see GSMTAP_TYPE_* */
//    std::cout << sizeof(uint8_t) << std::endl;
//// uint8_t timeslot;	/* timeslot (0..7 on Um) */
//    std::cout << sizeof(uint8_t) << std::endl;
////
//// uint16_t arfcn;		/* ARFCN (frequency) */
//    std::cout << sizeof(uint16_t) << std::endl;
//// int8_t signal_dbm;	/* signal level in dBm */
//    std::cout << sizeof(int8_t) << std::endl;
//// int8_t snr_db;		/* signal/noise ratio in dB */
//    std::cout << sizeof(int8_t) << std::endl;
////
//// uint32_t frame_number;	/* GSM Frame Number (FN) */
//    std::cout << sizeof(uint32_t) << std::endl;
////
//// uint8_t sub_type;	/* Type of burst/channel, see above */
//    std::cout << sizeof(int8_t) << std::endl;
//// uint8_t antenna_nr;	/* Antenna Number */
//    std::cout << sizeof(int8_t) << std::endl;
//// uint8_t sub_slot;	/* sub-slot within timeslot */
//    std::cout << sizeof(int8_t) << std::endl;
//// uint8_t res;		/* reserved for future use (RFU) */
//    std::cout << sizeof(int8_t) << std::endl;
//
    //endregion
//    return 0;

    std::signal(SIGINT, &sig_int_handler);
    std::signal(SIGHUP, &sig_int_handler);

    //region Parse options
    po::options_description desc("LLRecorder");

    std::string _dir;
    std::string _ab_mode;
    desc.add_options()
            ("opt", po::value<std::vector<std::string> >()->multitoken(), "description")
            ("dir", po::value<std::string>(&_dir)->default_value("./"), "name of the folder to write binary samples to")
            ("queue_size", po::value<uint>(&QUEUE_SIZE)->default_value(QUEUE_SIZE), "Queue size")
            ("recv_frame_size", po::value<uint>(&RECV_FRAME_SIZE)->default_value(RECV_FRAME_SIZE), "Recv frame size")
            ("num_recv_frames", po::value<uint>(&NUM_RECV_FRAMES)->default_value(NUM_RECV_FRAMES), "Num recv frames")
            ("verbose", po::value<bool>(&VERBOSE)->default_value(VERBOSE), "Print additional shit")
            ("ab_mode", po::value<std::string>(&_ab_mode)->default_value(""), "AB mode. Example: --ab_mode A:B:C")
            ("write_sleep", po::value<uint>(&WRITE_SLEEP_TIME)->default_value(WRITE_SLEEP_TIME), "Write sleep (milliseconds)")
            ("receive_and_die", po::value<uint>(&RECEIVE_AND_DIE)->default_value(RECEIVE_AND_DIE), "Receive frames and die");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    QUEUE_SIZE *= PARTS_PER_SECOND;

    if (_ab_mode.size() != 0) {
        std::vector<std::string> _ab_mode_strs;
        boost::split(_ab_mode_strs, _ab_mode, boost::is_any_of(":"));
        for (auto s: _ab_mode_strs) {
            if (s.size() != 1) {
                throw std::runtime_error(
                        "AB Mode error: '" + s + "' is invalid option. Only one symbol allowed, azaza.");
            }
            AB_MODE.push_back(s[0]);
        }
    }

    fs::path targetFolder = fs::path(_dir);
//    if (targetFolder.exs) {
//        throw std::runtime_error("Invalid opt dir: " + _dir);
//    }

    std::cout << log_prepend << "Trager folder is " << targetFolder << std::endl;

    std::vector<std::string> opts;
    if (vm["opt"].empty() || (opts = vm["opt"].as<std::vector<std::string> >()).size() == 0) {
        throw std::runtime_error("No opt provided");
    }

    if (opts.size() > device_serials.size()){
        throw std::runtime_error("Devices is not enough");
    }
    //endregion

    //regionDevice creation
    boost::thread_group create_threads_group;
    double gain(50);

    bool get_serial_from_opt = true;
    auto splitted_opts = std::vector<std::vector<std::string>>();
    for (std::string dev_opt : opts) {
        std::vector<std::string> strs;
        boost::split(strs, dev_opt, boost::is_any_of(":"));
        if (strs.size() != 4) get_serial_from_opt = false;
        if (!get_serial_from_opt && strs.size() != 3) {
            std::stringstream ss_err;
            ss_err << boost::format("Invalid option '%s', parts count: %s")
                      % dev_opt
                      % strs.size();
            throw std::runtime_error(ss_err.str());
        }
        splitted_opts.push_back(strs);
    }

    int device_serial_iterator = 0;
    for (auto strs : splitted_opts) {
        BandOpt bandOpt = BandOpt{stof(strs[0]), stof(strs[1]), fs::path(strs[2])};
        std::cout << log_prepend <<
                  boost::format("BandOpt freq %f, rate %f, file name %s")
                  % bandOpt.freq
                  % bandOpt.sample_rate
                  % bandOpt.targetFile
                  << std::endl;
        auto cr_th = create_threads_group.create_thread(boost::bind(
                &create_device,
                get_serial_from_opt ? strs[3] : device_serials[device_serial_iterator],
                bandOpt.sample_rate,
                bandOpt.freq,
                gain,
                (targetFolder / bandOpt.targetFile).string()
        ));
        //<<<<<<<<<<<<<<<<<<<<<<< uhd::set_thread_name(cr_th, "Creating");
        device_serial_iterator++;
    }

    std::cout << "wait for device creating..." << std::endl;

    create_threads_group.join_all();
//    create_threads_group.interrupt_all();
    std::cout << create_threads_group.size() << std::endl;

    std::cout << "all devices created" << std::endl;

    USRPController *common_dev = devices[0];
    uhd::time_spec_t clock_time = common_dev->device->get_time_now() + uhd::time_spec_t(1.0);
    for (USRPController *device : devices) {
        device->device->set_time_next_pps(uhd::time_spec_t(0.0));
    }

    boost::this_thread::sleep(boost::posix_time::seconds(2));

    uhd::time_spec_t start_timeout = common_dev->device->get_time_now() + uhd::time_spec_t(2.0);

    std::chrono::milliseconds ms = std::chrono::duration_cast< std::chrono::milliseconds >(
        std::chrono::system_clock::now().time_since_epoch()
    );
    std::cout << str(
        boost::format("<{\"action\": \"llrecorder_starts_at\", \"time\": %9i}\n")
        % (ms.count() + int(start_timeout.get_real_secs() * 1000))
        );

    for (USRPController *device : devices) {
        device->start(start_timeout);
    }

    boost::thread t{watcher};

    boost::thread_group work_threads_group;

    for (USRPController *device : devices) {
        auto th_r = work_threads_group.create_thread(boost::bind(&USRPController::read_thread, device));
        // <<<<<<<<<<<<<<<<<<<<< uhd::set_thread_name(th_r, "Reader");
        auto th_w = work_threads_group.create_thread(boost::bind(&USRPController::write_thread, device));
        // <<<<<<<<<<<<<<<<<<<<< uhd::set_thread_name(th_w, "Writer");
        auto th_sch = work_threads_group.create_thread(boost::bind(&USRPController::stream_runner, device));
        // <<<<<<<<<<<<<<<<<<<<< uhd::set_thread_name(th_sch, "Stream scheduler");
    }

    work_threads_group.join_all();
    //endregion

    return EXIT_SUCCESS;
}
