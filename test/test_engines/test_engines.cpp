#include <Arduino.h>
#include <unity.h>
#include "engines/DecibelEngine.h"
#include "engines/VisualizerEngine.h"
#include "engines/TempEngine.h"
#include "engines/MessageEngine.h"
#include "engines/MediaShowcaseEngines.h"
#include "engines/FrontendSyncEngine.h"
#include "engines/clocks/ClockFaceFont.h"
#include "core/drawing/IDrawingSurface.h"
#include "core/net/SecureHttpSession.h"
#include "core/net/ShowcaseDiscovery.h"
#include "core/ConfigLoader.h"
#include "core/NetworkBudget.h"
#include "core/I18n.h"

void setUp(void) {}
void tearDown(void) {}

/**
 * @brief Tests DecibelEngine descriptor contract and NoiseStatusLevel enum / threshold mapping.
 *
 * Verifies the canonical engine ID ("decibelMeter"), microphone requirement, lifecycle,
 * and NoiseStatusLevel classification boundaries:
 * - < 45 dB: NOISE_CALM (0)
 * - [45, 65) dB: NOISE_NORMAL (1)
 * - [65, 75) dB: NOISE_MODERATE (2)
 * - [75, 83) dB: NOISE_VIGILANCE (3)
 * - [83, 88] dB: NOISE_LIMIT (4)
 * - > 88 dB: NOISE_ALERT (5)
 */
void test_decibel_status_mapping(void) {
    DecibelEngineDescriptorHandler descHandler;
    EngineDescriptor desc = descHandler.getDescriptor();
    TEST_ASSERT_EQUAL_STRING("decibelMeter", desc.metadata.id);
    TEST_ASSERT_TRUE(desc.requirements.needsAudioInput);

    DecibelEngine engine;
    TEST_ASSERT_TRUE(engine.allowsOverlay());
    TEST_ASSERT_TRUE(engine.needsClear());

    TEST_ASSERT_EQUAL_INT(0, (int)NOISE_CALM);
    TEST_ASSERT_EQUAL_INT(1, (int)NOISE_NORMAL);
    TEST_ASSERT_EQUAL_INT(2, (int)NOISE_MODERATE);
    TEST_ASSERT_EQUAL_INT(3, (int)NOISE_VIGILANCE);
    TEST_ASSERT_EQUAL_INT(4, (int)NOISE_LIMIT);
    TEST_ASSERT_EQUAL_INT(5, (int)NOISE_ALERT);
}

/**
 * @brief Tests visualizer animation mode string parser.
 *
 * Verifies that VisualizerEngine cleanly parses all mode identifiers ("waveform", "radial",
 * "neon_fire", "spectrum") while maintaining inactive initial state.
 */
void test_visualizer_mode_parsing(void) {
    VisualizerEngine engine;
    engine.setMode("waveform");
    engine.setMode("radial");
    engine.setMode("neon_fire");
    engine.setMode("spectrum");
    TEST_ASSERT_FALSE(engine.isActive());
}

/**
 * @brief Tests MessageEngine lifecycle, thread-safe queueing, and continuous delta-time scrolling math.
 */
void test_message_scroll_math(void) {
    MessageEngineDescriptorHandler descHandler;
    EngineDescriptor desc = descHandler.getDescriptor();
    TEST_ASSERT_EQUAL_STRING("message", desc.metadata.id);

    MessageEngine engine;
    TEST_ASSERT_FALSE(engine.isActive());
    TEST_ASSERT_FALSE(engine.allowsOverlay());
    TEST_ASSERT_TRUE(engine.needsClear());

    MessageConfig cfg{ "HELLO ARCADE", 0xFFFF, 1, "rtl", 50, 10, 0 };
    engine.queueMessage(cfg);
    engine.deactivate();
    TEST_ASSERT_FALSE(engine.isActive());

    // 1. Frame clamp: dt > 100ms clamped to 16.6ms
    float dtLarge = 5000.0f;
    float dtClamped = (dtLarge > 100.0f) ? 16.6f : dtLarge;
    TEST_ASSERT_EQUAL_FLOAT(16.6f, dtClamped);

    // 2. Continuous sub-pixel advance:
    // With speed = 50ms/px and dt = 16.6ms, movePx = 16.6 / 50.0 = 0.332px
    int speedMs = 50;
    float movePx = dtClamped / (float)speedMs;
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 0.332f, movePx);

    // 3. Sub-pixel accumulator progression across 3 frames (~50ms)
    float cursorX = 128.0f;
    cursorX -= movePx;
    TEST_ASSERT_EQUAL_INT(128, (int16_t)roundf(cursorX)); // Frame 1: 127.668 -> rounds to 128
    cursorX -= movePx;
    TEST_ASSERT_EQUAL_INT(127, (int16_t)roundf(cursorX)); // Frame 2: 127.336 -> rounds to 127
    cursorX -= movePx;
    TEST_ASSERT_EQUAL_INT(127, (int16_t)roundf(cursorX)); // Frame 3: 127.004 -> rounds to 127
}


/**
 * @brief The glow outline: how a face's colour turns into an outline and a centre.
 */
void test_glow_modes_resolve_colours() {
    // With no configuration there is no glow, and the centre keeps the colour it was given.
    uint16_t halo = 0, core = 0;
    ClockFaceFont::Glow off = ClockFaceFont::resolveGlow(nullptr);
    TEST_ASSERT_EQUAL_UINT8(0, off.mode);
    TEST_ASSERT_FALSE(ClockFaceFont::glowFor(off, 0xF800, halo, core));
    TEST_ASSERT_EQUAL_UINT16(0xF800, core);

    // Neon takes the outline from the text colour; a custom glow takes the one it was given, and
    // falls back to the text colour when the setting is not a colour at all.
    ClockFaceFont::Glow neon; neon.mode = 1;
    TEST_ASSERT_TRUE(ClockFaceFont::glowFor(neon, 0x001F, halo, core));
    TEST_ASSERT_EQUAL_UINT16(0x001F, halo);

    ClockFaceFont::Glow custom; custom.mode = 2; custom.color = 0xF81F; custom.hasColor = true;
    TEST_ASSERT_TRUE(ClockFaceFont::glowFor(custom, 0x001F, halo, core));
    TEST_ASSERT_EQUAL_UINT16(0xF81F, halo);
    TEST_ASSERT_EQUAL_UINT16(0x001F, core);      // the centre keeps its own colour

    ClockFaceFont::Glow unset; unset.mode = 2;   // "custom" with nothing parseable behind it
    TEST_ASSERT_TRUE(ClockFaceFont::glowFor(unset, 0x07E0, halo, core));
    TEST_ASSERT_EQUAL_UINT16(0x07E0, halo);

    // A hex colour parses; anything else falls back rather than drawing black.
    TEST_ASSERT_EQUAL_UINT16(0xF800, ClockFaceFont::parseHex("#FF0000", 0x0000));
    TEST_ASSERT_EQUAL_UINT16(0x07E0, ClockFaceFont::parseHex("not a colour", 0x07E0));
    TEST_ASSERT_EQUAL_UINT16(0x001F, ClockFaceFont::parseHex("0000FF", 0x0000));
    // Black is a real colour, and is told apart from "not set".
    uint16_t parsed = 0xFFFF;
    TEST_ASSERT_TRUE(ClockFaceFont::tryParseHex("#000000", parsed));
    TEST_ASSERT_EQUAL_UINT16(0x0000, parsed);
    TEST_ASSERT_FALSE(ClockFaceFont::tryParseHex("nope", parsed));

    // Neon pales the centre towards white, which is what makes the outline read as an outline.
    uint16_t paled = ClockFaceFont::paled(0x001F);
    TEST_ASSERT_TRUE(((paled >> 11) & 0x1F) > 0);
    TEST_ASSERT_TRUE(((paled >> 5) & 0x3F) > 0);

    // Dimming lowers every channel and keeps the hue.
    uint16_t dim = ClockFaceFont::dim(0xFFFF, 1, 3);
    TEST_ASSERT_TRUE(((dim >> 11) & 0x1F) < 0x1F);
    TEST_ASSERT_TRUE((dim & 0x1F) < 0x1F);
}

/**
 * @brief The time spoken as words, in each language the sign offers.
 */
void test_spoken_time_reads_as_words() {
    char h[24], m[32];
    I18n::getSpokenTime(12, 0, Lang::EN, h, sizeof(h), m, sizeof(m));
    TEST_ASSERT_EQUAL_STRING("noon", h);
    TEST_ASSERT_EQUAL_STRING("", m);          // the hour stands alone, nothing left over
    I18n::getSpokenTime(0, 0, Lang::EN, h, sizeof(h), m, sizeof(m));
    TEST_ASSERT_EQUAL_STRING("midnight", h);
    I18n::getSpokenTime(8, 5, Lang::EN, h, sizeof(h), m, sizeof(m));
    TEST_ASSERT_EQUAL_STRING("eight", h);
    TEST_ASSERT_EQUAL_STRING("oh five", m);
    I18n::getSpokenTime(8, 30, Lang::EN, h, sizeof(h), m, sizeof(m));
    TEST_ASSERT_EQUAL_STRING("a half", m);
    I18n::getSpokenTime(8, 42, Lang::EN, h, sizeof(h), m, sizeof(m));
    TEST_ASSERT_EQUAL_STRING("forty two", m);

    // Each language gets its own phrasing rather than a word-for-word translation.
    I18n::getSpokenTime(8, 30, Lang::FR, h, sizeof(h), m, sizeof(m));
    TEST_ASSERT_EQUAL_STRING("huit", h);
    TEST_ASSERT_EQUAL_STRING("heures et demie", m);
    I18n::getSpokenTime(8, 21, Lang::FR, h, sizeof(h), m, sizeof(m));
    TEST_ASSERT_EQUAL_STRING("heures vingt et une", m);
    I18n::getSpokenTime(8, 30, Lang::ES, h, sizeof(h), m, sizeof(m));
    TEST_ASSERT_EQUAL_STRING("ocho", h);
    TEST_ASSERT_EQUAL_STRING("y media", m);
    I18n::getSpokenTime(8, 21, Lang::ES, h, sizeof(h), m, sizeof(m));
    TEST_ASSERT_EQUAL_STRING("y veintiuno", m);
}

/**
 * @brief Date order: month before day in English, day before month elsewhere.
 */
void test_date_line_follows_the_language() {
    char line[32];
    I18n::getDateLine(1, 8, 28, Lang::EN, line, sizeof(line));
    TEST_ASSERT_EQUAL_STRING("MON SEP 28", line);
    I18n::getDateLine(1, 8, 28, Lang::FR, line, sizeof(line));
    TEST_ASSERT_EQUAL_STRING("LUN 28 SEPT", line);
    I18n::getDateLine(1, 8, 28, Lang::ES, line, sizeof(line));
    TEST_ASSERT_EQUAL_STRING("LUN 28 SEP", line);
}

/**
 * @brief Verifies zero-copy MQTT `ready` payload parsing for MediaShowcaseBase (including
 *        both `stream_duration_s`/`total_stream_duration_s` and `stream_fps`/`fps` keys).
 */
void test_media_showcase_ready_payload_zero_copy_parsing() {
    RetroGamesEngine engine;
    String itemId;
    bool hasStream = false;
    uint32_t streamMaxMs = 0;
    bool explicitWarming = false;

    char warmingBuf[] = "{\"ready\":false,\"item_id\":\"pkg_1\"}";
    TEST_ASSERT_FALSE(engine.parseReadyPayloadInPlace(warmingBuf, false, itemId, hasStream, streamMaxMs, explicitWarming));
    TEST_ASSERT_TRUE(explicitWarming);

    char readyBuf[] =
        "{\"ready\":true,\"item_id\":\"pkg_games_defender\",\"domain\":\"games\",\"card\":{"
        "\"id\":\"pkg_games_defender\",\"item_id\":\"pkg_games_defender\",\"title\":\"Defender of the Crown\","
        "\"year\":\"1987\",\"badge\":\"NES\",\"badge_color\":\"#D32F2F\",\"subtitle\":\"Plateforme\","
        "\"rating\":\"8.7\",\"synopsis\":\"Defender of the Crown synopsis...\","
        "\"has_panoramas\":true,\"has_video\":true,\"stream_fps\":28,\"fps\":28,"
        "\"stream_duration_s\":20.5,\"total_stream_duration_s\":20.5}}";
    TEST_ASSERT_TRUE(engine.parseReadyPayloadInPlace(readyBuf, false, itemId, hasStream, streamMaxMs, explicitWarming));
    TEST_ASSERT_FALSE(explicitWarming);
    TEST_ASSERT_EQUAL_STRING("pkg_games_defender", itemId.c_str());
    TEST_ASSERT_TRUE(hasStream);
    TEST_ASSERT_EQUAL_UINT32(24500U, streamMaxMs);

    // Verify dynamic turn safety ceiling adapts live to `setRotationBudget` without requiring `onConfigChanged`
    MovieShowcaseEngine movieEng;
    // Default ConfigSlot: videoMaxDurationS=8, jacketPanorama=true (4s) -> 12s stream + 45s card/net = 57s per item
    TEST_ASSERT_EQUAL_UINT32(57000U, movieEng.computeTurnSafetyTimeoutMs());
    movieEng.setRotationBudget(3);
    TEST_ASSERT_EQUAL_UINT32(171000U, movieEng.computeTurnSafetyTimeoutMs());

    // Verify 256x64 geometry update (`48x64` Base64 poster = 8,192 B64 chars + synopsis > 9 KB)
    // and live TATE rotation (`256x64` -> `64x256`) bumping `configGeneration()` cleanly.
    uint32_t gen0 = movieEng.configGeneration();
    DisplayGeometry geom256;
    geom256.width = 256;
    geom256.height = 64;
    movieEng.onDisplayGeometryChanged(geom256);
    TEST_ASSERT_EQUAL_UINT16(256, movieEng.currentWidth());
    TEST_ASSERT_EQUAL_UINT16(64, movieEng.currentHeight());
    TEST_ASSERT_TRUE(movieEng.configGeneration() > gen0);

    String b64Poster48x64;
    b64Poster48x64.reserve(8192);
    for (int i = 0; i < 8192; ++i) {
        b64Poster48x64 += 'A'; // 8,192 'A's decode to 6,144 zero bytes (`48 * 64 * 2`)
    }
    String xlReadyJson =
        String("{\"ready\":true,\"item_id\":\"pkg_movie_256x64\",\"domain\":\"movies\",\"card\":{") +
        "\"id\":\"pkg_movie_256x64\",\"item_id\":\"pkg_movie_256x64\",\"domain\":\"movies\"," +
        "\"title\":\"Blade Runner 2049\",\"year\":\"2017\",\"badge\":\"SCIFI\",\"badge_color\":\"#00E5FF\"," +
        "\"subtitle\":\"Neo-Noir\",\"rating\":\"8.0\",\"lang\":\"fr\",\"source_used\":\"tmdb\"," +
        "\"poster_width\":48,\"poster_height\":64,\"poster_rgb565_b64\":\"" + b64Poster48x64 + "\"," +
        "\"poster_rgb565_url\":\"/api/v1/movies/poster/pkg_movie_256x64.rgb565\",\"text_scroll_speed\":\"4\"," +
        "\"synopsis\":\"Long 256x64 synopsis payload verifying zero-copy parsing above 9 KB across all 31 Showcase keys...\"," +
        "\"has_panoramas\":true,\"has_video\":true,\"stream_frames\":336,\"stream_fps\":24,\"fps\":24," +
        "\"stream_duration_s\":14.0,\"total_stream_duration_s\":14.0," +
        "\"assets\":{\"poster_rgb565\":\"/api/v1/movies/poster/pkg_movie_256x64.rgb565\"," +
        "\"unified_stream_rgb565\":\"/api/v1/movies/stream/pkg_movie_256x64.rgb565.stream\"}}}";
    std::vector<char> mutableXl(xlReadyJson.begin(), xlReadyJson.end());
    mutableXl.push_back('\0');
    TEST_ASSERT_TRUE(movieEng.parseReadyPayloadInPlace(mutableXl.data(), false, itemId, hasStream, streamMaxMs, explicitWarming));
    TEST_ASSERT_EQUAL_STRING("pkg_movie_256x64", itemId.c_str());
    TEST_ASSERT_TRUE(hasStream);

    uint32_t gen1 = movieEng.configGeneration();
    DisplayGeometry geomTate;
    geomTate.width = 64;
    geomTate.height = 256;
    movieEng.onDisplayGeometryChanged(geomTate);
    TEST_ASSERT_EQUAL_UINT16(64, movieEng.currentWidth());
    TEST_ASSERT_EQUAL_UINT16(256, movieEng.currentHeight());
    TEST_ASSERT_TRUE(movieEng.configGeneration() > gen1);
}

/**
 * @brief Verifies that `MediaShowcaseBase::copySafeCsvTokens` preserves the full 66-system
 *        canonical catalog (`MAX_SYSTEMS_FILTER_LEN = 560`) and truncates cleanly at the
 *        last complete comma boundary if an input string exceeds buffer capacity.
 */
void test_media_showcase_66_systems_csv_filter_capacity_and_boundary_truncation() {
    const char* all66SystemsCsv =
        "mame,cps1,cps2,cps3,neogeo,neogeocd,pgm,atomiswave,naomi,model2,model3,hikaru,"
        "zn1,system16,system32,stv,namcos11,namcos22,irem_m72,irem_m92,konami_arcade,"
        "taito_f3,cave,toaplan,daphne,atari2600,atari5200,atari7800,intellivision,"
        "colecovision,vectrex,nes,fds,snes,n64,gamecube,mastersystem,megadrive,sega32x,"
        "segacd,sg1000,saturn,dreamcast,pcengine,pcenginecd,supergrafx,pcfx,psx,ps2,3do,"
        "jaguar,cdi,gb,gbc,gba,nds,virtualboy,gamegear,lynx,ngp,ngpc,wonderswan,"
        "wonderswancolor,c64,amiga,msx,msx2,amstradcpc,zxspectrum,atari800,atarist,x68000,dos,scummvm";
    char sysBuf[MediaShowcaseBase::MAX_SYSTEMS_FILTER_LEN];
    MediaShowcaseBase::copySafeCsvTokens(sysBuf, sizeof(sysBuf), all66SystemsCsv);
    TEST_ASSERT_EQUAL_STRING(all66SystemsCsv, sysBuf);

    // Overflow truncation must cut cleanly at the last complete comma, never mid-token
    char smallBuf[24];
    MediaShowcaseBase::copySafeCsvTokens(smallBuf, sizeof(smallBuf), "cps2,neogeo,megadrive,wonderswancolor");
    TEST_ASSERT_EQUAL_STRING("cps2,neogeo,megadrive", smallBuf);
}

/**
 * @brief Regression test verifying that `MediaShowcaseBase::render()` and `InGameShowcaseEngine::render()`
 *        preserve `hasNewFrame() == true` immediately after drawing a dirty frame (preventing the
 *        `FPS: 0.0 present` black-screen bug) and return `false` on clean intermediate ticks.
 */
void test_media_showcase_render_new_frame_handoff_and_waiting_pixel_art() {
    class TestSurface128x32 : public IDrawingSurface {
    public:
        uint16_t pixels[128 * 32];
        bool dirty = false;
        TestSurface128x32() : IDrawingSurface(128, 32, 128, 32) {
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
        CanvasView acquireCanvas() override { return CanvasView{}; }
        void releaseCanvas() override {}
        bool hasCanvas() const override { return true; }
        CanvasStorage canvasStorage() const override { return CanvasStorage::SRAM; }
        PresentationStrategy presentationStrategy() const override { return PresentationStrategy::CANVAS_BURST_SINGLE; }
        size_t memoryUsageBytes() const override { return sizeof(pixels); }
        size_t countNonBlackPixels() const {
            size_t n = 0;
            for (size_t i = 0; i < 128 * 32; ++i) {
                if (pixels[i] != 0) ++n;
            }
            return n;
        }
    };

    class TestEngineContext : public EngineContext {
    public:
        IDrawingSurface* surf = nullptr;
        explicit TestEngineContext(IDrawingSurface* s) : surf(s) {}
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

    TestSurface128x32 surf;
    TestEngineContext ctx(&surf);

    RetroGamesEngine gamesEng;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EngineError::OK), static_cast<int>(gamesEng.initialize(&ctx, nullptr)));
    TEST_ASSERT_EQUAL_STRING("esp32_ut0001_games_128x32", gamesEng.sessionId());

    gamesEng.activate();
    gamesEng.render(&ctx);
    // CRITICAL: hasNewFrame() is queried AFTER render() by RotationManager / DisplayRuntime!
    TEST_ASSERT_TRUE(gamesEng.hasNewFrame());
    TEST_ASSERT_TRUE(surf.countNonBlackPixels() > 20U);

    // Immediate second render without dirty state change must NOT trigger redundant HUB75 flip
    gamesEng.render(&ctx);
    TEST_ASSERT_FALSE(gamesEng.hasNewFrame());
    gamesEng.deactivate();

    // Verify InGameShowcaseEngine follows the identical post-render hasNewFrame() contract
    InGameShowcaseEngine inGameEng;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EngineError::OK), static_cast<int>(inGameEng.initialize(&ctx, nullptr)));
    inGameEng.activate();
    static const uint16_t dummyRow[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
    inGameEng.publishFrame(dummyRow, 2, 2);
    inGameEng.update(&ctx);
    inGameEng.render(&ctx);
    TEST_ASSERT_TRUE(inGameEng.hasNewFrame());
    inGameEng.render(&ctx);
    TEST_ASSERT_FALSE(inGameEng.hasNewFrame());
    inGameEng.deactivate();
}

/**
 * @brief Regression test verifying:
 * 1. `MediaShowcaseBase::countUtf8Glyphs` accurately counts NFC/NFD accented strings and `œ`/`Œ` ligatures.
 * 2. 128x32 Card rendering strictly clips scrolling accented Title and Synopsis inside `[35, 126)`,
 *    writing ZERO text pixels behind the `32x32` jacket poster (`x = 0..32`), gutter (`x = 34`), or right margin (`x = 126..127`).
 * 3. Plain-HTTP `SecureHttpSession` (`setHttps(false)`, port 8890) bypasses `NetworkBudget::canStartTlsSession()`
 *    and never fails with `TransportError::TlsDenied` or increments `getTlsDeniedCount()`.
 */
void test_media_showcase_utf8_accents_poster_clipping_and_plain_http_no_tls() {
    // 1. UTF-8 glyph counting (NFC + NFD + ligatures)
    TEST_ASSERT_EQUAL_UINT32(23U, static_cast<uint32_t>(MediaShowcaseBase::countUtf8Glyphs("Épisode : L'Épée à Noël")));
    // NFD "E" + U+0301 combining acute + "pée" -> 4 rendered glyphs (É, p, é, e)
    TEST_ASSERT_EQUAL_UINT32(4U, static_cast<uint32_t>(MediaShowcaseBase::countUtf8Glyphs("E\xCC\x81p\xC3\xA9""e")));
    // Ligatures œ (o+e = 2 columns) and Œ (O+E = 2 columns)
    TEST_ASSERT_EQUAL_UINT32(5U, static_cast<uint32_t>(MediaShowcaseBase::countUtf8Glyphs("cœur")));
    TEST_ASSERT_EQUAL_UINT32(6U, static_cast<uint32_t>(MediaShowcaseBase::countUtf8Glyphs("Œuvre")));

    // 2. Strict horizontal clipping on 128x32 Card with all-black 32x32 poster (`2,048` zero bytes = 2,732 Base64 'A's)
    class ClipSurface128x32 : public IDrawingSurface {
    public:
        uint16_t pixels[128 * 32];
        bool dirty = false;
        ClipSurface128x32() : IDrawingSurface(128, 32, 128, 32) {
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
        CanvasView acquireCanvas() override { return CanvasView{}; }
        void releaseCanvas() override {}
        bool hasCanvas() const override { return true; }
        CanvasStorage canvasStorage() const override { return CanvasStorage::SRAM; }
        PresentationStrategy presentationStrategy() const override { return PresentationStrategy::CANVAS_BURST_SINGLE; }
        size_t memoryUsageBytes() const override { return sizeof(pixels); }
    };

    class ClipEngineContext : public EngineContext {
    public:
        IDrawingSurface* surf = nullptr;
        explicit ClipEngineContext(IDrawingSurface* s) : surf(s) {}
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

    ClipSurface128x32 surf;
    ClipEngineContext ctx(&surf);
    RetroGamesEngine gamesEng;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EngineError::OK), static_cast<int>(gamesEng.initialize(&ctx, nullptr)));
    gamesEng.activate();

    // Waiting screen (!hasData) renders centered steady status text on bottom band (y = 26..30)
    surf.clear(0);
    gamesEng.render(&ctx);
    size_t bottomStatusPixels = 0;
    for (int y = 26; y <= 30; ++y) {
        for (int x = 2; x < 126; ++x) {
            if (surf.pixels[y * 128 + x] != 0) ++bottomStatusPixels;
        }
    }
    TEST_ASSERT_TRUE(bottomStatusPixels > 15U);

    // Feed an accented Card payload with an all-zero 32x32 RGB565 poster
    String zeroPosterB64;
    zeroPosterB64.reserve(2732);
    for (int i = 0; i < 2732; ++i) {
        zeroPosterB64 += 'A';
    }
    String cardJson =
        String("{\"ready\":true,\"item_id\":\"pkg_clip_fr\",\"domain\":\"games\",\"card\":{") +
        "\"id\":\"pkg_clip_fr\",\"title\":\"L'Épée Légendaire de Noël — Édition Cœur d'Émeraude\"," +
        "\"year\":\"1994\",\"badge\":\"SNES\",\"badge_color\":\"#00E5FF\",\"rating\":\"9.5\"," +
        "\"poster_width\":32,\"poster_height\":32,\"poster_rgb565_b64\":\"" + zeroPosterB64 + "\"," +
        "\"synopsis\":\"À l'aube d'un été brûlant, le héros traverse château, forêt et île mystérieuse…\"," +
        "\"has_panoramas\":true,\"has_video\":true,\"stream_fps\":20,\"stream_duration_s\":10.0}}";
    std::vector<char> mutableCard(cardJson.begin(), cardJson.end());
    mutableCard.push_back('\0');
    char parsedId[64] = "";
    bool hasStream = false;
    uint32_t streamMaxMs = 0;
    bool explicitWarming = false;
    TEST_ASSERT_TRUE(gamesEng.parseReadyPayloadInPlaceBuf(
        mutableCard.data(), false, parsedId, sizeof(parsedId), hasStream, streamMaxMs, explicitWarming
    ));
    TEST_ASSERT_EQUAL_STRING("pkg_clip_fr", parsedId);

    // Advance scroll time so both Title and Synopsis are actively mid-scroll leftward toward x = 35
    delay(1800);
    gamesEng.update(&ctx);
    surf.clear(0);
    gamesEng.render(&ctx);

    size_t posterLeakPixels = 0;
    size_t rightMarginLeakPixels = 0;
    size_t clippedTextRegionPixels = 0;
    for (int y = 0; y < 32; ++y) {
        // Columns 0..31 (32x32 poster), 32 (left gutter), and 34 (right gutter around x=33 divider) must stay 100% black!
        for (int x = 0; x <= 34; ++x) {
            if (x == 33) continue; // Intentional 1px subtle vertical divider
            if (surf.pixels[y * 128 + x] != 0) ++posterLeakPixels;
        }
        for (int x = 35; x < 126; ++x) {
            if (surf.pixels[y * 128 + x] != 0) ++clippedTextRegionPixels;
        }
        for (int x = 126; x < 128; ++x) {
            if (surf.pixels[y * 128 + x] != 0) ++rightMarginLeakPixels;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(0U, static_cast<uint32_t>(posterLeakPixels));
    TEST_ASSERT_EQUAL_UINT32(0U, static_cast<uint32_t>(rightMarginLeakPixels));
    TEST_ASSERT_TRUE(clippedTextRegionPixels > 60U);
    gamesEng.deactivate();

    // 3. Plain-HTTP SecureHttpSession (`setHttps(false)`) must NEVER invoke `canStartTlsSession()`
    NetworkBudget::markBulkTransferActive(5000);
    uint32_t deniedBefore = NetworkBudget::getTlsDeniedCount().load(std::memory_order_relaxed);
    {
        net::SecureHttpOptions httpsOpts;
        httpsOpts.handshakeTimeoutSec = 1;
        httpsOpts.requestTimeoutMs = 200;
        net::SecureHttpSession httpsSess("127.0.0.1", 443, httpsOpts);
        TEST_ASSERT_FALSE(httpsSess.ensureConnected());
        TEST_ASSERT_EQUAL_INT(static_cast<int>(net::TransportError::TlsDenied), static_cast<int>(httpsSess.lastError()));
        uint32_t deniedAfterHttps = NetworkBudget::getTlsDeniedCount().load(std::memory_order_relaxed);
        TEST_ASSERT_TRUE(deniedAfterHttps > deniedBefore);

        net::SecureHttpOptions plainOpts;
        plainOpts.handshakeTimeoutSec = 1;
        plainOpts.requestTimeoutMs = 200;
        plainOpts.ownerId = net::OWNER_SHOWCASE;
        net::SecureHttpSession plainSess("127.0.0.1", 8890, plainOpts);
        plainSess.setHttps(false);
        plainSess.ensureConnected();
        TEST_ASSERT_NOT_EQUAL(static_cast<int>(net::TransportError::TlsDenied), static_cast<int>(plainSess.lastError()));
        TEST_ASSERT_EQUAL_UINT32(deniedAfterHttps, NetworkBudget::getTlsDeniedCount().load(std::memory_order_relaxed));
    }
    NetworkBudget::clearBulkTransfer();
}

void test_media_showcase_esp32dev_fps_cap_vs_psram_and_offline_hysteresis() {
    // Both classic ESP32 (`hasPsram = false`) and ESP32-S3 (`hasPsram = true`) honor the user's
    // WebUI `video_fps` override across the full Showcase protocol range `8..60` FPS over paced TCP.
    TEST_ASSERT_EQUAL_UINT8(8U, MediaShowcaseBase::resolveEffectiveVideoFps(4U, false));
    TEST_ASSERT_EQUAL_UINT8(12U, MediaShowcaseBase::resolveEffectiveVideoFps(12U, false));
    TEST_ASSERT_EQUAL_UINT8(15U, MediaShowcaseBase::resolveEffectiveVideoFps(15U, false));
    TEST_ASSERT_EQUAL_UINT8(20U, MediaShowcaseBase::resolveEffectiveVideoFps(20U, false));
    TEST_ASSERT_EQUAL_UINT8(25U, MediaShowcaseBase::resolveEffectiveVideoFps(25U, false));
    TEST_ASSERT_EQUAL_UINT8(30U, MediaShowcaseBase::resolveEffectiveVideoFps(30U, false));
    TEST_ASSERT_EQUAL_UINT8(60U, MediaShowcaseBase::resolveEffectiveVideoFps(60U, false));
    TEST_ASSERT_EQUAL_UINT8(60U, MediaShowcaseBase::resolveEffectiveVideoFps(90U, false));

    TEST_ASSERT_EQUAL_UINT8(8U, MediaShowcaseBase::resolveEffectiveVideoFps(5U, true));
    TEST_ASSERT_EQUAL_UINT8(20U, MediaShowcaseBase::resolveEffectiveVideoFps(20U, true));
    TEST_ASSERT_EQUAL_UINT8(25U, MediaShowcaseBase::resolveEffectiveVideoFps(25U, true));
    TEST_ASSERT_EQUAL_UINT8(30U, MediaShowcaseBase::resolveEffectiveVideoFps(30U, true));
    TEST_ASSERT_EQUAL_UINT8(60U, MediaShowcaseBase::resolveEffectiveVideoFps(60U, true));
    TEST_ASSERT_EQUAL_UINT8(60U, MediaShowcaseBase::resolveEffectiveVideoFps(90U, true));

    // 3-strike network offline hysteresis prevents transient single-packet drops from flashing offline.
    TEST_ASSERT_FALSE(MediaShowcaseBase::shouldReportNetworkOffline(0U));
    TEST_ASSERT_FALSE(MediaShowcaseBase::shouldReportNetworkOffline(1U));
    TEST_ASSERT_FALSE(MediaShowcaseBase::shouldReportNetworkOffline(2U));
    TEST_ASSERT_TRUE(MediaShowcaseBase::shouldReportNetworkOffline(3U));
    TEST_ASSERT_TRUE(MediaShowcaseBase::shouldReportNetworkOffline(5U));
}

void test_media_showcase_live_stream_handoff_and_online_status_no_false_offline() {
    class StreamSurface128x32 : public IDrawingSurface {
    public:
        uint16_t pixels[128 * 32];
        bool dirty = false;
        StreamSurface128x32() : IDrawingSurface(128, 32, 128, 32) {
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
        CanvasView acquireCanvas() override { return CanvasView{}; }
        void releaseCanvas() override {}
        bool hasCanvas() const override { return true; }
        CanvasStorage canvasStorage() const override { return CanvasStorage::SRAM; }
        PresentationStrategy presentationStrategy() const override { return PresentationStrategy::CANVAS_BURST_SINGLE; }
        size_t memoryUsageBytes() const override { return sizeof(pixels); }
    };

    class StreamEngineContext : public EngineContext {
    public:
        IDrawingSurface* surf = nullptr;
        explicit StreamEngineContext(IDrawingSurface* s) : surf(s) {}
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

    class TestableShowcaseEngine : public RetroGamesEngine {
    public:
        void setStatus(uint8_t st) {
            m_statusState.store(st, std::memory_order_release);
            m_dirty = true;
        }
        void renderWaitingAt(IDrawingSurface* s, uint32_t elapsedMs) {
            s->clear(0);
            drawWaitingPixelArt(s, 128, 32, elapsedMs);
        }
        void simulateFramePublish(uint16_t color, uint32_t seq) {
            if (!m_frame565) {
                TEST_ASSERT_TRUE(acquireStepCFrameBuffer());
            }
            TEST_ASSERT_NOT_NULL(m_frame565);
            for (size_t i = 0; i < m_framePixels; ++i) {
                m_frame565[i] = color;
            }
            m_publishedFramePtr.store(m_frame565, std::memory_order_release);
            m_seq.fetch_add(1, std::memory_order_acq_rel);
            m_liveCard.hasVideoFrame = true;
            m_seq.fetch_add(1, std::memory_order_release);
            m_phase.store(static_cast<uint8_t>(Phase::LiveStream), std::memory_order_release);
            m_frameSeq.store(seq, std::memory_order_release);
        }
        void setFrameSeqRaw(uint32_t seq) {
            m_frameSeq.store(seq, std::memory_order_release);
        }
        void writeFirstPixelRaw(uint16_t color) {
            if (m_frame565) m_frame565[0] = color;
        }
        void simulateCardWithPoster(uint16_t posterColor) {
            TEST_ASSERT_NOT_NULL(m_poster565);
            for (size_t i = 0; i < (32U * 32U); ++i) {
                m_poster565[i] = posterColor;
            }
            m_seq.fetch_add(1U, std::memory_order_acq_rel);
            snprintf(m_liveCard.title, sizeof(m_liveCard.title), "MATRIX");
            snprintf(m_liveCard.badge, sizeof(m_liveCard.badge), "MOVIE");
            m_liveCard.posterW = 32U;
            m_liveCard.posterH = 32U;
            m_liveCard.hasData = true;
            m_liveCard.hasPoster = true;
            m_liveCard.hasVideoFrame = false;
            m_seq.fetch_add(1U, std::memory_order_release);
            m_statusState.store(0U, std::memory_order_release);
            m_phase.store(static_cast<uint8_t>(Phase::Card), std::memory_order_release);
        }
        void invokeBlankCardBeforeStream() { blankCardBeforeStream(); }
        bool liveHasPoster() const { return m_liveCard.hasPoster; }
        uint16_t firstPosterPixel() const { return m_poster565 ? m_poster565[0] : 0U; }
        bool isEngineDirty() const { return m_dirty; }
        uint32_t consumedSeq() const { return m_consumedFrameSeq.load(std::memory_order_acquire); }
        uint32_t presentedCount() const { return m_presentedStreamFrames.load(std::memory_order_acquire); }
        uint32_t renderBlitSeqVal() const { return m_renderBlitSeq.load(std::memory_order_acquire); }
    };

    StreamSurface128x32 surf0;
    StreamSurface128x32 surf1;
    StreamSurface128x32 surf2;
    StreamEngineContext ctx(&surf0);
    TestableShowcaseEngine eng;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EngineError::OK), static_cast<int>(eng.initialize(&ctx, nullptr)));
    eng.activate();

    // 1. `m_statusState == 0` (`LiveReady`) must render the exact `Warming` label (`st == 1`), NEVER `Offline` (`st == 2`)!
    eng.setStatus(0U);
    eng.renderWaitingAt(&surf0, 500U);
    eng.setStatus(1U);
    eng.renderWaitingAt(&surf1, 500U);
    eng.setStatus(2U);
    eng.renderWaitingAt(&surf2, 500U);

    bool diff0vs1 = false;
    bool diff0vs2 = false;
    for (int y = 25; y < 32; ++y) {
        for (int x = 0; x < 128; ++x) {
            uint16_t p0 = surf0.pixels[y * 128 + x];
            uint16_t p1 = surf1.pixels[y * 128 + x];
            uint16_t p2 = surf2.pixels[y * 128 + x];
            if (p0 != p1) diff0vs1 = true;
            if (p0 != p2) diff0vs2 = true;
        }
    }
    TEST_ASSERT_FALSE(diff0vs1);
    TEST_ASSERT_TRUE(diff0vs2);

    // 1b. Imperceptible Card -> Stream transition: `blankCardBeforeStream()` clears the ENTIRE
    //     Card display to black (`0x0000`) BEFORE `hasPoster` or jacket pixels are cleared!
    surf0.clear(0U);
    eng.simulateCardWithPoster(0xF81FU);
    eng.update(&ctx);
    eng.render(&ctx);
    TEST_ASSERT_TRUE(eng.hasNewFrame());
    bool hadLitCardPixels = false;
    for (size_t i = 0; i < 128U * 32U; ++i) {
        if (surf0.pixels[i] != 0U) {
            hadLitCardPixels = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(hadLitCardPixels);
    eng.invokeBlankCardBeforeStream();
    TEST_ASSERT_TRUE(eng.liveHasPoster());
    TEST_ASSERT_EQUAL_HEX16(0xF81FU, eng.firstPosterPixel());
    eng.update(&ctx);
    eng.render(&ctx);
    TEST_ASSERT_TRUE(eng.hasNewFrame());
    for (size_t i = 0; i < 128U * 32U; ++i) {
        TEST_ASSERT_EQUAL_HEX16(0x0000U, surf0.pixels[i]);
    }

    // 2. Tear-free `m_frameSeq` / `m_consumedFrameSeq` handoff: an `ODD` sequence during `render()`
    //    must skip `BlitSeqGuard` (zero `seq_cst` traffic), clear `m_dirty` so Core 1 does not busy-spin,
    //    and re-arm `m_dirty` in `update()` once Core 0 publishes `EVEN`!
    surf0.clear(0x001F);
    surf0.present();
    eng.simulateFramePublish(0xF800, 2U); // Complete Frame #1 (`EVEN` seq = 2)
    eng.update(&ctx);
    TEST_ASSERT_TRUE(eng.isEngineDirty());

    // Simulate Core 0 starting Frame #2 (`ODD` seq = 3) microseconds before Core 1 `render()` runs
    const uint32_t blitSeqBeforeOdd = eng.renderBlitSeqVal();
    eng.setFrameSeqRaw(3U);
    eng.writeFirstPixelRaw(0x07E0);
    eng.render(&ctx);
    TEST_ASSERT_FALSE(eng.hasNewFrame());
    TEST_ASSERT_FALSE(eng.isEngineDirty()); // Cleared so Core 1 sleeps cleanly while Core 0 decodes
    TEST_ASSERT_EQUAL_UINT32(blitSeqBeforeOdd, eng.renderBlitSeqVal()); // Zero `seq_cst` atomic increments!
    TEST_ASSERT_EQUAL_HEX16(0x001F, surf0.pixels[0]); // Previous canvas untouched while ODD!

    // Core 0 finishes Frame #2 (`EVEN` seq = 4) -> `update()` re-arms `m_dirty` and `render()` blits immediately!
    eng.simulateFramePublish(0x07E0, 4U);
    eng.update(&ctx);
    TEST_ASSERT_TRUE(eng.isEngineDirty());
    eng.render(&ctx);
    TEST_ASSERT_TRUE(eng.hasNewFrame());
    TEST_ASSERT_FALSE(eng.isEngineDirty());
    TEST_ASSERT_EQUAL_UINT32(blitSeqBeforeOdd + 2U, eng.renderBlitSeqVal());
    TEST_ASSERT_EQUAL_HEX16(0x07E0, surf0.pixels[0]);
    TEST_ASSERT_EQUAL_HEX16(0x07E0, surf0.pixels[128 * 32 - 1]);
    TEST_ASSERT_EQUAL_UINT32(4U, eng.consumedSeq());
    TEST_ASSERT_EQUAL_UINT32(1U, eng.presentedCount());

    eng.deactivate();
}

void test_normalize_mdns_endpoint_url_and_showcase_discovery_parsing() {
    // 1. Legacy v1.0.0 & single-label LAN hostnames normalize to `.local`
    TEST_ASSERT_EQUAL_STRING(
        "http://dmdmediashowcase.local:8890",
        normalizeMdnsEndpointUrl("http://dmdmediashowcase:8890", true).c_str()
    );
    TEST_ASSERT_EQUAL_STRING(
        "http://dmdmediashowcase.local:8890",
        normalizeMdnsEndpointUrl("http://dmdmediashowcase:8080", true).c_str()
    );
    TEST_ASSERT_EQUAL_STRING(
        "http://mybox.local:8890",
        normalizeMdnsEndpointUrl("http://mybox:8890", false).c_str()
    );
    TEST_ASSERT_EQUAL_STRING(
        "mqtt://mosquitto.local:1883",
        normalizeMdnsEndpointUrl("mqtt://mosquitto:1883", false).c_str()
    );
    // 2. IPv4, localhost, IPv6, and existing `.local` / FQDN stay untouched
    TEST_ASSERT_EQUAL_STRING(
        "http://192.168.1.22:8890",
        normalizeMdnsEndpointUrl("http://192.168.1.22:8890", true).c_str()
    );
    TEST_ASSERT_EQUAL_STRING(
        "http://localhost:8890",
        normalizeMdnsEndpointUrl("http://localhost:8890", false).c_str()
    );
    TEST_ASSERT_EQUAL_STRING(
        "http://[::1]:8890",
        normalizeMdnsEndpointUrl("http://[::1]:8890", false).c_str()
    );
    TEST_ASSERT_EQUAL_STRING(
        "http://dmdmediashowcase.local:8890",
        normalizeMdnsEndpointUrl("http://dmdmediashowcase.local:8890", true).c_str()
    );

    // 3. ShowcaseDiscovery::parseEndpoint normalizes single-label LAN hostnames and classifies them as auto-discoverable
    char scheme[16] = "";
    char host[64] = "";
    uint16_t port = 0;
    TEST_ASSERT_TRUE(net::ShowcaseDiscovery::parseEndpoint(
        "http://dmdmediashowcase:8890", 8890, scheme, sizeof(scheme), host, sizeof(host), port
    ));
    TEST_ASSERT_EQUAL_STRING("http", scheme);
    TEST_ASSERT_EQUAL_STRING("dmdmediashowcase.local", host);
    TEST_ASSERT_EQUAL_UINT16(8890U, port);
    TEST_ASSERT_TRUE(net::ShowcaseDiscovery::isAutoDiscoverableHost("dmdmediashowcase"));
    TEST_ASSERT_TRUE(net::ShowcaseDiscovery::isAutoDiscoverableHost("myshowcase"));
    TEST_ASSERT_FALSE(net::ShowcaseDiscovery::isAutoDiscoverableHost("localhost"));
    TEST_ASSERT_FALSE(net::ShowcaseDiscovery::isAutoDiscoverableHost("192.168.1.22"));
}

void test_all_four_showcase_descriptors_dynamic_bit_depth_and_long_item_id_capacity() {
    // 1. All 4 Showcase engine descriptors must declare `scalesRgb565FrameWithGeometry = true`
    RetroGamesEngineDescriptorHandler retroH;
    MovieShowcaseEngineDescriptorHandler movieH;
    SeriesShowcaseEngineDescriptorHandler seriesH;
    AnimeShowcaseEngineDescriptorHandler animeH;

    EngineDescriptor dRetro = retroH.getDescriptor();
    EngineDescriptor dMovie = movieH.getDescriptor();
    EngineDescriptor dSeries = seriesH.getDescriptor();
    EngineDescriptor dAnime = animeH.getDescriptor();

    TEST_ASSERT_TRUE(dRetro.requirements.scalesRgb565FrameWithGeometry);
    TEST_ASSERT_TRUE(dMovie.requirements.scalesRgb565FrameWithGeometry);
    TEST_ASSERT_TRUE(dSeries.requirements.scalesRgb565FrameWithGeometry);
    TEST_ASSERT_TRUE(dAnime.requirements.scalesRgb565FrameWithGeometry);

    // 1b. All 4 Showcase engines opt into low-memory inactive eviction (~3.4 KB stateless C++ footprint),
    //     unlike stateful/TLS engines which keep `evictOnInactiveLowMemory() == false`.
    MovieShowcaseEngine movieEng;
    SeriesShowcaseEngine seriesEng;
    AnimeShowcaseEngine animeEng;
    TEST_ASSERT_TRUE(movieEng.evictOnInactiveLowMemory());
    TEST_ASSERT_TRUE(seriesEng.evictOnInactiveLowMemory());
    TEST_ASSERT_TRUE(animeEng.evictOnInactiveLowMemory());

    // 2. Long 85-char `item_id` must be preserved losslessly (`MAX_ITEM_ID_LEN = 128`, zero truncation)
    RetroGamesEngine eng;
    TEST_ASSERT_TRUE(eng.evictOnInactiveLowMemory());
    const char* longId = "pkg_games_screenscraper_castlevania_symphony_of_the_night_psx_usa_1997_1234567890abcd";
    char jsonBuf[512];
    snprintf(
        jsonBuf,
        sizeof(jsonBuf),
        "{\"ready\":true,\"item_id\":\"%s\",\"card\":{\"title\":\"Castlevania\",\"year\":\"1997\","
        "\"badge\":\"PSX\",\"synopsis\":\"Classic action RPG.\",\"has_panoramas\":true,\"has_video\":true,\"stream_duration_s\":9.0}}",
        longId
    );
    char parsedItemId[128] = "";
    bool hasStream = false;
    uint32_t streamMaxMs = 0;
    bool warming = false;
    TEST_ASSERT_TRUE(eng.parseReadyPayloadInPlaceBuf(
        jsonBuf, false, parsedItemId, sizeof(parsedItemId), hasStream, streamMaxMs, warming
    ));
    TEST_ASSERT_EQUAL_STRING(longId, parsedItemId);
    TEST_ASSERT_TRUE(hasStream);

    // 3. Server-reported long trailer (`stream_duration_s = 61.0s` -> `streamMaxMs = 65000 ms`)
    //    must override default `8s` `videoMaxDurationS` in `computeTurnSafetyTimeoutMs()` (`>= 110000 ms`)
    //    so `RotationManager` never evicts `movie_showcase` mid-trailer!
    char longMovieJson[384];
    snprintf(
        longMovieJson,
        sizeof(longMovieJson),
        "{\"ready\":true,\"item_id\":\"pkg_movies_long_trailer\",\"card\":{\"title\":\"Interstellar\",\"year\":\"2014\","
        "\"badge\":\"MOVIE\",\"synopsis\":\"Sci-fi epic.\",\"has_panoramas\":true,\"has_video\":true,\"stream_duration_s\":61.0}}"
    );
    uint32_t longStreamMaxMs = 0;
    TEST_ASSERT_TRUE(movieEng.parseReadyPayloadInPlaceBuf(
        longMovieJson, false, parsedItemId, sizeof(parsedItemId), hasStream, longStreamMaxMs, warming
    ));
    TEST_ASSERT_EQUAL_UINT32(65000U, longStreamMaxMs);
    TEST_ASSERT_EQUAL_UINT32(110000U, movieEng.computeTurnSafetyTimeoutMs());
}

void test_http_chunked_decoder_uvicorn_dmds_stream_and_header_detection() {
    // 1. Header detection (case-insensitive Uvicorn HTTP/1.1 vs unchunked Content-Length)
    const char* uvicornChunkedHdr =
        "HTTP/1.1 200 OK\r\n"
        "server: uvicorn\r\n"
        "x-dmd-paced: 1\r\n"
        "transfer-encoding: chunked\r\n"
        "content-type: application/octet-stream";
    const char* unchunkedHdr =
        "HTTP/1.1 200 OK\r\n"
        "server: uvicorn\r\n"
        "x-dmd-paced: 1\r\n"
        "content-length: 16400\r\n"
        "content-type: application/octet-stream";
    TEST_ASSERT_TRUE(MediaShowcaseBase::hasChunkedTransferEncoding(uvicornChunkedHdr));
    TEST_ASSERT_FALSE(MediaShowcaseBase::hasChunkedTransferEncoding(unchunkedHdr));

    // 2. Uvicorn's exact `2010\r\nDMDS...` wire framing (8208 B = 16 B DMDS header + 8192 B Frame 0,
    //    followed by `2000;ext=ok\r\n` + 8192 B Frame 1 + `0\r\n\r\n`)
    std::vector<uint8_t> expectedBody(16U + 8192U * 2U, 0);
    expectedBody[0] = 'D';
    expectedBody[1] = 'M';
    expectedBody[2] = 'D';
    expectedBody[3] = 'S';
    expectedBody[4] = 1;
    expectedBody[5] = 20;
    expectedBody[6] = 128;
    expectedBody[7] = 0;
    expectedBody[8] = 32;
    expectedBody[9] = 0;
    expectedBody[10] = 2;
    expectedBody[11] = 0;
    for (size_t i = 0; i < 8192U * 2U; ++i) {
        expectedBody[16U + i] = static_cast<uint8_t>((i * 37U + 11U) & 0xFFU);
    }

    std::vector<uint8_t> wire;
    auto appendStr = [&](const char* s) {
        size_t n = strlen(s);
        wire.insert(wire.end(), reinterpret_cast<const uint8_t*>(s), reinterpret_cast<const uint8_t*>(s) + n);
    };
    appendStr("2010\r\n");
    wire.insert(wire.end(), expectedBody.begin(), expectedBody.begin() + 8208U);
    appendStr("\r\n2000;ext=ok\r\n");
    wire.insert(wire.end(), expectedBody.begin() + 8208U, expectedBody.end());
    appendStr("\r\n0\r\n\r\n");

    // Feed through HttpChunkedDecoder in 7-byte fragmented network drips into 16 B hdr + two 8192 B frames
    HttpChunkedDecoder dec;
    std::vector<uint8_t> decoded(expectedBody.size(), 0);
    size_t wireOff = 0;
    auto readDecodedExact = [&](uint8_t* dst, size_t wantLen) -> bool {
        size_t got = 0;
        while (got < wantLen && wireOff < wire.size()) {
            size_t avail = std::min<size_t>(7U, wire.size() - wireOff);
            size_t consumed = 0;
            size_t produced = dec.decode(wire.data() + wireOff, avail, consumed, dst + got, wantLen - got);
            wireOff += consumed;
            got += produced;
            if (dec.hasError()) return false;
        }
        return (got == wantLen);
    };

    TEST_ASSERT_TRUE(readDecodedExact(decoded.data(), 16U));
    TEST_ASSERT_EQUAL_MEMORY("DMDS", decoded.data(), 4U);
    TEST_ASSERT_TRUE(readDecodedExact(decoded.data() + 16U, 8192U));
    TEST_ASSERT_TRUE(readDecodedExact(decoded.data() + 16U + 8192U, 8192U));
    TEST_ASSERT_EQUAL_MEMORY(expectedBody.data(), decoded.data(), expectedBody.size());
}

void setup() {
    Serial.begin(115200);
    delay(100);
    UNITY_BEGIN();
    RUN_TEST(test_decibel_status_mapping);
    RUN_TEST(test_visualizer_mode_parsing);
    RUN_TEST(test_message_scroll_math);
    RUN_TEST(test_glow_modes_resolve_colours);
    RUN_TEST(test_spoken_time_reads_as_words);
    RUN_TEST(test_date_line_follows_the_language);
    RUN_TEST(test_media_showcase_ready_payload_zero_copy_parsing);
    RUN_TEST(test_media_showcase_66_systems_csv_filter_capacity_and_boundary_truncation);
    RUN_TEST(test_media_showcase_render_new_frame_handoff_and_waiting_pixel_art);
    RUN_TEST(test_media_showcase_utf8_accents_poster_clipping_and_plain_http_no_tls);
    RUN_TEST(test_media_showcase_esp32dev_fps_cap_vs_psram_and_offline_hysteresis);
    RUN_TEST(test_media_showcase_live_stream_handoff_and_online_status_no_false_offline);
    RUN_TEST(test_normalize_mdns_endpoint_url_and_showcase_discovery_parsing);
    RUN_TEST(test_all_four_showcase_descriptors_dynamic_bit_depth_and_long_item_id_capacity);
    RUN_TEST(test_http_chunked_decoder_uvicorn_dmds_stream_and_header_detection);
    UNITY_END();
}

void loop() {
    delay(100);
}

