// This compile-time bridge checks C++ declarations against the model boundary.
// It does not establish C++ refinement of either proof.
#include "device_wipe.h"
#include "wipe_confirmation.h"
#include <string_view>

static_assert(WipeConfirmation::HOLD_MS == 3000);
static_assert(WipeConfirmation::TIMEOUT_MS == 30000);
static_assert(WipeConfirmation::MAX_POLL_GAP_MS == 250);
#ifndef LAUNCHLAB_MODEL_OWNED
#error LAUNCHLAB_MODEL_OWNED must come from the checked TLC configuration
#endif
static_assert(DeviceWipe::NAMESPACE_COUNT + 1 == LAUNCHLAB_MODEL_OWNED,
    "TLA+ counts each firmware NVS namespace and the separate app flash partition");
constexpr std::string_view kOwned[] = {
    "launchlab-mini", "ll-practice", "ll-appearance", "ll-bests",
    "ll-motion", "ll-power", "ll-prac-qa", "ll-mot-qa",
};
constexpr bool ownedNamespacesMatch() {
    if (sizeof kOwned / sizeof *kOwned != DeviceWipe::NAMESPACE_COUNT) return false;
    for (size_t i = 0; i < DeviceWipe::NAMESPACE_COUNT; ++i)
        if (kOwned[i] != DeviceWipe::NAMESPACES[i]) return false;
    return true;
}
static_assert(ownedNamespacesMatch(), "Review the model erase scope when namespaces change");
static_assert(std::string_view(DeviceWipe::INTENT_SPACE) == "ll-reset");
static_assert(std::string_view(DeviceWipe::INTENT_KEY) == "pending");
