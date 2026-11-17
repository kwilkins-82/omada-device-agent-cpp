#include "openomada/application/csv_telemetry.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

void write_temp(const std::string& path, const std::string& text) {
    std::ofstream output(path);
    output << text;
}

openomada::application::CsvTelemetryPaths temp_paths() {
    return {"/tmp/opencode/test_wap_ecsp_state.csv", "/tmp/opencode/test_wap_ecsp_clients.csv"};
}

} // namespace

int main() {
    using namespace openomada;
    const auto paths = temp_paths();

    write_temp(
        paths.aps_csv,
        "ap_name,ip,eth_mac,bssid_5g,ssid_5g,channel_5g,bandwidth_5g,phy_5g,bssid_2g,ssid_2g,channel_2g,bandwidth_2g,phy_2g,clients_24g,clients_5g,total_clients,updated_at\n"
        "WAC124,192.0.2.10,02:00:00:00:00:01,AA-BB-CC-DD-EE-FF,Example_SSID_5G,36,80MHz,vht,AB-CD-EF-01-02-03,Example_SSID_2G,6,20MHz,ht,1,3,4,2026-11-15 10:00:00\n"
        "Other,192.0.2.12,00:11:22:33:44:55,11-22-33-44-55-66,Skip,44,80MHz,he,11-22-33-44-55-77,Skip_2,11,20MHz,ht,0,0,0,2026-11-15 10:00:00\n"
    );
    write_temp(
        paths.clients_csv,
        "ap_name,ap_eth_mac,client_mac,client_ip,client_name,band,ssid,updated_at\n"
        "WAC124,02:00:00:00:00:01,aa:bb:cc:dd:ee:01,192.0.2.51,Test-Phone,5g,Example_SSID_5G,2026-11-15 10:00:00\n"
        "WAC124,02:00:00:00:00:01,aa:bb:cc:dd:ee:02,192.0.2.52,Test-Speaker,2.4g,Example_SSID_5G,2026-11-15 10:00:00\n"
        "Other,00:11:22:33:44:55,aa:bb:cc:dd:ee:03,192.0.2.53,Foreign,5g,Skip,2026-11-15 10:00:00\n"
    );

    const auto telemetry = application::load_csv_telemetry(paths);
    require(telemetry.ok, "csv load ok");
    require(telemetry.aps.size() == 2, "two ap rows parsed");
    require(telemetry.clients.size() == 3, "three client rows parsed");

    const auto& matched = telemetry.aps.front();
    require(matched.eth_mac.bytes()[0] == 0x02, "ap eth mac parsed");
    require(matched.channel_5g == "36", "channel parsed");
    require(matched.bandwidth_5g == "80MHz", "bandwidth parsed");
    require(matched.channel_2g == "6", "2g channel parsed");
    require(matched.bandwidth_2g == "20MHz", "2g bandwidth parsed");
    require(matched.ssid_2g == "Example_SSID_2G", "2g ssid parsed");
    require(matched.has_2g(), "2g fields present");

    const auto& first_client = telemetry.clients.front();
    require(first_client.client_ip == "192.0.2.51", "client ip parsed");
    require(first_client.client_name == "Test-Phone", "client name parsed");

    openomada::application::AgentSettings settings;
    auto mac = domain::MacAddress::parse("02:00:00:00:00:01");
    require(mac.has_value(), "self mac parses");
    settings.mac = *mac;
    settings.model = "WAC124";

    const std::string wsettings = application::wsettings_5g_json(matched);
    require(
        wsettings == "{\"region\":840,\"ch\":\"36\",\"bw\":\"80MHz\",\"txPower\":\"21dBm\",\"bssid\":\"AA-BB-CC-DD-EE-FF\",\"ssid\":\"Example_SSID_5G\"}",
        "wSettings_5g json: " + wsettings
    );

    const std::string wsettings_2g = application::wsettings_2g_json(matched);
    require(
        wsettings_2g == "{\"region\":840,\"ch\":\"6\",\"bw\":\"20MHz\",\"txPower\":\"21dBm\",\"bssid\":\"AB-CD-EF-01-02-03\",\"ssid\":\"Example_SSID_2G\"}",
        "wSettings_2g json with 2g columns: " + wsettings_2g
    );

    openomada::application::ApStateCsvRow legacy_row = matched;
    legacy_row.bssid_2g.clear();
    legacy_row.ssid_2g.clear();
    legacy_row.channel_2g.clear();
    legacy_row.bandwidth_2g.clear();
    require(!legacy_row.has_2g(), "legacy row has no 2g fields");
    const std::string wsettings_2g_legacy = application::wsettings_2g_json(legacy_row);
    require(
        wsettings_2g_legacy == "{\"region\":840,\"txPower\":\"21dBm\"}",
        "wSettings_2g json fallback: " + wsettings_2g_legacy
    );

    const std::string client_5g = application::client_stats_row_json(telemetry.clients[0], settings, "vht");
    require(
        client_5g ==
            "{\"mac\":\"AA-BB-CC-DD-EE-01\",\"rid\":1,\"ap\":\"WAC124-02-00-00-00-00-01\","
            "\"ssid\":\"Example_SSID_5G\",\"snr\":35,\"rssi\":-60,\"ccq\":0,\"down\":0,\"up\":0,"
            "\"time\":\"0 days 00:10:00\",\"ip\":\"192.0.2.51\",\"name\":\"Test-Phone\",\"type\":\"unknown\","
            "\"pm\":5,\"ps\":0,\"txR\":400000,\"rxR\":400000,\"txT\":0,\"guest\":0,"
            "\"rxP\":100,\"txP\":100,\"aTime\":60}",
        "5g client json: " + client_5g
    );

    const std::string client_24g = application::client_stats_row_json(telemetry.clients[1], settings, "vht");
    require(client_24g.find("\"rid\":0") != std::string::npos, "2.4g rid 0");
    require(client_24g.find("\"pm\":4") != std::string::npos, "2.4g pm 4");
    require(client_24g.find("\"ip\":\"192.0.2.52\"") != std::string::npos, "2.4g client ip forwarded");
    require(client_24g.find("\"name\":\"Test-Speaker\"") != std::string::npos, "2.4g client name forwarded");

    openomada::application::ClientCsvRow legacy_client = telemetry.clients.front();
    legacy_client.client_ip.clear();
    legacy_client.client_name.clear();
    const std::string client_legacy = application::client_stats_row_json(legacy_client, settings, "vht");
    require(client_legacy.find("\"ip\":\"0.0.0.0\"") != std::string::npos, "legacy client ip fallback");
    require(client_legacy.find("\"name\":\"client\"") != std::string::npos, "legacy client name fallback");

    const std::string client_he = application::client_stats_row_json(telemetry.clients[0], settings, "he");
    require(client_he.find("\"pm\":6") != std::string::npos, "he pm 6");

    openomada::application::CsvInformProvider provider(settings, paths);
    const auto snapshot = provider.build(true, 120);
    require(snapshot.need_reply, "need_reply preserved");
    bool has_wsettings = false;
    bool has_wsettings_2g = false;
    bool has_client_stats = false;
    for (const auto& member : snapshot.extra_body_members) {
        if (member.key == "wSettings_5G") {
            has_wsettings = true;
        }
        if (member.key == "wSettings_2G") {
            has_wsettings_2g = true;
            require(member.json.find("\"ch\":\"6\"") != std::string::npos, "wSettings_2G channel");
            require(member.json.find("\"bw\":\"20MHz\"") != std::string::npos, "wSettings_2G bandwidth");
            require(member.json.find("\"bssid\":\"AB-CD-EF-01-02-03\"") != std::string::npos, "wSettings_2G bssid");
            require(member.json.find("\"ssid\":\"Example_SSID_2G\"") != std::string::npos, "wSettings_2G ssid");
            require(member.json.find("\"region\":840") != std::string::npos, "wSettings_2G region");
        }
        if (member.key == "clients") {
            has_client_stats = true;
            require(member.json.find("AA-BB-CC-DD-EE-01") != std::string::npos, "client 1 included");
            require(member.json.find("AA-BB-CC-DD-EE-02") != std::string::npos, "client 2 included");
            require(member.json.find("AA-BB-CC-DD-EE-03") == std::string::npos, "foreign ap excluded");
        }
    }
    require(has_wsettings, "wSettings_5G member present");
    require(has_wsettings_2g, "wSettings_2G member present");
    require(has_client_stats, "clients member present");

    const std::string body = application::build_inform_body_json(domain::AccessPointProfile(settings), snapshot);
    require(body.find("\"wSettings_5G\":{\"region\":840") != std::string::npos, "wSettings merged into inform body");
    require(body.find("\"wSettings_2G\":{\"region\":840") != std::string::npos, "wSettings_2G merged into inform body");
    require(body.find("\"clients\":[") != std::string::npos, "clients merged into inform body");

    std::remove(paths.aps_csv.c_str());
    std::remove(paths.clients_csv.c_str());

    if (g_failures == 0) {
        std::cout << "openomada-csv-telemetry-tests passed\n";
        return 0;
    }
    return 1;
}
