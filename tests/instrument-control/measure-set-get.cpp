#include <boost/asio/ip/tcp.hpp>
#include <boost/process.hpp>
#include <boost/process/v2/start_dir.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
#include <instrument-script-server/client/instrument-server-client.hpp>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;
namespace bp = boost::process::v2;
namespace v1 = instserver::daemon::v1;
using Client = instserver::client::InstrumentServerClient;
using namespace std::chrono_literals;

class MeasurementScriptTest : public ::testing::Test {
protected:
  boost::asio::io_context io_;
  std::unique_ptr<bp::process> daemon_;
  std::unique_ptr<Client> client_;
  fs::path run_dir_;
  const fs::path scripts_ = fs::path(CONTROLLER_TEST_DIR) / "instrument-control";
  std::vector<std::pair<std::string, std::optional<std::string>>> saved_env_;

  static void SetEnvironment(const std::string &name, const char *value) {
#ifdef _WIN32
    const int result = _putenv_s(name.c_str(), value != nullptr ? value : "");
#else
    const int result = value != nullptr ? setenv(name.c_str(), value, 1)
                                        : unsetenv(name.c_str());
#endif
    if (result != 0) {
      throw std::runtime_error("Could not set environment variable " + name);
    }
  }

  void UseEnvironment(const std::string &name, const std::string &value) {
    const char *old = std::getenv(name.c_str());
    saved_env_.emplace_back(name, old != nullptr
                                      ? std::optional<std::string>(old)
                                      : std::nullopt);
    SetEnvironment(name, value.c_str());
  }

  static void WriteFile(const fs::path &path, const std::string &contents) {
    std::ofstream output(path);
    output << contents;
    output.close();
    if (!output) {
      throw std::runtime_error("Could not write " + path.string());
    }
  }

  void RunTool(const fs::path &executable,
               const std::vector<std::string> &arguments) {
    bp::process process(io_, executable.string(), arguments,
                        bp::process_start_dir(run_dir_.string()));
    if (process.wait() != 0) {
      throw std::runtime_error(executable.string() + " failed; run: " +
                               run_dir_.string());
    }
  }

  void StartInstrument(const std::string &name, const std::string &config_name,
                       const std::string &api,
                       const fs::path &plugin) {
    const fs::path config = run_dir_ / (name + ".yml");
    std::ifstream input(scripts_ / config_name);
    ASSERT_TRUE(input) << "Could not read " << scripts_ / config_name;
    std::ofstream output(config);
    std::string line;
    while (std::getline(input, line)) {
      if (line.rfind("api_ref:", 0) == 0) {
        output << "api_ref: " << std::quoted((run_dir_ / api).generic_string())
               << '\n';
      } else {
        output << line << '\n';
      }
    }
    output.close();
    ASSERT_TRUE(output) << "Could not write " << config;
    v1::StartInstrumentRequest request;
    request.set_config_path(config.string());
    request.set_plugin_path(plugin.string());
    const auto response = client_->start_instrument(request);
    ASSERT_TRUE(response.standard_response().ok())
        << response.standard_response().DebugString();
  }

  void SetUp() override {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    run_dir_ = fs::path(CONTROLLER_TEST_DIR) / "test-runs" /
               ("measure_set_get_" + std::to_string(stamp));
    fs::create_directories(run_dir_ / "runtime");
    fs::create_directories(run_dir_ / "tmp");
    UseEnvironment("INSTRUMENT_SCRIPT_SERVER_RUNTIME_DIR",
                   (run_dir_ / "runtime").string());
    for (const char *name : {"TMPDIR", "TMP", "TEMP"}) {
      UseEnvironment(name, (run_dir_ / "tmp").string());
    }
#ifdef _WIN32
    const std::string suffix = ".exe";
    const char separator = ';';
#else
    const std::string suffix;
    const char separator = ':';
#endif
    const fs::path bin = ISS_BIN_DIR;
    const char *old_path = std::getenv("PATH");
    UseEnvironment("PATH", bin.string() + separator +
                               (old_path != nullptr ? old_path : ""));
    UseEnvironment("INSTRUMENT_SCRIPT_SERVER_OPT_LUA_LIB",
                   (scripts_ / "instrument-libs").string() + ";" +
                       (scripts_ / "lua-utils").string());
    // The mock meter reads a fixture file; it is not electrically coupled to
    // the source. Source readback below independently verifies the write.
    WriteFile(run_dir_ / "meter.txt", "0.375\n0.625\n");
    UseEnvironment("MOCK_MULTIMETER_DATA_FILE", (run_dir_ / "meter.txt").string());

    for (const char *name : {"source", "multimeter"}) {
      RunTool(bin / ("template-expander" + suffix),
              {(fs::path(CONTROLLER_TEST_DIR) / "instrument-apis" /
                (std::string(name) + "-api.yml.tmpl")).string(),
               (run_dir_ / (std::string(name) + "-api.yml")).string()});
    }

    boost::asio::ip::tcp::acceptor reservation(
        io_, {boost::asio::ip::make_address("127.0.0.1"), 0});
    const auto port = reservation.local_endpoint().port();
    reservation.close();
    UseEnvironment("INSTRUMENT_SCRIPT_SERVER_RPC_PORT", std::to_string(port));
    daemon_ = std::make_unique<bp::process>(
        io_, (bin / ("instrument-script-server-daemon" + suffix)).string(),
        std::vector<std::string>{"--log-level", "warn"},
        bp::process_start_dir(run_dir_.string()));
    client_ = std::make_unique<Client>(port);
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    bool ready = false;
    while (std::chrono::steady_clock::now() < deadline && daemon_->running()) {
      if (client_->is_daemon_running()) {
        ready = true;
        break;
      }
      std::this_thread::sleep_for(50ms);
    }
    ASSERT_TRUE(ready) << "ISS failed to start; logs in " << run_dir_;
    StartInstrument("Source1", "source-config.yml", "source-api.yml", MOCK_SOURCE_PLUGIN);
    if (HasFatalFailure()) return;
    StartInstrument("Meter1", "multimeter-config.yml", "multimeter-api.yml", MOCK_METER_PLUGIN);
  }

  void TearDown() override {
    if (daemon_ && daemon_->running()) {
      // Stop only this test's daemon; its shutdown stops the instrument workers.
      try {
        if (client_) client_->stop_daemon(v1::DaemonStop{});
      } catch (const std::exception &error) {
        ADD_FAILURE() << "ISS shutdown failed: " << error.what();
      }
      const auto deadline = std::chrono::steady_clock::now() + 5s;
      while (daemon_->running() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(50ms);
      }
      if (daemon_->running()) {
        ADD_FAILURE() << "ISS did not exit; logs in " << run_dir_;
        daemon_->terminate();
      }
    }
    if (daemon_) daemon_->wait();
    client_.reset();
    daemon_.reset();
    for (auto it = saved_env_.rbegin(); it != saved_env_.rend(); ++it) {
      SetEnvironment(it->first, it->second ? it->second->c_str() : nullptr);
    }
  }

  v1::MeasureJobResultResponse RunScript(const v1::MeasureJobRequest &request) {
    const auto job = client_->measure_job(request);
    if (!job.standard_response().ok()) {
      throw std::runtime_error(job.standard_response().DebugString());
    }
    v1::MeasureJobResultRequest result_request;
    result_request.set_job_id(job.job_id());
    v1::JobStatusRequest status_request;
    status_request.set_job_id(job.job_id());
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < deadline) {
      const auto status = client_->job_status(status_request);
      if (!status.standard_response().ok()) {
        throw std::runtime_error(status.standard_response().DebugString());
      }
      if (status.job().status() == v1::JOB_STATUS_COMPLETED ||
          status.job().status() == v1::JOB_STATUS_FAILED ||
          status.job().status() == v1::JOB_STATUS_CANCELLED) {
        return client_->measure_job_result(result_request);
      }
      std::this_thread::sleep_for(25ms);
    }
    throw std::runtime_error("Measurement timed out; logs in " + run_dir_.string());
  }

  static v1::VariableValue &Argument(v1::MeasureJobRequest &request,
                                      const std::string &name, v1::LuaTypes type) {
    auto *parameter = request.mutable_type_manifest()->add_parameters();
    parameter->set_name(name);
    parameter->set_type(type);
    return (*request.mutable_globals()->mutable_map())[name];
  }
};

TEST_F(MeasurementScriptTest, measure_set_get) {
  constexpr double voltage = 0.123;
  v1::MeasureJobRequest request;
  request.set_script_path((scripts_ / "measurement-scripts" /
                           "measure_get_set.lua").string());
  Argument(request, "setVoltages", v1::LUA_TYPES_DOUBLE_ARRAY)
      .mutable_d_array()->add_values(voltage);
  // Native InstrumentTarget serialization: instrument|group|channel.
  Argument(request, "getters", v1::LUA_TYPES_TARGET_ARRAY)
      .mutable_t_array()->add_values("Meter1|analog|2");
  Argument(request, "setters", v1::LUA_TYPES_TARGET_ARRAY)
      .mutable_t_array()->add_values("Source1|analog|1");

  const auto result = RunScript(request);
  ASSERT_TRUE(result.standard_response().ok())
      << result.standard_response().DebugString();
  ASSERT_EQ(result.status(), v1::JOB_STATUS_COMPLETED);
  ASSERT_EQ(result.results_size(), 2);
  const auto &write = result.results(0);
  EXPECT_EQ(write.instrument_name(), "Source1");
  EXPECT_EQ(write.verb(), "SET_VOLTAGE");
  EXPECT_EQ(write.group(), "analog");
  EXPECT_EQ(write.channel(), 1);
  const auto &reading = result.results(1);
  EXPECT_EQ(reading.instrument_name(), "Meter1");
  EXPECT_EQ(reading.verb(), "GET_DATAPOINT");
  EXPECT_EQ(reading.group(), "analog");
  EXPECT_EQ(reading.channel(), 2);
  ASSERT_EQ(reading.param_size(), 1);
  ASSERT_EQ(reading.param(0).type(), v1::LUA_TYPES_DOUBLE);
  EXPECT_DOUBLE_EQ(reading.param(0).value().d(), 0.375);

  v1::MeasureJobRequest readback;
  readback.set_script_path((scripts_ / "settings-scripts" /
                            "get_many_voltages.lua").string());
  Argument(readback, "getters", v1::LUA_TYPES_TARGET_ARRAY)
      .mutable_t_array()->add_values("Source1|analog|1");
  const auto echo = RunScript(readback);
  ASSERT_TRUE(echo.standard_response().ok())
      << echo.standard_response().DebugString();
  ASSERT_EQ(echo.status(), v1::JOB_STATUS_COMPLETED);
  ASSERT_EQ(echo.results_size(), 1);
  EXPECT_EQ(echo.results(0).instrument_name(), "Source1");
  EXPECT_EQ(echo.results(0).verb(), "GET_VOLTAGE");
  ASSERT_EQ(echo.results(0).param_size(), 1);
  ASSERT_EQ(echo.results(0).param(0).type(), v1::LUA_TYPES_DOUBLE);
  EXPECT_NEAR(echo.results(0).param(0).value().d(), voltage, 1e-7);
}
} // namespace
