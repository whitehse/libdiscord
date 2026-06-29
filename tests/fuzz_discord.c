/* fuzz_discord.c — libFuzzer harness for libdiscord
 *
 * Targets (routed by first byte of input):
 *   1. API surface: exercises discord_create_message, edit, delete, get_channel
 *   2. Event translation: feeds raw bytes into discord_feed_input and drains events
 *   3. Config fuzzing: exercises create_with_config with various config values
 *
 * Build: cmake -B build -S . -DENABLE_FUZZ=ON && cmake --build build
 */

#include "discord.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* Fuzz the API surface: exercise REST helpers with arbitrary channel/message IDs. */
static void fuzz_api_surface(const uint8_t *data, size_t size) {
    if (size < 8) return;

    discord_config_t cfg = { .event_queue_size = 32, .bot_token = "fuzz_token" };
    discord_ctx_t *ctx = discord_create_with_config(DISCORD_ROLE_CLIENT, &cfg);
    if (!ctx) return;

    /* Build channel_id and message_id from fuzz input */
    char channel_id[64];
    char message_id[64];
    size_t half = size / 2;
    if (half >= sizeof(channel_id)) half = sizeof(channel_id) - 1;
    size_t qtr = size / 4;
    if (qtr >= sizeof(message_id)) qtr = sizeof(message_id) - 1;

    memcpy(channel_id, data, half);
    channel_id[half] = '\0';
    memcpy(message_id, data + half, qtr);
    message_id[qtr] = '\0';

    /* Build content from remaining bytes */
    char content[256];
    size_t clen = size - half - qtr;
    if (clen >= sizeof(content)) clen = sizeof(content) - 1;
    memcpy(content, data + half + qtr, clen);
    content[clen] = '\0';

    /* Exercise each API helper (may fail due to state, that's fine) */
    discord_create_message(ctx, channel_id, content);
    discord_edit_message(ctx, channel_id, message_id, content);
    discord_delete_message(ctx, channel_id, message_id);
    discord_get_channel(ctx, channel_id);

    /* Drain output */
    uint8_t out_buf[4096];
    while (discord_get_output(ctx, out_buf, sizeof(out_buf)) > 0) { /* drain */ }

    discord_destroy(ctx);
}

/* Fuzz event translation: feed raw bytes and drain events. */
static void fuzz_event_translation(const uint8_t *data, size_t size) {
    if (size < 2) return;

    uint8_t qsize = data[0] % 128;
    if (qsize < 2) qsize = 2;

    discord_config_t cfg = { .event_queue_size = qsize, .bot_token = "fuzz" };

    for (int i = 0; i < 4; i++) {
        discord_ctx_t *ctx = discord_create_with_config(i & 1 ? DISCORD_ROLE_CLIENT : DISCORD_ROLE_SERVER, &cfg);
        if (!ctx) continue;

        /* Feed arbitrary data */
        discord_feed_input(ctx, data + 1, size - 1);

        /* Drain events */
        discord_event_t ev;
        while (discord_next_event(ctx, &ev) == 1) { /* drain */ }

        /* Drain output */
        uint8_t out_buf[256];
        while (discord_get_output(ctx, out_buf, sizeof(out_buf)) > 0) { /* drain */ }

        /* Exercise gateway helpers (may fail, no crash) */
        if (i & 1) {
            discord_gateway_send_heartbeat(ctx, (int)data[0]);
        }

        discord_destroy(ctx);
    }
}

/* Fuzz config variations. */
static void fuzz_config_fuzzing(const uint8_t *data, size_t size) {
    if (size < 4) return;

    /* Vary config parameters */
    size_t qsize = (size_t)(data[0] % 128);
    if (qsize < 2) qsize = 2;

    /* Build token from fuzz data */
    char token[128];
    size_t tlen = size / 2;
    if (tlen >= sizeof(token)) tlen = sizeof(token) - 1;
    memcpy(token, data + 1, tlen);
    token[tlen] = '\0';

    /* Build api_base from fuzz data */
    char api_base[256];
    size_t blen = size - tlen - 1;
    if (blen >= sizeof(api_base)) blen = sizeof(api_base) - 1;
    memcpy(api_base, data + 1 + tlen, blen);
    api_base[blen] = '\0';

    discord_config_t cfg = {
        .event_queue_size = qsize,
        .bot_token = token[0] ? token : NULL,
        .api_base_url = api_base[0] ? api_base : NULL
    };

    /* Create and immediately destroy */
    discord_ctx_t *ctx = discord_create_with_config(DISCORD_ROLE_CLIENT, &cfg);
    if (ctx) {
        discord_event_t ev;
        while (discord_next_event(ctx, &ev) == 1) { /* drain */ }
        uint8_t buf[256];
        while (discord_get_output(ctx, buf, sizeof(buf)) > 0) { /* drain */ }
        discord_destroy(ctx);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0) return 0;

    switch (data[0] & 0x3) {
        case 0:
            fuzz_api_surface(data + 1, size - 1);
            break;
        case 1:
            fuzz_event_translation(data + 1, size - 1);
            break;
        case 2:
            fuzz_config_fuzzing(data + 1, size - 1);
            break;
        default:
            fuzz_api_surface(data + 1, size - 1);
            break;
    }

    return 0;
}
