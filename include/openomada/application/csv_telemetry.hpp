#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openomada/application/inform.hpp"
#include "openomada/application/settings.hpp"
#include "openomada/domain/mac_address.hpp"

namespace openomada::application {

// Row of the uplink AP state CSV (wap_ecsp_state.csv):
// ap_name,ip,eth_mac,bssid_5g,ssid_5g,channel_5g,bandwidth_5g,phy_5g,
// bssid_2g,ssid_2g,channel_2g,bandwidth_2g,phy_2g,
// clients_24g,clients_5g,total_clients,updated_at
// The *_2g columns are optional; older CSVs without them still load.
struct ApStateCsvRow {
    std::string ap_name{};
    std::string ip{};
    domain::MacAddress eth_mac{};
    std::string bssid_5g{};
    std::string ssid_5g{};
    std::string channel_5g{};
    std::string bandwidth_5g{};
    std::string phy_5g{};
    std::string bssid_2g{};
    std::string ssid_2g{};
    std::string channel_2g{};
    std::string bandwidth_2g{};
    std::string phy_2g{};
    std::string clients_24g{};
    std::string clients_5g{};
    std::string total_clients{};
    std::string updated_at{};

    bool has_2g() const noexcept {
        return !bssid_2g.empty() || !ssid_2g.empty() || !channel_2g.empty() ||
               !bandwidth_2g.empty();
    }
};

// Row of the connected clients CSV (wap_ecsp_clients.csv):
// ap_name,ap_eth_mac,client_mac,band,ssid,updated_at
struct ClientCsvRow {
    std::string ap_name{};
    domain::MacAddress ap_eth_mac{};
    domain::MacAddress client_mac{};
    bool band_5g{true};
    std::string ssid{};
    std::string updated_at{};
};

struct CsvTelemetryPaths {
    std::string aps_csv{};
    std::string clients_csv{};
};

struct CsvTelemetryResult {
    bool ok{false};
    std::string error{};
    std::vector<ApStateCsvRow> aps{};
    std::vector<ClientCsvRow> clients{};
};

std::string default_aps_csv_path();
std::string default_clients_csv_path();

CsvTelemetryResult load_csv_telemetry(const CsvTelemetryPaths& paths) noexcept;

std::string wsettings_5g_json(const ApStateCsvRow& row);
std::string wsettings_2g_json(const ApStateCsvRow& row);
std::string client_stats_row_json(
    const ClientCsvRow& row,
    const AgentSettings& settings,
    std::string_view phy
);

// InformProvider that reads the uplink CSV files on every INFORM cycle and
// attaches radio telemetry (wSettings_5g) plus the connected client stats
// array (clientStats) filtered to this instance's --eth-mac.
class CsvInformProvider final : public InformProvider {
public:
    explicit CsvInformProvider(
        AgentSettings settings,
        CsvTelemetryPaths paths = {},
        LanObservation lan = {}
    );

    InformSnapshot build(bool need_reply, std::uint64_t uptime_seconds) override;

private:
    AgentSettings settings_;
    CsvTelemetryPaths paths_;
    LanObservation lan_;
};

} // namespace openomada::application
