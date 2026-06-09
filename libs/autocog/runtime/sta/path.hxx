#ifndef AUTOCOG_RUNTIME_STA_PATH_HXX
#define AUTOCOG_RUNTIME_STA_PATH_HXX

// PathStep/StepRange are the shared autocog::data types; this header re-exports
// them into the runtime::sta namespace for source compatibility.
#include "autocog/data/path.hxx"

namespace autocog::runtime::sta {
using PathStep  = ::autocog::data::PathStep;
using StepRange = ::autocog::data::StepRange;
}

#endif // AUTOCOG_RUNTIME_STA_PATH_HXX
