#include "libs/sensor/threshold_event_engine.hpp"

#include "libs/common/time_utils.hpp"

namespace sensor {

ThresholdResult ThresholdEventEngine::evaluate(const SensorReading& reading,
                                               const Threshold& threshold) {
    const auto active = activeFaults_[reading.id];
    ActiveFault next = active;

    if (active == ActiveFault::none) {
        if (threshold.upperCritical && reading.reading > *threshold.upperCritical) {
            next = ActiveFault::upper;
        } else if (threshold.lowerCritical && reading.reading < *threshold.lowerCritical) {
            next = ActiveFault::lower;
        }
    } else if (active == ActiveFault::upper && threshold.upperCritical &&
               reading.reading <= *threshold.upperCritical - threshold.hysteresis) {
        next = ActiveFault::none;
    } else if (active == ActiveFault::lower && threshold.lowerCritical &&
               reading.reading >= *threshold.lowerCritical + threshold.hysteresis) {
        next = ActiveFault::none;
    }

    ThresholdResult result;
    result.health = next == ActiveFault::none ? Health::ok : threshold.assertedHealth;
    if (next == active) {
        return result;
    }

    EventRecord event;
    event.id = std::to_string(nextEventId_++);
    event.timestamp = common::iso8601Now();
    event.sensorId = reading.id;
    event.originOfCondition = "/redfish/v1/Chassis/GPU0/Sensors/" + reading.id;
    if (next == ActiveFault::none) {
        event.severity = "OK";
        event.recovery = true;
        event.message = reading.name + " returned to the normal range";
    } else {
        event.severity = toString(threshold.assertedHealth);
        event.message = reading.name +
                        (next == ActiveFault::upper
                             ? " exceeded upper critical threshold"
                             : " dropped below lower critical threshold");
    }
    activeFaults_[reading.id] = next;
    result.event = std::move(event);
    return result;
}

} // namespace sensor
