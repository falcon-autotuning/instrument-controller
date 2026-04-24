#include <falcon-core/generic/Map.hpp>
#include <falcon-core/instrument_interfaces/Waveform.hpp>
#include <falcon-core/instrument_interfaces/names/InstrumentPort.hpp>
#include <falcon-core/instrument_interfaces/names/InstrumentTypes.hpp>
#include <falcon-core/physics/device_structures/Connection.hpp>
#include <falcon-routine/hub.hpp>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <sys/types.h>
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
const int TIMEOUT_MS = 1000;
namespace fs = std::filesystem;
const fs::path DATA_1D_DIR = fs::path(__FILE__).parent_path();
const fs::path TEST_ROOT_DIR = DATA_1D_DIR.parent_path();
const fs::path TEST_DATA_DIR = TEST_ROOT_DIR / "test-data";
const fs::path TEST_DATA_FILE = TEST_DATA_DIR / "gaussian-1d.txt";
const fs::path TEST_DATA_SCRIPT = TEST_DATA_DIR / "gen_data.cpp";
const fs::path TEST_DATA_GENERATOR = TEST_DATA_DIR / "gen_data";
const fs::path TEST_CONFIG_FILE = DATA_1D_DIR / "test-config.yaml";
const fs::path VCPKG_BIN_DIR =
    std::get_env("VCPKG_INSTALLED_DIR") / std::get_env("VCPKG_TRIPLET") / "bin";
const fs::path VCPKG_LIB_DIR =
    std::get_env("VCPKG_INSTALLED_DIR") / std::get_env("VCPKG_TRIPLET") / "lib";
const fs::path INSTRUMENT_APIS_DIR =
    TEST_ROOT_DIR / fs::path("instrument-apis");
const fs::path TEAL_APIS_DIR = TEST_ROOT_DIR / "teal";
const fs::path LUA_SCRIPTS_DIR = TEST_ROOT_DIR / "lua";
const fs::path DATA_DIR = TEST_ROOT_DIR / "data";
const fs::path MEASUREMENT_SCRIPTS_DIR = DATA_1D_DIR / "measurement-scripts";
const fs::path INSTRUMENT_LUA_LIBS_DIR = TEST_ROOT_DIR / "instrument-lua-libs";
const fs::path WORKING_DIR = TEST_ROOT_DIR / "hub";

class DataRetrievalTest : public ::testing::Test {
protected:
  void SetUp() override {
    BuildTestData(TEST_DATA_SCRIPT, TEST_DATA_GENERATOR);
    RunTestDataGenerator(TEST_DATA_GENERATOR);
    WriteConfigFile(TEST_CONFIG_FILE);
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
    CompileTeal((MEASUREMENT_SCRIPTS_DIR / "measure_get_set.tl").string(),
                (LUA_SCRIPTS_DIR / "measure_get_set.lua").string());
    CompileTeal((MEASUREMENT_SCRIPTS_DIR / "set_voltage.tl").string(),
                (LUA_SCRIPTS_DIR / "set_voltage.lua").string());
    SetISSLuaLibs(std::vector<std::filesystem::path>{
        INSTRUMENT_LUA_LIBS_DIR / "multimeter.lua",
        INSTRUMENT_LUA_LIBS_DIR / "source.lua",
        // TODO: Insert the library from the measurement-lib
    });
    // TODO: Make sure that the instrument-hub is actually in the bin directory
    StartInstrumentHub(VCPKG_BIN_DIR / "instrument-hub");
    setenv("MOCK_MULTIMETER_DATA_FILE", TEST_DATA_FILE.c_str(), 1);
  }

  void TearDown() override {
    unsetenv("MOCK_MULTIMETER_DATA_FILE");
    StopInstrumentHub();
    // TODO: Uncomment these when no more debugging necessary - currently we
    // want to inspect the generated files after the test runs
    //   fs::remove(TEST_DATA_FILE);
    //   fs::remove(TEST_DATA_GENERATOR);
    //   fs::remove(INSTRUMENT_APIS_DIR / "generated-multimeter-api.yml");
    //   fs::remove(INSTRUMENT_APIS_DIR / "generated-source-api.yml");
    //   fs::remove_all(TEAL_APIS_DIR);
    //   fs::remove_all(LUA_SCRIPTS_DIR);
    //   fs::remove_all(DATA_DIR);
    //   fs::remove_all(INSTUMENT_LUA_LIBS_DIR);
  }
  void StartInstrumentHub(const std::filesystem::path &hub_executable) {
#ifdef _WIN32
    std::string exe = hub_executable.string() + ".exe";
    std::string cmd = exe + "start --hub-config " + TEST_CONFIG_FILE.string() +
                      " --iss-lib-path " + VCPKG_LIB_DIR.string() +
                      " --working-dir " + WORKING_DIR.string() +
                      " --iss-binary " +
                      (VCPKG_BIN_DIR / "instrument-script-server").string();
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
      execl(hub_executable.c_str(), hub_executable.c_str(), "start",
            "--hub-config", config_path.c_str(), "--iss-lib-path",
            VCPKG_LIB_DIR.string(), "--working-dir", WORKING_DIR.string(),
            "--iss-binary",
            (VCPKG_BIN_DIR / "instrument-script-server").string(),
            (char *)NULL);
      std::cerr << "Failed to exec Instrument Hub\n";
      std::exit(1);
    } else if (pid > 0) {
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
      hub_pid_ = -1;
    }
#endif
  }
  void GenerateTealInstrumentLibs(const std::string &local_path,
                                  const std::string &out_path) {
    std::string out_base = VCPKG_BIN_DIR / "teal-api-gen-cli";
#ifdef _WIN32
    std::string exe_path = out_base + ".exe";
    std::string cmd = exe_path + " " + local_path + " " + out_path;
#else
    std::string exe_path = out_base;
    std::string cmd = "./" + exe_path + " " + local_path + " " + out_path;
#endif
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
      std::cerr << "Teal API Generator failed with code " << ret << std::endl;
      std::exit(1);
    }
  }
  void WriteConfigFile(fs::path configLocation) {
    std::ofstream ofs(config_path);
    ofs << "wiremap: "
        << (DATA_1D_DIR / "2-dot-1-chargesensor-wiremap.yml").string() << "\n";
    ofs << "quantum-dot-config: "
        << (TEST_ROOT_DIR / "device-configs" / "2-dot-1-chargesensor.yml")
               .string()
        << "\n";
    ofs << "inst-config: " << (DATA_1D_DIR / "multimeter-config.yml").string()
        << ";" << (DATA_1D_DIR / "source-config.yml").string() << "\n";
    ofs << "instrument-server-port: 5555\n";
    ofs << "local-database: " << (DATA_DIR).string() << "\n";
    ofs << "user-measurement-luas: " << (LUA_SCRIPTS_DIR).string() << "\n";
    ofs.close();
  }
  void BuildTestData(const std::string &cpp_path, const std::string &out_base) {
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
    if (ret != 0) {
      std::cerr << "Build failed with code " << ret << std::endl;
      std::exit(1);
    }
  }
  void SetISSLuaLibs(const std::vector<std::filesystem::path> &paths) {
    std::string libs;
    for (size_t i = 0; i < paths.size(); ++i) {
      libs += paths[i].string();
      if (i + 1 < paths.size())
        libs += ";";
    }
    setenv("INSTRUMENT_SCRIPT_SERVER_OPT_LUA_LIB", libs.c_str(), 1);
  }

  void RunTestDataGenerator(const std::string &out_base) {
#ifdef _WIN32
    std::string exe_path = out_base + ".exe";
    std::string cmd = exe_path;
#else
    std::string exe_path = out_base;
    std::string cmd = "./" + exe_path;
#endif
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
      std::cerr << "Data generation failed with code " << ret << std::endl;
      std::exit(1);
    }
  }
  void ExtendInstrumentApis(const std::string &local_path,
                            const std::string &out_path) {
    std::string out_base = VCPKG_BIN_DIR / "template-expander";
#ifdef _WIN32
    std::string exe_path = out_base + ".exe";
    std::string cmd = exe_path + " " + local_path + " " + out_path;
#else
    std::string exe_path = out_base;
    std::string cmd = "./" + exe_path + " " + local_path + " " + out_path;
#endif
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
      std::cerr << "API extension failed with code " << ret << std::endl;
      std::exit(1);
    }
  }
  void CompileTeal(const std::string &teal_path, const std::string &out_path) {
    std::string cmd = "tl gen " + teal_path + " -o " + out_path;
    std::system(cmd.c_str());
    if (ret != 0) {
      std::cerr << "Teal compilation failed with code " << ret << std::endl;
      std::exit(1);
    }
  }
#ifdef _WIN32
  PROCESS_INFORMATION hub_process_info_{};
#else
  pid_t hub_pid_ = -1;
#endif
};
TEST_F(DataRetrievalTest, Gaussian1D) {
  const int NUM_POINTS = 100;
  const double START_TIME_SECONDS = 0.0;
  const double MAX_TIME_SECONDS = 1.0;
  const char *DEPENDANT_NAME = "P1";
  const char *GETTER_NAME = "O1";
  const double MIN_INDEPENDANT_VALUE = 0.0;
  const double MAX_INDEPENDANT_VALUE = 0.5;
  ConfigSP config = request_config(TIMEOUT_MS);
  ASSERT_NE(config, nullptr) << "Failed to get config from request_config";

  std::tuple<Ports, Ports> ports_ = request_port_payload(TIMEOUT_MS);
  Ports totalKnobs = std::get<0>(ports_);
  Ports totalMeters = std::get<1>(ports_);
  PortsSP getters = std::make_shared<Ports>();
  for (const auto &port : totalMeters) {
    if (*port->pseudo_name() == *Connection::Ohmic(GETTER_NAME)) {
      getters->push_back(port);
      break;
    }
  }
  ConnectionSP independant = Connection::PlungerGate(DEPENDANT_NAME);
  InstrumentPortSP independantKnob;
  for (const auto &port : totalKnobs) {
    if (*port->pseudo_name() == *independant) {
      independantKnob = port;
      break;
    }
  }
  InstrumentPortSP clock;
  for (const auto &port : totalKnobs) {
    if (port->instrument_type() == InstrumentTypes::CLOCK) {
      clock = port;
      break;
    }
  }
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
      "Taking a measurement to simulate 100 datapoints", "1D Gaussian Noise",
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
  EXPECT_EQ(*labelledArray->units(), *SymbolUnit::MilliVolt())
      << "Expected units to be mV, but got " << labelledArray->units()
      << " instead";
  EXPECT_EQ(labelledArray->size(), NUM_POINTS)
      << "Expected 100 data points, but got " << labelledArray->size()
      << " instead";
}
