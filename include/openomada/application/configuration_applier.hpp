#pragma once

#include <string>
#include <vector>

#include "openomada/application/configuration.hpp"

namespace openomada::application {

struct ConfigurationApplyResult {
    bool ok{false};
    bool changed{false};
    std::string error{};
};

class ConfigurationApplier {
public:
    virtual ~ConfigurationApplier() = default;
    virtual ConfigurationApplyResult apply(const AccessPointConfigUpdate& update) = 0;

    // Strict read-only mode: appliers that accept every downlink update without
    // executing any system change report true here, so the dispatcher ACKs
    // controller commands with errorCode 0 even for unsupported keys.
    virtual bool accepts_all() const noexcept { return false; }
};

// Accepts every downlink configuration update and changes nothing. Used when
// the agent must run as a strict read-only/no-op device on a general-purpose
// host (no UCI, ubus, hostapd or other system mutation backends).
class DownlinkNoOpConfigurationApplier final : public ConfigurationApplier {
public:
    ConfigurationApplyResult apply(const AccessPointConfigUpdate& /*update*/) override {
        return {true, false, {}};
    }

    bool accepts_all() const noexcept override { return true; }
};

class CompositeConfigurationApplier final : public ConfigurationApplier {
public:
    explicit CompositeConfigurationApplier(std::vector<ConfigurationApplier*> appliers = {});

    void add(ConfigurationApplier& applier);
    ConfigurationApplyResult apply(const AccessPointConfigUpdate& update) override;

private:
    std::vector<ConfigurationApplier*> appliers_;
};

} // namespace openomada::application
