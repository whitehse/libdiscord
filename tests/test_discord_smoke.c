/* test_discord_smoke.c — Smoke test for libdiscord
 *
 * Exercises create/destroy lifecycle, NULL handling, config validation,
 * event queue drain, and API helper NULL checks.
 */

#include "discord.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>

static void test_create_destroy_lifecycle(void) {
    printf("  test_create_destroy_lifecycle...\n");

    /* Default create */
    discord_ctx_t *client = discord_create(DISCORD_ROLE_CLIENT);
    assert(client != NULL);
    discord_destroy(client);

    /* Server role */
    discord_ctx_t *server = discord_create(DISCORD_ROLE_SERVER);
    assert(server != NULL);
    discord_destroy(server);

    /* NULL destroy is safe */
    discord_destroy(NULL);

    printf("  test_create_destroy_lifecycle PASSED\n");
}

static void test_config_create(void) {
    printf("  test_config_create...\n");

    discord_config_t cfg = {
        .event_queue_size = 32,
        .bot_token = "test_token_123",
        .gateway_url = NULL,
        .api_base_url = NULL
    };
    discord_ctx_t *ctx = discord_create_with_config(DISCORD_ROLE_CLIENT, &cfg);
    assert(ctx != NULL);
    discord_destroy(ctx);

    /* Zero queue size should fail */
    discord_config_t bad_cfg = { .event_queue_size = 0 };
    discord_ctx_t *bad = discord_create_with_config(DISCORD_ROLE_CLIENT, &bad_cfg);
    assert(bad == NULL);

    printf("  test_config_create PASSED\n");
}

static void test_null_ctx_handling(void) {
    printf("  test_null_ctx_handling...\n");

    discord_event_t ev;
    assert(discord_next_event(NULL, &ev) == 0);

    uint8_t buf[64];
    assert(discord_get_output(NULL, buf, sizeof(buf)) == 0);

    assert(discord_feed_input(NULL, (const uint8_t *)"x", 1) == -1);

    /* API helpers */
    assert(discord_create_message(NULL, "123", "hello") == -1);
    assert(discord_edit_message(NULL, "123", "456", "hello") == -1);
    assert(discord_delete_message(NULL, "123", "456") == -1);
    assert(discord_get_channel(NULL, "123") == -1);

    /* Gateway helpers */
    assert(discord_gateway_connect(NULL) == -1);
    assert(discord_gateway_send_identify(NULL) == -1);
    assert(discord_gateway_send_heartbeat(NULL, 0) == -1);
    assert(discord_gateway_send_resume(NULL) == -1);

    printf("  test_null_ctx_handling PASSED\n");
}

static void test_api_helper_null_args(void) {
    printf("  test_api_helper_null_args...\n");

    discord_ctx_t *ctx = discord_create(DISCORD_ROLE_CLIENT);
    assert(ctx != NULL);

    /* NULL channel_id */
    assert(discord_create_message(ctx, NULL, "hello") == -1);
    assert(discord_create_message(ctx, "123", NULL) == -1);

    /* NULL message_id */
    assert(discord_edit_message(ctx, NULL, "456", "hello") == -1);
    assert(discord_edit_message(ctx, "123", NULL, "hello") == -1);
    assert(discord_edit_message(ctx, "123", "456", NULL) == -1);

    assert(discord_delete_message(ctx, NULL, "456") == -1);
    assert(discord_delete_message(ctx, "123", NULL) == -1);

    assert(discord_get_channel(ctx, NULL) == -1);

    discord_destroy(ctx);
    printf("  test_api_helper_null_args PASSED\n");
}

static void test_event_queue_drain(void) {
    printf("  test_event_queue_drain...\n");

    discord_config_t cfg = { .event_queue_size = 8 };
    discord_ctx_t *ctx = discord_create_with_config(DISCORD_ROLE_CLIENT, &cfg);
    assert(ctx != NULL);

    /* No events initially */
    discord_event_t ev;
    assert(discord_next_event(ctx, &ev) == 0);

    /* Feed some data (may not produce Discord events but should not crash) */
    uint8_t settings[9] = {0,0,0, 4, 0, 0,0,0,0};
    (void)discord_feed_input(ctx, settings, sizeof(settings));

    /* Drain any events that were produced */
    int count = 0;
    while (discord_next_event(ctx, &ev) == 1) {
        count++;
    }
    /* May or may not have events — just verify no crash */

    discord_destroy(ctx);
    printf("  test_event_queue_drain PASSED\n");
}

static void test_output_delegation(void) {
    printf("  test_output_delegation...\n");

    discord_ctx_t *ctx = discord_create(DISCORD_ROLE_CLIENT);
    assert(ctx != NULL);

    /* Output should come through (librest/shaggy produces preface) */
    uint8_t out[512];
    int n = discord_get_output(ctx, out, sizeof(out));
    /* Should get at least the HTTP/2 preface from shaggy via librest */
    assert(n >= 0);  /* may be 0 or positive depending on librest state */

    discord_destroy(ctx);
    printf("  test_output_delegation PASSED\n");
}

static void test_gateway_heartbeat_helpers(void) {
    printf("  test_gateway_heartbeat_helpers...\n");

    discord_ctx_t *ctx = discord_create(DISCORD_ROLE_CLIENT);
    assert(ctx != NULL);

    /* process_heartbeat returns 1 (always send) */
    assert(discord_gateway_process_heartbeat(ctx) == 1);

    /* send_heartbeat should work (generates WS text frame) */
    int rc = discord_gateway_send_heartbeat(ctx, -1);
    /* May succeed or fail depending on gateway state — just verify no crash */
    (void)rc;

    discord_destroy(ctx);
    printf("  test_gateway_heartbeat_helpers PASSED\n");
}

int main(void) {
    printf("libdiscord smoke test starting...\n");

    test_create_destroy_lifecycle();
    test_config_create();
    test_null_ctx_handling();
    test_api_helper_null_args();
    test_event_queue_drain();
    test_output_delegation();
    test_gateway_heartbeat_helpers();

    printf("libdiscord smoke test PASSED.\n");
    return 0;
}
