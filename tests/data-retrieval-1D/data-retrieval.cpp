#include <falcon-core/generic/Map.hpp>
#include <falcon-core/instrument_interfaces/Waveform.hpp>
#include <falcon-core/instrument_interfaces/names/InstrumentPort.hpp>
#include <falcon-core/instrument_interfaces/names/InstrumentTypes.hpp>
#include <falcon-core/physics/device_structures/Connection.hpp>
#include <falcon-routine/hub.hpp>
#include <gtest/gtest.h>
#include <string>

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
const std::string TEST_DATA_FILE{"test-data/gaussian-1d.txt"};
const std::string TEST_CONFIG_FILE{"test-data/test-config.yaml"};

class DataRetrievalTest : public ::testing::Test {
protected:
  void SetUp() override {
    setenv("MOCK_MULTIMETER_DATA_FILE", TEST_DATA_FILE.c_str(), 1);
  }

  void TearDown() override { unsetenv("MOCK_MULTIMETER_DATA_FILE"); }
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
