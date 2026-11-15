#include "openomada/domain/device_profile.hpp"

#include "openomada/protocol/ecsp_message.hpp"

#include <cstddef>
#include <cstdio>

namespace openomada::domain {
namespace {

std::string quote(std::string_view value) {
    std::string out = "\"";
    out += protocol::json_escape(value);
    out += "\"";
    return out;
}

} // namespace

// Deterministic 32-hex-digit hardware id derived from the model string. The
// controller stores hwId/oemId from the adoption deviceInfo and uses them for
// model-template and cloud lookups; a stable synthetic value keeps those paths
// from logging "hwId or oemId is null".
std::string hardware_id_for(std::string_view model) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (char ch : model) {
        hash ^= static_cast<unsigned char>(ch);
        hash *= 1099511628211ULL;
    }
    char digits[33];
    for (int i = 0; i < 32; ++i) {
        digits[i] = "0123456789ABCDEF"[(hash >> ((i % 8) * 4)) & 0xF];
        if (i % 8 == 7) {
            hash = hash * 1099511628211ULL + static_cast<std::uint64_t>(i);
        }
    }
    digits[32] = '\0';
    return std::string(digits, 32);
}

std::string serial_for(const domain::MacAddress& mac) {
    std::string sn = "SN";
    for (std::uint8_t byte : mac.bytes()) {
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02X", byte);
        sn += buf;
    }
    return sn;
}

std::string uptime_string(std::uint64_t uptime_seconds) {
    const std::uint64_t days = uptime_seconds / 86400;
    const std::uint64_t hours = (uptime_seconds % 86400) / 3600;
    const std::uint64_t minutes = (uptime_seconds % 3600) / 60;
    const std::uint64_t seconds = uptime_seconds % 60;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%llu days %02llu:%02llu:%02llu",
                  static_cast<unsigned long long>(days),
                  static_cast<unsigned long long>(hours),
                  static_cast<unsigned long long>(minutes),
                  static_cast<unsigned long long>(seconds));
    return buf;
}

AccessPointProfile::AccessPointProfile(application::AgentSettings settings)
    : settings_(std::move(settings)),
      components_v2_{
          {"system", "2.0"},
          {"configVersion", "1.0"},
          {"time", "1.0"},
          {"informInterval", "1.2"},
          {"devInform", "2.0"},
      } {}

std::string AccessPointProfile::device_info_json(bool is_factory, bool include_ip, bool include_factory_flag) const {
    std::string out = "{";
    bool comma = false;
    const auto add_comma = [&out, &comma]() {
        if (comma) {
            out.push_back(',');
        }
        comma = true;
    };
    if (include_ip) {
        add_comma();
        out += "\"ip\":";
        out += quote(settings_.device_ip);
    }
    if (include_factory_flag) {
        add_comma();
        out += "\"isFactory\":";
        out += is_factory ? "true" : "false";
    }
    add_comma();
    out += "\"name\":";
    out += quote(settings_.device_name);
    add_comma();
    out += "\"model\":";
    out += quote(settings_.model);
    add_comma();
    out += "\"modelVersion\":";
    out += quote(settings_.model_version);
    add_comma();
    out += "\"firmwareVersion\":";
    out += quote(settings_.firmware_version);
    add_comma();
    out += "\"hardwareVersion\":";
    out += quote(settings_.hardware_version);
    add_comma();
    out += "\"upTime\":0,\"cpuUti\":0,\"memUti\":0,\"wirelessLinked\":false,\"p2p\":false,\"supportBridge\":0";
    add_comma();
    out += "\"sn\":";
    out += quote(serial_for(settings_.mac));
    add_comma();
    out += "\"hwId\":";
    out += quote(hardware_id_for(settings_.model));
    add_comma();
    out += "\"oemId\":\"C9E1D39A4319702269CA46C37429750D\"";
    add_comma();
    out += "\"mainMac\":";
    out += quote(settings_.mac.omada());
    out += "}";
    return out;
}

std::string AccessPointProfile::adoption_device_info_json() const {
    return device_info_json(false, false, false);
}

std::string AccessPointProfile::inform_device_info_json(std::uint64_t uptime_seconds) const {
    std::string out = "{";
    out += "\"ip\":";
    out += quote(settings_.device_ip);
    out += ",\"isFactory\":false";
    out += ",\"name\":";
    out += quote(settings_.device_name);
    out += ",\"model\":";
    out += quote(settings_.model);
    out += ",\"modelVersion\":";
    out += quote(settings_.model_version);
    out += ",\"firmwareVersion\":";
    out += quote(settings_.firmware_version);
    out += ",\"hardwareVersion\":";
    out += quote(settings_.hardware_version);
    out += ",\"upTime\":";
    out += quote(uptime_string(uptime_seconds));
    out += ",\"cpuUti\":0,\"memUti\":0,\"wirelessLinked\":false,\"p2p\":false,\"supportBridge\":0";
    out += ",\"sn\":";
    out += quote(serial_for(settings_.mac));
    out += ",\"hwId\":";
    out += quote(hardware_id_for(settings_.model));
    out += ",\"oemId\":\"C9E1D39A4319702269CA46C37429750D\"";
    out += ",\"mainMac\":";
    out += quote(settings_.mac.omada());
    out += "}";
    return out;
}

std::string AccessPointProfile::device_misc_json() const {
    std::string out;
    out.reserve(256);
    out += "{\"support_5g\":true,\"support_11ac\":true,\"support_lag\":false,";
    out += "\"supportMesh\":0,\"customizeRegion\":";
    out += std::to_string(settings_.customize_region);
    out += ",\"support_channelLimit\":false,\"channelLimit_mode\":0,\"supportDfs\":7,";
    out += "\"lanPortsNum\":1,\"lanVlanPorts\":[],\"lanPoePorts\":[],\"supportRoaming\":1}";
    return out;
}

std::string AccessPointProfile::components_v2_json() const {
    std::string out = "{";
    bool comma = false;
    for (const auto& component : components_v2_) {
        if (comma) {
            out.push_back(',');
        }
        comma = true;
        out += quote(component.name);
        out.push_back(':');
        out += quote(component.version);
    }
    out += "}";
    return out;
}

namespace {

struct ChannelEntry {
    int fr;
    int vl;
    std::uint32_t c_flag;
    int m_pow;
    int d_flag;
    bool has_d_flag;
};

constexpr int kMinPow = 4;
constexpr int kMaxPow = 21;
constexpr int kMimo = 2;
constexpr int kOfdma = 1;
constexpr int kSupportSsidNum = 8;
constexpr int kSupportMaxAssocClient = 64;

constexpr ChannelEntry kChannels24G[] = {
    {2412, 1, 196832, kMaxPow, 0, false},
    {2417, 2, 196832, kMaxPow, 0, false},
    {2422, 3, 196832, kMaxPow, 0, false},
    {2427, 4, 196832, kMaxPow, 0, false},
    {2432, 5, 458976, kMaxPow, 0, false},
    {2437, 6, 458976, kMaxPow, 0, false},
    {2442, 7, 458976, kMaxPow, 0, false},
    {2447, 8, 327904, kMaxPow, 0, false},
    {2452, 9, 327904, kMaxPow, 0, false},
    {2457, 10, 327904, kMaxPow, 0, false},
    {2462, 11, 327904, kMaxPow, 0, false},
};

constexpr ChannelEntry kChannels5G[] = {
    // UNII-1 (ch 36-48)
    {5180, 1, 11731264, kMaxPow, 0, true},
    {5200, 2, 13959488, kMaxPow, 0, true},
    {5220, 3, 11731264, kMaxPow, 0, true},
    {5240, 4, 13959488, kMaxPow, 0, true},
    // UNII-2 / DFS (ch 52-140)
    {5260, 5, 11731776, kMaxPow, 4418, true},
    {5280, 6, 13960000, kMaxPow, 4418, true},
    {5300, 7, 11731776, kMaxPow, 4418, true},
    {5320, 8, 13960000, kMaxPow, 4418, true},
    {5500, 9, 11731776, kMaxPow, 4674, true},
    {5520, 10, 13960000, kMaxPow, 4674, true},
    {5540, 11, 11731776, kMaxPow, 4674, true},
    {5560, 12, 13960000, kMaxPow, 4674, true},
    {5580, 13, 11731776, kMaxPow, 4674, true},
    {5600, 14, 13960000, kMaxPow, 5698, true},
    {5620, 15, 11731776, kMaxPow, 5698, true},
    {5640, 16, 13960000, kMaxPow, 5698, true},
    {5660, 17, 3343168, kMaxPow, 4674, true},
    {5680, 18, 5571392, kMaxPow, 4674, true},
    {5700, 19, 1114944, kMaxPow, 4674, true},
    // UNII-3 (ch 149-165)
    {5745, 20, 11731264, kMaxPow, 0, true},
    {5765, 21, 13959488, kMaxPow, 0, true},
    {5785, 22, 11731264, kMaxPow, 0, true},
    {5805, 23, 13959488, kMaxPow, 0, true},
    {5825, 24, 1114432, kMaxPow, 0, true},
};

void append_radio_cap_item(std::string& out, int radio_id) {
    out += "{\"radioId\":";
    out += std::to_string(radio_id);
    out += ",\"minPow\":";
    out += std::to_string(kMinPow);
    out += ",\"maxPow\":";
    out += std::to_string(kMaxPow);
    out += ",\"mimo\":";
    out += std::to_string(kMimo);
    out += ",\"ofdma\":";
    out += std::to_string(kOfdma);
    out += ",\"ofdmaEnable\":0,\"supportSsidNum\":";
    out += std::to_string(kSupportSsidNum);
    out += ",\"supportMaxAssocClient\":";
    out += std::to_string(kSupportMaxAssocClient);
    out += "}";
}

void append_channel_item(std::string& out, int radio_id, std::string_view band, const ChannelEntry* channels, std::size_t count) {
    out += "{\"radioId\":";
    out += std::to_string(radio_id);
    out += ",\"band\":";
    out += quote(band);
    out += ",\"channelList\":[";
    for (std::size_t i = 0; i < count; ++i) {
        const ChannelEntry& channel = channels[i];
        if (i != 0) {
            out.push_back(',');
        }
        out += "{\"fr\":";
        out += std::to_string(channel.fr);
        out += ",\"vl\":";
        out += std::to_string(channel.vl);
        out += ",\"cFlag\":";
        out += std::to_string(channel.c_flag);
        out += ",\"mPow\":";
        out += std::to_string(channel.m_pow);
        if (channel.has_d_flag) {
            out += ",\"dFlag\":";
            out += std::to_string(channel.d_flag);
        }
        out.push_back('}');
    }
    out += "]}";
}

} // namespace

std::string AccessPointProfile::channel_info_json() const {
    std::string out;
    out.reserve(4096);
    out.push_back('[');
    append_channel_item(out, 0, "2.4G", kChannels24G, sizeof(kChannels24G) / sizeof(kChannels24G[0]));
    out.push_back(',');
    append_channel_item(out, 1, "5G", kChannels5G, sizeof(kChannels5G) / sizeof(kChannels5G[0]));
    out.push_back(']');
    return out;
}

std::string AccessPointProfile::radio_cap_json() const {
    std::string out;
    out.reserve(256);
    out.push_back('[');
    append_radio_cap_item(out, 0);
    out.push_back(',');
    append_radio_cap_item(out, 1);
    out.push_back(']');
    return out;
}

std::string controller_setting_json(std::string_view controller_id, std::string_view destination_id) {
    std::string out = "{\"controllerId\":";
    out += quote(controller_id);
    out += ",\"destOmadacId\":";
    out += quote(destination_id.empty() ? controller_id : destination_id);
    out += "}";
    return out;
}

} // namespace openomada::domain
