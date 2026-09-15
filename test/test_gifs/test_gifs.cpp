#include <Arduino.h>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------------------------
// Test helpers mirroring the GIF library logic in WebServerAPI (see the test_api.cpp convention):
// the production helpers are lambdas local to WebServerAPI::begin(), so the behaviour under test
// is restated here and kept in step with it.
// ---------------------------------------------------------------------------------------------

/**
 * @brief Normalises the `?orientation=` query value. Anything unrecognised stays horizontal, so a
 *        typo can never silently read or write the other library.
 */
static String gifOrientationOf(String raw) {
    raw.trim();
    raw.toLowerCase();
    return raw == "tate" ? String("tate") : String("yoko");
}

/**
 * @brief Maps an orientation onto its SD card root, matching GifEngine's own split.
 */
static String gifRootFor(const String& orientation) {
    return orientation == "tate" ? String("/gifs_tate") : String("/gifs");
}

/**
 * @brief Keeps only a safe basename: letters, digits, `_ - space` (and `.` when an extension is allowed).
 */
static String sanitizeName(const String& in, bool allowExt) {
    String s = in;
    int slash = s.lastIndexOf('/'); if (slash >= 0) s = s.substring(slash + 1);
    slash = s.lastIndexOf('\\'); if (slash >= 0) s = s.substring(slash + 1);
    String out = "";
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        bool ok = isalnum((unsigned char)c) || c == '_' || c == '-' || c == ' ' || (allowExt && c == '.');
        if (ok) out += c;
    }
    out.trim();
    while (out.startsWith(".")) out = out.substring(1);
    if (out.length() > 64) out = out.substring(0, 64);
    return out;
}

static bool hasGifExt(const String& name) {
    String l = name; l.toLowerCase();
    return l.endsWith(".gif") || l.endsWith(".png") || l.endsWith(".raw");
}

static bool badName(const String& raw) {
    return raw.indexOf('/') >= 0 || raw.indexOf('\\') >= 0 || raw.indexOf("..") >= 0;
}

/**
 * @brief Orientation defaults to horizontal; only an explicit "tate" switches library.
 */
void test_gif_orientation_parsing(void) {
    TEST_ASSERT_EQUAL_STRING("yoko", gifOrientationOf("").c_str());
    TEST_ASSERT_EQUAL_STRING("yoko", gifOrientationOf("yoko").c_str());
    TEST_ASSERT_EQUAL_STRING("tate", gifOrientationOf("tate").c_str());
    TEST_ASSERT_EQUAL_STRING("tate", gifOrientationOf("TATE").c_str());
    TEST_ASSERT_EQUAL_STRING("tate", gifOrientationOf("  tate  ").c_str());
    // an unknown value must not fall through to the vertical library
    TEST_ASSERT_EQUAL_STRING("yoko", gifOrientationOf("sideways").c_str());
}

/**
 * @brief Each orientation resolves to its own SD root, as GifEngine expects.
 */
void test_gif_root_for_orientation(void) {
    TEST_ASSERT_EQUAL_STRING("/gifs", gifRootFor("yoko").c_str());
    TEST_ASSERT_EQUAL_STRING("/gifs_tate", gifRootFor("tate").c_str());
    // the horizontal root must never be a prefix match that swallows the vertical one
    TEST_ASSERT_FALSE(gifRootFor("yoko") == gifRootFor("tate"));
}

/**
 * @brief A request path is built from the resolved root, so vertical playlists land under /gifs_tate.
 */
void test_gif_path_uses_resolved_root(void) {
    const String folder = sanitizeName("Arcade", false);
    const String name   = sanitizeName("logo.gif", true);
    TEST_ASSERT_EQUAL_STRING("/gifs_tate/Arcade/logo.gif",
                             (gifRootFor("tate") + "/" + folder + "/" + name).c_str());
    TEST_ASSERT_EQUAL_STRING("/gifs/Arcade/logo.gif",
                             (gifRootFor("yoko") + "/" + folder + "/" + name).c_str());
}

/**
 * @brief Names are reduced to a safe basename; traversal and separators never survive.
 */
void test_gif_sanitize_name(void) {
    TEST_ASSERT_EQUAL_STRING("Logo", sanitizeName("Logo", false).c_str());
    TEST_ASSERT_EQUAL_STRING("My Playlist-2", sanitizeName("My Playlist-2", false).c_str());
    TEST_ASSERT_EQUAL_STRING("passwd", sanitizeName("../../etc/passwd", false).c_str());
    TEST_ASSERT_EQUAL_STRING("name.gif", sanitizeName("C:\\evil\\name.gif", true).c_str());
    TEST_ASSERT_EQUAL_STRING("hidden.gif", sanitizeName(".hidden.gif", true).c_str());
    String long_name; for (int i = 0; i < 100; i++) long_name += "x";
    TEST_ASSERT_EQUAL_INT(64, sanitizeName(long_name, false).length());
}

/**
 * @brief Only .gif/.png/.raw are treated as media, case-insensitively.
 */
void test_gif_media_extension(void) {
    TEST_ASSERT_TRUE(hasGifExt("a.gif"));
    TEST_ASSERT_TRUE(hasGifExt("A.GIF"));
    TEST_ASSERT_TRUE(hasGifExt("b.png"));
    TEST_ASSERT_TRUE(hasGifExt("c.raw"));
    TEST_ASSERT_FALSE(hasGifExt("notes.txt"));
    TEST_ASSERT_FALSE(hasGifExt("index"));
}

/**
 * @brief Raw query values carrying separators or traversal are rejected before any path is built.
 */
void test_gif_bad_name_rejected(void) {
    TEST_ASSERT_TRUE(badName("../x"));
    TEST_ASSERT_TRUE(badName("a/b"));
    TEST_ASSERT_TRUE(badName("a\\b"));
    TEST_ASSERT_TRUE(badName("..hidden"));
    TEST_ASSERT_FALSE(badName("Logo"));
    TEST_ASSERT_FALSE(badName("My Playlist"));
}

void setup() {
    Serial.begin(115200);
    delay(100);
    UNITY_BEGIN();
    RUN_TEST(test_gif_orientation_parsing);
    RUN_TEST(test_gif_root_for_orientation);
    RUN_TEST(test_gif_path_uses_resolved_root);
    RUN_TEST(test_gif_sanitize_name);
    RUN_TEST(test_gif_media_extension);
    RUN_TEST(test_gif_bad_name_rejected);
    UNITY_END();
}

void loop() {
    delay(100);
}
