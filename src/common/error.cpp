#include "common/error.h"

namespace menos::common {

Error::Error(std::string_view message) : std::runtime_error(std::string(message)) {}

InvalidCircuit::InvalidCircuit(std::string_view message) : Error(message) {}

UnsupportedFeature::UnsupportedFeature(std::string_view message) : Error(message) {}

ResourceExceeded::ResourceExceeded(std::string_view message) : Error(message) {}

InternalError::InternalError(std::string_view message) : Error(message) {}

void ensure(bool condition, std::string_view message) {
    if (!condition) {
        throw InternalError(message);
    }
}

}  // namespace menos::common
