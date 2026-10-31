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

/**
 * @brief Verifies that `InGameShowcaseEngine` shares `ShowcaseStreamPlayer` (lossless 16-bit
 *        `DMDZ` v2 Zero-Copy `ScopedCanvasGuard` with `0 B` engine fallback buffer) and disables
 *        status overlays during live gameplay showcase playback (`allowsOverlay() == false`).
 */
void test_ingame_showcase_engine_zero_copy_dmdz2_and_no_overlay(void) {
    class MockInGameCanvasSurface : public IDrawingSurface {
    public:
        uint16_t pixels[128 * 32];
        bool dirty = false;
        uint32_t extDraws = 0U;
        MockInGameCanvasSurface() : IDrawingSurface(128, 32, 128, 32) {
            memset(pixels, 0, sizeof(pixels));
        }
        void drawPixel(int16_t x, int16_t y, uint16_t color) override {
            if (x >= 0 && x < 128 && y >= 0 && y < 32) {
                pixels[y * 128 + x] = color;
                dirty = true;
            }
        }
        void clear(uint16_t color = 0) override {
            for (size_t i = 0; i < 128 * 32; ++i) pixels[i] = color;
            dirty = true;
        }
        bool isDirty() const override { return dirty; }
        void markDirty() override { dirty = true; }
        void markExternalDraw() override {
            dirty = true;
            ++extDraws;
        }
        PresentationTiming present() override {
            dirty = false;
            return PresentationTiming{};
        }
        void blit565(const uint16_t* src, int16_t x, int16_t y, int16_t w, int16_t h, int16_t stride = -1) override {
            int16_t st = stride > 0 ? stride : w;
            for (int16_t ry = 0; ry < h; ++ry) {
                for (int16_t rx = 0; rx < w; ++rx) {
                    drawPixel(x + rx, y + ry, src[ry * st + rx]);
                }
            }
        }
        CanvasView acquireCanvas() override {
            dirty = true;
            return CanvasView{pixels, 128U, 32U, 128U};
        }
        void releaseCanvas() override {}
        bool hasCanvas() const override { return true; }
        CanvasStorage canvasStorage() const override { return CanvasStorage::SRAM; }
        PresentationStrategy presentationStrategy() const override { return PresentationStrategy::CANVAS_BURST_SINGLE; }
        size_t memoryUsageBytes() const override { return sizeof(pixels); }
    };

    class MockInGameContext : public EngineContext {
    public:
        IDrawingSurface* surf = nullptr;
        explicit MockInGameContext(IDrawingSurface* s) : surf(s) {}
        IDrawingSurface* getSurface() override { return surf; }
        FrontendSyncEngine* getEventBus() override { return nullptr; }
        void getSystemTime(struct tm* t) override {
            if (t) memset(t, 0, sizeof(*t));
        }
        DisplayGeometry getGeometry() const override {
            DisplayGeometry g;
            g.width = 128;
            g.height = 32;
            g.nativeWidth = 128;
            g.nativeHeight = 32;
            return g;
        }
    };

    MockInGameCanvasSurface surf;
    MockInGameContext ctx(&surf);
    InGameShowcaseEngine inGame;

    TEST_ASSERT_FALSE(inGame.allowsOverlay());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EngineError::OK), static_cast<int>(inGame.initialize(&ctx, nullptr)));
    inGame.activate();
    TEST_ASSERT_TRUE(inGame.isDisplayEngaged());
    TEST_ASSERT_TRUE(inGame.streamPlayer().isZeroCopyEligible());
    TEST_ASSERT_NULL(inGame.streamPlayer().fallbackFrameBuffer());

    // Push a lossless 16-bit `DMDZ` v2 `kFrameKey` (`0x07FF` cyan) into the shared `8,448 B` SPSC ring
    TEST_ASSERT_TRUE(inGame.streamPlayer().ensureDecoderAllocated(false));
    TEST_ASSERT_TRUE(
        inGame.streamPlayer().allocateSpscRing(
            engines::showcase::ShowcaseStreamPlayer::SRAM_SPSC_DEFAULT_RING_BYTES,
            false
        )
    );
    std::vector<uint8_t> wire;
    wire.push_back(0U);
    wire.push_back(0U);
    wire.push_back(core::drawing::DmdzStreamDecoder::kFrameKey);
    wire.push_back(0xE0U); // LIT 1: 0x07FF (cyan)
    wire.push_back(0xFFU);
    wire.push_back(0x07U);
    uint32_t remPx = 128U * 32U - 1U;
    while (remPx > 0U) {
        const uint8_t run = static_cast<uint8_t>(remPx > 64U ? 64U : remPx);
        wire.push_back(static_cast<uint8_t>(0x40U | (run - 1U))); // RUN(1..64)
        remPx -= run;
    }
    const uint16_t payLen = static_cast<uint16_t>(wire.size() - 2U);
    wire[0] = static_cast<uint8_t>(payLen & 0xFFU);
    wire[1] = static_cast<uint8_t>(payLen >> 8);
    TEST_ASSERT_EQUAL_UINT32(
        wire.size(),
        static_cast<uint32_t>(inGame.streamPlayer().spscRing().push(wire.data(), wire.size()))
    );
    inGame.streamPlayer().armSpscStreamForTest(128U, 32U, 2U, true, 1U, 20U);

    inGame.update(&ctx);
    inGame.render(&ctx);

    TEST_ASSERT_NULL(inGame.streamPlayer().fallbackFrameBuffer());
    TEST_ASSERT_EQUAL_UINT32(1U, surf.extDraws);
    TEST_ASSERT_EQUAL_UINT32(1U, inGame.presentedFrames());
    TEST_ASSERT_EQUAL_HEX16(0x07FFU, surf.pixels[0]);
    TEST_ASSERT_EQUAL_HEX16(0x07FFU, surf.pixels[128 * 32 - 1]);

    inGame.deactivate();
    inGame.streamPlayer().releaseAll();
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
    RUN_TEST(test_ingame_showcase_engine_zero_copy_dmdz2_and_no_overlay);
    UNITY_END();
}

void loop() {
    delay(100);
}
