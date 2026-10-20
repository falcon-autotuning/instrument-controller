#include <boost/asio/ip/tcp.hpp>
#include <boost/process.hpp>
#include <cctype>
#include <chrono>
#include <cmath>
#include <falcon-comms/natsManager.hpp>
#include <falcon-comms/runtime_comms.hpp>
#include <falcon-core/generic/Map.hpp>
#include <falcon-core/instrument_interfaces/Waveform.hpp>
#include <falcon-core/instrument_interfaces/names/InstrumentPort.hpp>
#include <falcon-core/math/Axes.hpp>
#include <falcon-core/physics/device_structures/Connection.hpp>
#include <falcon-routine/hub.hpp>
#include <filesystem>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <arpa/inet.h>
#include <csignal>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace falcon::routine;
using namespace falcon_core::physics::device_structures;
using namespace falcon_core::instrument_interfaces::names;
using namespace falcon_core::communications::messages;
using namespace falcon_core::physics::config;
using namespace falcon_core::physics::units;
using namespace falcon_core::physics::config::core;
using namespace falcon_core::instrument_interfaces;
using namespace falcon_core::instrument_interfaces::port_transforms;
using namespace falcon_core::instrument_interfaces::names;
using namespace falcon_core::autotuner_interfaces::names;
using namespace falcon_core::math::domains;
using namespace falcon_core::generic;
namespace bp = boost::process::v2;
const int TIMEOUT_MS = 5000;

double FloatEchoTolerance(double expected_value) {
  const double magnitude = std::abs(expected_value);
  const double scale = magnitude > 1.0 ? magnitude : 1.0;
  return std::numeric_limits<float>::epsilon() * scale;
}

namespace fs = std::filesystem;
const fs::path DATA_1D_DIR = fs::path(__FILE__).parent_path();
const fs::path TEST_ROOT_DIR = DATA_1D_DIR.parent_path();
const fs::path TEST_DATA_DIR_PATH = TEST_ROOT_DIR / "test-data";
const fs::path TEST_DATA_FILE = TEST_DATA_DIR_PATH / "linear-1d.txt";

// These will be initialized in SetUp() since they depend on environment
// variables
fs::path VCPKG_BIN_DIR;
fs::path VCPKG_LIB_DIR;
fs::path LUA_LIB_DIR;

const fs::path INSTRUMENT_APIS_DIR =
    TEST_ROOT_DIR / fs::path("instrument-apis");
const fs::path TEAL_APIS_DIR = TEST_ROOT_DIR / "teal";
const fs::path LUA_SCRIPTS_DIR = TEST_ROOT_DIR / "lua";
const fs::path DATA_DIR = TEST_ROOT_DIR / "data";
const fs::path MEASUREMENT_SCRIPTS_DIR = DATA_1D_DIR / "measurement-scripts";
const fs::path INSTRUMENT_LUA_LIBS_DIR = TEST_ROOT_DIR / "instrument-lua-libs";
const fs::path WORKING_DIR = TEST_ROOT_DIR / "hub";
const fs::path TEST_RUNS_DIR = TEST_ROOT_DIR / "test-runs";
const double ABS_ERROR = 1e-4;

class DataRetrievalTest : public ::testing::Test {
protected:
  fs::path current_run_dir_;
  fs::path current_working_dir_;
  fs::path current_data_dir_;
  fs::path current_config_path_;
  fs::path current_scripts_dir_ = LUA_SCRIPTS_DIR;
  fs::path current_tmp_dir_;
  std::string previous_tmpdir_;
  std::string previous_tmp_;
  std::string previous_temp_;
  std::string previous_iss_rpc_port_;
  bool iss_rpc_port_configured_ = false;
  bool had_iss_rpc_port_ = false;
  bool had_tmpdir_ = false;
  bool had_tmp_ = false;
  bool had_temp_ = false;
  PortsSP published_knobs_;
  PortsSP published_meters_;
  PortsSP published_settings_;
  bool buffered_measurement_ = false;

  void SetUp() override {
    // Initialize paths from environment variables here, not at global scope
    const char *vcpkg_installed = std::getenv("VCPKG_INSTALLED_DIR");
    const char *vcpkg_triplet = std::getenv("VCPKG_TRIPLET");

    if ((vcpkg_installed == nullptr) || ((*vcpkg_installed) == 0) ||
        (vcpkg_triplet == nullptr) || ((*vcpkg_triplet) == 0)) {
      FAIL() << "Required environment variables not set: VCPKG_INSTALLED_DIR "
                "or VCPKG_TRIPLET";
    }
    std::cout << "Using vcpkg installed dir: " << vcpkg_installed << '\n';
    std::cout << "Using vcpkg triplet: " << vcpkg_triplet << '\n';

    VCPKG_BIN_DIR = fs::path(vcpkg_installed) / vcpkg_triplet / "bin";
    VCPKG_LIB_DIR = fs::path(vcpkg_installed) / vcpkg_triplet / "lib";
    LUA_LIB_DIR = VCPKG_LIB_DIR / "lua";
    fs::create_directories(LUA_SCRIPTS_DIR);
    fs::create_directories(TEST_RUNS_DIR);

    current_run_dir_ = MakeCurrentRunDir();
    current_working_dir_ = current_run_dir_ / "hub";
    current_data_dir_ = current_run_dir_ / "data";
    current_config_path_ = current_run_dir_ / "test-config.yaml";
    current_tmp_dir_ = current_run_dir_ / "tmp";

    fs::create_directories(current_working_dir_);
    fs::create_directories(current_data_dir_);
    fs::create_directories(current_tmp_dir_);

    ConfigurePerTestTempDir();
    BuildTestData(TEST_DATA_DIR_PATH / "gen_data.cpp",
                  TEST_DATA_DIR_PATH / "gen_data");
    RunTestDataGenerator(TEST_DATA_DIR_PATH / "gen_data");
    const std::string test_name =
        ::testing::UnitTest::GetInstance()->current_test_info()->name();
    const bool only_1D = test_name == "measure_2D_buffered_with_only_1D";
    buffered_measurement_ = test_name == "measure_1D_buffered" ||
                            test_name == "measure_2D_buffered" || only_1D;
    const bool settings_test = test_name == "set_then_get_then_set_and_get";
    if (test_name == "measure_set_get" || buffered_measurement_ ||
        settings_test) {
      current_scripts_dir_ = current_run_dir_ / "scripts";
      fs::create_directories(current_scripts_dir_);
      // Settings requests need only get/set scripts. Measurement tests stage
      // just their selected primitive, preventing lower-dimensional fallback.
      if (!settings_test) {
        const std::string script = only_1D
                                       ? "measure_1D_buffered.lua"
                                       : (buffered_measurement_
                                              ? test_name + ".lua"
                                              : "measure_get_set.lua");
        fs::copy_file(MEASUREMENT_SCRIPTS_DIR / script,
                      current_scripts_dir_ / script);
      }
      for (const char *action : {"get", "set"}) {
        fs::copy_file(DATA_1D_DIR / "settings-scripts" /
                          (std::string(action) + "_source_voltage.lua"),
                      current_scripts_dir_ /
                          (std::string(action) + "_source_voltage.lua"));
      }
      if (buffered_measurement_) {
        fs::copy_file(DATA_1D_DIR /
                          "settings-scripts/get_number_of_samples.lua",
                      current_scripts_dir_ / "get_number_of_samples.lua");
        fs::create_directories(current_run_dir_ / "lua-utils");
        fs::copy_file(DATA_1D_DIR / "lua-utils/sweep_math.lua",
                      current_run_dir_ / "lua-utils/sweep_math.lua");
      }
    }
    WriteConfigFile(current_config_path_);
    ExtendInstrumentApis(
        (INSTRUMENT_APIS_DIR / "multimeter-api.yml.tmpl").string(),
        (INSTRUMENT_APIS_DIR / "generated-multimeter-api.yml").string());
    ExtendInstrumentApis(
        (INSTRUMENT_APIS_DIR / "source-api.yml.tmpl").string(),
        (INSTRUMENT_APIS_DIR / "generated-source-api.yml").string());

    // Ensure spawned hub/ISS processes inherit test-specific runtime variables.
    // Channel 1 (O1) receives Gaussian data; channel 2 (O2) receives linear
    // data.
    std::string multimeter_data_files =
        TEST_DATA_FILE.string() + ";" +
        (TEST_DATA_DIR_PATH / "linear-1d.txt").string();
    setenv("MOCK_MULTIMETER_DATA_FILE", multimeter_data_files.c_str(), 1);
    setenv("NATS_URL", "nats://localhost:4222", 1);

    // The ISS CLI defaults to a different port than this fixture's hub config.
    // Save the caller's value and target the test daemon for start/stop
    // commands.
    if (const char *value = std::getenv("INSTRUMENT_SCRIPT_SERVER_RPC_PORT")) {
      had_iss_rpc_port_ = true;
      previous_iss_rpc_port_ = value;
    }
    setenv("INSTRUMENT_SCRIPT_SERVER_RPC_PORT", "5555", 1);
    iss_rpc_port_configured_ = true;
    // Remove a daemon left by an earlier run before the hub can attach to it.
    // Fresh plugin initialization loads this test's data and resets its cursor.
    StopInstrumentServer();
    StartInstrumentHub(VCPKG_BIN_DIR / "instrument-hub", current_config_path_);
    WaitForHubReady(10000); // Wait for hub to finish setting up handlers
    LoadPublishedPorts();
    std::cout << "Setup complete, starting test" << '\n';
  }

  void TearDown() override {
    StopInstrumentHub();
    if (iss_rpc_port_configured_) {
      // Also stop daemons the hub did not own, including after a test failure.
      EXPECT_NO_THROW(StopInstrumentServer());
      if (had_iss_rpc_port_) {
        setenv("INSTRUMENT_SCRIPT_SERVER_RPC_PORT",
               previous_iss_rpc_port_.c_str(), 1);
      } else {
        unsetenv("INSTRUMENT_SCRIPT_SERVER_RPC_PORT");
      }
    }
    unsetenv("MOCK_MULTIMETER_DATA_FILE");
    unsetenv("NATS_URL");
    RestorePerTestTempDir();
    // TODO: Uncomment these when no more debugging necessary - currently we
    // want to inspect the generated files after the test runs
    //   fs::remove(TEST_DATA_FILE);
    //   fs::remove(TEST_DATA_DIR_PATH / "gen_data");
    //   fs::remove(INSTRUMENT_APIS_DIR / "generated-multimeter-api.yml");
    //   fs::remove(INSTRUMENT_APIS_DIR / "generated-source-api.yml");
    //   fs::remove_all(TEAL_APIS_DIR);
    //   fs::remove_all(LUA_SCRIPTS_DIR);
    //   fs::remove_all(DATA_DIR);
    //   fs::remove_all(INSTUMENT_LUA_LIBS_DIR);
    //   fs::remove_all(current_run_dir_);
  }
  static fs::path MakeCurrentRunDir() {
    const ::testing::TestInfo *test_info =
        ::testing::UnitTest::GetInstance()->current_test_info();
    std::string test_name =
        (test_info != nullptr) ? test_info->name() : "unknown_test";
    std::string suite_name =
        (test_info != nullptr) ? test_info->test_suite_name() : "unknown_suite";
    std::string slug = suite_name + "_" + test_name;
    for (char &ch : slug) {
      auto uch = static_cast<unsigned char>(ch);
      if ((std::isalnum(uch) == 0) && ch != '_' && ch != '-') {
        ch = '_';
      }
    }

#ifdef _WIN32
    int pid = static_cast<int>(GetCurrentProcessId());
#else
    int pid = static_cast<int>(getpid());
#endif
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    return TEST_RUNS_DIR /
           (slug + "_" + std::to_string(pid) + "_" + std::to_string(now));
  }
  void ConfigurePerTestTempDir() {
    if (const char *value = std::getenv("TMPDIR")) {
      had_tmpdir_ = true;
      previous_tmpdir_ = value;
    }
    if (const char *value = std::getenv("TMP")) {
      had_tmp_ = true;
      previous_tmp_ = value;
    }
    if (const char *value = std::getenv("TEMP")) {
      had_temp_ = true;
      previous_temp_ = value;
    }

    std::string tmp = current_tmp_dir_.string();
    setenv("TMPDIR", tmp.c_str(), 1);
    setenv("TMP", tmp.c_str(), 1);
    setenv("TEMP", tmp.c_str(), 1);
  }
  void RestorePerTestTempDir() {
    if (had_tmpdir_) {
      setenv("TMPDIR", previous_tmpdir_.c_str(), 1);
    } else {
      unsetenv("TMPDIR");
    }

    if (had_tmp_) {
      setenv("TMP", previous_tmp_.c_str(), 1);
    } else {
      unsetenv("TMP");
    }

    if (had_temp_) {
      setenv("TEMP", previous_temp_.c_str(), 1);
    } else {
      unsetenv("TEMP");
    }
  }
  void StartInstrumentHub(const std::filesystem::path &hub_executable,
                          const std::filesystem::path &config_path) {
    std::vector<std::string> args = {
        hub_executable.string(),
        "--config",
        config_path.string(),
    };

    std::vector<char *> argv;
    argv.reserve(args.size() + 1);

    for (std::string &arg : args) {
      argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

#ifdef _WIN32
    // Join arguments for Windows command line
    std::string cmd;
    for (size_t i = 0; i < args.size(); ++i) {
      if (i > 0)
        cmd += " ";
      cmd += args[i];
    }
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, cmd.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si,
                        &pi)) {
      std::cerr << "Failed to start Instrument Hub, error: " << GetLastError()
                << std::endl;
      std::exit(1);
    }
    hub_process_info_ = pi;
#else
    pid_t pid = fork();
    if (pid == 0) {
      if (buffered_measurement_) {
        std::string helpers = (current_run_dir_ / "lua-utils").string();
        if (const char *existing =
                std::getenv("INSTRUMENT_SCRIPT_SERVER_OPT_LUA_LIB")) {
          helpers += ";" + std::string(existing);
        }
        setenv("INSTRUMENT_SCRIPT_SERVER_OPT_LUA_LIB", helpers.c_str(), 1);
      }
      execv(argv.front(), argv.data());
      std::cerr << "Failed to exec Instrument Hub\n";
      std::exit(1);
    }
    if (pid > 0) {
      hub_pid_ = pid;
    } else {
      std::cerr << "Failed to fork for Instrument Hub\n";
      std::exit(1);
    }
#endif
  }
  void StopInstrumentServer() {
    auto port_open = [] {
      boost::asio::io_context ctx;
      boost::asio::ip::tcp::socket socket(ctx);
      boost::system::error_code error;
      socket.connect({boost::asio::ip::address_v4::loopback(), 5555}, error);
      return !error;
    };
    if (!port_open()) {
      return; // The hub may already have stopped the daemon it started.
    }

    boost::asio::io_context ctx;
    bp::process proc(ctx, (VCPKG_BIN_DIR / "instrument-script-server").string(),
                     {"daemon", "stop"});
    proc.wait();
    // The stop RPC returns before asynchronous shutdown finishes. Do not let
    // the next test attach until the daemon has released its listening port.
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (port_open()) {
      if (std::chrono::steady_clock::now() >= deadline) {
        throw std::runtime_error(
            "ISS did not stop on port 5555; daemon stop exit code: " +
            std::to_string(proc.exit_code()));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
  void StopInstrumentHub() {
#ifdef _WIN32
    if (hub_process_info_.hProcess) {
      TerminateProcess(hub_process_info_.hProcess, 0);
      CloseHandle(hub_process_info_.hProcess);
      CloseHandle(hub_process_info_.hThread);
      hub_process_info_ = {};
    }
#else
    if (hub_pid_ > 0) {
      kill(hub_pid_, SIGTERM);
      // Wait for the hub to fully exit so its cleanup (stopISSDaemon) completes
      // before the next test run tries to start a new daemon. Without this,
      // a stale ISS daemon from the previous run causes "Instrument already
      // exists" errors in subsequent test runs.
      int status = 0;
      waitpid(hub_pid_, &status, 0);
      hub_pid_ = -1;
    }
#endif
  }

  void WriteConfigFile(const fs::path &configLocation) {
    std::ofstream ofs(configLocation);

    ofs << "wiremap:\n";

    auto addGate = [&](const char *gate, const char *instrument, int index) {
      ofs << "- name: " << gate << "\n";
      ofs << "  instrument:\n";
      ofs << "    name: " << instrument << "\n";
      ofs << "    channel_group: analog\n";
      ofs << "    index: " << index << "\n";
    };

    addGate("S1", "Source1", 1);
    addGate("S2", "Source1", 2);
    addGate("S3", "Source1", 3);
    addGate("P1", "Source1", 4);
    addGate("P2", "Source1", 5);
    addGate("P3", "Source1", 6);
    addGate("B1", "Source1", 7);
    addGate("B2", "Source1", 8);
    addGate("B3", "Source1", 9);
    addGate("B4", "Source1", 10);
    addGate("B5", "Source1", 11);
    addGate("R1", "Source1", 12);
    addGate("R2", "Source1", 13);
    addGate("R3", "Source1", 14);
    addGate("R4", "Source1", 15);
    addGate("O1", "Meter1", 1);
    addGate("O2", "Meter1", 2);
    addGate("O3", "Meter1", 3);
    addGate("O4", "Meter1", 4);

    ofs << "\n";
    ofs << "quantum-dot-config: "
        << (TEST_ROOT_DIR / "device-configs" / "2-dot-1-chargesensor.yml")
               .string()
        << "\n";

    ofs << "initial-voltage-state: []\n";

    ofs << "nats:\n";
    ofs << "  host: 127.0.0.1\n";
    ofs << "  port: 4222\n";

    ofs << "database:\n";
    ofs << "  local-database: " << current_data_dir_.string() << "\n";
    ofs << "  local-cache: " << current_data_dir_.string() << "/cache\n";
    ofs << "  owner: integration-test\n";

    ofs << "working-directory: " << current_data_dir_.string() << "\n";

    ofs << "user-measurement-luas: " << current_scripts_dir_.string() << "\n";

    ofs << "instrument-server:\n";
    ofs << "  rpc-port: 5555\n";
    ofs << "  autostart: true\n";
    ofs << "  instruments:\n";

    ofs << "    - config: " << (DATA_1D_DIR / "source-config.yml").string()
        << "\n";
    ofs << "      plugin: "
        << (fs::path(PLUGIN_OUTPUT_DIR) / "mock_voltage_source.so").string()
        << "\n";
    ofs << "      type: dc_voltage_source\n";
    ofs << "      characteristics:\n";
    ofs << "        - identifier: source_voltage\n";
    ofs << "          readCommand:\n";
    ofs << "            command: GET_VOLTAGE\n";
    ofs << "          writeCommand:\n";
    ofs << "            command: SET_VOLTAGE\n";

    ofs << "    - config: " << (DATA_1D_DIR / "multimeter-config.yml").string()
        << "\n";
    ofs << "      plugin: "
        << (fs::path(PLUGIN_OUTPUT_DIR) / "mock_multimeter.so").string()
        << "\n";
    ofs << "      type: voltmeter\n";
    ofs << "      characteristics:\n";
    ofs << "        - identifier: sample_rate\n";
    ofs << "          readCommand: {}\n";
    ofs << "          writeCommand:\n";
    ofs << "            command: SET_SAMPLE_RATE\n";
    ofs << "        - identifier: number_of_samples\n";
    ofs << "          readCommand:\n";
    ofs << "            command: GET_BINS\n";
    ofs << "          writeCommand:\n";
    ofs << "            command: SET_BINS\n";

    ofs.close();
  }
  static void BuildTestData(const std::string &cpp_path,
                            const std::string &out_base) {
#ifdef _WIN32
    std::string out_path = out_base + ".exe";
    const char *compiler = std::getenv("CXX");
    std::string cmd = (compiler ? compiler : "clang-cl");
    cmd += " /EHsc /Fe:" + out_path + " " + cpp_path;
#else
    const std::string &out_path = out_base;
    const char *compiler = std::getenv("CXX");
    std::string cmd = ((compiler != nullptr) ? compiler : "clang++");
    cmd += " -std=c++17 -o " + out_path + " " + cpp_path;
#endif
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
      std::cerr << "Build failed with code " << ret << '\n';
      std::exit(1);
    }
  }

  static void RunTestDataGenerator(const std::string &out_base) {
#ifdef _WIN32
    std::string exe_path = out_base + ".exe";
#else
    const std::string &exe_path = out_base;
#endif

    boost::asio::io_context ctx;
    bp::process proc(ctx, exe_path, {out_base});
    proc.wait();
    if (proc.exit_code() != 0) {

      std::cerr << "Data generation failed with code " << proc.exit_code()
                << '\n';
      std::exit(1);
    }
  }

  static void ExtendInstrumentApis(const std::string &local_path,
                                   const std::string &out_path) {
    std::string out_base = VCPKG_BIN_DIR / "template-expander";
#ifdef _WIN32
    std::string exe_path = out_base + ".exe";
#else
    const std::string &exe_path = out_base;
#endif

    boost::asio::io_context ctx;
    bp::process proc(ctx, exe_path, {local_path, out_path});
    proc.wait();
    if (proc.exit_code() != 0) {
      std::cerr << "API extension failed with code " << proc.exit_code()
                << '\n';
      std::exit(1);
    }
  }
  void LoadPublishedPorts() {
    falcon::comms::RuntimeComms comms;
    const auto payload = comms.subscribe_port_payload(TIMEOUT_MS, 0);
    published_knobs_ = Ports::from_json_string<Ports>(payload.knobs);
    published_meters_ = Ports::from_json_string<Ports>(payload.meters);
    published_settings_ = Ports::from_json_string<Ports>(payload.settings);
    if (!published_knobs_ || !published_meters_) {
      throw std::runtime_error("Hub PORT_PAYLOAD did not contain Ports");
    }
  }
  static InstrumentPortSP PublishedPort(const ConnectionSP &connection,
                                        const PortsSP &ports,
                                        const PortType kind) {
    if (!connection) {
      throw std::runtime_error("PublishedPort requires a connection");
    }
    if (ports) {
      for (const auto &port : ports->items()) {
        if (*port->pseudo_name() == *connection) {
          return port;
        }
      }
    }
    throw std::runtime_error("No hub-published " + std::string(ToString(kind)) +
                             " for connection " + connection->name());
  }
  [[nodiscard]] InstrumentPortSP
  PublishedKnob(const ConnectionSP &connection) const {
    return PublishedPort(connection, published_knobs_, PortType::Knob);
  }
  [[nodiscard]] InstrumentPortSP
  PublishedMeter(const ConnectionSP &connection) const {
    return PublishedPort(connection, published_meters_, PortType::Meter);
  }
  [[nodiscard]] InstrumentPortSP
  PublishedSetting(const ConnectionSP &connection,
                   const InstrumentCharacteristic category) const {
    if (!connection) {
      throw std::runtime_error("PublishedPort requires a connection");
    }
    if (published_settings_) {
      for (const auto &port : published_settings_->items()) {
        if (*port->pseudo_name() == *connection &&
            port->characteristic() == category) {
          return port;
        }
      }
    }
    throw std::runtime_error("No hub-published setting for connection " +
                             connection->name());
  }
  static MeasurementRequestSP
  MakeSinglePointRequest(const std::string &message,
                         const InstrumentPortSP &target, double target_value) {
    InstrumentPortSP clock = InstrumentPort::ExecutionClock();
    MapSP<InstrumentPort, PortTransform> transforms =
        std::make_shared<Map<InstrumentPort, PortTransform>>(
            std::vector<std::pair<InstrumentPortSP, PortTransformSP>>{
                {target, PortTransform::IdentityTransform(target)},
            });
    LabelledDomainSP time_domain =
        LabelledDomain::from_port(std::pair<double, double>{0.0, 0.0}, clock);
    LabelledDomainSP target_domain = LabelledDomain::from_port(
        std::pair<double, double>{target_value, target_value}, target);
    CoupledLabelledDomainSP coupled_domain =
        std::make_shared<CoupledLabelledDomain>(
            std::vector<LabelledDomainSP>{target_domain});
    MapSP<std::string, bool> increasing =
        std::make_shared<Map<std::string, bool>>(
            std::vector<std::pair<std::string, bool>>{
                {target->default_name(), true}});
    auto waveform =
        Waveform::CartesianIdentityWaveform1D(1, coupled_domain, increasing);
    ListSP<Waveform> waveforms =
        std::make_shared<List<Waveform>>(std::vector<WaveformSP>{waveform});

    return std::make_shared<MeasurementRequest>(
        message, waveforms, std::make_shared<Ports>(), transforms, time_domain);
  }
  MeasurementRequestSP MakeTargetOnlyRequest(const std::string &message,
                                             const InstrumentPortSP &target) {
    return MakeSinglePointRequest(message, target, 1.0);
  }
  static MeasurementRequestSP
  MakeGetterOnlyRequest(const std::string &message,
                        const InstrumentPortSP &target) {
    return MakeGetterOnlyRequest(message,
                                 std::vector<InstrumentPortSP>{target});
  }
  static SettingRequestSP MakeSettingRequest(
      const std::string &message,
      const instrument_interfaces::names::PortsSP &getters,
      const generic::MapSP<instrument_interfaces::names::InstrumentPort,
                           math::Quantity> &setters) {
    return std::make_shared<SettingRequest>(message, getters, setters);
  }
  static MeasurementRequestSP
  MakeGetterOnlyRequest(const std::string &message,
                        const std::vector<InstrumentPortSP> &targets) {
    InstrumentPortSP clock = InstrumentPort::ExecutionClock();
    ListSP<Waveform> waveforms =
        std::make_shared<List<Waveform>>(std::vector<WaveformSP>{});
    PortsSP getters = std::make_shared<Ports>(targets);
    MapSP<InstrumentPort, PortTransform> transforms =
        std::make_shared<Map<InstrumentPort, PortTransform>>(
            std::vector<std::pair<InstrumentPortSP, PortTransformSP>>{});
    LabelledDomainSP time_domain =
        LabelledDomain::from_port(std::pair<double, double>{0.0, 0.0}, clock);
    return std::make_shared<MeasurementRequest>(message, waveforms, getters,
                                                transforms, time_domain);
  }
  static MeasurementRequestSP MakeSinglePointGetterRequest(
      const std::string &message, const InstrumentPortSP &setter,
      double setter_value, const std::vector<InstrumentPortSP> &getters) {
    InstrumentPortSP clock = InstrumentPort::ExecutionClock();
    MapSP<InstrumentPort, PortTransform> transforms =
        std::make_shared<Map<InstrumentPort, PortTransform>>(
            std::vector<std::pair<InstrumentPortSP, PortTransformSP>>{
                {setter, PortTransform::IdentityTransform(setter)}});
    LabelledDomainSP time_domain =
        LabelledDomain::from_port(std::pair<double, double>{0.0, 0.0}, clock);
    LabelledDomainSP target_domain = LabelledDomain::from_port(
        std::pair<double, double>{setter_value, setter_value}, setter);
    CoupledLabelledDomainSP coupled_domain =
        std::make_shared<CoupledLabelledDomain>(
            std::vector<LabelledDomainSP>{target_domain});
    MapSP<std::string, bool> increasing =
        std::make_shared<Map<std::string, bool>>(
            std::vector<std::pair<std::string, bool>>{
                {setter->instrument_facing_name(), true}});
    auto waveform =
        Waveform::CartesianIdentityWaveform1D(1, coupled_domain, increasing);
    ListSP<Waveform> waveforms =
        std::make_shared<List<Waveform>>(std::vector<WaveformSP>{waveform});

    return std::make_shared<MeasurementRequest>(
        message, waveforms, std::make_shared<Ports>(getters), transforms,
        time_domain);
  }
  void ExpectBufferedMeasurement(const std::vector<ConnectionSP> &connections,
                                 const std::vector<int> &points) {
    // Each source connection supplies one independent voltage axis. points[i]
    // is the number of positions along that axis, e.g. {4} or {3, 2}.
    ASSERT_EQ(connections.size(), points.size());

    // O1 publishes both scalar-voltage and stream ports. Select the stream
    // metadata supplied by the hub so the request identifies a buffer getter.
    const auto getter_connection = Connection::Ohmic("O1");
    InstrumentPortSP getter;
    for (const auto &port : published_meters_->items()) {
      if (*port->pseudo_name() == *getter_connection &&
          port->default_name().find(".stream") != std::string::npos) {
        getter = port;
        break;
      }
    }
    ASSERT_NE(getter, nullptr) << "No buffer stream port published for O1";

    std::vector<CoupledLabelledDomainSP> domains;
    std::vector<MapSP<std::string, bool>> increasing;
    auto transforms = std::make_shared<Map<InstrumentPort, PortTransform>>();
    size_t sample_count = 1;
    for (size_t axis = 0; axis < connections.size(); ++axis) {
      const auto setter = PublishedKnob(connections[axis]);
      // Label the voltage range with its source port. A coupled domain can
      // describe several sources moving together; this test uses one per axis.
      domains.push_back(std::make_shared<CoupledLabelledDomain>(
          std::vector<LabelledDomainSP>{LabelledDomain::from_port(
              std::pair<double, double>{-0.1, 0.1}, setter)}));
      auto direction = std::make_shared<Map<std::string, bool>>();
      // Direction keys use device connection names (S1/S2), as expected by
      // falcon-core and the hub planner, rather than fully qualified port
      // names.
      direction->insert(setter->instrument_facing_name(), true);
      increasing.push_back(direction);
      // Identity transforms leave the requested voltages unchanged.
      transforms->insert(setter, PortTransform::IdentityTransform(setter));
      // The mock meter starts with one sample per buffer, so the total number
      // of returned samples is the product of the axis lengths.
      sample_count *= static_cast<size_t>(points[axis] + 1);
    }

    // One waveform with all axes; two separate 1D waveforms would not test
    // the hub's 2D primitive.
    const auto waveform = Waveform::CartesianIdentityWaveform(
        std::make_shared<falcon_core::math::Axes<int>>(points),
        std::make_shared<falcon_core::math::Axes<CoupledLabelledDomain>>(
            domains),
        std::make_shared<falcon_core::math::Axes<Map<std::string, bool>>>(
            increasing));
    // MeasurementRequest also requires a labelled time domain. This clock
    // describes execution time; it does not add another voltage sweep axis.
    const auto time_domain = LabelledDomain::from_port(
        std::pair<double, double>{0.0, 1.0}, InstrumentPort::ExecutionClock());
    const auto request = std::make_shared<MeasurementRequest>(
        "buffered voltage sweep at O1",
        std::make_shared<List<Waveform>>(std::vector<WaveformSP>{waveform}),
        std::make_shared<Ports>(std::vector<InstrumentPortSP>{getter}),
        transforms, time_domain);
    // Send the request through NATS -> hub -> ISS -> Lua -> mock plugins, then
    // validate the response returned through the controller's public API.
    const auto response = request_measurement(request, TIMEOUT_MS);
    ASSERT_NE(response, nullptr);
    ASSERT_NE(response->arrays(), nullptr);
    ASSERT_TRUE(response->arrays()->is_measured_arrays());
    // One getter produces one measured array, regardless of sweep dimension.
    ASSERT_EQ(response->arrays()->size(), 1);
    const auto array = response->arrays()->arrays()[0];
    ASSERT_NE(array, nullptr);
    ASSERT_NE(array->connection(), nullptr);
    EXPECT_EQ(*array->connection(), *getter_connection);
    EXPECT_EQ(array->instrument_type(), Instrument::Voltmeter);
    ASSERT_NE(array->units(), nullptr);
    EXPECT_EQ(*array->units(), *SymbolUnit::Volt());
    ASSERT_EQ(array->size(), sample_count);
    // Check dimensions as well as sample count: a flattened six-point trace
    // must not pass as the requested 3-by-2 measurement.
    ASSERT_EQ(array->shape().size(), points.size());
    for (size_t axis = 0; axis < points.size(); ++axis) {
      EXPECT_EQ(array->shape()[axis], static_cast<size_t>(points[axis] + 1));
    }
    // Each fresh mock meter reads O1 sequentially from gaussian-1d.txt.
    // Compare every transported sample, including buffer concatenation order.
    // These file-backed readings are independent of the applied source voltage.
    std::ifstream expected_data(TEST_DATA_FILE);
    ASSERT_TRUE(expected_data.is_open());
    for (size_t i = 0; i < sample_count; ++i) {
      double expected;
      ASSERT_TRUE(static_cast<bool>(expected_data >> expected));
      EXPECT_DOUBLE_EQ(array->raw_data()[i], expected) << "sample " << i;
    }
  }
  MeasurementRequestSP MakeMultiTargetPointRequest(
      const std::string &message, const std::string &measurement_name,
      const std::vector<std::pair<InstrumentPortSP, double>> &targets) {
    InstrumentPortSP clock = InstrumentPort::ExecutionClock();
    std::vector<std::pair<InstrumentPortSP, PortTransformSP>> transform_pairs;
    std::vector<WaveformSP> waveform_list;
    transform_pairs.reserve(targets.size());
    waveform_list.reserve(targets.size());

    for (const auto &[target, target_value] : targets) {
      transform_pairs.emplace_back(target,
                                   PortTransform::IdentityTransform(target));
      LabelledDomainSP target_domain = LabelledDomain::from_port(
          std::pair<double, double>{target_value, target_value}, target);
      CoupledLabelledDomainSP coupled_domain =
          std::make_shared<CoupledLabelledDomain>(
              std::vector<LabelledDomainSP>{target_domain});
      MapSP<std::string, bool> increasing =
          std::make_shared<Map<std::string, bool>>(
              std::vector<std::pair<std::string, bool>>{
                  {target->default_name(), true}});
      waveform_list.emplace_back(
          Waveform::CartesianIdentityWaveform1D(1, coupled_domain, increasing));
    }

    MapSP<InstrumentPort, PortTransform> transforms =
        std::make_shared<Map<InstrumentPort, PortTransform>>(transform_pairs);
    LabelledDomainSP time_domain =
        LabelledDomain::from_port(std::pair<double, double>{0.0, 0.0}, clock);
    ListSP<Waveform> waveforms =
        std::make_shared<List<Waveform>>(waveform_list);

    return std::make_shared<MeasurementRequest>(
        message, waveforms, std::make_shared<Ports>(), transforms, time_domain);
  }
  void ExpectSinglePointEchoResponse(const MeasurementResponseSP &resp,
                                     const InstrumentPortSP &,
                                     const ConnectionSP &connection,
                                     double expected_value) {
    ASSERT_NE(resp, nullptr) << "measurement returned a null response";
    ASSERT_NE(resp->arrays(), nullptr)
        << "measurement did not return labelled arrays";
    EXPECT_TRUE(resp->arrays()->is_measured_arrays())
        << "Expected a measured-array response envelope";
    ASSERT_EQ(resp->arrays()->size(), 1)
        << "Expected exactly one measured array";

    auto labelledArray = resp->arrays()->arrays()[0];
    EXPECT_EQ(*labelledArray->connection(), *connection)
        << "Expected connection to be " << connection->name() << ", but got "
        << labelledArray->connection()->name() << " instead";
    ASSERT_EQ(labelledArray->size(), 1) << "Expected 1 data point, but got "
                                        << labelledArray->size() << " instead";
    EXPECT_NEAR((*labelledArray)(0), expected_value, 1e-9)
        << "Expected schema response to echo the applied value";
  }
  void ExpectMultiTargetEchoResponse(
      const MeasurementResponseSP &resp,
      const std::vector<std::tuple<InstrumentPortSP, ConnectionSP, double>>
          &expected_targets) {
    ASSERT_NE(resp, nullptr) << "measurement returned a null response";
    ASSERT_NE(resp->arrays(), nullptr)
        << "measurement did not return labelled arrays";
    EXPECT_TRUE(resp->arrays()->is_measured_arrays())
        << "Expected a measured-array response envelope";
    ASSERT_EQ(resp->arrays()->size(), expected_targets.size())
        << "Expected " << expected_targets.size()
        << " measured arrays in the response";

    std::map<std::string, falcon_core::math::arrays::LabelledMeasuredArraySP>
        arrays_by_connection;
    for (const auto &array : resp->arrays()->arrays()) {
      arrays_by_connection[array->connection()->name()] = array;
    }

    for (const auto &[target, connection, expected_value] : expected_targets) {
      (void)target;
      auto found = arrays_by_connection.find(connection->name());
      ASSERT_NE(found, arrays_by_connection.end())
          << "Expected a response array for connection " << connection->name();

      auto labelledArray = found->second;
      EXPECT_EQ(*labelledArray->connection(), *connection)
          << "Expected connection to be " << connection->name();
      ASSERT_EQ(labelledArray->size(), 1)
          << "Expected 1 data point for " << connection->name();
      EXPECT_NEAR((*labelledArray)(0), expected_value,
                  FloatEchoTolerance(expected_value))
          << "Expected schema response to echo the applied value for "
          << connection->name();
    }
  }
  double ReadFirstScalarFromFile(const fs::path &data_file) {
    std::ifstream input(data_file);
    double value = 0.0;
    if (!(input >> value)) {
      throw std::runtime_error("Failed to read first scalar value from " +
                               data_file.string());
    }
    return value;
  }
  void WaitForHubReady(int timeout_ms) {
    // Subscribe to STATUS.instrument-server which the hub publishes after
    // all handlers are set up (completing Phase 3 of hub startup).
    // This ensures DeviceConfigRequest and other handler subscriptions are
    // ready.
    auto prom = std::make_shared<std::promise<bool>>();
    auto fut = prom->get_future();

    falcon::comms::NatsManager &nm = falcon::comms::NatsManager::instance();
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    // The embedded NATS server starts after ISS. Retry the initial connection
    // before subscribing instead of failing immediately during hub startup.
    while (!nm.is_connected()) {
      try {
        nm.connect(std::getenv("NATS_URL"));
      } catch (const std::exception &) {
        if (std::chrono::steady_clock::now() >= deadline) {
          throw std::runtime_error("Timeout connecting to hub NATS; run: " +
                                   current_run_dir_.string());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }

    nm.subscribe("STATUS.instrument-server", [prom](const std::string &) {
      try {
        prom->set_value(true);
      } catch (const std::future_error &) {
        // A periodic status message already satisfied the promise.
      }
    });

    auto status = fut.wait_for(std::chrono::milliseconds(timeout_ms));
    nm.unsubscribe("STATUS.instrument-server");

    if (status != std::future_status::ready) {
      throw std::runtime_error(
          "Timeout waiting for hub ready signal (STATUS.instrument-server)");
    }
  }
#ifdef _WIN32
  PROCESS_INFORMATION hub_process_info_{};
#else
  pid_t hub_pid_ = -1;
#endif
};

TEST_F(DataRetrievalTest, set_then_get_then_set_and_get) {
  const auto setter =
      PublishedSetting(Connection::ScreeningGate("S1"),
                       InstrumentCharacteristic::Source_Voltage);
  ASSERT_NE(setter, nullptr);

  const auto getter =
      PublishedSetting(Connection::ScreeningGate("S1"),
                       InstrumentCharacteristic::Source_Voltage);
  ASSERT_NE(getter, nullptr);

  auto performGet = [&](double expectedValue) {
    auto getters = std::make_shared<Ports>();
    getters->push_back(getter);

    // A read-only request still requires a non-null, empty setters map.
    auto request = MakeSettingRequest(
        "get source voltage", getters,
        std::make_shared<Map<InstrumentPort, falcon_core::math::Quantity>>());

    auto response = request_setting(request, TIMEOUT_MS);

    ASSERT_NE(response, nullptr);
    ASSERT_NE(response->getters(), nullptr);

    ASSERT_TRUE(response->getters()->contains(getter));

    const auto quantity = response->getters()->at(getter);

    ASSERT_NE(quantity, nullptr);

    EXPECT_EQ(*quantity->unit(), *SymbolUnit::Volt());
    EXPECT_NEAR(quantity->value(), expectedValue, ABS_ERROR);
  };

  //
  // Set 0.123
  //
  {
    auto setters =
        std::make_shared<Map<InstrumentPort, falcon_core::math::Quantity>>();

    setters->insert(setter, std::make_shared<falcon_core::math::Quantity>(
                                0.123, SymbolUnit::Volt()));

    auto request =
        MakeSettingRequest("set voltage", std::make_shared<Ports>(), setters);

    auto response = request_setting(request, TIMEOUT_MS);

    ASSERT_NE(response, nullptr);
  }

  //
  // Get only
  //
  performGet(0.123);

  //
  // Set 0.456
  //
  {
    auto setters =
        std::make_shared<Map<InstrumentPort, falcon_core::math::Quantity>>();

    setters->insert(setter, std::make_shared<falcon_core::math::Quantity>(
                                0.456, SymbolUnit::Volt()));

    auto request =
        MakeSettingRequest("set voltage", std::make_shared<Ports>(), setters);

    auto response = request_setting(request, TIMEOUT_MS);

    ASSERT_NE(response, nullptr);
  }

  //
  // Get again
  //
  performGet(0.456);
}

TEST_F(DataRetrievalTest, measure_set_get) {
  const auto setter = PublishedKnob(Connection::ScreeningGate("S1"));
  const auto getter_connection = Connection::Ohmic("O1");
  InstrumentPortSP getter;
  for (const auto &port : published_meters_->items()) {
    if (*port->pseudo_name() == *getter_connection &&
        port->default_name().find(".analog.1.voltage") != std::string::npos) {
      getter = port;
      break;
    }
  }
  ASSERT_NE(getter, nullptr) << "No scalar voltage port published for O1";
  const auto request = MakeSinglePointGetterRequest(
      "measure_get_set: set S1 and read O1", setter, 0.123, {getter});
  const auto response = request_measurement(request, TIMEOUT_MS);
  ASSERT_NE(response, nullptr);
  ASSERT_NE(response->arrays(), nullptr);
  ASSERT_TRUE(response->arrays()->is_measured_arrays());
  ASSERT_EQ(response->arrays()->size(), 1);
  const auto array = response->arrays()->arrays()[0];
  ASSERT_NE(array, nullptr);
  ASSERT_NE(array->connection(), nullptr);
  EXPECT_EQ(*array->connection(), *getter_connection);
  EXPECT_EQ(array->instrument_type(), Instrument::Voltmeter);
  ASSERT_NE(array->units(), nullptr);
  EXPECT_EQ(*array->units(), *SymbolUnit::Volt());
  ASSERT_EQ(array->size(), 2);
  EXPECT_DOUBLE_EQ((*array)(0), ReadFirstScalarFromFile(TEST_DATA_FILE));
}

TEST_F(DataRetrievalTest, measure_1D_buffered) {
  ExpectBufferedMeasurement({Connection::ScreeningGate("S1")}, {4});
}

TEST_F(DataRetrievalTest, measure_2D_buffered) {
  ExpectBufferedMeasurement(
      {Connection::ScreeningGate("S1"), Connection::ScreeningGate("S2")},
      {3, 2});
}

TEST_F(DataRetrievalTest, measure_1D_many_knobs_and_meters) {
  std::vector<ConnectionSP> connections = {
      Connection::ScreeningGate("S1"),
      Connection::ScreeningGate("S2"),
  };
  std::vector<int> points = {4};

  // O1 publishes both scalar-voltage and stream ports. Select the stream
  // metadata supplied by the hub so the request identifies a buffer getter.
  const auto getter_connection = Connection::Ohmic("O1");
  InstrumentPortSP getter;
  for (const auto &port : published_meters_->items()) {
    if (*port->pseudo_name() == *getter_connection &&
        port->default_name().find(".stream") != std::string::npos) {
      getter = port;
      break;
    }
  }
  ASSERT_NE(getter, nullptr) << "No buffer stream port published for O1";

  const auto getter_connection1 = Connection::Ohmic("O2");
  InstrumentPortSP getter1;
  for (const auto &port : published_meters_->items()) {
    if (*port->pseudo_name() == *getter_connection &&
        port->default_name().find(".stream") != std::string::npos) {
      getter1 = port;
      break;
    }
  }
  ASSERT_NE(getter1, nullptr) << "No buffer stream port published for O2";

  std::vector<CoupledLabelledDomainSP> domains;
  std::vector<MapSP<std::string, bool>> increasing;
  auto transforms = std::make_shared<Map<InstrumentPort, PortTransform>>();
  size_t sample_count = 1;
  int axis = 0;
  const auto setter = PublishedKnob(connections[0]);
  const auto setter1 = PublishedKnob(connections[1]);
  // Label the voltage range with its source port. A coupled domain can
  // describe several sources moving together; this test uses one per axis.
  domains.push_back(
      std::make_shared<CoupledLabelledDomain>(std::vector<LabelledDomainSP>{
          LabelledDomain::from_port(std::pair<double, double>{-0.1, 0.1},
                                    setter),
          LabelledDomain::from_port(std::pair<double, double>{-0.1, 0.5},
                                    setter1)}));
  auto direction = std::make_shared<Map<std::string, bool>>();
  // Direction keys use device connection names (S1/S2), as expected by
  // falcon-core and the hub planner, rather than fully qualified port
  // names.
  direction->insert(setter->instrument_facing_name(), true);
  direction->insert(setter1->instrument_facing_name(), true);
  increasing.push_back(direction);
  // Identity transforms leave the requested voltages unchanged.
  transforms->insert(setter, PortTransform::IdentityTransform(setter));
  // The mock meter starts with one sample per buffer, so the total number
  // of returned samples is the product of the axis lengths.
  sample_count *= static_cast<size_t>(points[axis] + 1);

  // One waveform with all axes; two separate 1D waveforms would not test
  // the hub's 2D primitive.
  const auto waveform = Waveform::CartesianIdentityWaveform(
      std::make_shared<falcon_core::math::Axes<int>>(points),
      std::make_shared<falcon_core::math::Axes<CoupledLabelledDomain>>(domains),
      std::make_shared<falcon_core::math::Axes<Map<std::string, bool>>>(
          increasing));
  // MeasurementRequest also requires a labelled time domain. This clock
  // describes execution time; it does not add another voltage sweep axis.
  const auto time_domain = LabelledDomain::from_port(
      std::pair<double, double>{0.0, 1.0}, InstrumentPort::ExecutionClock());
  const auto request = std::make_shared<MeasurementRequest>(
      "buffered voltage sweep at O1",
      std::make_shared<List<Waveform>>(std::vector<WaveformSP>{waveform}),
      std::make_shared<Ports>(std::vector<InstrumentPortSP>{getter, getter1}),
      transforms, time_domain);
  // Send the request through NATS -> hub -> ISS -> Lua -> mock plugins, then
  // validate the response returned through the controller's public API.
  const auto response = request_measurement(request, TIMEOUT_MS);
  ASSERT_NE(response, nullptr);
  ASSERT_NE(response->arrays(), nullptr);
  ASSERT_TRUE(response->arrays()->is_measured_arrays());
  // One getter produces one measured array, regardless of sweep dimension.
  ASSERT_EQ(response->arrays()->size(), 2);

  // Open once and consume sequentially across all arrays.
  std::ifstream expected_data(TEST_DATA_FILE);
  ASSERT_TRUE(expected_data.is_open());

  for (size_t i = 0; i < response->arrays()->arrays().size(); ++i) {
    const auto array = response->arrays()->arrays()[i];

    ASSERT_NE(array, nullptr);
    ASSERT_NE(array->connection(), nullptr);
    EXPECT_EQ(*array->connection(), *getter_connection);
    EXPECT_EQ(array->instrument_type(), Instrument::Voltmeter);

    ASSERT_NE(array->units(), nullptr);
    EXPECT_EQ(*array->units(), *SymbolUnit::Volt());

    ASSERT_EQ(array->size(), sample_count);

    // Check dimensions as well as sample count: a flattened six-point trace
    // must not pass as the requested 3-by-2 measurement.
    ASSERT_EQ(array->shape().size(), points.size());
    for (size_t axis = 0; axis < points.size(); ++axis) {
      EXPECT_EQ(array->shape()[axis], static_cast<size_t>(points[axis] + 1));
    }

    // Compare every transported sample. The expected data stream advances
    // across arrays rather than restarting for each one.
    for (size_t sample = 0; sample < sample_count; ++sample) {
      double expected;
      ASSERT_TRUE(static_cast<bool>(expected_data >> expected));

      EXPECT_DOUBLE_EQ(array->raw_data()[sample], expected)
          << "array " << i << ", sample " << sample;
    }
  }
}

TEST_F(DataRetrievalTest, measure_2D_buffered_with_only_1D) {
  // The fixture stages only the 1D primitive for this 2D request.
  ExpectBufferedMeasurement(
      {Connection::ScreeningGate("S1"), Connection::ScreeningGate("S2")},
      {3, 2});
}
