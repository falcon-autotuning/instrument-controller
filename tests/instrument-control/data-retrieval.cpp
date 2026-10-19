#include <boost/process.hpp>
#include <cctype>
#include <chrono>
#include <cmath>
#include <falcon-comms/natsManager.hpp>
#include <falcon-comms/runtime_comms.hpp>
#include <falcon-core/generic/Map.hpp>
#include <falcon-core/instrument_interfaces/Waveform.hpp>
#include <falcon-core/instrument_interfaces/names/InstrumentPort.hpp>
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
const fs::path TEST_DATA_FILE = TEST_DATA_DIR_PATH / "gaussian-1d.txt";

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
  bool had_tmpdir_ = false;
  bool had_tmp_ = false;
  bool had_temp_ = false;
  PortsSP published_knobs_;
  PortsSP published_meters_;
  PortsSP published_settings_;

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
    if (std::string(
            ::testing::UnitTest::GetInstance()->current_test_info()->name()) ==
        "measure_set_get") {
      // Enable only the scalar primitive, avoiding sweep/sample-count
      // prerequisites.
      current_scripts_dir_ = current_run_dir_ / "scripts";
      fs::create_directories(current_scripts_dir_);
      fs::copy_file(MEASUREMENT_SCRIPTS_DIR / "measure_get_set.lua",
                    current_scripts_dir_ / "measure_get_set.lua");
      for (const char *action : {"get", "set"}) {
        fs::copy_file(DATA_1D_DIR / "settings-scripts" /
                          (std::string(action) + "_voltage.lua"),
                      current_scripts_dir_ /
                          (std::string(action) + "_source_voltage.lua"));
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

    StartInstrumentHub(VCPKG_BIN_DIR / "instrument-hub", current_config_path_);
    WaitForHubReady(10000); // Wait for hub to finish setting up handlers
    LoadPublishedPorts();
    std::cout << "Setup complete, starting test" << '\n';
  }

  void TearDown() override {
    unsetenv("MOCK_MULTIMETER_DATA_FILE");
    unsetenv("NATS_URL");
    StopInstrumentHub();
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

    auto request = MakeSettingRequest("get source voltage", getters, nullptr);

    auto response = request_setting(request, TIMEOUT_MS);

    ASSERT_NE(response, nullptr);
    ASSERT_NE(response->getters(), nullptr);

    ASSERT_TRUE(response->getters()->contains(getter));

    const auto quantity = response->getters()->at(getter);

    ASSERT_NE(quantity, nullptr);

    EXPECT_EQ(*quantity->unit(), *SymbolUnit::Volt());
    EXPECT_DOUBLE_EQ(quantity->value(), expectedValue);
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
        port->default_name().find(".voltage") != std::string::npos) {
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
  ASSERT_EQ(array->size(), 1);
  EXPECT_DOUBLE_EQ((*array)(0), ReadFirstScalarFromFile(TEST_DATA_FILE));
}

TEST_F(DataRetrievalTest, VoltageSweepCurrent) {
  GTEST_SKIP() << "Skipping all tests for this fixture";
  const int NUM_POINTS = 100;
  const double START_TIME_SECONDS = 0.0;
  const double MAX_TIME_SECONDS = 1.0;
  const char *SWEEP_NAME = "P1";
  const char *GETTER_NAME = "O2";
  const double MIN_SWEEP_VOLTAGE = 0.0;
  const double MAX_SWEEP_VOLTAGE = 0.5;
  ConfigSP config = request_config(TIMEOUT_MS);
  ASSERT_NE(config, nullptr) << "Failed to get config from request_config";

  ConnectionSP getter_connection = Connection::Ohmic(GETTER_NAME);
  InstrumentPortSP currentMeter = PublishedMeter(getter_connection);
  PortsSP getters =
      std::make_shared<Ports>(std::vector<InstrumentPortSP>{currentMeter});

  // O2 shares Meter1 (VOLTMETER) with O1. PORT_PAYLOAD now publishes the
  // canonical getter metadata directly, including the expected mV units.
  // In a physical setup the raw mV reading would be scaled to nA by the
  // transresistance amplifier gain; the test data (linear-1d.txt) represents
  // that conceptual current sweep.
  ConnectionSP sweep_connection = Connection::PlungerGate(SWEEP_NAME);
  InstrumentPortSP voltageKnob = PublishedKnob(sweep_connection);
  InstrumentPortSP clock = InstrumentPort::ExecutionClock();

  MapSP<InstrumentPort, PortTransform> transforms =
      std::make_shared<Map<InstrumentPort, PortTransform>>(
          std::vector<std::pair<InstrumentPortSP, PortTransformSP>>{
              {voltageKnob, PortTransform::IdentityTransform(voltageKnob)}});
  LabelledDomainSP time_domain = LabelledDomain::from_port(
      std::pair<double, double>{START_TIME_SECONDS, MAX_TIME_SECONDS}, clock);
  LabelledDomainSP sweepDomain = LabelledDomain::from_port(
      std::pair<double, double>{MIN_SWEEP_VOLTAGE, MAX_SWEEP_VOLTAGE},
      voltageKnob);
  CoupledLabelledDomainSP coupledDomain =
      std::make_shared<CoupledLabelledDomain>(
          std::vector<LabelledDomainSP>{sweepDomain});
  MapSP<std::string, bool> increasing =
      std::make_shared<Map<std::string, bool>>(
          std::vector<std::pair<std::string, bool>>{
              {voltageKnob->default_name(), true}});

  auto waveform = Waveform::CartesianIdentityWaveform1D(
      NUM_POINTS, coupledDomain, increasing);
  ListSP<Waveform> waveforms =
      std::make_shared<List<Waveform>>(std::vector<WaveformSP>{waveform});

  MeasurementRequestSP request = std::make_shared<MeasurementRequest>(
      "1D voltage sweep recording current at O2", waveforms, getters,
      transforms, time_domain);
  auto resp = request_measurement(request, TIMEOUT_MS);

  EXPECT_TRUE(resp->arrays()->is_measured_arrays())
      << "The arrays were not measured arrays";
  EXPECT_EQ(resp->arrays()->size(), 1) << "Expected exactly one measured array";
  auto labelledArray = resp->arrays()->arrays()[0];
  EXPECT_EQ(*labelledArray->connection(), *Connection::PlungerGate(SWEEP_NAME))
      << "Expected connection to be P1, but got "
      << labelledArray->connection()->name() << " instead";
  EXPECT_EQ(labelledArray->instrument_type(), Instrument::Voltmeter)
      << "Expected instrument type to be VOLTMETER, but got "
      << ToString(labelledArray->instrument_type()) << " instead";
  EXPECT_EQ(*labelledArray->units(), *SymbolUnit::Volt())
      << "Expected units to be mV, but got " << labelledArray->units()
      << " instead";
  EXPECT_EQ(labelledArray->size(), NUM_POINTS)
      << "Expected 100 data points, but got " << labelledArray->size()
      << " instead";
}

TEST_F(DataRetrievalTest, VoltageSweepCurrent2D) {
  GTEST_SKIP() << "Skipping all tests for this fixture";
  const int N_FAST = 10;
  const int N_SLOW = 10;
  const double START_TIME_SECONDS = 0.0;
  const double MAX_TIME_SECONDS = 2.0;
  const char *FAST_GATE = "P1";
  const char *SLOW_GATE = "P2";
  const char *GETTER_NAME = "O2";
  const double FAST_MIN_V = 0.0, FAST_MAX_V = 0.5;
  const double SLOW_MIN_V = -0.1, SLOW_MAX_V = 0.1;

  ConfigSP config = request_config(TIMEOUT_MS);
  ASSERT_NE(config, nullptr) << "Failed to get config from request_config";

  ConnectionSP getter_connection = Connection::Ohmic(GETTER_NAME);
  InstrumentPortSP currentMeter = PublishedMeter(getter_connection);
  PortsSP getters =
      std::make_shared<Ports>(std::vector<InstrumentPortSP>{currentMeter});

  ConnectionSP fast_connection = Connection::PlungerGate(FAST_GATE);
  ConnectionSP slow_connection = Connection::PlungerGate(SLOW_GATE);
  InstrumentPortSP fastKnob = PublishedKnob(fast_connection);
  InstrumentPortSP slowKnob = PublishedKnob(slow_connection);
  InstrumentPortSP clock = InstrumentPort::ExecutionClock();

  MapSP<InstrumentPort, PortTransform> transforms =
      std::make_shared<Map<InstrumentPort, PortTransform>>(
          std::vector<std::pair<InstrumentPortSP, PortTransformSP>>{
              {fastKnob, PortTransform::IdentityTransform(fastKnob)},
              {slowKnob, PortTransform::IdentityTransform(slowKnob)}});

  LabelledDomainSP time_domain = LabelledDomain::from_port(
      std::pair<double, double>{START_TIME_SECONDS, MAX_TIME_SECONDS}, clock);
  LabelledDomainSP fastDomain = LabelledDomain::from_port(
      std::pair<double, double>{FAST_MIN_V, FAST_MAX_V}, fastKnob);
  LabelledDomainSP slowDomain = LabelledDomain::from_port(
      std::pair<double, double>{SLOW_MIN_V, SLOW_MAX_V}, slowKnob);

  CoupledLabelledDomainSP fastCoupled = std::make_shared<CoupledLabelledDomain>(
      std::vector<LabelledDomainSP>{fastDomain});
  CoupledLabelledDomainSP slowCoupled = std::make_shared<CoupledLabelledDomain>(
      std::vector<LabelledDomainSP>{slowDomain});

  MapSP<std::string, bool> fastIncreasing =
      std::make_shared<Map<std::string, bool>>(
          std::vector<std::pair<std::string, bool>>{
              {fastKnob->default_name(), true}});
  MapSP<std::string, bool> slowIncreasing =
      std::make_shared<Map<std::string, bool>>(
          std::vector<std::pair<std::string, bool>>{
              {slowKnob->default_name(), true}});

  WaveformSP fastWaveform = Waveform::CartesianIdentityWaveform1D(
      N_FAST, fastCoupled, fastIncreasing);
  WaveformSP slowWaveform = Waveform::CartesianIdentityWaveform1D(
      N_SLOW, slowCoupled, slowIncreasing);
  ListSP<Waveform> waveforms = std::make_shared<List<Waveform>>(
      std::vector<WaveformSP>{fastWaveform, slowWaveform});

  MeasurementRequestSP request = std::make_shared<MeasurementRequest>(
      "2D voltage sweep recording current at O2", waveforms, getters,
      transforms, time_domain);
  auto resp = request_measurement(request, TIMEOUT_MS);

  EXPECT_TRUE(resp->arrays()->is_measured_arrays())
      << "The arrays were not measured arrays";
  EXPECT_EQ(resp->arrays()->size(), 1) << "Expected exactly one measured array";
  auto labelledArray = resp->arrays()->arrays()[0];
  EXPECT_EQ(*labelledArray->connection(), *Connection::PlungerGate(FAST_GATE))
      << "Expected connection to be P1, but got "
      << labelledArray->connection()->name() << " instead";
  EXPECT_EQ(labelledArray->instrument_type(), Instrument::Voltmeter)
      << "Expected instrument type to be VOLTMETER, but got "
      << ToString(labelledArray->instrument_type()) << " instead";
  EXPECT_EQ(*labelledArray->units(), *SymbolUnit::Volt())
      << "Expected units to be mV, but got " << labelledArray->units()
      << " instead";
  EXPECT_EQ(labelledArray->size(), N_FAST * N_SLOW)
      << "Expected " << N_FAST * N_SLOW << " data points, but got "
      << labelledArray->size() << " instead";
}
