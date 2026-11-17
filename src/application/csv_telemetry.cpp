#include "openomada/application/csv_telemetry.hpp"

#include "openomada/protocol/ecsp_message.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <utility>

namespace openomada::application {
namespace {

constexpr std::uint32_t kRadioRegion = 840;
constexpr int kPm24G = 4;
constexpr int kPmVht = 5;
constexpr int kPmHe = 6;

constexpr int kDefaultTxPower = 21;

std::string quoted(std::string_view value) {
    std::string out = "\"";
    out += protocol::json_escape(value);
    out += "\"";
    return out;
}

bool band_is_5g(std::string_view band) noexcept {
    if (band.find('5') != std::string_view::npos) {
        return true;
    }
    if (band.find('2') != std::string_view::npos) {
        return false;
    }
    return true;
}

int pm_for(bool band_5g, std::string_view phy) noexcept {
    if (!band_5g) {
        return kPm24G;
    }
    std::string lowered;
    lowered.reserve(phy.size());
    for (char ch : phy) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    if (lowered.find("he") != std::string::npos) {
        return kPmHe;
    }
    return kPmVht;
}

std::vector<std::vector<std::string>> parse_csv_rows(std::string_view text) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool in_quotes = false;
    bool field_started = false;

    auto finish_field = [&]() {
        row.push_back(std::move(field));
        field.clear();
        field_started = false;
    };
    auto finish_row = [&]() {
        finish_field();
        rows.push_back(std::move(row));
        row.clear();
    };

    for (std::size_t index = 0; index < text.size(); ++index) {
        const char ch = text[index];
        if (in_quotes) {
            if (ch == '"') {
                if (index + 1 < text.size() && text[index + 1] == '"') {
                    field.push_back('"');
                    ++index;
                } else {
                    in_quotes = false;
                }
            } else {
                field.push_back(ch);
            }
            field_started = true;
            continue;
        }
        if (ch == '"') {
            in_quotes = true;
            field_started = true;
            continue;
        }
        if (ch == ',') {
            finish_field();
            continue;
        }
        if (ch == '\r') {
            if (index + 1 < text.size() && text[index + 1] == '\n') {
                ++index;
            }
            finish_row();
            continue;
        }
        if (ch == '\n') {
            finish_row();
            continue;
        }
        field.push_back(ch);
        field_started = true;
    }
    if (field_started || !row.empty()) {
        finish_row();
    }
    return rows;
}

std::map<std::string, std::size_t> header_index_map(const std::vector<std::string>& header) {
    std::map<std::string, std::size_t> index;
    for (std::size_t i = 0; i < header.size(); ++i) {
        std::string key;
        key.reserve(header[i].size());
        for (char ch : header[i]) {
            key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
        index.emplace(std::move(key), i);
    }
    return index;
}

std::string column(
    const std::vector<std::string>& row,
    const std::map<std::string, std::size_t>& index,
    const char* name
) {
    const auto it = index.find(name);
    if (it == index.end() || it->second >= row.size()) {
        return {};
    }
    return row[it->second];
}

} // namespace

std::string default_aps_csv_path() {
    const char* home = std::getenv("HOME");
    const std::string base = (home != nullptr && *home != '\0') ? home : "/root";
    return base + "/src/wap_ecsp_state.csv";
}

std::string default_clients_csv_path() {
    const char* home = std::getenv("HOME");
    const std::string base = (home != nullptr && *home != '\0') ? home : "/root";
    return base + "/src/wap_ecsp_clients.csv";
}

CsvTelemetryResult load_csv_telemetry(const CsvTelemetryPaths& paths) noexcept {
    CsvTelemetryResult result;
    result.ok = true;

    auto read_file = [](const std::string& path, std::string* out) -> bool {
        std::FILE* file = std::fopen(path.c_str(), "rb");
        if (file == nullptr) {
            return false;
        }
        char buffer[8192];
        std::size_t read = 0;
        while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
            out->append(buffer, read);
        }
        std::fclose(file);
        return true;
    };

    auto load_csv = [&](const std::string& path, auto fill_row, auto* sink) -> std::string {
        std::string text;
        if (!read_file(path, &text)) {
            return {};
        }
        const auto rows = parse_csv_rows(text);
        if (rows.empty()) {
            return {};
        }
        const bool has_header = !rows.empty() && rows.front().size() >= 1 &&
                                rows.front().front() == "ap_name";
        std::map<std::string, std::size_t> index;
        std::size_t start = 0;
        if (has_header) {
            index = header_index_map(rows.front());
            start = 1;
        }
        for (std::size_t i = start; i < rows.size(); ++i) {
            const auto& raw = rows[i];
            bool empty = true;
            for (const auto& cell : raw) {
                if (!cell.empty()) {
                    empty = false;
                    break;
                }
            }
            if (empty) {
                continue;
            }
            if (!fill_row(raw, index, sink)) {
                continue;
            }
        }
        return path;
    };

    if (!paths.aps_csv.empty()) {
        const std::string error = load_csv(
            paths.aps_csv,
            [](const std::vector<std::string>& raw,
               const std::map<std::string, std::size_t>& index,
               std::vector<ApStateCsvRow>* sink) {
                ApStateCsvRow parsed;
                parsed.ap_name = column(raw, index, "ap_name");
                parsed.ip = column(raw, index, "ip");
                auto mac = domain::MacAddress::parse(column(raw, index, "eth_mac"));
                if (!mac.has_value()) {
                    return false;
                }
                parsed.eth_mac = *mac;
                parsed.bssid_5g = column(raw, index, "bssid_5g");
                parsed.ssid_5g = column(raw, index, "ssid_5g");
                parsed.channel_5g = column(raw, index, "channel_5g");
                parsed.bandwidth_5g = column(raw, index, "bandwidth_5g");
                parsed.phy_5g = column(raw, index, "phy_5g");
                parsed.bssid_2g = column(raw, index, "bssid_2g");
                parsed.ssid_2g = column(raw, index, "ssid_2g");
                parsed.channel_2g = column(raw, index, "channel_2g");
                parsed.bandwidth_2g = column(raw, index, "bandwidth_2g");
                parsed.phy_2g = column(raw, index, "phy_2g");
                parsed.clients_24g = column(raw, index, "clients_24g");
                parsed.clients_5g = column(raw, index, "clients_5g");
                parsed.total_clients = column(raw, index, "total_clients");
                parsed.updated_at = column(raw, index, "updated_at");
                sink->push_back(std::move(parsed));
                return true;
            },
            &result.aps
        );
        (void)error;
    }

    if (!paths.clients_csv.empty()) {
        const std::string error = load_csv(
            paths.clients_csv,
            [](const std::vector<std::string>& raw,
               const std::map<std::string, std::size_t>& index,
               std::vector<ClientCsvRow>* sink) {
                ClientCsvRow parsed;
                parsed.ap_name = column(raw, index, "ap_name");
                auto ap_mac = domain::MacAddress::parse(column(raw, index, "ap_eth_mac"));
                auto client_mac = domain::MacAddress::parse(column(raw, index, "client_mac"));
                if (!ap_mac.has_value() || !client_mac.has_value()) {
                    return false;
                }
                parsed.ap_eth_mac = *ap_mac;
                parsed.client_mac = *client_mac;
                parsed.band_5g = band_is_5g(column(raw, index, "band"));
                parsed.ssid = column(raw, index, "ssid");
                parsed.updated_at = column(raw, index, "updated_at");
                parsed.client_ip = column(raw, index, "client_ip");
                parsed.client_name = column(raw, index, "client_name");
                sink->push_back(std::move(parsed));
                return true;
            },
            &result.clients
        );
        (void)error;
    }

    return result;
}

std::string wsettings_5g_json(const ApStateCsvRow& row) {
    std::string out = "{\"region\":";
    out += std::to_string(kRadioRegion);
    if (!row.channel_5g.empty()) {
        out += ",\"ch\":";
        out += quoted(row.channel_5g);
    }
    if (!row.bandwidth_5g.empty()) {
        out += ",\"bw\":";
        out += quoted(row.bandwidth_5g);
    }
    out += ",\"txPower\":\"";
    out += std::to_string(kDefaultTxPower);
    out += "dBm\"";
    if (auto bssid = domain::MacAddress::parse(row.bssid_5g); bssid.has_value()) {
        out += ",\"bssid\":";
        out += quoted(bssid->omada());
    }
    if (!row.ssid_5g.empty()) {
        out += ",\"ssid\":";
        out += quoted(row.ssid_5g);
    }
    out += "}";
    return out;
}

std::string wsettings_2g_json(const ApStateCsvRow& row) {
    std::string out = "{\"region\":";
    out += std::to_string(kRadioRegion);
    if (!row.has_2g()) {
        out += ",\"txPower\":\"";
        out += std::to_string(kDefaultTxPower);
        out += "dBm\"}";
        return out;
    }
    if (!row.channel_2g.empty()) {
        out += ",\"ch\":";
        out += quoted(row.channel_2g);
    }
    if (!row.bandwidth_2g.empty()) {
        out += ",\"bw\":";
        out += quoted(row.bandwidth_2g);
    }
    out += ",\"txPower\":\"";
    out += std::to_string(kDefaultTxPower);
    out += "dBm\"";
    if (auto bssid = domain::MacAddress::parse(row.bssid_2g); bssid.has_value()) {
        out += ",\"bssid\":";
        out += quoted(bssid->omada());
    }
    if (!row.ssid_2g.empty()) {
        out += ",\"ssid\":";
        out += quoted(row.ssid_2g);
    }
    out += "}";
    return out;
}

std::string client_stats_row_json(
    const ClientCsvRow& row,
    const AgentSettings& settings,
    std::string_view phy
) {
    const int pm = pm_for(row.band_5g, phy);
    const int rid = row.band_5g ? 1 : 0;
    std::string out = "{\"mac\":";
    out += quoted(row.client_mac.omada());
    out += ",\"rid\":";
    out += std::to_string(rid);
    out += ",\"ap\":";
    out += quoted(settings.model + "-" + settings.mac.omada());
    out += ",\"ssid\":";
    out += quoted(row.ssid.empty() ? std::string_view("Example_SSID_5G") : std::string_view(row.ssid));
    out += ",\"snr\":35,\"rssi\":-60,\"ccq\":0,\"down\":0,\"up\":0";
    out += ",\"time\":\"0 days 00:10:00\",\"ip\":";
    out += quoted(row.client_ip.empty() ? std::string_view("0.0.0.0") : std::string_view(row.client_ip));
    out += ",\"name\":";
    out += quoted(row.client_name.empty() ? std::string_view("client") : std::string_view(row.client_name));
    out += ",\"type\":\"unknown\",\"pm\":";
    out += std::to_string(pm);
    out += ",\"ps\":0,\"txR\":400000,\"rxR\":400000,\"txT\":0,\"guest\":0";
    out += ",\"rxP\":100,\"txP\":100,\"aTime\":60}";
    return out;
}

CsvInformProvider::CsvInformProvider(AgentSettings settings, CsvTelemetryPaths paths, LanObservation lan)
    : settings_(std::move(settings)),
      paths_(std::move(paths)),
      lan_(std::move(lan)) {
    if (paths_.aps_csv.empty()) {
        paths_.aps_csv = default_aps_csv_path();
    }
    if (paths_.clients_csv.empty()) {
        paths_.clients_csv = default_clients_csv_path();
    }
}

InformSnapshot CsvInformProvider::build(bool need_reply, std::uint64_t uptime_seconds) {
    InformSnapshot snapshot;
    snapshot.need_reply = need_reply;
    snapshot.uptime_seconds = uptime_seconds;
    snapshot.lan = lan_;

    const auto telemetry = load_csv_telemetry(paths_);

    // Radio telemetry: match this instance's eth_mac; prefer the last row so the
    // most recently appended state wins.
    const ApStateCsvRow* matched_state = nullptr;
    for (const auto& row : telemetry.aps) {
        if (row.eth_mac == settings_.mac) {
            matched_state = &row;
        }
    }
    if (matched_state != nullptr) {
        snapshot.extra_body_members.push_back({"wSettings_2G", wsettings_2g_json(*matched_state)});
        snapshot.extra_body_members.push_back({"wSettings_5G", wsettings_5g_json(*matched_state)});
    }

    if (!telemetry.clients.empty()) {
        std::string phy;
        if (matched_state != nullptr && !matched_state->phy_5g.empty()) {
            phy = matched_state->phy_5g;
        } else if (!settings_.phy.empty()) {
            phy = settings_.phy;
        } else {
            phy = "vht";
        }
        std::string array = "[";
        bool first = true;
        for (const auto& row : telemetry.clients) {
            if (row.ap_eth_mac != settings_.mac) {
                continue;
            }
            if (!first) {
                array.push_back(',');
            }
            first = false;
            array += client_stats_row_json(row, settings_, phy);
        }
        if (!first) {
            array.push_back(']');
            snapshot.extra_body_members.push_back({"clients", std::move(array)});
        }
    }

    return snapshot;
}

} // namespace openomada::application
