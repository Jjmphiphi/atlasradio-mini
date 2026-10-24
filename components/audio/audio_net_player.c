#include "audio_net_player.h"
#include "audio_engine.h"
#include "audio_element.h"      // AEL_STATUS_* status codes
#include "metadata.h"
#include "app_state.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"   // xTaskCreatePinnedToCoreWithCaps
#include <string.h>
#include <strings.h>
#include <sys/types.h>

static const char *TAG = "AUDIO_NET";

#define MAX_RETRIES 5
// A URL that never produces audio gets fewer rebuilds — each HTTPS open
// allocates a TLS session, and five of those on the Super Mini exhausts
// internal RAM and reboots instead of reporting ERROR.
#define MAX_OPEN_RETRIES 2
// Teardown of the previous station reports ERROR/FINISHED on the same HTTP
// element after a new play has already started. Ignore those so they don't
// restart (or codec-flip) the station the user just picked.
#define ERROR_GRACE_US        (800 * 1000LL)
#define BUFFERING_GIVEUP_US   (12 * 1000000LL)

// How long a stream has to have been producing audio for its loss to count as a
// fresh incident rather than another failure in the same sequence.
//
// The budget exists for streams that never work — a dead URL, no route, an
// internal-RAM shortage that kills the TLS read a second after it opens. Those
// fail within a couple of seconds of the open, so five of them burn the budget
// and the device reports an error instead of restarting the pipeline forever.
// A station that played for half a minute and then dropped is the other thing
// entirely: that is the network, and internet radio is supposed to keep
// reconnecting to it. So it gets its five attempts back.
#define STABLE_PLAY_US  (30 * 1000000LL)

static TaskHandle_t s_retry_task_handle = NULL;
static int s_retry_count = 0;
static bool s_finite = false;   // current stream is a finite episode, not radio
static char s_url[512];
static uint32_t s_offset_bytes = 0;
static audio_codec_t s_codec = AUDIO_CODEC_MP3;
static char s_learned_url[512];
static audio_codec_t s_learned_codec = AUDIO_CODEC_UNKNOWN;
static bool s_flip_codec_on_retry = false;

static const char *codec_name(audio_codec_t c)
{
    switch (c) {
        case AUDIO_CODEC_MP3:  return "mp3";
        case AUDIO_CODEC_AAC:  return "aac";
        case AUDIO_CODEC_FLAC: return "flac";
        case AUDIO_CODEC_WAV:  return "wav";
        default:               return "?";
    }
}

static audio_codec_t other_stream_codec(audio_codec_t c)
{
    return (c == AUDIO_CODEC_MP3) ? AUDIO_CODEC_AAC : AUDIO_CODEC_MP3;
}

// A retry was scheduled and has not run yet. Separate from the budget because
// the two answer different questions: this one is "is there a restart pending",
// the counter is "how many have we already spent". Folding them together is what
// made the budget unusable — the cancellation path had to zero the counter, and
// it zeroed it on every successful start, so MAX_RETRIES was never reached.
static bool s_retry_pending = false;
// True only while this layer asked the engine to play HTTP. File playback
// shares the decoder, so MUSIC_INFO must not advertise radio PLAYING then.
static bool s_http_session = false;

// esp_timer_get_time() at the last MUSIC_INFO, i.e. when the current attempt
// started producing audio. 0 = this attempt never got that far (a failed open,
// or a restart that has not connected yet).
static int64_t s_playing_since_us = 0;
static int64_t s_error_grace_until_us = 0;
static uint32_t s_play_gen = 0;
static esp_timer_handle_t s_buf_timer = NULL;
// Real stop() is unsafe on the engine event task. The retry worker does it.
static volatile bool s_need_stop = false;

static void on_meta(const char *meta);
static void on_info(void);
static void on_error(int status);
static void retry_task(void *param);
static void disarm_buf_timer(void);
static void arm_buf_timer(void);


/*
static audio_codec_t detect_codec_from_url(const char *url)
Quick URL-based detection — no HTTP request. Returns AUDIO_CODEC_UNKNOWN when the
URL has no readable hint (typical SHOUTcast/Icecast endpoints without an
extension, e.g. /1, /stream); the caller then defaults to MP3.
*/
static audio_codec_t detect_codec_from_url(const char *url)
{
    if (!url) return AUDIO_CODEC_UNKNOWN;

    // .flac must include the dot — bare "flac" in a stream name may mean
    // OGG/FLAC (audio/ogg, e.g. juventus_FLAC), which is not supported,
    // so without the dot we go to probe instead of assuming FLAC.
    if (strcasestr(url, ".flac")) {
        return AUDIO_CODEC_FLAC;
    }

    // HLS (.m3u8): AAC is the common case, so link it up front. If the TS PMT
    // says the segments carry MP3 instead, icy_http_stream reports it in the
    // POST_REQUEST event and the engine relinks to the MP3 decoder.
    if (strcasestr(url, ".m3u8")) {
        return AUDIO_CODEC_AAC;
    }

    if (strcasestr(url, ".aac")  ||
        strcasestr(url, ".aacp") ||
        strcasestr(url, ".adts") ||
        strcasestr(url, "aacp")  ||   // e.g. /stream.aacp or /live_aacp
        strcasestr(url, "/aac")  ||   // e.g. /stream/aac/128
        strcasestr(url, "#aac")  ||
        strcasestr(url, "codec=aac") ||
        strcasestr(url, "playradio.org")) {  // /90HD etc. are AAC, no extension
        return AUDIO_CODEC_AAC;
    }

    if (strcasestr(url, ".mp3") ||
        strcasestr(url, "#mp3") ||
        strcasestr(url, "codec=mp3")) {
        return AUDIO_CODEC_MP3;
    }

    return AUDIO_CODEC_UNKNOWN;
}


// Radio France nginx and Fun Radio's Audiomeans Icecast answer HTTP/1.1 with
// Transfer-Encoding: chunked. The prebuilt icy raw socket does not dechunk, so
// hex chunk sizes land in the MP3 decoder → stutter / ERROR. HTTP/1.0 makes
// those servers send a plain identity body. The reader formats
// "GET %s HTTP/1.1" and calls lwip_send; rewrite only ICY stream requests.
#define ICY_HTTP10_WRAP_MAX 1536

static const char *find_bytes(const char *hay, size_t n, const char *needle, size_t nlen)
{
    if (!hay || !needle || n < nlen) return NULL;
    for (size_t i = 0; i + nlen <= n; i++) {
        if (memcmp(hay + i, needle, nlen) == 0) return hay + i;
    }
    return NULL;
}

ssize_t __real_lwip_send(int s, const void *data, size_t size, int flags);

ssize_t __wrap_lwip_send(int s, const void *data, size_t size, int flags)
{
    const char *req = (const char *)data;
    if (data && size >= 24 && size < ICY_HTTP10_WRAP_MAX &&
        req[0] == 'G' && req[1] == 'E' && req[2] == 'T' && req[3] == ' ' &&
        find_bytes(req, size, "Icy-MetaData:", 13) &&
        find_bytes(req, size, "HTTP/1.1", 8)) {
        char tmp[ICY_HTTP10_WRAP_MAX];
        memcpy(tmp, req, size);
        char *ver = (char *)find_bytes(tmp, size, "HTTP/1.1", 8);
        if (ver) {
            ver[7] = '0';
            return __real_lwip_send(s, tmp, size, flags);
        }
    }
    return __real_lwip_send(s, data, size, flags);
}


// Fun Radio's historic host 302s to streamer-N.rtl.fr, which now 404s.
// Audiomeans still serves the 128 kbps MP3 over HTTP.
#define FUN_RADIO_URL \
    "http://streaming-ice.audiomeans.fr/rtl/feed-funradio-mp3-128" \
    "?listen=webCwsBCggNCQgLDQUGBAcGBg"

static bool is_radiofrance_host(const char *url)
{
    return strcasestr(url, "radiofrance.fr") ||
           strcasestr(url, "fipradio.fr") ||
           strcasestr(url, "franceinfo.fr") ||
           strcasestr(url, "franceinter.fr") ||
           strcasestr(url, "francebleu.fr");
}

// Shoutcast CDN (RTL 102.5 etc.): HTTPS is 320 kbps + TLS + chunked. The same
// mount answers HTTP, and HTTP/1.0 (see __wrap_lwip_send) drops chunked so the
// Mini is not decoding TLS and hex chunk sizes at the same time.
static bool is_shoutcast_cdn(const char *url)
{
    return strcasestr(url, "shoutcast.com");
}

static bool is_funradio_url(const char *url)
{
    if (strcasestr(url, "audiomeans.fr") && strcasestr(url, "funradio")) {
        return false;   // already on the working host
    }
    if (strcasestr(url, "funradio.fr")) return true;
    return strcasestr(url, "fun-1-44") &&
           (strcasestr(url, "rtl.fr") || strcasestr(url, "funradio"));
}

static void rewrite_known_streams(char *url, size_t url_sz)
{
    if (!url || !url[0]) return;

    // Same Mini TLS trap as playradio: Radio France HTTPS works in curl but
    // the handshake plus 48 kHz MP3 leaves too little internal RAM for the
    // decoder. Their Icecast still speaks plain HTTP on the same path.
    if (strncasecmp(url, "https://", 8) == 0 &&
        (is_radiofrance_host(url) || is_shoutcast_cdn(url))) {
        memmove(url + 4, url + 5, strlen(url + 5) + 1);
        ESP_LOGW(TAG, "stream: HTTPS→HTTP %s", url);
    }

    if (is_funradio_url(url)) {
        strncpy(url, FUN_RADIO_URL, url_sz - 1);
        url[url_sz - 1] = 0;
        ESP_LOGW(TAG, "Fun Radio: rewrite %s", url);
    }
}


/*
static void sanitize(char *s)
*/
static void sanitize(char *s)
{
    for (int i = 0; s[i]; i++) {
        if (s[i] == '"' || s[i] == '\\') {
            s[i] = ' ';
        }
    }
}


/*
static void on_meta(const char *meta)
ICY StreamTitle parser, registered with the engine as the metadata callback.
*/
static void on_meta(const char *meta)
{
    if (!meta) return;

    const char *start = strstr(meta, "StreamTitle='");
    char quote = '\'';
    if (start) {
        start += strlen("StreamTitle='");
    } else if ((start = strstr(meta, "StreamTitle=\"")) != NULL) {
        start += strlen("StreamTitle=\"");
        quote = '"';
    } else {
        return;
    }

    // ICY wraps the title in quotes and ends the field with ';'. Using the first
    // quote would cut French titles at the apostrophe ("J'te l'dis" → "J").
    char term[3] = { quote, ';', 0 };
    const char *end = strstr(start, term);
    if (!end) {
        end = strrchr(start, quote);
        if (!end) return;
    }

    int len = (int)(end - start);
    if (len <= 0 || len >= 128) return;

    char raw[128];
    memcpy(raw, start, len);
    raw[len] = 0;
    for (int i = 0; i < len; i++) {
        if (raw[i] == '\r' || raw[i] == '\n') raw[i] = ' ';
    }

    char title[128];
    meta_to_utf8(title, sizeof(title), raw);
    sanitize(title);
    app_state_update(&(app_state_patch_t){
        .has_title = true,
        .title = title
    });
}


/*
static void on_info(void)
A decoder reported MUSIC_INFO → the stream is producing output. Cancel any
scheduled retry (esp-adf may have recovered the HTTP element internally) and
start the clock that decides whether this attempt counts as healthy.

Deliberately does NOT clear the retry budget. MUSIC_INFO fires a second or two
after the connect, long before the stream has proven anything — when the failure
is a TLS read dying just after the decoder reads the sample rate, this callback
runs on every single attempt and would hand back a full budget each time.
*/
static void disarm_buf_timer(void)
{
    if (s_buf_timer) esp_timer_stop(s_buf_timer);
}

static void request_pipeline_stop(void)
{
    s_need_stop     = true;
    s_retry_pending = false;
    if (s_retry_task_handle)
        xTaskNotifyGive(s_retry_task_handle);
}

static void buffering_giveup(void *arg)
{
    (void)arg;
    ESP_LOGE(TAG, "Buffering timed out — giving up");
    s_retry_count      = 0;
    s_retry_pending    = false;
    s_playing_since_us = 0;
    s_flip_codec_on_retry = false;
    app_state_update(&(app_state_patch_t){
        .has_radio   = true,
        .radio_state = RADIO_STATE_ERROR
    });
    // Do not mark_stopped() here: that makes is_playing() false while ADF
    // still has the pipeline RUNNING, and the next play logs
    // "Pipeline already started" and never reconnects.
    request_pipeline_stop();
}

static void arm_buf_timer(void)
{
    if (!s_buf_timer) {
        const esp_timer_create_args_t args = {
            .callback = buffering_giveup,
            .name     = "buf_to",
        };
        if (esp_timer_create(&args, &s_buf_timer) != ESP_OK) {
            ESP_LOGW(TAG, "buffering timer create failed");
            return;
        }
    }
    esp_timer_stop(s_buf_timer);
    esp_timer_start_once(s_buf_timer, BUFFERING_GIVEUP_US);
}

static void on_info(void)
{
    if (!s_http_session) return;
    s_retry_pending    = false;
    s_playing_since_us = esp_timer_get_time();
    disarm_buf_timer();
    ESP_LOGW(TAG, "MUSIC_INFO → playing (%s)", codec_name(s_codec));
    if (s_url[0]) {
        strncpy(s_learned_url, s_url, sizeof(s_learned_url) - 1);
        s_learned_url[sizeof(s_learned_url) - 1] = 0;
        s_learned_codec = s_codec;
    }
    app_state_update(&(app_state_patch_t){
        .has_radio = true,
        .radio_state = RADIO_STATE_PLAYING
    });
}


/*
static void on_error(int status)
HTTP reader status from the engine. Internet radio has no legitimate "end", so
both an open failure and a mid-stream loss are anomalies → schedule a retry.
  1) ERROR_OPEN — HTTP didn't open (e.g. 404, TLS timeout at start).
  2) stream-lost — TLS died mid-playback (INPUT_DONE/STATE_FINISHED/ERROR_*).
*/
static void on_error(int status)
{
    if (!s_http_session) return;
    bool is_open_error  = (status == AEL_STATUS_ERROR_OPEN
                        || status == AEL_STATUS_ERROR_TIMEOUT);
    // Teardown of the previous station posts ABORT/FINISHED after the new
    // play has started. Swallow those. ERROR_OPEN is the new URL (410, 404)
    // and must not be ignored — RMC's 410 arrived in <800 ms and was.
    if (!is_open_error && esp_timer_get_time() < s_error_grace_until_us) {
        ESP_LOGI(TAG, "Ignoring status %d during station-switch grace", status);
        return;
    }

    bool is_stream_lost = audio_engine_is_playing()
        && (status == AEL_STATUS_INPUT_DONE
         || status == AEL_STATUS_STATE_FINISHED
         || status == AEL_STATUS_ERROR_INPUT
         || status == AEL_STATUS_ERROR_PROCESS);

    if (!is_open_error && !is_stream_lost) return;

    // Finite stream (podcast episode): the end of the stream is success, not an
    // anomaly. Any stream-end/loss ends playback cleanly with no retry; only a
    // failed open still retries (episode URL may be slow/redirected on first
    // hit). A mid-episode WiFi drop also lands here → reported as finished;
    // Phase-2 resume (Range request) will refine this.
    if (s_finite && is_stream_lost) {
        ESP_LOGI(TAG, "Finite stream ended (status=%d)", status);
        s_retry_count      = 0;
        s_retry_pending    = false;
        s_playing_since_us = 0;
        disarm_buf_timer();
        request_pipeline_stop();
        app_state_update(&(app_state_patch_t){
            .has_radio   = true,
            .radio_state = RADIO_STATE_FINISHED
        });
        return;
    }

    // HTTP 4xx/410/DNS: rebuilding the pipeline cannot fix it, and leaving
    // ADF in RUNNING makes the next station hit "Pipeline already started".
    if (is_open_error) {
        ESP_LOGE(TAG, "HTTP open failed (status=%d) — not retrying", status);
        s_retry_count      = 0;
        s_retry_pending    = false;
        s_playing_since_us = 0;
        disarm_buf_timer();
        app_state_update(&(app_state_patch_t){
            .has_radio   = true,
            .radio_state = RADIO_STATE_ERROR
        });
        request_pipeline_stop();
        return;
    }

    // Did this attempt play long enough to earn a fresh budget? Asked before the
    // cap, so a station that ran for a while and then dropped never trips it.
    int64_t played_us = s_playing_since_us
        ? esp_timer_get_time() - s_playing_since_us : 0;

    if (played_us > STABLE_PLAY_US) {
        ESP_LOGI(TAG, "Stream ran %lld s before dropping — retry budget reset",
                 played_us / 1000000);
        s_retry_count = 0;
    }
    s_playing_since_us = 0;   // the next attempt has not produced audio yet

    const int retry_cap = (played_us == 0) ? MAX_OPEN_RETRIES : MAX_RETRIES;
    if (s_retry_count >= retry_cap) {
        ESP_LOGE(TAG, "Max retries reached, giving up");
        s_retry_count   = 0;
        s_retry_pending = false;   // nothing is owed a restart any more
        disarm_buf_timer();
        app_state_update(&(app_state_patch_t){
            .has_radio   = true,
            .radio_state = RADIO_STATE_ERROR
        });
        request_pipeline_stop();
        return;
    }

    // Never reached MUSIC_INFO: the URL-hinted codec was probably wrong
    // (MP3 pipeline + AAC Content-Type relink abort). Next retry starts the
    // other decoder instead of restarting the empty one.
    s_flip_codec_on_retry = (played_us == 0);

    s_retry_count++;
    s_retry_pending = true;
    ESP_LOGW(TAG, "%s (%d/%d), scheduling retry...",
             is_open_error ? "HTTP open failed" : "Stream lost",
             s_retry_count, retry_cap);

    if (s_retry_task_handle) {
        xTaskNotifyGive(s_retry_task_handle);
    }
}


/*
static void retry_task(void *param)
Restarts the pipeline after an HTTP error. Triggered by xTaskNotifyGive from
on_error (which runs on the engine's event task). Must be a separate task —
restarting from inside the event listener overflows the event queue and causes
a FreeRTOS assert.
*/
static void retry_task(void *param)
{
    // Internal RAM on this board runs tight enough that a retry can fail for want
    // of memory rather than network, so it is worth having in the log. Reported
    // over the DMA-capable subset, not MALLOC_CAP_INTERNAL: RTCRAM counts as
    // internal but cannot be DMAed into, so the plain internal figure has read up
    // to 8 KB high — it said 17408 while the block that actually mattered was
    // 1728. Diagnostic only; a low reading never blocks the retry.
    //
    // (The previous note here pointed at CONFIG_MBEDTLS_DYNAMIC_BUFFER as "the
    // real fix". It is not: sdkconfig.defaults documents that DYNAMIC_BUFFER
    // breaks HTTPS against a renegotiating CDN.)
    const size_t WARN_DMA_LARGEST = 4 * 1024;

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint32_t gen = s_play_gen;

        if (s_need_stop) {
            s_need_stop = false;
            if (!s_http_session) continue;
            ESP_LOGW(TAG, "Stopping pipeline after failed open / buffering timeout");
            audio_engine_stop();
            audio_engine_mark_stopped();
            continue;
        }

        if (!s_http_session) continue;
        if (!audio_engine_is_playing()) continue;

        // Codec flip is a wrong-decoder open, not a network fade — retry sooner.
        int delay_ms = s_flip_codec_on_retry ? 400
                     : (s_retry_count <= 1) ? 3000 : 2000;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));

        if (gen != s_play_gen) continue;
        if (!audio_engine_is_playing()) continue;

        // pipeline may have resumed by itself (esp-adf does internal element
        // recovery) — on_info() clears the pending flag, so don't restart a
        // stream that came back on its own.
        if (!s_retry_pending) {
            ESP_LOGI(TAG, "Retry cancelled — pipeline recovered");
            continue;
        }

        size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                          MALLOC_CAP_DMA);
        if (largest < WARN_DMA_LARGEST) {
            ESP_LOGW(TAG, "DMA-capable RAM critically low (largest=%u) — retry may fail",
                     largest);
        }

        ESP_LOGW(TAG, "Retry %d (DMA largest=%u)...",
                 s_retry_count, largest);

        if (s_flip_codec_on_retry && s_url[0]) {
            audio_codec_t next = other_stream_codec(s_codec);
            ESP_LOGW(TAG, "Retry as %s (started as %s, no audio)",
                     codec_name(next), codec_name(s_codec));
            s_codec = next;
            strncpy(s_learned_url, s_url, sizeof(s_learned_url) - 1);
            s_learned_url[sizeof(s_learned_url) - 1] = 0;
            s_learned_codec = next;
            s_flip_codec_on_retry = false;
            audio_engine_set_http_offset(s_offset_bytes);
            audio_engine_play(AUDIO_SRC_HTTP, s_codec, s_url, 0);
        } else {
            audio_engine_restart_current();
        }

        ESP_LOGI(TAG, "Retry pipeline started");
    }
}


void audio_net_player_init(void)
{
    // Retry orchestration only (waits on a notify, then restarts the pipeline) —
    // off the audio hot path and no flash access, so its stack goes to PSRAM to
    // spare contiguous internal SRAM for the TLS handshake.
    xTaskCreatePinnedToCoreWithCaps(retry_task, "audio_retry", 4096, NULL, 4,
                                    &s_retry_task_handle, tskNO_AFFINITY,
                                    MALLOC_CAP_SPIRAM);

    audio_engine_set_meta_cb(on_meta);
    audio_engine_set_info_cb(on_info);
    audio_engine_set_error_cb(on_error);
}


void audio_net_player_suspend(void)
{
    s_http_session  = false;
    s_need_stop     = false;
    s_retry_pending = false;
    s_play_gen++;
    disarm_buf_timer();
}

void audio_net_player_play(const char *url, bool finite, uint32_t offset_bytes)
{
    if (!url) return;

    // A deliberate play is a clean slate: full budget, nothing scheduled, and no
    // playback time carried over from whatever was on before.
    s_play_gen++;
    s_http_session     = true;
    s_need_stop        = false;
    s_retry_count      = 0;
    s_retry_pending    = false;
    s_playing_since_us = 0;
    s_error_grace_until_us = esp_timer_get_time() + ERROR_GRACE_US;
    s_finite = finite;
    // ADF can stay RUNNING after ERROR_OPEN. mark_stopped() only clears our
    // flag, so the next play() logs "Pipeline already started, state:3" and
    // never opens the new URL (Skyrock after RMC 410). Tear it down first.
    audio_engine_stop();
    arm_buf_timer();

    // Codec from URL hint; extension-less URLs (e.g. SHOUTcast host:port/;)
    // default to MP3 — the vast majority of such streams are MP3, and a
    // pre-flight HTTP probe is unreliable against ancient SHOUTcast servers.
    audio_codec_t codec = detect_codec_from_url(url);
    if (codec == AUDIO_CODEC_UNKNOWN) {
        if (s_learned_url[0] && strcmp(url, s_learned_url) == 0 &&
            s_learned_codec != AUDIO_CODEC_UNKNOWN) {
            codec = s_learned_codec;
            ESP_LOGW(TAG, "No URL hint, using learned %s", codec_name(codec));
        } else {
            ESP_LOGI(TAG, "No codec hint in URL, defaulting to MP3");
            codec = AUDIO_CODEC_MP3;
        }
    }

    strncpy(s_url, url, sizeof(s_url) - 1);
    s_url[sizeof(s_url) - 1] = 0;
    char *hash = strchr(s_url, '#');
    if (hash) *hash = 0;

    // playradio Icecast also speaks plain HTTP on the same ports. HTTPS here
    // is 192 kbps AAC-LC plus a TLS session: after connect INT largest drops
    // to ~32 KB and the decoder never reports MUSIC_INFO on the Super Mini.
    if (strncasecmp(s_url, "https://", 8) == 0 && strcasestr(s_url, "playradio.org")) {
        memmove(s_url + 4, s_url + 5, strlen(s_url + 5) + 1);
        ESP_LOGW(TAG, "playradio: HTTPS→HTTP %s", s_url);
    }

    rewrite_known_streams(s_url, sizeof(s_url));

    s_offset_bytes = offset_bytes;
    s_codec = codec;
    s_flip_codec_on_retry = false;

    // Byte offset for a resuming podcast (0 for radio / play-from-start). Set
    // before the async play so the request task reads it at open time.
    audio_engine_set_http_offset(offset_bytes);

    ESP_LOGW(TAG, "Start %s %s", codec_name(codec), s_url);
    audio_engine_play(AUDIO_SRC_HTTP, codec, s_url, 0);
}
