#include <falcon-core/physics/device_structures/Connection.hpp>
#include <falcon-routine/hub.hpp>
#include <string>

namespace falcon::routine;
namespace falcon_core::physics::device_structures;
namespace falcon_core::instrument_interfaces::names;
namespace falcon_core::communications::messages;
namespace falcon_core::physics::config;
namespace falcon_core::physics::config::core;
namespace falcon_core::instrument_interfaces::port_transforms;
namespace falcon_Core::instrument_interfaces::names;
const int TIMEOUT_MS = 1000;
const std::string TEST_DATA_FILE{"test-data/gaussian-1d.txt"};
const std::string TEST_CONFIG_FILE{"test-data/test-config.yaml"};

class DataRetrievalTest : public ::testing::Test {
protected:
  void SetUp() override {
    setenv("MOCK_MULTIMETER_DATA_FILE", TEST_DATA_FILE, 1);
  }

  void TearDown() override { unsetenv("MOCK_MULTIMETER_DATA_FILE"); }
};
TEST_F(DataRetrievalTest, Gaussian1D) {
  ConfigSP config = hub::request_config(TIMEOUT_MS);
  ASSERT_NE(config, nullptr) << "Failed to get config from hub::request_config";

  std::tuple<Ports, Ports> ports_ = hub::request_port_payload(TIMEOUT_MS);
  Ports totalKnobs = std::get<0>(ports_);
  Ports totalMeters = std::get<1>(ports_);

  ConnectionsSP ohmics = config_.get_channel_ohmics(Channel("I_O1"));
  ConnectionsSP getters = MeasurementRequest request{
      "Taking a measurement to simulate 100 datapoints", "1D Gaussian Noise",
      waveforms, getters, transforms};
  auto resp = request_measurement(request, TIMEOUT_MS);

  EXPECT_TRUE(resp.arrays().is_measured_arrays())
      << "The arrays were not measured arrays";
  EXPECT_EQ(resp.arrays().measured_arrays().size(), 1)
      << "Expected exactly one measured array";
  auto labelledArray = resp.arrays().measured_arrays()[0];
  EXPECT_EQ(*labelledArray->connection(), Connection::PlungerGate("P1"))
      << "Expected connection to be P1, but got "
      << labelledArray->connection()->name() << " instead";
  EXPECT_EQ(*labelledArray->instrument_types(), InstrumentTypes::VOLTMETER)
      << "Expected instrument type to be VOLTMETER, but got "
      << labelledArray->instrument_types()->to_string() << " instead";
  EXPEXT_EQ(*labelledArray->units(), *SymbolUnit::MilliVolt())
      << "Expected units to be mV, but got "
      << labelledArray->units()->to_string() << " instead";
  EXPECT_EQ(labelledArray->array().size(), 200)
      << "Expected 200 data points, but got " << labelledArray->array().size()
      << " instead";
}
