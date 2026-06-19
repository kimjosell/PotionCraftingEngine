#pragma once

#include "effect_engine/engine_config.h"

#include <stdexcept>
#include <string>

namespace effect_engine {

class ConfigParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

EngineConfig loadEngineConfigFromString(const std::string& jsonText);
EngineConfig loadEngineConfigFromFile(const std::string& path);

}
