#include <boost/asio/ip/tcp.hpp>
#include <boost/process.hpp>
#include <cctype>
#include <chrono>
#include <cmath>
#include <falcon-comms/natsManager.hpp>
#include <falcon-comms/runtime_comms.hpp>
#include <falcon-core/communications/HDF5Data.hpp>
#include <falcon-core/generic/Map.hpp>
#include <falcon-core/instrument_interfaces/Waveform.hpp>
#include <falcon-core/instrument_interfaces/names/InstrumentPort.hpp>
#include <falcon-core/math/Axes.hpp>
#include <falcon-core/physics/device_structures/Connection.hpp>
#include <falcon-database/DatabaseConnection.hpp>
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

struct ScriptFixtureConfig {
  std::vector<fs::path> staged_files;
  bool stage_sweep_math = false;
};

struct VoltageStateConfig {
  std::string connection;
  double voltage;
  int unit;
};

std::map<std::string, double>
VoltageStateMap(const VoltageStatesResponseSP &response) {
  std::map<std::string, double> out;

  for (const auto &state : response->states()->items()) {

    out[state->connection()->name()] = state->voltage();
  }

  return out;
}

class DataRetrievalTest : public ::testing::Test {
protected:
  fs::path current_run_dir_;
  fs::path current_working_dir_;
  fs::path current_data_dir_;
  fs::path current_config_path_;
  fs::path current_scripts_dir_ = LUA_SCRIPTS_DIR;
  fs::path current_tmp_dir_;
  std::string current_data_catalog_;
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
  ScriptFixtureConfig current_config_;

  void StageConfiguredFiles(const ScriptFixtureConfig &cfg) {
    if (cfg.staged_files.empty()) {
      return;
    }

    current_scripts_dir_ = current_run_dir_ / "scripts";
    fs::create_directories(current_scripts_dir_);

    for (const auto &file : cfg.staged_files) {
      fs::path source;

      if (file.string().find("measure_") == 0) {
        source = MEASUREMENT_SCRIPTS_DIR / file;
      } else if (file == "get_source_voltage.lua" ||
                 file == "set_source_voltage.lua" ||
                 file == "get_number_of_samples.lua" ||
                 file == "set_number_of_samples.lua") {
        source = DATA_1D_DIR / "settings-scripts" / file;
      } else {
        FAIL() << "Unknown staged file: " << file.string();
      }

      fs::copy_file(source, current_scripts_dir_ / file.filename(),
                    fs::copy_options::overwrite_existing);
    }

    if (cfg.stage_sweep_math) {
      fs::create_directories(current_run_dir_ / "lua-utils");

      fs::copy_file(DATA_1D_DIR / "lua-utils/sweep_math.lua",
                    current_run_dir_ / "lua-utils/sweep_math.lua");
    }
  }

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
    current_data_catalog_ = "measurements.db";
    current_config_path_ = current_run_dir_ / "test-config.yaml";
    current_tmp_dir_ = current_run_dir_ / "tmp";

    fs::create_directories(current_working_dir_);
    fs::create_directories(current_data_dir_);
    fs::create_directories(current_tmp_dir_);

    ConfigurePerTestTempDir();
    BuildTestData(TEST_DATA_DIR_PATH / "gen_data.cpp",
                  TEST_DATA_DIR_PATH / "gen_data");
    RunTestDataGenerator(TEST_DATA_DIR_PATH / "gen_data");
    // Sets up the files for each measurement
    current_config_ = ConfigureScripts();
    StageConfiguredFiles(current_config_);

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
  [[nodiscard]] virtual ScriptFixtureConfig ConfigureScripts() const {
    return {};
  }
  [[nodiscard]] virtual std::vector<VoltageStateConfig>
  InitialVoltageStates() const {
    return {};
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
      if (current_config_.stage_sweep_math) {
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

    const auto initialStates = InitialVoltageStates();

    if (initialStates.empty()) {
      ofs << "initial-voltage-state: []\n";
    } else {
      ofs << "initial-voltage-state:\n";
      for (const auto &state : initialStates) {
        ofs << "  - name: " << state.connection << "\n";
        ofs << "    voltage: " << state.voltage << "\n";
        ofs << "    unit: " << state.unit << "\n";
      }
    }

    ofs << "nats:\n";
    ofs << "  host: 127.0.0.1\n";
    ofs << "  port: 4222\n";

    ofs << "database:\n";
    ofs << "  local-database: " << current_data_dir_.string() << "\n";
    ofs << "  local-cache: " << current_data_dir_.string() << "/cache\n";
    ofs << "  catalog-file: " << current_data_catalog_ << "\n";
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
    ofs << "      type: amnmeter\n";
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
      // describe several sources moving together; this test uses one per
      // axis.
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
    // Send the request through NATS -> hub -> ISS -> Lua -> mock plugins,
    // then validate the response returned through the controller's public
    // API.
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
    EXPECT_EQ(array->instrument_type(), Instrument::Amnmeter);
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
    // These file-backed readings are independent of the applied source
    // voltage.
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
  void ExpectVoltageState(const VoltageStatesResponseSP &response,
                          const std::map<std::string, double> &expected) {

    const auto actual = VoltageStateMap(response);

    for (const auto &[connection, voltage] : expected) {
      auto it = actual.find(connection);

      ASSERT_NE(it, actual.end()) << "Missing connection " << connection;

      EXPECT_DOUBLE_EQ(it->second, voltage) << connection;
    }
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

class SettingVoltageTest : public DataRetrievalTest {
protected:
  [[nodiscard]] ScriptFixtureConfig ConfigureScripts() const override {
    return {
        .staged_files =
            {
                "get_source_voltage.lua",
                "set_source_voltage.lua",
            },
    };
  }
};

TEST_F(SettingVoltageTest, set_then_get_then_set_and_get) {
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

  // Set 0.123
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

  // Get only
  performGet(0.123);

  // Set 0.456
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

TEST_F(SettingVoltageTest, device_state_is_stable) {
  const auto state1 = request_device_state(TIMEOUT_MS);
  const auto state2 = request_device_state(TIMEOUT_MS);

  ASSERT_NE(state1, nullptr);
  ASSERT_NE(state2, nullptr);

  EXPECT_EQ(VoltageStateMap(state1), VoltageStateMap(state2));
}

TEST_F(SettingVoltageTest, initial_voltage_state_defaults_to_zero) {
  const auto response = request_device_state(TIMEOUT_MS);

  ASSERT_NE(response, nullptr);

  const auto states = VoltageStateMap(response);

  for (const auto &[connection, voltage] : states) {
    EXPECT_DOUBLE_EQ(voltage, 0.0) << connection;
  }
}

class SeededVoltageStateTest : public SettingVoltageTest {
protected:
  [[nodiscard]] std::vector<VoltageStateConfig>
  InitialVoltageStates() const override {
    return {
        {
            .connection = "S1",
            .voltage = 0.123,
            .unit = 0,
        },
        {
            .connection = "S2",
            .voltage = -0.456,
            .unit = 0,
        },
    };
  }
};

TEST_F(SeededVoltageStateTest, startup_state_matches_config) {
  const auto response = request_device_state(TIMEOUT_MS);

  ASSERT_NE(response, nullptr);

  ExpectVoltageState(response, {
                                   {"S1", 0.123},
                                   {"S2", -0.456},
                               });
}

class MeasureSetGetTest : public DataRetrievalTest {
protected:
  [[nodiscard]] ScriptFixtureConfig ConfigureScripts() const override {
    return {
        .staged_files =
            {
                "measure_get_set.lua",
                "get_source_voltage.lua",
                "set_source_voltage.lua",
            },
    };
  }
};

TEST_F(MeasureSetGetTest, measure_set_get) {
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
  EXPECT_EQ(array->instrument_type(), Instrument::Amnmeter);
  ASSERT_NE(array->units(), nullptr);
  EXPECT_EQ(*array->units(), *SymbolUnit::Volt());
  ASSERT_EQ(array->size(), 2);
  EXPECT_DOUBLE_EQ((*array)(0), ReadFirstScalarFromFile(TEST_DATA_FILE));
}

class BufferedMeasurementTest : public DataRetrievalTest {
protected:
  [[nodiscard]] ScriptFixtureConfig ConfigureScripts() const override {
    return {
        .staged_files =
            {
                "get_source_voltage.lua",
                "set_source_voltage.lua",
                "get_number_of_samples.lua",
                "set_number_of_samples.lua",
            },
        .stage_sweep_math = true,
    };
  }
};

class Buffered1DTest : public BufferedMeasurementTest {
protected:
  [[nodiscard]] ScriptFixtureConfig ConfigureScripts() const override {
    auto cfg = BufferedMeasurementTest::ConfigureScripts();
    cfg.staged_files.emplace_back("measure_1D_buffered.lua");
    return cfg;
  }
};

TEST_F(Buffered1DTest, measure_1D_buffered) {
  ExpectBufferedMeasurement({Connection::ScreeningGate("S1")}, {4});
}

TEST_F(Buffered1DTest, measure_1D_buffered_persists_data) {
  // Four divisions include both endpoints, giving five measured samples.
  // The hub writes its measurement store before acknowledging this request.
  constexpr size_t sample_count = 5;
  ASSERT_NO_FATAL_FAILURE(
      ExpectBufferedMeasurement({Connection::ScreeningGate("S1")}, {4}));

  // Reopen the persisted data after the writer has exited, rather than
  // inspecting only the in-memory measurement response.
  StopInstrumentHub();
  ASSERT_TRUE(fs::is_regular_file(current_data_dir_ / "measurements.db"));
  std::vector<fs::path> measurement_files;
  for (const auto &entry : fs::recursive_directory_iterator(current_data_dir_)) {
    if (entry.is_regular_file() && entry.path().extension() == ".h5") {
      measurement_files.push_back(entry.path());
    }
  }
  // This test has a private store and performs exactly one measurement.
  ASSERT_EQ(measurement_files.size(), 1);
  SCOPED_TRACE(measurement_files.front().string());
  std::cout << "Reading persisted measurement: " << measurement_files.front()
            << '\n';
  const auto stored = falcon_core::communications::HDF5Data::from_file(
      measurement_files.front().string());
  ASSERT_NE(stored, nullptr);
  ASSERT_NE(stored->shape(), nullptr);
  ASSERT_EQ(stored->shape()->size(), 1);
  EXPECT_EQ(stored->shape()->at(0), sample_count);

  // Check that the saved sweep identifies S1 and preserves its voltage bounds.
  ASSERT_NE(stored->domain_labels(), nullptr);
  ASSERT_EQ(stored->domain_labels()->size(), 1);
  const auto axis = stored->domain_labels()->at(0);
  ASSERT_NE(axis, nullptr);
  ASSERT_EQ(axis->size(), 1);
  const auto domain = axis->at(0);
  ASSERT_NE(domain, nullptr);
  ASSERT_NE(domain->port(), nullptr);
  ASSERT_NE(domain->port()->pseudo_name(), nullptr);
  EXPECT_EQ(*domain->port()->pseudo_name(), *Connection::ScreeningGate("S1"));
  EXPECT_DOUBLE_EQ(domain->lesser_bound(), -0.1);
  EXPECT_DOUBLE_EQ(domain->greater_bound(), 0.1);

  // Read the measured range through the same public HDF5 API used by clients.
  ASSERT_NE(stored->ranges(), nullptr);
  ASSERT_EQ(stored->ranges()->size(), 1);
  const auto array = stored->ranges()->arrays()[0];
  ASSERT_NE(array, nullptr);
  ASSERT_NE(array->connection(), nullptr);
  EXPECT_EQ(*array->connection(), *Connection::Ohmic("O1"));
  EXPECT_EQ(array->instrument_type(), Instrument::Amnmeter);
  ASSERT_NE(array->units(), nullptr);
  EXPECT_EQ(*array->units(), *SymbolUnit::Volt());
  ASSERT_EQ(array->shape().size(), 1);
  ASSERT_EQ(array->shape()[0], sample_count);
  ASSERT_EQ(array->size(), sample_count);

  // Use the generated input file as an independent reference for every value.
  std::ifstream expected_data(TEST_DATA_FILE);
  ASSERT_TRUE(expected_data.is_open());
  for (size_t i = 0; i < sample_count; ++i) {
    double expected;
    ASSERT_TRUE(static_cast<bool>(expected_data >> expected));
    EXPECT_DOUBLE_EQ(array->raw_data()[i], expected) << "saved sample " << i;
  }
}

TEST_F(Buffered1DTest, measure_1D_buffered_with_averaging) {
  constexpr int bins = 10;

  const auto sample_setting = PublishedSetting(
      Connection::Ohmic("O1"), InstrumentCharacteristic::Number_Of_Samples);
  ASSERT_NE(sample_setting, nullptr);
  auto setters =
      std::make_shared<Map<InstrumentPort, falcon_core::math::Quantity>>();
  setters->insert(sample_setting, std::make_shared<falcon_core::math::Quantity>(
                                      bins, SymbolUnit::Dimensionless()));
  auto settingRequest = MakeSettingRequest("set number of samples",
                                           std::make_shared<Ports>(), setters);
  auto settingResponse = request_setting(settingRequest, TIMEOUT_MS);
  ASSERT_NE(settingResponse, nullptr);

  // Build the same request as measure_1D_buffered
  const auto getter_connection = Connection::Ohmic("O1");
  InstrumentPortSP getter;
  for (const auto &port : published_meters_->items()) {
    if (*port->pseudo_name() == *getter_connection &&
        port->default_name().find(".stream") != std::string::npos) {
      getter = port;
      break;
    }
  }
  ASSERT_NE(getter, nullptr);
  const auto setterPort = PublishedKnob(Connection::ScreeningGate("S1"));
  auto transforms = std::make_shared<Map<InstrumentPort, PortTransform>>();
  transforms->insert(setterPort, PortTransform::IdentityTransform(setterPort));
  auto domain =
      std::make_shared<CoupledLabelledDomain>(std::vector<LabelledDomainSP>{
          LabelledDomain::from_port({-0.1, 0.1}, setterPort),
      });
  auto direction = std::make_shared<Map<std::string, bool>>();
  direction->insert(setterPort->instrument_facing_name(), true);
  auto waveform = Waveform::CartesianIdentityWaveform(
      std::make_shared<falcon_core::math::Axes<int>>(std::vector<int>{4}),
      std::make_shared<falcon_core::math::Axes<CoupledLabelledDomain>>(
          std::vector<CoupledLabelledDomainSP>{domain}),
      std::make_shared<falcon_core::math::Axes<Map<std::string, bool>>>(
          std::vector<MapSP<std::string, bool>>{direction}));
  auto request = std::make_shared<MeasurementRequest>(
      "buffered averaged sweep",
      std::make_shared<List<Waveform>>(std::vector<WaveformSP>{waveform}),
      std::make_shared<Ports>(std::vector<InstrumentPortSP>{getter}),
      transforms,
      LabelledDomain::from_port({0.0, 1.0}, InstrumentPort::ExecutionClock()));

  const auto response = request_measurement(request, TIMEOUT_MS);
  ASSERT_NE(response, nullptr);
  ASSERT_NE(response->arrays(), nullptr);
  ASSERT_EQ(response->arrays()->size(), 1);
  const auto array = response->arrays()->arrays()[0];
  ASSERT_NE(array, nullptr);
  // Verify averaged values
  ASSERT_EQ(array->size(), 5u);

  std::ifstream expected_data(TEST_DATA_FILE);
  ASSERT_TRUE(expected_data.is_open());

  for (size_t point = 0; point < 5; ++point) {
    double sum = 0.0;

    for (int i = 0; i < bins; ++i) {
      double value;
      ASSERT_TRUE(static_cast<bool>(expected_data >> value));
      sum += value;
    }
    const double expected = sum / bins;
    EXPECT_NEAR(array->raw_data()[point], expected, ABS_ERROR)
        << "point " << point;
  }
}

TEST_F(Buffered1DTest, measure_1D_many_knobs_and_meters) {
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
    if (*port->pseudo_name() == *getter_connection1 &&
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
  transforms->insert(setter1, PortTransform::IdentityTransform(setter1));
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

  // The hub may return getters in either order. Each mock channel has its own
  // data cursor and starts at the beginning of its configured file.
  std::map<std::string, fs::path> expected_files = {
      {getter_connection->name(), TEST_DATA_FILE},
      {getter_connection1->name(), TEST_DATA_DIR_PATH / "linear-1d.txt"}};

  for (size_t i = 0; i < response->arrays()->arrays().size(); ++i) {
    const auto array = response->arrays()->arrays()[i];

    ASSERT_NE(array, nullptr);
    ASSERT_NE(array->connection(), nullptr);
    const auto expected_file = expected_files.find(array->connection()->name());
    ASSERT_NE(expected_file, expected_files.end())
        << "Unexpected or repeated connection " << array->connection()->name();
    std::ifstream expected_data(expected_file->second);
    ASSERT_TRUE(expected_data.is_open());
    expected_files.erase(expected_file);
    EXPECT_EQ(array->instrument_type(), Instrument::Amnmeter);

    ASSERT_NE(array->units(), nullptr);
    EXPECT_EQ(*array->units(), *SymbolUnit::Volt());

    ASSERT_EQ(array->size(), sample_count);

    // Check dimensions as well as sample count: a flattened six-point trace
    // must not pass as the requested 3-by-2 measurement.
    ASSERT_EQ(array->shape().size(), points.size());
    for (size_t axis = 0; axis < points.size(); ++axis) {
      EXPECT_EQ(array->shape()[axis], static_cast<size_t>(points[axis] + 1));
    }

    // Compare this channel's samples against its own file, starting at zero.
    for (size_t sample = 0; sample < sample_count; ++sample) {
      double expected;
      ASSERT_TRUE(static_cast<bool>(expected_data >> expected));

      EXPECT_DOUBLE_EQ(array->raw_data()[sample], expected)
          << "array " << i << ", sample " << sample;
    }
  }
  EXPECT_TRUE(expected_files.empty());
}

TEST_F(Buffered1DTest, measure_2D_buffered_with_only_1D) {
  // The fixture stages only the 1D primitive for this 2D request.
  ExpectBufferedMeasurement(
      {Connection::ScreeningGate("S1"), Connection::ScreeningGate("S2")},
      {3, 2});
}

TEST_F(Buffered1DTest, ramp_updates_device_state) {
  const auto before = request_device_state(TIMEOUT_MS);

  ASSERT_NE(before, nullptr);

  ExpectVoltageState(before, {
                                 {"S1", 0.0},
                             });

  auto end_point = std::make_shared<falcon_core::math::Point>();

  end_point->insert(
      Connection::ScreeningGate("S1"),
      std::make_shared<falcon_core::math::Quantity>(0.5, SymbolUnit::Volt()));

  ASSERT_TRUE(ramp(end_point, 0.1, TIMEOUT_MS));

  const auto after = request_device_state(TIMEOUT_MS);

  ASSERT_NE(after, nullptr);

  ExpectVoltageState(after, {
                                {"S1", 0.5},
                            });
}

class Buffered2DTest : public BufferedMeasurementTest {
protected:
  [[nodiscard]] ScriptFixtureConfig ConfigureScripts() const override {
    auto cfg = BufferedMeasurementTest::ConfigureScripts();
    cfg.staged_files.emplace_back("measure_2D_buffered.lua");
    return cfg;
  }
};

TEST_F(Buffered2DTest, measure_2D_buffered) {
  ExpectBufferedMeasurement(
      {Connection::ScreeningGate("S1"), Connection::ScreeningGate("S2")},
      {3, 2});
}
