#include <bitset>
#include <thread>

#include "L0_Platform/msp432p401r/msp432p401r.h"
#include "L1_Peripheral/msp432p401r/system_controller.hpp"
#include "L4_Testing/testing_frameworks.hpp"
#include "utility/units.hpp"

namespace sjsu
{
namespace msp432p401r
{
EMIT_ALL_METHODS(SystemController);

TEST_CASE("Testing msp432p401r SystemController",
          "[msp432p401r-system-controller]")
{
  // Simulate local version of clock system (CS) registers.
  CS_Type local_cs;
  testing::ClearStructure(&local_cs);
  SystemController::clock_system = &local_cs;

  // Simulate local version of tag length value (TLV) registers.
  TLV_Type local_tlv;
  testing::ClearStructure(&local_tlv);
  SystemController::device_descriptors = &local_tlv;

  SystemController::ClockConfiguration_t clock_configuration;
  SystemController test_subject(clock_configuration);

  auto primary_clocks_become_ready([&local_cs]() {
    std::this_thread::sleep_for(2ms);

    constexpr uint8_t kAuxiliaryClockReadyBit         = 24;
    constexpr uint8_t kMasterClockReadyBit            = 25;
    constexpr uint8_t kSubsytemClockReadyBit          = 26;
    constexpr uint8_t kLowSpeedSubsystemClockReadyBit = 27;
    constexpr uint8_t kBackupClockReadyBit            = 28;

    bit::Register(&local_cs.STAT)
        .Set(bit::MaskFromRange(kAuxiliaryClockReadyBit))
        .Set(bit::MaskFromRange(kMasterClockReadyBit))
        .Set(bit::MaskFromRange(kSubsytemClockReadyBit))
        .Set(bit::MaskFromRange(kLowSpeedSubsystemClockReadyBit))
        .Set(bit::MaskFromRange(kBackupClockReadyBit))
        .Save();
  });

  SECTION("GetClockConfiguration()")
  {
    CHECK(&clock_configuration == test_subject.GetClockConfiguration());
  }

  SECTION("Initialize")
  {
    // Setup
    testing::ClearStructure(&local_cs);
    std::thread simulated_primary_clocks_become_ready(
        primary_clocks_become_ready);

    // Generate a test case for each clock source where for each case, each
    // clock divider shall be tested.
    auto expected_clock_source =
        GENERATE(SystemController::Oscillator::kLowFrequency,
                 SystemController::Oscillator::kVeryLowFrequency,
                 SystemController::Oscillator::kReference,
                 SystemController::Oscillator::kDigitallyControlled,
                 SystemController::Oscillator::kModule,
                 SystemController::Oscillator::kHighFrequency);
    auto expected_clock_divider =
        GENERATE(SystemController::ClockDivider::kDivideBy1,
                 SystemController::ClockDivider::kDivideBy2,
                 SystemController::ClockDivider::kDivideBy4,
                 SystemController::ClockDivider::kDivideBy8,
                 SystemController::ClockDivider::kDivideBy16,
                 SystemController::ClockDivider::kDivideBy32,
                 SystemController::ClockDivider::kDivideBy64,
                 SystemController::ClockDivider::kDivideBy128);

    auto expected_reference_frequency =
        SystemController::ReferenceClockFrequency::kF32768Hz;
    // When the clock source is SystemController::Oscillator::kReference,
    // generate additional test cases to test for when the frequency select
    // value is 0b0 or 0b1.
    if (expected_clock_source == SystemController::Oscillator::kReference)
    {
      expected_reference_frequency =
          GENERATE(SystemController::ReferenceClockFrequency::kF32768Hz,
                   SystemController::ReferenceClockFrequency::kF128kHz);
    }

    uint8_t expected_reference_frequency_select =
        Value(expected_reference_frequency);

    constexpr auto kDcoFrequency   = 12_MHz;
    const auto kReferenceFrequency = SystemController::InternalOscillator::
        kReference[expected_reference_frequency];
    const std::array<units::frequency::hertz_t, 6> kClockRates = {
      SystemController::ExternalOscillator::kLowFrequency,
      SystemController::InternalOscillator::kVeryLowFrequency,
      kReferenceFrequency,
      kDcoFrequency,
      SystemController::InternalOscillator::kModule,
      SystemController::ExternalOscillator::kHighFrequency,
    };

    uint8_t clock_select        = Value(expected_clock_source);
    uint8_t clock_divider_value = (1 << Value(expected_clock_divider));
    units::frequency::hertz_t expected_pre_divided_clock_rate =
        kClockRates[clock_select];
    units::frequency::hertz_t expected_clock_rate =
        expected_pre_divided_clock_rate / clock_divider_value;

    INFO("clock source: 0b" << std::bitset<3>(clock_select) << " ("
                            << static_cast<size_t>(clock_select) << ")");
    INFO("reference frequency select: 0b"
         << std::bitset<1>(expected_reference_frequency_select));
    INFO("divider select: 0b"
         << std::bitset<3>(Value(expected_clock_divider)) << " ("
         << static_cast<size_t>(expected_clock_divider) << ")");
    INFO("divider: " << static_cast<size_t>(clock_divider_value));
    INFO("pre-divided clock rate: "
         << expected_pre_divided_clock_rate.to<size_t>());

    // Initial clock configuration setup
    clock_configuration.reference.frequency = expected_reference_frequency;
    clock_configuration.dco.frequency       = kDcoFrequency;

    SECTION("Configure auxiliary clock")
    {
      // Exercise
      clock_configuration.auxiliary.clock_source = expected_clock_source;
      clock_configuration.auxiliary.divider      = expected_clock_divider;

      test_subject.Initialize();
      simulated_primary_clocks_become_ready.join();

      // Verify
      switch (expected_clock_source)
      {
        case SystemController::Oscillator::kLowFrequency: [[fallthrough]];
        case SystemController::Oscillator::kVeryLowFrequency: [[fallthrough]];
        case SystemController::Oscillator::kReference:
        {
          uint8_t actual_reference_frequency_select = bit::Read(
              local_cs.CLKEN,
              SystemController::ClockEnableRegister::kReferenceFrequencySelect);
          uint8_t actual_auxiliary_clock_select = bit::Extract(
              local_cs.CTL1,
              SystemController::Control1Register::kAuxiliaryClockSourceSelect);
          uint8_t actual_auxiliary_clock_divider = bit::Extract(
              local_cs.CTL1,
              SystemController::Control1Register::kAuxiliaryClockDividerSelect);
          auto actual_auxiliary_clock_rate = test_subject.GetClockRate(
              SystemController::Modules::kAuxiliaryClock);

          CHECK(actual_reference_frequency_select ==
                expected_reference_frequency_select);
          CHECK(actual_auxiliary_clock_select == clock_select);
          CHECK(actual_auxiliary_clock_divider ==
                Value(expected_clock_divider));
          CHECK(actual_auxiliary_clock_rate.to<uint32_t>() ==
                expected_clock_rate.to<uint32_t>());
          break;
        }
        default: break;
      }
    }  // Configure auxiliary clock

    SECTION("Configure master clock")
    {
      // Exercise
      clock_configuration.master.clock_source = expected_clock_source;
      clock_configuration.master.divider      = expected_clock_divider;

      test_subject.Initialize();
      simulated_primary_clocks_become_ready.join();

      // Verify
      uint8_t actual_reference_frequency_select = bit::Read(
          local_cs.CLKEN,
          SystemController::ClockEnableRegister::kReferenceFrequencySelect);
      uint8_t actual_master_clock_select = bit::Extract(
          local_cs.CTL1,
          SystemController::Control1Register::kMasterClockSourceSelect);
      uint8_t actual_master_clock_divider = bit::Extract(
          local_cs.CTL1,
          SystemController::Control1Register::kMasterClockDividerSelect);
      auto actual_master_clock_rate =
          test_subject.GetClockRate(SystemController::Modules::kMasterClock);

      CHECK(actual_reference_frequency_select ==
            expected_reference_frequency_select);
      CHECK(actual_master_clock_select == clock_select);
      CHECK(actual_master_clock_divider == Value(expected_clock_divider));
      CHECK(actual_master_clock_rate.to<uint32_t>() ==
            expected_clock_rate.to<uint32_t>());
    }  // Configure master clock

    SECTION("Configure subsystem master clocks")
    {
      // Exercise
      clock_configuration.subsystem_master.clock_source = expected_clock_source;
      clock_configuration.subsystem_master.divider = expected_clock_divider;
      clock_configuration.subsystem_master.low_speed_divider =
          expected_clock_divider;

      test_subject.Initialize();
      simulated_primary_clocks_become_ready.join();

      // Verify
      uint8_t actual_reference_frequency_select = bit::Read(
          local_cs.CLKEN,
          SystemController::ClockEnableRegister::kReferenceFrequencySelect);
      uint8_t actual_clock_select = bit::Extract(
          local_cs.CTL1,
          SystemController::Control1Register::kSubsystemClockSourceSelect);
      uint8_t actual_clock_divider = bit::Extract(
          local_cs.CTL1,
          SystemController::Control1Register::kSubsystemClockDividerSelect);
      uint8_t actual_low_speed_clock_divider =
          bit::Extract(local_cs.CTL1, SystemController::Control1Register::
                                          kLowSpeedSubsystemClockDividerSelect);
      auto actual_clock_rate = test_subject.GetClockRate(
          SystemController::Modules::kSubsystemMasterClock);
      auto actual_low_speed_clock_rate = test_subject.GetClockRate(
          SystemController::Modules::kLowSpeedSubsystemMasterClock);

      CHECK(actual_reference_frequency_select ==
            expected_reference_frequency_select);
      CHECK(actual_clock_select == clock_select);
      CHECK(actual_clock_divider == Value(expected_clock_divider));
      CHECK(actual_low_speed_clock_divider == Value(expected_clock_divider));
      CHECK(actual_clock_rate.to<uint32_t>() ==
            expected_clock_rate.to<uint32_t>());
      CHECK(actual_low_speed_clock_rate.to<uint32_t>() ==
            expected_clock_rate.to<uint32_t>());
    }  // Configure subsystem master clocks

    SECTION("Configure backup clock")
    {
      // Exercise
      clock_configuration.backup.clock_source = expected_clock_source;

      test_subject.Initialize();
      simulated_primary_clocks_become_ready.join();

      // Verify
      if (expected_clock_source == SystemController::Oscillator::kReference)
      {
        // Note: A different select value, that does not corresponds to the
        //       Oscillator enum value, is used when selecting the reference
        //       clock as a source for the back up clock.
        clock_select = 0b1;
      }

      switch (expected_clock_source)
      {
        case SystemController::Oscillator::kLowFrequency: [[fallthrough]];
        case SystemController::Oscillator::kReference:
        {
          uint8_t actual_reference_frequency_select = bit::Read(
              local_cs.CLKEN,
              SystemController::ClockEnableRegister::kReferenceFrequencySelect);
          uint8_t actual_backup_clock_select = bit::Extract(
              local_cs.CTL1,
              SystemController::Control1Register::kBackupClockSourceSelect);
          auto actual_backup_clock_rate = test_subject.GetClockRate(
              SystemController::Modules::kBackupClock);

          CHECK(actual_reference_frequency_select ==
                expected_reference_frequency_select);
          CHECK(actual_backup_clock_select == clock_select);
          // NOTE: Check pre-divided clock rate since the backup clock does not
          //       utilize clock dividers.
          CHECK(actual_backup_clock_rate.to<uint32_t>() ==
                expected_pre_divided_clock_rate.to<uint32_t>());
        }
        break;
        default: break;
      }
    }  // Configure backup clock
  }    // Initialize

  SECTION("Get clock rate")
  {
    // Testing GetClockRate() for getting the clock rates of the internal and
    // external oscillators that have a fixed frequency.

    units::frequency::hertz_t expected_clock_rate;
    units::frequency::hertz_t actual_clock_rate;
    SystemController::ResourceID clock_peripheral;
    auto frequency_select =
        SystemController::ReferenceClockFrequency::kF32768Hz;

    SECTION("Low frequency clock")
    {
      // Setup
      expected_clock_rate = SystemController::ExternalOscillator::kLowFrequency;
      clock_peripheral    = SystemController::Modules::kLowFrequencyClock;
    }

    SECTION("Very low frequency clock")
    {
      // Setup
      expected_clock_rate =
          SystemController::InternalOscillator::kVeryLowFrequency;
      clock_peripheral = SystemController::Modules::kVeryLowFrequencyClock;
    }

    SECTION("Reference clock")
    {
      std::thread simulated_primary_clocks_become_ready(
          primary_clocks_become_ready);

      SECTION("When 32.768 kHz is selected")
      {
        // Setup
        frequency_select = SystemController::ReferenceClockFrequency::kF32768Hz;
        expected_clock_rate =
            SystemController::InternalOscillator::kReference[frequency_select];
      }

      SECTION("When 128 kHz is selected")
      {
        // Setup
        frequency_select = SystemController::ReferenceClockFrequency::kF128kHz;
        expected_clock_rate =
            SystemController::InternalOscillator::kReference[frequency_select];
      }

      clock_peripheral = SystemController::Modules::kReferenceClock;
      clock_configuration.reference.frequency = frequency_select;

      test_subject.Initialize();
      simulated_primary_clocks_become_ready.join();
    }

    SECTION("Module clock")
    {
      // Setup
      expected_clock_rate = SystemController::InternalOscillator::kModule;
      clock_peripheral    = SystemController::Modules::kModuleClock;
    }

    SECTION("System clock")
    {
      // Setup
      expected_clock_rate = SystemController::InternalOscillator::kSystem;
      clock_peripheral    = SystemController::Modules::kSystemClock;
    }

    SECTION("Unknown resource")
    {
      // Setup
      expected_clock_rate = 0_Hz;
      clock_peripheral    = SystemController::ResourceID({ .device_id = 100 });
    }

    INFO("reference clock frequency select: 0b"
         << std::bitset<1>(frequency_select));
    INFO("clock peripheral id: "
         << static_cast<size_t>(clock_peripheral.device_id));

    // Exercise
    actual_clock_rate = test_subject.GetClockRate(clock_peripheral);

    // Verify
    CHECK(actual_clock_rate.to<uint32_t>() ==
          expected_clock_rate.to<uint32_t>());
  }

  SECTION("Set clock divider")
  {
    constexpr bit::Mask kDividerSelectMasks[] = {
      SystemController::Control1Register::kAuxiliaryClockDividerSelect,
      SystemController::Control1Register::kMasterClockDividerSelect,
      SystemController::Control1Register::kSubsystemClockDividerSelect,
      SystemController::Control1Register::kLowSpeedSubsystemClockDividerSelect,
    };

    // Test each clock divider select value for each primary clock.
    auto clock_divider = GENERATE(SystemController::ClockDivider::kDivideBy1,
                                  SystemController::ClockDivider::kDivideBy2,
                                  SystemController::ClockDivider::kDivideBy4,
                                  SystemController::ClockDivider::kDivideBy8,
                                  SystemController::ClockDivider::kDivideBy16,
                                  SystemController::ClockDivider::kDivideBy32,
                                  SystemController::ClockDivider::kDivideBy64,
                                  SystemController::ClockDivider::kDivideBy128);
    auto clock_source  = GENERATE(
        SystemController::Clock::kAuxiliary, SystemController::Clock::kMaster,
        SystemController::Clock::kSubsystemMaster,
        SystemController::Clock::kLowSpeedSubsystemMaster);

    std::thread simulated_primary_clocks_become_ready(
        primary_clocks_become_ready);

    // Setup
    uint8_t clock_select            = Value(clock_source);
    uint8_t expected_divider_select = Value(clock_divider);

    INFO("clock_source: 0b" << std::bitset<3>(clock_select));
    INFO("clock_divider: 0b" << std::bitset<3>(expected_divider_select));

    // Exercise
    test_subject.SetClockDivider(clock_source, clock_divider);
    simulated_primary_clocks_become_ready.join();

    // Verify
    uint8_t actual_divider_select =
        bit::Extract(local_cs.CTL1, kDividerSelectMasks[clock_select]);
    CHECK(actual_divider_select == expected_divider_select);
  }

  SECTION("Is peripheral powered up")
  {
    CHECK(!test_subject.IsPeripheralPoweredUp({}));
  }

  SystemController::clock_system       = msp432p401r::CS;
  SystemController::device_descriptors = msp432p401r::TLV;
}
}  // namespace msp432p401r
}  // namespace sjsu
