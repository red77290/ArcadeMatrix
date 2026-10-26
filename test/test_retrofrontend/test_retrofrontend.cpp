#include <Arduino.h>
#include <ArduinoJson.h>
#include <unity.h>
#include "engines/FrontendSyncEngine.h"
#include "core/ConfigLoader.h"
#include "core/ConfigSanitizer.h"

void setUp(void) {}
void tearDown(void) {}

/**
 * @brief Tests sanitization and stripping of verbose system strings from Recalbox/Batocera MQTT events.
 *
 * Verifies that prefixes like "Arcade manufacturer " or "Arcade System " are cleanly removed
 * to produce canonical system identifiers matching the SD card filesystem directory structure.
 */
void test_clean_system_name(void) {
    TEST_ASSERT_EQUAL_STRING("Toaplan", FrontendSyncEngine::cleanSystemName("Arcade manufacturer Toaplan").c_str());
    TEST_ASSERT_EQUAL_STRING("Atari", FrontendSyncEngine::cleanSystemName("Arcade Manufacturer Atari").c_str());
    TEST_ASSERT_EQUAL_STRING("NeoGeo", FrontendSyncEngine::cleanSystemName("Arcade manufacturer NeoGeo").c_str());
    TEST_ASSERT_EQUAL_STRING("CPS1", FrontendSyncEngine::cleanSystemName("Arcade System CPS1").c_str());
}

/**
 * @brief Tests lookup priority and variant generation for Pixelcade artwork on SD card.
 *
 * Verifies that given a system identifier (e.g. "snes"), the generator creates all expected
 * fallback paths in the correct priority order:
 * 1. "console/default-<system>"
 * 2. "console/<system>"
 */
void test_system_name_variants_priority(void) {
    std::vector<FrontendSyncEngine::SystemVariant> variants = FrontendSyncEngine::getSystemNameVariants("snes");
    TEST_ASSERT_TRUE(variants.size() > 0);
    
    // First variant must be the standard default console logo
    TEST_ASSERT_EQUAL_STRING("console", variants[0].folder.c_str());
    TEST_ASSERT_EQUAL_STRING("default-snes", variants[0].name.c_str());

    bool foundConsoleDefault = false;
    bool foundConsoleSnes = false;
    for (const auto& v : variants) {
        if (v.folder == "console" && v.name == "default-snes") foundConsoleDefault = true;
        if (v.folder == "console" && v.name == "snes") foundConsoleSnes = true;
    }
    TEST_ASSERT_TRUE(foundConsoleDefault);
    TEST_ASSERT_TRUE(foundConsoleSnes);
}

/**
 * @brief Tests variant generation for arcade manufacturer strings with casing variations.
 *
 * Verifies that arcade manufacturers (e.g. "Arcade manufacturer Toaplan" and "Arcade Manufacturer Atari")
 * resolve to their appropriate default artwork paths and system aliases.
 */
void test_arcade_manufacturer_cleaning(void) {
    std::vector<FrontendSyncEngine::SystemVariant> variants = FrontendSyncEngine::getSystemNameVariants("Arcade manufacturer Toaplan");
    bool foundDefaultToaplan = false;
    bool foundToaplan = false;
    for (const auto& v : variants) {
        if (v.folder == "console" && v.name == "default-Toaplan") foundDefaultToaplan = true;
        if (v.folder == "console" && v.name == "Toaplan") foundToaplan = true;
    }
    TEST_ASSERT_TRUE(foundDefaultToaplan);
    TEST_ASSERT_TRUE(foundToaplan);

    std::vector<FrontendSyncEngine::SystemVariant> atariVariants = FrontendSyncEngine::getSystemNameVariants("Arcade Manufacturer Atari");
    bool foundDefaultZatari = false;
    bool foundDirectAtari = false;
    for (const auto& v : atariVariants) {
        if (v.folder == "console" && v.name == "default-zatari") foundDefaultZatari = true;
        if (v.folder == "console" && v.name == "atari") foundDirectAtari = true;
    }
    TEST_ASSERT_TRUE(foundDefaultZatari);
    TEST_ASSERT_TRUE(foundDirectAtari);
}

/**
 * @brief Tests system ID to Pixelcade artwork folder mapping.
 *
 * Verifies that console systems correctly map to their canonical Pixelcade folders
 * without any erroneous "console/" prefix (which is strictly reserved for system logos).
 */
void test_map_system_to_pixelcade_folder(void) {
    TEST_ASSERT_EQUAL_STRING("snes", FrontendSyncEngine::mapSystemToPixelcadeFolder("snes").c_str());
    TEST_ASSERT_EQUAL_STRING("snes", FrontendSyncEngine::mapSystemToPixelcadeFolder("supernintendo").c_str());
    TEST_ASSERT_EQUAL_STRING("nes", FrontendSyncEngine::mapSystemToPixelcadeFolder("nes").c_str());
    TEST_ASSERT_EQUAL_STRING("genesis", FrontendSyncEngine::mapSystemToPixelcadeFolder("megadrive").c_str());
    TEST_ASSERT_EQUAL_STRING("genesis", FrontendSyncEngine::mapSystemToPixelcadeFolder("genesis").c_str());
    TEST_ASSERT_EQUAL_STRING("psx", FrontendSyncEngine::mapSystemToPixelcadeFolder("psx").c_str());
    TEST_ASSERT_EQUAL_STRING("psx", FrontendSyncEngine::mapSystemToPixelcadeFolder("playstation").c_str());
    TEST_ASSERT_EQUAL_STRING("mame", FrontendSyncEngine::mapSystemToPixelcadeFolder("cps1").c_str());
    TEST_ASSERT_EQUAL_STRING("mame", FrontendSyncEngine::mapSystemToPixelcadeFolder("cps2").c_str());
    TEST_ASSERT_EQUAL_STRING("mame", FrontendSyncEngine::mapSystemToPixelcadeFolder("mame").c_str());
    TEST_ASSERT_EQUAL_STRING("neogeo", FrontendSyncEngine::mapSystemToPixelcadeFolder("neogeo").c_str());
    TEST_ASSERT_EQUAL_STRING("n64", FrontendSyncEngine::mapSystemToPixelcadeFolder("n64").c_str());
    TEST_ASSERT_EQUAL_STRING("gba", FrontendSyncEngine::mapSystemToPixelcadeFolder("gba").c_str());

    // Verify no "console/" prefix is introduced in game folder resolution
    String snesFolder = FrontendSyncEngine::mapSystemToPixelcadeFolder("snes");
    TEST_ASSERT_FALSE(snesFolder.startsWith("console/"));
    String nesFolder = FrontendSyncEngine::mapSystemToPixelcadeFolder("nes");
    TEST_ASSERT_FALSE(nesFolder.startsWith("console/"));
}

/**
 * @brief Tests parsing of DMD Media Showcase /api/v1/health MQTT broker metadata.
 */
void test_parse_showcase_health_broker(void) {
    String host;
    uint16_t port = 0;

    // 1. Embedded broker reporting loopback substitutes the Showcase server host
    const char* embeddedJson = "{\"status\":\"ok\",\"mqtt_mode\":\"embedded\",\"mqtt_host\":\"127.0.0.1\",\"mqtt_port\":1883}";
    TEST_ASSERT_TRUE(FrontendSyncEngine::parseShowcaseHealthBroker(embeddedJson, "192.168.1.50", host, port));
    TEST_ASSERT_EQUAL_STRING("192.168.1.50", host.c_str());
    TEST_ASSERT_EQUAL_UINT16(1883, port);

    // 2. External broker preserves explicit external broker IP and custom port
    const char* externalJson = "{\"status\":\"ok\",\"mqtt_mode\":\"external\",\"mqtt_host\":\"192.168.1.222\",\"mqtt_port\":1884}";
    TEST_ASSERT_TRUE(FrontendSyncEngine::parseShowcaseHealthBroker(externalJson, "192.168.1.50", host, port));
    TEST_ASSERT_EQUAL_STRING("192.168.1.222", host.c_str());
    TEST_ASSERT_EQUAL_UINT16(1884, port);

    // 3. Malformed JSON is cleanly rejected
    TEST_ASSERT_FALSE(FrontendSyncEngine::parseShowcaseHealthBroker("{broken", "192.168.1.50", host, port));
}

/**
 * @brief Tests on-demand queue JSON payload generation for Showcase Profile vs Custom DMD Card modes.
 */
void test_build_showcase_queue_request_json(void) {
    MqttConfig cfg;
    cfg.card_style_source = "showcase_profile";

    String profilePayload = FrontendSyncEngine::buildShowcaseQueueRequestJson(
        cfg, FrontendSyncEngine::cleanSystemName("Arcade manufacturer NeoGeo"), "mslugx", 128, 32, "fr"
    );
    StaticJsonDocument<1024> docProfile;
    TEST_ASSERT_FALSE(deserializeJson(docProfile, profilePayload));
    TEST_ASSERT_EQUAL_STRING("games", docProfile["domain"] | "");
    TEST_ASSERT_EQUAL_STRING("NeoGeo", docProfile["systems"][0] | "");
    TEST_ASSERT_EQUAL_STRING("mslugx", docProfile["query"] | "");
    TEST_ASSERT_EQUAL_STRING("play_now", docProfile["mode"] | "");
    TEST_ASSERT_FALSE(docProfile["mark_seen"].as<bool>());
    TEST_ASSERT_TRUE(docProfile["async_prepare"].as<bool>());
    TEST_ASSERT_TRUE(docProfile["render_card_in_stream"].as<bool>());
    TEST_ASSERT_FALSE(docProfile.containsKey("card_layout"));

    // Custom presentation overrides on DMD
    cfg.card_style_source = "custom";
    cfg.include_card_intro = true;
    cfg.card_layout = "arcade_cabinet";
    cfg.card_theme = "neon_border";
    cfg.show_poster = true;
    cfg.show_synopsis = false;
    cfg.text_scroll_speed = "6";
    cfg.jacket_panorama = true;
    cfg.marquee_panorama = false;
    cfg.snapshot_panorama_count = 3;
    cfg.show_video = true;
    cfg.video_max_duration_s = 25;
    cfg.video_fps = 24;
    cfg.video_mode = "panorama";
    cfg.color_boost = "pixel_art";

    String customPayload = FrontendSyncEngine::buildShowcaseQueueRequestJson(
        cfg, "mame", "ssf2t", 128, 64, "en"
    );
    StaticJsonDocument<1024> docCustom;
    TEST_ASSERT_FALSE(deserializeJson(docCustom, customPayload));
    TEST_ASSERT_EQUAL_STRING("arcade_cabinet", docCustom["card_layout"] | "");
    TEST_ASSERT_EQUAL_STRING("neon_border", docCustom["card_theme"] | "");
    TEST_ASSERT_FALSE(docCustom["include_synopsis"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("6", docCustom["text_scroll_speed"] | "");
    TEST_ASSERT_EQUAL_UINT8(3, docCustom["snapshot_panorama_count"].as<uint8_t>());
    TEST_ASSERT_EQUAL_UINT16(25, docCustom["video_max_duration_s"].as<uint16_t>());
    TEST_ASSERT_EQUAL_UINT8(24, docCustom["video_fps"].as<uint8_t>());
    TEST_ASSERT_EQUAL_STRING("panorama", docCustom["video_mode"] | "");
    TEST_ASSERT_EQUAL_STRING("pixel_art", docCustom["color_boost"] | "");
}

/**
 * @brief Tests MqttConfig FrontendSync Showcase sanitization and JSON round-trip persistence.
 */
void test_mqtt_config_sanitization_and_roundtrip(void) {
    ConfigLoader loader;
    const char* rawJson = R"JSON({
        "mqtt": {
            "enabled": true,
            "use_showcase_broker": false,
            "ingame_showcase_enabled": true,
            "ingame_cycle_mode": "invalid_mode",
            "ingame_marquee_interval_sec": 9999,
            "card_style_source": "custom",
            "card_layout": "bad_layout",
            "card_theme": "bad_theme",
            "text_scroll_speed": "",
            "snapshot_panorama_count": 9,
            "video_max_duration_s": 1,
            "video_fps": 120,
            "video_mode": "bad_vmode",
            "color_boost": "bad_boost"
        }
    })JSON";
    TEST_ASSERT_TRUE(loader.parseFromJson(rawJson));
    ConfigSanitizer::sanitize(loader, false);

    const MqttConfig& m = loader.mqtt;
    TEST_ASSERT_FALSE(m.use_showcase_broker);
    TEST_ASSERT_TRUE(m.ingame_showcase_enabled);
    TEST_ASSERT_EQUAL_STRING("once", m.ingame_cycle_mode.c_str());
    TEST_ASSERT_EQUAL_INT(600, m.ingame_marquee_interval_sec);
    TEST_ASSERT_EQUAL_STRING("custom", m.card_style_source.c_str());
    TEST_ASSERT_EQUAL_STRING("classic_split", m.card_layout.c_str());
    TEST_ASSERT_EQUAL_STRING("tinted", m.card_theme.c_str());
    TEST_ASSERT_EQUAL_STRING("4", m.text_scroll_speed.c_str());
    TEST_ASSERT_EQUAL_INT(5, m.snapshot_panorama_count);
    TEST_ASSERT_EQUAL_INT(2, m.video_max_duration_s);
    TEST_ASSERT_EQUAL_INT(60, m.video_fps);
    TEST_ASSERT_EQUAL_STRING("auto_action", m.video_mode.c_str());
    TEST_ASSERT_EQUAL_STRING("auto", m.color_boost.c_str());

    String serialized = loader.serializeToJson(false);
    ConfigLoader reloaded;
    TEST_ASSERT_TRUE(reloaded.parseFromJson(serialized.c_str()));
    TEST_ASSERT_EQUAL_STRING("classic_split", reloaded.mqtt.card_layout.c_str());
    TEST_ASSERT_EQUAL_INT(600, reloaded.mqtt.ingame_marquee_interval_sec);
}

void setup() {
    Serial.begin(115200);
    delay(100);
    UNITY_BEGIN();
    RUN_TEST(test_clean_system_name);
    RUN_TEST(test_system_name_variants_priority);
    RUN_TEST(test_arcade_manufacturer_cleaning);
    RUN_TEST(test_map_system_to_pixelcade_folder);
    RUN_TEST(test_parse_showcase_health_broker);
    RUN_TEST(test_build_showcase_queue_request_json);
    RUN_TEST(test_mqtt_config_sanitization_and_roundtrip);
    UNITY_END();
}

void loop() {
    delay(100);
}
