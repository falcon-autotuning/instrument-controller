#include <falcon-core/generic/Map.hpp>
#include <falcon-core/instrument_interfaces/Waveform.hpp>
#include <falcon-core/instrument_interfaces/names/InstrumentPort.hpp>
#include <falcon-core/instrument_interfaces/names/InstrumentTypes.hpp>
#include <falcon-core/physics/device_structures/Connection.hpp>
#include <falcon-comms/runtime_comms.hpp>
#include <falcon-routine/hub.hpp>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <chrono>
#include <cctype>
#include <future>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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
const int TIMEOUT_MS = 5000;
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
const char *HUB_SOURCE_VOLTAGE_PORT = "Mock.Source1.analog.voltage";
const char *HUB_SOURCE_MEASURED_VOLTAGE_PORT =
    "Mock.Source1.analog.measured_voltage";
const char *HUB_METER_VOLTAGE_PORT = "Mock.Meter1.analog.voltage";
const char *HUB_METER_STREAM_PORT = "Mock.Meter1.analog.stream";
const char *HUB_METER_SAMPLE_RATE_PORT = "Mock.Meter1.analog.sample_rate";
const char *HUB_METER_BINS_PORT = "Mock.Meter1.analog.bins";
const char *HUB_SOURCE_SLOPE_PORT = "Mock.Source1.analog.slope";
const char *HUB_METER_TRIGGER_LEADER_PORT =
    "Mock.Meter1.analog.trigger_leader";

static InstrumentPortSP request_knob(const std::string &port_name,
                                     const ConnectionSP &connection,
                                     int timeout_ms)
{
  try {
    auto [knobs, meters] = request_port_payload(timeout_ms);
    for (const auto &knob : *knobs.ports()) {
      if (knob->default_name() == port_name) {
        return knob;
      }
    }
  } catch (...) {}
  return InstrumentPort::Knob(port_name, connection, InstrumentTypes::DC_VOLTAGE_SOURCE);
}

static InstrumentPortSP request_meter(const std::string &port_name,
                                      const ConnectionSP &connection,
                                      int timeout_ms)
{
  try {
    auto [knobs, meters] = request_port_payload(timeout_ms);
    for (const auto &meter : *meters.ports()) {
      if (meter->default_name() == port_name) {
        return meter;
      }
    }
  } catch (...) {}
  return InstrumentPort::Meter(port_name, connection, InstrumentTypes::VOLTMETER);
}

class DataRetrievalTest : public ::testing::Test
{
protected:
  fs::path current_run_dir_;
  fs::path current_working_dir_;
  fs::path current_data_dir_;
  fs::path current_config_path_;
  fs::path current_tmp_dir_;
  std::string previous_tmpdir_;
  std::string previous_tmp_;
  std::string previous_temp_;
  bool had_tmpdir_ = false;
  bool had_tmp_ = false;
  bool had_temp_ = false;

  void SetUp() override
  {
    // Initialize paths from environment variables here, not at global scope
    const char *vcpkg_installed = std::getenv("VCPKG_INSTALLED_DIR");
    const char *vcpkg_triplet = std::getenv("VCPKG_TRIPLET");

    if (!vcpkg_installed || !*vcpkg_installed || !vcpkg_triplet ||
        !*vcpkg_triplet)
    {
      FAIL() << "Required environment variables not set: VCPKG_INSTALLED_DIR "
                "or VCPKG_TRIPLET";
    }
    std::cout << "Using vcpkg installed dir: " << vcpkg_installed << std::endl;
    std::cout << "Using vcpkg triplet: " << vcpkg_triplet << std::endl;

    VCPKG_BIN_DIR = fs::path(vcpkg_installed) / vcpkg_triplet / "bin";
    VCPKG_LIB_DIR = fs::path(vcpkg_installed) / vcpkg_triplet / "lib";
    LUA_LIB_DIR = VCPKG_LIB_DIR / "lua";
    fs::create_directories(LUA_LIB_DIR);
    fs::create_directories(TEAL_APIS_DIR);
    fs::create_directories(LUA_SCRIPTS_DIR);
    fs::create_directories(INSTRUMENT_LUA_LIBS_DIR);
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
    WriteConfigFile(current_config_path_);
    ExtendInstrumentApis(
        (INSTRUMENT_APIS_DIR / "multimeter-api.yml.tmpl").string(),
        (INSTRUMENT_APIS_DIR / "generated-multimeter-api.yml").string());
    ExtendInstrumentApis(
        (INSTRUMENT_APIS_DIR / "source-api.yml.tmpl").string(),
        (INSTRUMENT_APIS_DIR / "generated-source-api.yml").string());
    GenerateTealInstrumentLibs(
        (INSTRUMENT_APIS_DIR / "generated-multimeter-api.yml").string(),
        (TEAL_APIS_DIR / "multimeter.tl").string());
    GenerateTealInstrumentLibs(
        (INSTRUMENT_APIS_DIR / "generated-source-api.yml").string(),
        (TEAL_APIS_DIR / "source.tl").string());
    CompileTeal((TEAL_APIS_DIR / "multimeter.tl").string(),
                (INSTRUMENT_LUA_LIBS_DIR / "multimeter.lua").string());
    CompileTeal((TEAL_APIS_DIR / "source.tl").string(),
                (INSTRUMENT_LUA_LIBS_DIR / "source.lua").string());
    CompileMeasurementScripts({
        "get_all_voltages.tl",
        "get_many_voltages.tl",
        "get_voltage.tl",
        "get_sample_rate.tl",
        "get_number_of_samples.tl",
        "get_slope.tl",
        "get_trigger_leader.tl",
        "measure_current.tl",
        "measure_illumination.tl",
        "measure_leakage.tl",
        "measure_get_set.tl",
        "measure_1D_buffered.tl",
        "measure_2D_buffered.tl",
        "set_voltage.tl",
        "set_sample_rate.tl",
        "set_number_of_samples.tl",
        "set_many_voltages.tl",
        "ramp.tl",
        "set_slope.tl",
        "set_trigger_leader.tl",
    });
    SetISSLuaLibs(std::vector<std::filesystem::path>{
        INSTRUMENT_LUA_LIBS_DIR / "multimeter.lua",
        INSTRUMENT_LUA_LIBS_DIR / "source.lua", LUA_LIB_DIR});

    // Ensure spawned hub/ISS processes inherit test-specific runtime variables.
    // Channel 1 (O1) receives Gaussian data; channel 2 (O2) receives linear data.
    std::string multimeter_data_files =
        TEST_DATA_FILE.string() + ";" +
        (TEST_DATA_DIR_PATH / "linear-1d.txt").string();
    setenv("MOCK_MULTIMETER_DATA_FILE", multimeter_data_files.c_str(), 1);
    setenv("NATS_URL", "nats://localhost:4222", 1);

    StartInstrumentHub(VCPKG_BIN_DIR / "instrument-hub",
                       current_config_path_, VCPKG_LIB_DIR,
                       current_working_dir_, VCPKG_BIN_DIR);
    WaitForNats("127.0.0.1", 4222, 10000);
    WaitForHubReady(10000);  // Wait for hub to finish setting up handlers
    std::cout << "Setup complete, starting test" << std::endl;
  }

  void TearDown() override
  {
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
  fs::path MakeCurrentRunDir() const
  {
    const ::testing::TestInfo *test_info =
        ::testing::UnitTest::GetInstance()->current_test_info();
    std::string test_name = test_info ? test_info->name() : "unknown_test";
    std::string suite_name = test_info ? test_info->test_suite_name()
                                       : "unknown_suite";
    std::string slug = suite_name + "_" + test_name;
    for (char &ch : slug)
    {
      unsigned char uch = static_cast<unsigned char>(ch);
      if (!std::isalnum(uch) && ch != '_' && ch != '-')
      {
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
  void ConfigurePerTestTempDir()
  {
    if (const char *value = std::getenv("TMPDIR"))
    {
      had_tmpdir_ = true;
      previous_tmpdir_ = value;
    }
    if (const char *value = std::getenv("TMP"))
    {
      had_tmp_ = true;
      previous_tmp_ = value;
    }
    if (const char *value = std::getenv("TEMP"))
    {
      had_temp_ = true;
      previous_temp_ = value;
    }

    std::string tmp = current_tmp_dir_.string();
    setenv("TMPDIR", tmp.c_str(), 1);
    setenv("TMP", tmp.c_str(), 1);
    setenv("TEMP", tmp.c_str(), 1);
  }
  void RestorePerTestTempDir()
  {
    if (had_tmpdir_)
    {
      setenv("TMPDIR", previous_tmpdir_.c_str(), 1);
    }
    else
    {
      unsetenv("TMPDIR");
    }

    if (had_tmp_)
    {
      setenv("TMP", previous_tmp_.c_str(), 1);
    }
    else
    {
      unsetenv("TMP");
    }

    if (had_temp_)
    {
      setenv("TEMP", previous_temp_.c_str(), 1);
    }
    else
    {
      unsetenv("TEMP");
    }
  }
  void StartInstrumentHub(const std::filesystem::path &hub_executable,
                          const std::filesystem::path &config_path,
                          const std::filesystem::path &vcpkg_lib_dir,
                          const std::filesystem::path &working_dir,
                          const std::filesystem::path &vcpkg_bin_dir)
  {
    // Build argument list as std::string
    std::vector<std::string> args = {
        hub_executable.string(),
        "start",
        "--hub-config",
        config_path.string(),
        "--iss-lib-path",
        vcpkg_lib_dir.string(),
        "--working-dir",
        working_dir.string(),
        "--iss-binary",
        (vcpkg_bin_dir / "instrument-script-server").string()};

    // Build char* array for execv
    std::vector<char *> argv;
    for (auto &arg : args)
      argv.push_back(const_cast<char *>(arg.c_str()));
    argv.push_back(nullptr);

#ifdef _WIN32
    // Join arguments for Windows command line
    std::string cmd;
    for (size_t i = 0; i < args.size(); ++i)
    {
      if (i > 0)
        cmd += " ";
      cmd += args[i];
    }
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, cmd.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si,
                        &pi))
    {
      std::cerr << "Failed to start Instrument Hub, error: " << GetLastError()
                << std::endl;
      std::exit(1);
    }
    hub_process_info_ = pi;
#else
    pid_t pid = fork();
    if (pid == 0)
    {
      execv(argv[0], argv.data());
      std::cerr << "Failed to exec Instrument Hub\n";
      std::exit(1);
    }
    else if (pid > 0)
    {
      hub_pid_ = pid;
    }
    else
    {
      std::cerr << "Failed to fork for Instrument Hub\n";
      std::exit(1);
    }
#endif
  }
  void StopInstrumentHub()
  {
#ifdef _WIN32
    if (hub_process_info_.hProcess)
    {
      TerminateProcess(hub_process_info_.hProcess, 0);
      CloseHandle(hub_process_info_.hProcess);
      CloseHandle(hub_process_info_.hThread);
      hub_process_info_ = {};
    }
#else
    if (hub_pid_ > 0)
    {
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
  void GenerateTealInstrumentLibs(const std::string &local_path,
                                  const std::string &out_path)
  {
    std::string out_base = VCPKG_BIN_DIR / "teal-api-gen-cli";
#ifdef _WIN32
    std::string exe_path = out_base + ".exe";
#else
    std::string exe_path = out_base;
#endif
    std::string cmd = exe_path + " " + local_path + " " + out_path;
    int ret = std::system(cmd.c_str());
    if (ret != 0)
    {
      std::cerr << "Teal API Generator failed with code " << ret << std::endl;
      std::exit(1);
    }
  }
  void WriteConfigFile(fs::path configLocation)
  {
    std::ofstream ofs(configLocation);
    ofs << "wiremap: "
        << (DATA_1D_DIR / "2-dot-1-chargesensor-wiremap.yml").string() << "\n";
    ofs << "quantum-dot-config: "
        << (TEST_ROOT_DIR / "device-configs" / "2-dot-1-chargesensor.yml")
               .string()
        << "\n";
    ofs << "inst-config: " << (DATA_1D_DIR / "multimeter-config.yml").string()
        << ";" << (DATA_1D_DIR / "source-config.yml").string() << "\n";
    ofs << "inst-plugins: "
        << (fs::path(PLUGIN_OUTPUT_DIR) / "mock_multimeter.so").string() << ";"
        << (fs::path(PLUGIN_OUTPUT_DIR) / "mock_voltage_source.so").string()
        << "\n";
    ofs << "instrument-server-port: 5555\n";
    ofs << "local-database: " << current_data_dir_.string() << "\n";
    ofs << "start-embedded-nats: true\n";
    ofs << "user-measurement-luas: " << (LUA_SCRIPTS_DIR).string() << "\n";
    ofs << "instrument-apis:\n";
    ofs << "  - "
        << (INSTRUMENT_APIS_DIR / "generated-multimeter-api.yml").string()
        << "\n";
    ofs << "  - "
        << (INSTRUMENT_APIS_DIR / "generated-source-api.yml").string()
        << "\n";
    ofs.close();
  }
  void BuildTestData(const std::string &cpp_path, const std::string &out_base)
  {
#ifdef _WIN32
    std::string out_path = out_base + ".exe";
    const char *compiler = std::getenv("CXX");
    std::string cmd = (compiler ? compiler : "clang-cl");
    cmd += " /EHsc /Fe:" + out_path + " " + cpp_path;
#else
    std::string out_path = out_base;
    const char *compiler = std::getenv("CXX");
    std::string cmd = (compiler ? compiler : "clang++");
    cmd += " -std=c++17 -o " + out_path + " " + cpp_path;
#endif
    int ret = std::system(cmd.c_str());
    if (ret != 0)
    {
      std::cerr << "Build failed with code " << ret << std::endl;
      std::exit(1);
    }
  }
  void SetISSLuaLibs(const std::vector<std::filesystem::path> &paths)
  {
    std::string libs;
    for (size_t i = 0; i < paths.size(); ++i)
    {
      libs += paths[i].string();
      if (i + 1 < paths.size())
        libs += ";";
    }
    setenv("INSTRUMENT_SCRIPT_SERVER_OPT_LUA_LIB", libs.c_str(), 1);
  }

  void RunTestDataGenerator(const std::string &out_base)
  {
#ifdef _WIN32
    std::string exe_path = out_base + ".exe";
#else
    std::string exe_path = out_base;
#endif
    int ret = std::system(exe_path.c_str());
    if (ret != 0)
    {
      std::cerr << "Data generation failed with code " << ret << std::endl;
      std::exit(1);
    }
  }
  void ExtendInstrumentApis(const std::string &local_path,
                            const std::string &out_path)
  {
    std::string out_base = VCPKG_BIN_DIR / "template-expander";
#ifdef _WIN32
    std::string exe_path = out_base + ".exe";
#else
    std::string exe_path = out_base;
#endif
    std::string cmd = exe_path + " " + local_path + " " + out_path;
    int ret = std::system(cmd.c_str());
    if (ret != 0)
    {
      std::cerr << "API extension failed with code " << ret << std::endl;
      std::exit(1);
    }
  }
  void CompileTeal(const std::string &teal_path, const std::string &out_path)
  {
    std::string cmd = "/home/daniel/.luarocks/bin/tl gen \"" + teal_path + "\" -o \"" + out_path + "\"";
    int ret = std::system(cmd.c_str());
    if (ret != 0)
    {
      std::cerr << "Teal compilation failed with code " << ret << std::endl;
      std::exit(1);
    }
  }
  void CompileMeasurementScripts(
      const std::vector<std::string> &script_names)
  {
    for (const std::string &script_name : script_names)
    {
      fs::path script_path = MEASUREMENT_SCRIPTS_DIR / script_name;
      fs::path out_path = LUA_SCRIPTS_DIR / script_path.stem();
      out_path += ".lua";
      CompileTeal(script_path.string(), out_path.string());
    }
  }
  InstrumentPortSP LookupKnobPort(const char *port_name,
                                  const ConnectionSP &connection)
  {
    return request_knob(port_name, connection, TIMEOUT_MS);
  }
  InstrumentPortSP LookupMeterPort(const char *port_name,
                                   const ConnectionSP &connection)
  {
    return request_meter(port_name, connection, TIMEOUT_MS);
  }
  InstrumentPortSP BuildSettingPort(const char *port_name,
                                    const ConnectionSP &connection,
                                    const Instrument &instrument_type,
                                    const SymbolUnitSP &units,
                                    const std::string &description)
  {
    // Settings are not exposed through PORT_PAYLOAD by design. We model them
    // as channel-scoped request targets here so the hub can still resolve the
    // instrument/channel from the connection and execute the user script.
    return InstrumentPort::Knob(
        port_name, connection, instrument_type, units, description);
  }
  InstrumentPortSP BuildGetterPort(const char *port_name,
                                   const ConnectionSP &connection,
                                   const Instrument &instrument_type,
                                   const SymbolUnitSP &units,
                                   const std::string &description)
  {
    return InstrumentPort::Meter(
        port_name, connection, instrument_type, units, description);
  }
  MeasurementRequestSP MakeSinglePointRequest(
      const std::string &message, const std::string &measurement_name,
      const InstrumentPortSP &target, double target_value)
  {
    InstrumentPortSP clock = InstrumentPort::ExecutionClock();
    MapSP<InstrumentPort, PortTransform> transforms =
        std::make_shared<Map<InstrumentPort, PortTransform>>(
            std::vector<std::pair<InstrumentPortSP, PortTransformSP>>{
                {target, PortTransform::IdentityTransform(target)}});
    LabelledDomainSP time_domain = LabelledDomain::from_port(
        std::pair<double, double>{0.0, 0.0}, clock);
    LabelledDomainSP target_domain = LabelledDomain::from_port(
        std::pair<double, double>{target_value, target_value}, target);
    CoupledLabelledDomainSP coupled_domain =
        std::make_shared<CoupledLabelledDomain>(
            std::vector<LabelledDomainSP>{target_domain});
    MapSP<std::string, bool> increasing =
        std::make_shared<Map<std::string, bool>>(
            std::vector<std::pair<std::string, bool>>{
                {target->default_name(), true}});
    auto waveform = Waveform::CartesianIdentityWaveform1D(
        1, coupled_domain, increasing);
    ListSP<Waveform> waveforms =
        std::make_shared<List<Waveform>>(std::vector<WaveformSP>{waveform});

    return std::make_shared<MeasurementRequest>(
        message, measurement_name, waveforms, std::make_shared<Ports>(),
        transforms, time_domain);
  }
  MeasurementRequestSP MakeTargetOnlyRequest(
      const std::string &message, const std::string &measurement_name,
      const InstrumentPortSP &target)
  {
    return MakeSinglePointRequest(message, measurement_name, target, 1.0);
  }
  MeasurementRequestSP MakeGetterOnlyRequest(
      const std::string &message, const std::string &measurement_name,
      const InstrumentPortSP &target)
  {
    return MakeGetterOnlyRequest(
        message, measurement_name, std::vector<InstrumentPortSP>{target});
  }
  MeasurementRequestSP MakeGetterOnlyRequest(
      const std::string &message, const std::string &measurement_name,
      const std::vector<InstrumentPortSP> &targets)
  {
    InstrumentPortSP clock = InstrumentPort::ExecutionClock();
    ListSP<Waveform> waveforms =
        std::make_shared<List<Waveform>>(std::vector<WaveformSP>{});
    PortsSP getters = std::make_shared<Ports>(targets);
    MapSP<InstrumentPort, PortTransform> transforms =
        std::make_shared<Map<InstrumentPort, PortTransform>>(
            std::vector<std::pair<InstrumentPortSP, PortTransformSP>>{});
    LabelledDomainSP time_domain = LabelledDomain::from_port(
        std::pair<double, double>{0.0, 0.0}, clock);
    return std::make_shared<MeasurementRequest>(
        message, measurement_name, waveforms, getters, transforms,
        time_domain);
  }
  MeasurementRequestSP MakeSinglePointGetterRequest(
      const std::string &message, const std::string &measurement_name,
      const InstrumentPortSP &setter, double setter_value,
      const std::vector<InstrumentPortSP> &getters)
  {
    InstrumentPortSP clock = InstrumentPort::ExecutionClock();
    MapSP<InstrumentPort, PortTransform> transforms =
        std::make_shared<Map<InstrumentPort, PortTransform>>(
            std::vector<std::pair<InstrumentPortSP, PortTransformSP>>{
                {setter, PortTransform::IdentityTransform(setter)}});
    LabelledDomainSP time_domain = LabelledDomain::from_port(
        std::pair<double, double>{0.0, 0.0}, clock);
    LabelledDomainSP target_domain = LabelledDomain::from_port(
        std::pair<double, double>{setter_value, setter_value}, setter);
    CoupledLabelledDomainSP coupled_domain =
        std::make_shared<CoupledLabelledDomain>(
            std::vector<LabelledDomainSP>{target_domain});
    MapSP<std::string, bool> increasing =
        std::make_shared<Map<std::string, bool>>(
            std::vector<std::pair<std::string, bool>>{
                {setter->default_name(), true}});
    auto waveform = Waveform::CartesianIdentityWaveform1D(
        1, coupled_domain, increasing);
    ListSP<Waveform> waveforms =
        std::make_shared<List<Waveform>>(std::vector<WaveformSP>{waveform});

    return std::make_shared<MeasurementRequest>(
        message, measurement_name, waveforms, std::make_shared<Ports>(getters),
        transforms, time_domain);
  }
  MeasurementRequestSP MakeMultiTargetPointRequest(
      const std::string &message, const std::string &measurement_name,
      const std::vector<std::pair<InstrumentPortSP, double>> &targets)
  {
    InstrumentPortSP clock = InstrumentPort::ExecutionClock();
    std::vector<std::pair<InstrumentPortSP, PortTransformSP>> transform_pairs;
    std::vector<WaveformSP> waveform_list;
    transform_pairs.reserve(targets.size());
    waveform_list.reserve(targets.size());

    for (const auto &[target, target_value] : targets)
    {
      transform_pairs.emplace_back(
          target, PortTransform::IdentityTransform(target));
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
    LabelledDomainSP time_domain = LabelledDomain::from_port(
        std::pair<double, double>{0.0, 0.0}, clock);
    ListSP<Waveform> waveforms =
        std::make_shared<List<Waveform>>(waveform_list);

    return std::make_shared<MeasurementRequest>(
        message, measurement_name, waveforms, std::make_shared<Ports>(),
        transforms, time_domain);
  }
  void ExpectSinglePointEchoResponse(const MeasurementResponseSP &resp,
                                     const InstrumentPortSP &target,
                                     const ConnectionSP &connection,
                                     double expected_value)
  {
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
    EXPECT_EQ(labelledArray->instrument_type(), target->instrument_type())
        << "Expected instrument type to match target metadata";
    EXPECT_EQ(*labelledArray->units(), *target->units())
        << "Expected units to match target metadata";
    ASSERT_EQ(labelledArray->size(), 1)
        << "Expected 1 data point, but got " << labelledArray->size()
        << " instead";
    EXPECT_NEAR((*labelledArray)(0), expected_value, 1e-9)
        << "Expected schema response to echo the applied value";
  }
  void ExpectMultiTargetEchoResponse(
      const MeasurementResponseSP &resp,
      const std::vector<std::tuple<InstrumentPortSP, ConnectionSP, double>>
          &expected_targets)
  {
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
    for (const auto &array : resp->arrays()->arrays())
    {
      arrays_by_connection[array->connection()->name()] = array;
    }

    for (const auto &[target, connection, expected_value] : expected_targets)
    {
      auto found = arrays_by_connection.find(connection->name());
      ASSERT_NE(found, arrays_by_connection.end())
          << "Expected a response array for connection " << connection->name();

      auto labelledArray = found->second;
      EXPECT_EQ(*labelledArray->connection(), *connection)
          << "Expected connection to be " << connection->name();
      EXPECT_EQ(labelledArray->instrument_type(), target->instrument_type())
          << "Expected instrument type to match target metadata";
      EXPECT_EQ(*labelledArray->units(), *target->units())
          << "Expected units to match target metadata";
      ASSERT_EQ(labelledArray->size(), 1)
          << "Expected 1 data point for " << connection->name();
      EXPECT_NEAR((*labelledArray)(0), expected_value, 1e-9)
          << "Expected schema response to echo the applied value for "
          << connection->name();
    }
  }
  double ReadFirstScalarFromFile(const fs::path &data_file)
  {
    std::ifstream input(data_file);
    double value = 0.0;
    if (!(input >> value))
    {
      throw std::runtime_error(
          "Failed to read first scalar value from " + data_file.string());
    }
    return value;
  }
  void WaitForNats(const char *host, int port, int timeout_ms)
  {
    int waited = 0;
    while (waited < timeout_ms)
    {
#ifdef _WIN32
      SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_port = htons(port);
      inet_pton(AF_INET, host, &addr.sin_addr);
      if (connect(s, (sockaddr *)&addr, sizeof(addr)) == 0)
      {
        closesocket(s);
        return;
      }
      closesocket(s);
      Sleep(100);
#else
      int s = socket(AF_INET, SOCK_STREAM, 0);
      struct sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_port = htons(port);
      inet_pton(AF_INET, host, &addr.sin_addr);
      if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == 0)
      {
        close(s);
        return;
      }
      close(s);
      usleep(100000);
#endif
      waited += 100;
    }
    std::cerr << "NATS did not become available within " << timeout_ms << "ms\n";
  }
  void WaitForHubReady(int timeout_ms)
  {
    // Subscribe to STATUS.instrument-server which the hub publishes after
    // all handlers are set up (completing Phase 3 of hub startup).
    // This ensures DeviceConfigRequest and other handler subscriptions are ready.
    std::promise<bool> prom;
    auto fut = prom.get_future();
    std::atomic<bool> done{false};

    falcon::comms::NatsManager &nm = falcon::comms::NatsManager::instance();
    
    nm.subscribe("STATUS.instrument-server", 
                 [&prom, &done](const std::string &) {
        if (!done.exchange(true)) {
          try {
            prom.set_value(true);
          } catch (const std::future_error &) {
            // promise already set
          }
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


TEST_F(DataRetrievalTest, SetVoltage)
{
  const char *SETTER_NAME = "P1";
  const double TARGET_VOLTAGE = 0.123;
  InstrumentPortSP setter = LookupKnobPort(
      HUB_SOURCE_VOLTAGE_PORT, Connection::PlungerGate(SETTER_NAME));
  MeasurementRequestSP request = MakeSinglePointRequest(
      "Setting P1 via set_voltage schema", "set_voltage", setter,
      TARGET_VOLTAGE);
  auto resp = request_measurement(request, TIMEOUT_MS);

  ExpectSinglePointEchoResponse(
      resp, setter, Connection::PlungerGate(SETTER_NAME), TARGET_VOLTAGE);
}

TEST_F(DataRetrievalTest, SetSampleRate)
{

  const char *GETTER_NAME = "O1";
  const double TARGET_SAMPLE_RATE = 1000.0;
  InstrumentPortSP getter = BuildSettingPort(
      HUB_METER_SAMPLE_RATE_PORT, Connection::Ohmic(GETTER_NAME),
      InstrumentTypes::VOLTMETER, SymbolUnit::Hertz(),
      "Sample rate setting for multimeter channel");
  MeasurementRequestSP request = MakeSinglePointRequest(
      "Setting O1 sample rate via set_sample_rate schema",
      "set_sample_rate", getter, TARGET_SAMPLE_RATE);
  auto resp = request_measurement(request, TIMEOUT_MS);

  ExpectSinglePointEchoResponse(
      resp, getter, Connection::Ohmic(GETTER_NAME), TARGET_SAMPLE_RATE);
}

TEST_F(DataRetrievalTest, SetNumberOfSamples)
{
  const char *GETTER_NAME = "O1";
  const double TARGET_NUMBER_OF_SAMPLES = 16.0;
  InstrumentPortSP getter = BuildSettingPort(
      HUB_METER_BINS_PORT, Connection::Ohmic(GETTER_NAME),
      InstrumentTypes::VOLTMETER, SymbolUnit::Dimensionless(),
      "Averaging bin-count setting for multimeter channel");
  MeasurementRequestSP request = MakeSinglePointRequest(
      "Setting O1 bins via set_number_of_samples schema",
      "set_number_of_samples", getter, TARGET_NUMBER_OF_SAMPLES);
  auto resp = request_measurement(request, TIMEOUT_MS);

  ExpectSinglePointEchoResponse(resp, getter, Connection::Ohmic(GETTER_NAME),
                                TARGET_NUMBER_OF_SAMPLES);
}

TEST_F(DataRetrievalTest, SetManyVoltages)
{
  InstrumentPortSP setter1 = LookupKnobPort(
      HUB_SOURCE_VOLTAGE_PORT, Connection::PlungerGate("P1"));
  InstrumentPortSP setter2 = LookupKnobPort(
      HUB_SOURCE_VOLTAGE_PORT, Connection::PlungerGate("P2"));
  MeasurementRequestSP request = MakeMultiTargetPointRequest(
      "Setting P1 and P2 via set_many_voltages schema", "set_many_voltages",
      {{setter1, 0.123}, {setter2, -0.234}});
  auto resp = request_measurement(request, TIMEOUT_MS);

  ExpectMultiTargetEchoResponse(
      resp,
      {{setter1, Connection::PlungerGate("P1"), 0.123},
       {setter2, Connection::PlungerGate("P2"), -0.234}});
}

TEST_F(DataRetrievalTest, Ramp)
{
  InstrumentPortSP setter1 = LookupKnobPort(
      HUB_SOURCE_VOLTAGE_PORT, Connection::PlungerGate("P1"));
  InstrumentPortSP setter2 = LookupKnobPort(
      HUB_SOURCE_VOLTAGE_PORT, Connection::PlungerGate("P2"));
  MeasurementRequestSP request = MakeMultiTargetPointRequest(
      "Ramping P1 and P2 via ramp schema", "ramp",
      {{setter1, 0.25}, {setter2, -0.15}});
  auto resp = request_measurement(request, TIMEOUT_MS);

  ExpectMultiTargetEchoResponse(
      resp,
      {{setter1, Connection::PlungerGate("P1"), 0.25},
       {setter2, Connection::PlungerGate("P2"), -0.15}});
}

TEST_F(DataRetrievalTest, SetSlope)
{
  const char *SETTER_NAME = "P1";
  const double TARGET_SLOPE = 0.75;
  InstrumentPortSP setter = BuildSettingPort(
      HUB_SOURCE_SLOPE_PORT, Connection::PlungerGate(SETTER_NAME),
      InstrumentTypes::DC_VOLTAGE_SOURCE, SymbolUnit::VoltsPerSecond(),
      "Slope setting for source channel");
  MeasurementRequestSP request = MakeSinglePointRequest(
      "Setting P1 slope via set_slope schema", "set_slope", setter,
      TARGET_SLOPE);
  auto resp = request_measurement(request, TIMEOUT_MS);

  ExpectSinglePointEchoResponse(
      resp, setter, Connection::PlungerGate(SETTER_NAME), TARGET_SLOPE);
}

TEST_F(DataRetrievalTest, SetTriggerLeader)
{
  const char *GETTER_NAME = "O1";
  const double ACK_VALUE = 1.0;
  InstrumentPortSP getter = BuildSettingPort(
      HUB_METER_TRIGGER_LEADER_PORT, Connection::Ohmic(GETTER_NAME),
      InstrumentTypes::VOLTMETER, SymbolUnit::Dimensionless(),
      "Trigger leader selection for multimeter channel");
  MeasurementRequestSP request = MakeTargetOnlyRequest(
      "Setting O1 trigger leader via set_trigger_leader schema",
      "set_trigger_leader", getter);
  auto resp = request_measurement(request, TIMEOUT_MS);

  ExpectSinglePointEchoResponse(
      resp, getter, Connection::Ohmic(GETTER_NAME), ACK_VALUE);
}

TEST_F(DataRetrievalTest, GetVoltage)
{
  const char *TARGET_NAME = "P1";
  const double TARGET_VOLTAGE = 0.456;
  InstrumentPortSP setter = LookupKnobPort(
      HUB_SOURCE_VOLTAGE_PORT, Connection::PlungerGate(TARGET_NAME));
  auto set_request = MakeSinglePointRequest(
      "Priming P1 via set_voltage schema", "set_voltage", setter,
      TARGET_VOLTAGE);
  (void)request_measurement(set_request, TIMEOUT_MS);

  InstrumentPortSP getter = BuildGetterPort(
      HUB_SOURCE_MEASURED_VOLTAGE_PORT, Connection::PlungerGate(TARGET_NAME),
      InstrumentTypes::DC_VOLTAGE_SOURCE, SymbolUnit::Volt(),
      "Measured voltage getter for source channel");
  auto request = MakeGetterOnlyRequest(
      "Reading P1 via get_voltage schema", "get_voltage", getter);
  auto resp = request_measurement(request, TIMEOUT_MS);

  ExpectSinglePointEchoResponse(
      resp, getter, Connection::PlungerGate(TARGET_NAME), TARGET_VOLTAGE);
}

TEST_F(DataRetrievalTest, GetSampleRate)
{
  const char *GETTER_NAME = "O1";
  const double TARGET_SAMPLE_RATE = 1000.0;
  InstrumentPortSP setter = BuildSettingPort(
      HUB_METER_SAMPLE_RATE_PORT, Connection::Ohmic(GETTER_NAME),
      InstrumentTypes::VOLTMETER, SymbolUnit::Hertz(),
      "Sample rate setting for multimeter channel");
  auto set_request = MakeSinglePointRequest(
      "Priming O1 sample rate via set_sample_rate schema",
      "set_sample_rate", setter, TARGET_SAMPLE_RATE);
  (void)request_measurement(set_request, TIMEOUT_MS);

  InstrumentPortSP getter = BuildGetterPort(
      HUB_METER_SAMPLE_RATE_PORT, Connection::Ohmic(GETTER_NAME),
      InstrumentTypes::VOLTMETER, SymbolUnit::Hertz(),
      "Sample rate getter for multimeter channel");
  auto request = MakeGetterOnlyRequest(
      "Reading O1 sample rate via get_sample_rate schema",
      "get_sample_rate", getter);
  auto resp = request_measurement(request, TIMEOUT_MS);

  ExpectSinglePointEchoResponse(
      resp, getter, Connection::Ohmic(GETTER_NAME), TARGET_SAMPLE_RATE);
}

TEST_F(DataRetrievalTest, GetNumberOfSamples)
{
  const char *GETTER_NAME = "O1";
  const double TARGET_NUMBER_OF_SAMPLES = 16.0;
  InstrumentPortSP setter = BuildSettingPort(
      HUB_METER_BINS_PORT, Connection::Ohmic(GETTER_NAME),
      InstrumentTypes::VOLTMETER, SymbolUnit::Dimensionless(),
      "Averaging bin-count setting for multimeter channel");
  auto set_request = MakeSinglePointRequest(
      "Priming O1 bins via set_number_of_samples schema",
      "set_number_of_samples", setter, TARGET_NUMBER_OF_SAMPLES);
  (void)request_measurement(set_request, TIMEOUT_MS);

  InstrumentPortSP getter = BuildGetterPort(
      HUB_METER_BINS_PORT, Connection::Ohmic(GETTER_NAME),
      InstrumentTypes::VOLTMETER, SymbolUnit::Dimensionless(),
      "Averaging bin-count getter for multimeter channel");
  auto request = MakeGetterOnlyRequest(
      "Reading O1 bins via get_number_of_samples schema",
      "get_number_of_samples", getter);
  auto resp = request_measurement(request, TIMEOUT_MS);

  ExpectSinglePointEchoResponse(
      resp, getter, Connection::Ohmic(GETTER_NAME),
      TARGET_NUMBER_OF_SAMPLES);
}

TEST_F(DataRetrievalTest, GetSlope)
{
  const char *TARGET_NAME = "P1";
  const double TARGET_SLOPE = 0.75;
  InstrumentPortSP setter = BuildSettingPort(
      HUB_SOURCE_SLOPE_PORT, Connection::PlungerGate(TARGET_NAME),
      InstrumentTypes::DC_VOLTAGE_SOURCE, SymbolUnit::VoltsPerSecond(),
      "Slope setting for source channel");
  auto set_request = MakeSinglePointRequest(
      "Priming P1 slope via set_slope schema", "set_slope", setter,
      TARGET_SLOPE);
  (void)request_measurement(set_request, TIMEOUT_MS);

  InstrumentPortSP getter = BuildGetterPort(
      HUB_SOURCE_SLOPE_PORT, Connection::PlungerGate(TARGET_NAME),
      InstrumentTypes::DC_VOLTAGE_SOURCE, SymbolUnit::VoltsPerSecond(),
      "Slope getter for source channel");
  auto request = MakeGetterOnlyRequest(
      "Reading P1 slope via get_slope schema", "get_slope", getter);
  auto resp = request_measurement(request, TIMEOUT_MS);

  ExpectSinglePointEchoResponse(
      resp, getter, Connection::PlungerGate(TARGET_NAME), TARGET_SLOPE);
}

TEST_F(DataRetrievalTest, GetTriggerLeader)
{
  const char *GETTER_NAME = "O1";
  const double EXPECTED_VALUE = 1.0;
  InstrumentPortSP setter = BuildSettingPort(
      HUB_METER_TRIGGER_LEADER_PORT, Connection::Ohmic(GETTER_NAME),
      InstrumentTypes::VOLTMETER, SymbolUnit::Dimensionless(),
      "Trigger leader selection for multimeter channel");
  auto set_request = MakeTargetOnlyRequest(
      "Priming O1 trigger leader via set_trigger_leader schema",
      "set_trigger_leader", setter);
  (void)request_measurement(set_request, TIMEOUT_MS);

  InstrumentPortSP getter = BuildGetterPort(
      HUB_METER_TRIGGER_LEADER_PORT, Connection::Ohmic(GETTER_NAME),
      InstrumentTypes::VOLTMETER, SymbolUnit::Dimensionless(),
      "Trigger leader getter for multimeter channel");
  auto request = MakeGetterOnlyRequest(
      "Reading O1 trigger leader via get_trigger_leader schema",
      "get_trigger_leader", getter);
  auto resp = request_measurement(request, TIMEOUT_MS);

  ExpectSinglePointEchoResponse(
      resp, getter, Connection::Ohmic(GETTER_NAME), EXPECTED_VALUE);
}

TEST_F(DataRetrievalTest, GetManyVoltages)
{
  const double P1_VOLTAGE = 0.111;
  const double P2_VOLTAGE = 0.222;

  InstrumentPortSP setter1 = LookupKnobPort(
      HUB_SOURCE_VOLTAGE_PORT, Connection::PlungerGate("P1"));
  InstrumentPortSP setter2 = LookupKnobPort(
      HUB_SOURCE_VOLTAGE_PORT, Connection::PlungerGate("P2"));
  auto set_request = MakeMultiTargetPointRequest(
      "Priming P1/P2 via set_many_voltages schema", "set_many_voltages",
      {{setter1, P1_VOLTAGE}, {setter2, P2_VOLTAGE}});
  (void)request_measurement(set_request, TIMEOUT_MS);

  InstrumentPortSP getter1 = BuildGetterPort(
      HUB_SOURCE_MEASURED_VOLTAGE_PORT, Connection::PlungerGate("P1"),
      InstrumentTypes::DC_VOLTAGE_SOURCE, SymbolUnit::Volt(),
      "Measured voltage getter for source channel P1");
  InstrumentPortSP getter2 = BuildGetterPort(
      HUB_SOURCE_MEASURED_VOLTAGE_PORT, Connection::PlungerGate("P2"),
      InstrumentTypes::DC_VOLTAGE_SOURCE, SymbolUnit::Volt(),
      "Measured voltage getter for source channel P2");
  auto request = MakeGetterOnlyRequest(
      "Reading P1/P2 via get_many_voltages schema", "get_many_voltages",
      std::vector<InstrumentPortSP>{getter1, getter2});
  auto resp = request_measurement(request, TIMEOUT_MS);

  ExpectMultiTargetEchoResponse(
      resp,
      {{getter1, Connection::PlungerGate("P1"), P1_VOLTAGE},
       {getter2, Connection::PlungerGate("P2"), P2_VOLTAGE}});
}

TEST_F(DataRetrievalTest, GetAllVoltages)
{
  const double P1_VOLTAGE = 0.333;
  const double P2_VOLTAGE = 0.444;

  InstrumentPortSP setter1 = LookupKnobPort(
      HUB_SOURCE_VOLTAGE_PORT, Connection::PlungerGate("P1"));
  InstrumentPortSP setter2 = LookupKnobPort(
      HUB_SOURCE_VOLTAGE_PORT, Connection::PlungerGate("P2"));
  auto set_request = MakeMultiTargetPointRequest(
      "Priming P1/P2 via set_many_voltages schema for get_all_voltages",
      "set_many_voltages", {{setter1, P1_VOLTAGE}, {setter2, P2_VOLTAGE}});
  (void)request_measurement(set_request, TIMEOUT_MS);

  InstrumentPortSP getter1 = BuildGetterPort(
      HUB_SOURCE_MEASURED_VOLTAGE_PORT, Connection::PlungerGate("P1"),
      InstrumentTypes::DC_VOLTAGE_SOURCE, SymbolUnit::Volt(),
      "Measured voltage getter for source channel P1");
  InstrumentPortSP getter2 = BuildGetterPort(
      HUB_SOURCE_MEASURED_VOLTAGE_PORT, Connection::PlungerGate("P2"),
      InstrumentTypes::DC_VOLTAGE_SOURCE, SymbolUnit::Volt(),
      "Measured voltage getter for source channel P2");
  auto request = MakeGetterOnlyRequest(
      "Reading all voltages via get_all_voltages schema",
      "get_all_voltages", std::vector<InstrumentPortSP>{getter1, getter2});
  auto resp = request_measurement(request, TIMEOUT_MS);

  ExpectMultiTargetEchoResponse(
      resp,
      {{getter1, Connection::PlungerGate("P1"), P1_VOLTAGE},
       {getter2, Connection::PlungerGate("P2"), P2_VOLTAGE}});
}

TEST_F(DataRetrievalTest, MeasureCurrent)
{
  const char *GETTER_NAME = "O1";
  const double EXPECTED_CURRENT = ReadFirstScalarFromFile(TEST_DATA_FILE);

  InstrumentPortSP getter = BuildGetterPort(
      HUB_METER_VOLTAGE_PORT, Connection::Ohmic(GETTER_NAME),
      InstrumentTypes::VOLTMETER, SymbolUnit::NanoAmpere(),
      "Current measurement getter for multimeter channel");
  auto request = MakeGetterOnlyRequest(
      "Measuring current via measure_current schema", "measure_current",
      getter);
  auto resp = request_measurement(request, TIMEOUT_MS);

  ExpectSinglePointEchoResponse(
      resp, getter, Connection::Ohmic(GETTER_NAME), EXPECTED_CURRENT);
}

TEST_F(DataRetrievalTest, MeasureIllumination)
{
  const char *GETTER_NAME = "O2";
  const double EXPECTED_CURRENT =
      ReadFirstScalarFromFile(TEST_DATA_DIR_PATH / "linear-1d.txt");

  InstrumentPortSP getter = BuildGetterPort(
      HUB_METER_VOLTAGE_PORT, Connection::Ohmic(GETTER_NAME),
      InstrumentTypes::VOLTMETER, SymbolUnit::NanoAmpere(),
      "Illumination measurement getter for multimeter channel");
  auto request = MakeGetterOnlyRequest(
      "Measuring illumination via measure_illumination schema",
      "measure_illumination", getter);
  auto resp = request_measurement(request, TIMEOUT_MS);

  ExpectSinglePointEchoResponse(
      resp, getter, Connection::Ohmic(GETTER_NAME), EXPECTED_CURRENT);
}

TEST_F(DataRetrievalTest, MeasureLeakage)
{
  const char *TARGET_NAME = "P1";
  const double LEAKAGE_VOLTAGE = 0.512;

  InstrumentPortSP setter = LookupKnobPort(
      HUB_SOURCE_VOLTAGE_PORT, Connection::PlungerGate(TARGET_NAME));
  InstrumentPortSP getter = BuildGetterPort(
      HUB_SOURCE_MEASURED_VOLTAGE_PORT, Connection::PlungerGate(TARGET_NAME),
      InstrumentTypes::DC_VOLTAGE_SOURCE, SymbolUnit::NanoAmpere(),
      "Leakage measurement getter for source channel");
  auto request = MakeSinglePointGetterRequest(
      "Measuring leakage via measure_leakage schema", "measure_leakage",
      setter, LEAKAGE_VOLTAGE, std::vector<InstrumentPortSP>{getter});
  auto resp = request_measurement(request, TIMEOUT_MS);

  ExpectSinglePointEchoResponse(
      resp, getter, Connection::PlungerGate(TARGET_NAME), LEAKAGE_VOLTAGE);
}


TEST_F(DataRetrievalTest, Gaussian1DMeasureGetSet)
{
  const int NUM_POINTS = 100;
  const double START_TIME_SECONDS = 0.0;
  const double MAX_TIME_SECONDS = 1.0;
  const char *DEPENDANT_NAME = "P1";
  const char *GETTER_NAME = "O1";
  const double MIN_INDEPENDANT_VALUE = 0.0;
  const double MAX_INDEPENDANT_VALUE = 0.5;
  ConfigSP config = request_config(TIMEOUT_MS);
  ASSERT_NE(config, nullptr) << "Failed to get config from request_config";

  InstrumentPortSP getter = request_meter(HUB_METER_STREAM_PORT,
                                          Connection::Ohmic(GETTER_NAME),
                                          TIMEOUT_MS);
  PortsSP getters =
      std::make_shared<Ports>(std::vector<InstrumentPortSP>{getter});

  InstrumentPortSP independantKnob =
      request_knob(HUB_SOURCE_VOLTAGE_PORT,
                   Connection::PlungerGate(DEPENDANT_NAME), TIMEOUT_MS);
  InstrumentPortSP clock = InstrumentPort::ExecutionClock();

  MapSP<InstrumentPort, PortTransform> transforms =
      std::make_shared<Map<InstrumentPort, PortTransform>>(
          std::vector<std::pair<InstrumentPortSP, PortTransformSP>>{
              {independantKnob,
               PortTransform::IdentityTransform(independantKnob)}});
  LabelledDomainSP time_domain = LabelledDomain::from_port(
      std::pair<double, double>{START_TIME_SECONDS, MAX_TIME_SECONDS}, clock);
  LabelledDomainSP independantDomain = LabelledDomain::from_port(
      std::pair<double, double>{MIN_INDEPENDANT_VALUE, MAX_INDEPENDANT_VALUE},
      independantKnob);
  CoupledLabelledDomainSP coupledDomain =
      std::make_shared<CoupledLabelledDomain>(
          std::vector<LabelledDomainSP>{independantDomain});
  MapSP<std::string, bool> increasing =
      std::make_shared<Map<std::string, bool>>(
          std::vector<std::pair<std::string, bool>>{
              {independantKnob->default_name(), true}});

  auto waveform = Waveform::CartesianIdentityWaveform1D(
      NUM_POINTS, coupledDomain, increasing);
  ListSP<Waveform> waveforms =
      std::make_shared<List<Waveform>>(std::vector<WaveformSP>{waveform});

  MeasurementRequestSP request = std::make_shared<MeasurementRequest>(
      "Taking a measurement via measure_get_set", "measure_get_set",
      waveforms, getters, transforms, time_domain);
  auto resp = request_measurement(request, TIMEOUT_MS);

  EXPECT_TRUE(resp->arrays()->is_measured_arrays())
      << "The arrays were not measured arrays";
  EXPECT_EQ(resp->arrays()->size(), 1) << "Expected exactly one measured array";
  auto labelledArray = resp->arrays()->arrays()[0];
  EXPECT_EQ(*labelledArray->connection(),
            *Connection::PlungerGate(DEPENDANT_NAME))
      << "Expected connection to be P1, but got "
      << labelledArray->connection()->name() << " instead";
  EXPECT_EQ(labelledArray->instrument_type(), InstrumentTypes::VOLTMETER)
      << "Expected instrument type to be VOLTMETER, but got "
      << labelledArray->instrument_type() << " instead";
  EXPECT_EQ(*labelledArray->units(), *SymbolUnit::Volt())
      << "Expected units to be mV, but got " << labelledArray->units()
      << " instead";
  EXPECT_EQ(labelledArray->size(), NUM_POINTS)
      << "Expected 100 data points, but got " << labelledArray->size()
      << " instead";
}

TEST_F(DataRetrievalTest, VoltageSweepCurrent)
{
    // GTEST_SKIP() << "Skipping all tests for this fixture";
  const int NUM_POINTS = 100;
  const double START_TIME_SECONDS = 0.0;
  const double MAX_TIME_SECONDS = 1.0;
  const char *SWEEP_NAME = "P1";
  const char *GETTER_NAME = "O2";
  const double MIN_SWEEP_VOLTAGE = 0.0;
  const double MAX_SWEEP_VOLTAGE = 0.5;
  ConfigSP config = request_config(TIMEOUT_MS);
  ASSERT_NE(config, nullptr) << "Failed to get config from request_config";

  InstrumentPortSP currentMeter = request_meter(HUB_METER_STREAM_PORT,
                                                Connection::Ohmic(GETTER_NAME),
                                                TIMEOUT_MS);
  PortsSP getters =
      std::make_shared<Ports>(std::vector<InstrumentPortSP>{currentMeter});

  // O2 shares Meter1 (VOLTMETER) with O1. PORT_PAYLOAD now publishes the
  // canonical getter metadata directly, including the expected mV units.
  // In a physical setup the raw mV reading would be scaled to nA by the
  // transresistance amplifier gain; the test data (linear-1d.txt) represents
  // that conceptual current sweep.
  InstrumentPortSP voltageKnob =
      request_knob(HUB_SOURCE_VOLTAGE_PORT,
                   Connection::PlungerGate(SWEEP_NAME), TIMEOUT_MS);
  InstrumentPortSP clock = InstrumentPort::ExecutionClock();

  MapSP<InstrumentPort, PortTransform> transforms =
      std::make_shared<Map<InstrumentPort, PortTransform>>(
          std::vector<std::pair<InstrumentPortSP, PortTransformSP>>{
              {voltageKnob,
               PortTransform::IdentityTransform(voltageKnob)}});
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
      "1D voltage sweep recording current at O2", "measure_1D_buffered",
      waveforms, getters, transforms, time_domain);
  auto resp = request_measurement(request, TIMEOUT_MS);

  EXPECT_TRUE(resp->arrays()->is_measured_arrays())
      << "The arrays were not measured arrays";
  EXPECT_EQ(resp->arrays()->size(), 1) << "Expected exactly one measured array";
  auto labelledArray = resp->arrays()->arrays()[0];
  EXPECT_EQ(*labelledArray->connection(),
            *Connection::PlungerGate(SWEEP_NAME))
      << "Expected connection to be P1, but got "
      << labelledArray->connection()->name() << " instead";
  EXPECT_EQ(labelledArray->instrument_type(), InstrumentTypes::VOLTMETER)
      << "Expected instrument type to be VOLTMETER, but got "
      << labelledArray->instrument_type() << " instead";
  EXPECT_EQ(*labelledArray->units(), *SymbolUnit::Volt())
      << "Expected units to be mV, but got " << labelledArray->units()
      << " instead";
  EXPECT_EQ(labelledArray->size(), NUM_POINTS)
      << "Expected 100 data points, but got " << labelledArray->size()
      << " instead";
}

TEST_F(DataRetrievalTest, VoltageSweepCurrent2D)
{
    // GTEST_SKIP() << "Skipping all tests for this fixture";
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

  InstrumentPortSP currentMeter = request_meter(HUB_METER_STREAM_PORT,
                                                Connection::Ohmic(GETTER_NAME),
                                                TIMEOUT_MS);
  PortsSP getters =
      std::make_shared<Ports>(std::vector<InstrumentPortSP>{currentMeter});

  InstrumentPortSP fastKnob =
      request_knob(HUB_SOURCE_VOLTAGE_PORT,
                   Connection::PlungerGate(FAST_GATE), TIMEOUT_MS);
  InstrumentPortSP slowKnob =
      request_knob(HUB_SOURCE_VOLTAGE_PORT,
                   Connection::PlungerGate(SLOW_GATE), TIMEOUT_MS);
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

  CoupledLabelledDomainSP fastCoupled =
      std::make_shared<CoupledLabelledDomain>(
          std::vector<LabelledDomainSP>{fastDomain});
  CoupledLabelledDomainSP slowCoupled =
      std::make_shared<CoupledLabelledDomain>(
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
  ListSP<Waveform> waveforms =
      std::make_shared<List<Waveform>>(
          std::vector<WaveformSP>{fastWaveform, slowWaveform});

  MeasurementRequestSP request = std::make_shared<MeasurementRequest>(
      "2D voltage sweep recording current at O2", "measure_2D_buffered",
      waveforms, getters, transforms, time_domain);
  auto resp = request_measurement(request, TIMEOUT_MS);

  EXPECT_TRUE(resp->arrays()->is_measured_arrays())
      << "The arrays were not measured arrays";
  EXPECT_EQ(resp->arrays()->size(), 1) << "Expected exactly one measured array";
  auto labelledArray = resp->arrays()->arrays()[0];
  EXPECT_EQ(*labelledArray->connection(),
            *Connection::PlungerGate(FAST_GATE))
      << "Expected connection to be P1, but got "
      << labelledArray->connection()->name() << " instead";
  EXPECT_EQ(labelledArray->instrument_type(), InstrumentTypes::VOLTMETER)
      << "Expected instrument type to be VOLTMETER, but got "
      << labelledArray->instrument_type() << " instead";
  EXPECT_EQ(*labelledArray->units(), *SymbolUnit::Volt())
      << "Expected units to be mV, but got " << labelledArray->units()
      << " instead";
  EXPECT_EQ(labelledArray->size(), N_FAST * N_SLOW)
      << "Expected " << N_FAST * N_SLOW << " data points, but got "
      << labelledArray->size() << " instead";
}
