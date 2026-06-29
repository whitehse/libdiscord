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

    /* Config with intents and shards */
    discord_config_t shard_cfg = {
        .event_queue_size = 16,
        .bot_token = "shard_token",
        .intents = DISCORD_INTENT_GUILDS | DISCORD_INTENT_GUILD_MESSAGES,
        .shard_id = 1,
        .num_shards = 4
    };
    discord_ctx_t *shard_ctx = discord_create_with_config(DISCORD_ROLE_CLIENT, &shard_cfg);
    assert(shard_ctx != NULL);
    discord_destroy(shard_ctx);

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
    assert(discord_get_guild(NULL, "123") == -1);
    assert(discord_get_channel_messages(NULL, "123") == -1);
    assert(discord_add_reaction(NULL, "123", "456", "emoji") == -1);
    assert(discord_delete_own_reaction(NULL, "123", "456", "emoji") == -1);
    assert(discord_create_interaction_response(NULL, "1", "tok", "{}") == -1);
    assert(discord_create_dm(NULL, "123") == -1);

    /* Gateway helpers */
    assert(discord_gateway_connect(NULL) == -1);
    assert(discord_gateway_send_identify(NULL) == -1);
    assert(discord_gateway_send_heartbeat(NULL, 0, 0) == -1);
    assert(discord_gateway_send_resume(NULL) == -1);

    /* New helpers */
    assert(discord_gateway_state(NULL) == DISCORD_GW_DISCONNECTED);
    assert(discord_set_token(NULL, "x") == -1);
    discord_reset(NULL); /* should not crash */

    assert(discord_gateway_process_heartbeat(NULL, 0) == 0);

    printf("  test_null_ctx_handling PASSED\n");
}

static void test_api_helper_null_args(void) {
    printf("  test_api_helper_null_args...\n");

    discord_ctx_t *ctx = discord_create(DISCORD_ROLE_CLIENT);
    assert(ctx != NULL);

    /* NULL channel_id */
    assert(discord_create_message(ctx, NULL, "hello") == -1);
    assert(discord_create_message_ex(ctx, NULL, "x", NULL, NULL) == -1);
    assert(discord_create_message_ex(ctx, "123", NULL, NULL, NULL) == 0); /* content=NULL is OK */

    /* NULL message_id */
    assert(discord_edit_message(ctx, NULL, "456", "hello") == -1);
    assert(discord_edit_message(ctx, "123", NULL, "hello") == -1);
    assert(discord_edit_message(ctx, "123", "456", NULL) == -1);

    assert(discord_delete_message(ctx, NULL, "456") == -1);
    assert(discord_delete_message(ctx, "123", NULL) == -1);

    assert(discord_get_channel(ctx, NULL) == -1);
    assert(discord_get_guild(ctx, NULL) == -1);
    assert(discord_get_channel_messages(ctx, NULL) == -1);

    assert(discord_add_reaction(ctx, "123", "456", NULL) == -1);
    assert(discord_delete_own_reaction(ctx, NULL, "456", "e") == -1);
    assert(discord_create_interaction_response(ctx, NULL, "t", "{}") == -1);
    assert(discord_create_interaction_response(ctx, "1", NULL, "{}") == -1);
    assert(discord_create_interaction_response(ctx, "1", "t", NULL) == -1);
    assert(discord_create_dm(ctx, NULL) == -1);

    assert(discord_set_token(ctx, NULL) == -1);

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

    uint8_t out[512];
    int n = discord_get_output(ctx, out, sizeof(out));
    assert(n >= 0);

    discord_destroy(ctx);
    printf("  test_output_delegation PASSED\n");
}

static void test_gateway_heartbeat_helpers(void) {
    printf("  test_gateway_heartbeat_helpers...\n");

    discord_ctx_t *ctx = discord_create(DISCORD_ROLE_CLIENT);
    assert(ctx != NULL);

    /* First heartbeat should always fire (never sent before) */
    assert(discord_gateway_process_heartbeat(ctx, 1000) == 1);

    /* send_heartbeat with timestamp */
    int rc = discord_gateway_send_heartbeat(ctx, -1, 1000);
    (void)rc;

    /* Immediately after sending, should NOT need another heartbeat */
    assert(discord_gateway_process_heartbeat(ctx, 1000) == 0);
    assert(discord_gateway_process_heartbeat(ctx, 5000) == 0);

    /* After interval elapses (41250ms default), should fire */
    assert(discord_gateway_process_heartbeat(ctx, 1000 + 41250) == 1);

    discord_destroy(ctx);
    printf("  test_gateway_heartbeat_helpers PASSED\n");
}

static void test_gateway_state_accessor(void) {
    printf("  test_gateway_state_accessor...\n");

    discord_ctx_t *ctx = discord_create(DISCORD_ROLE_CLIENT);
    assert(ctx != NULL);

    /* Initial state should be disconnected */
    assert(discord_gateway_state(ctx) == DISCORD_GW_DISCONNECTED);

    discord_destroy(ctx);
    printf("  test_gateway_state_accessor PASSED\n");
}

static void test_reset(void) {
    printf("  test_reset...\n");

    discord_config_t cfg = { .event_queue_size = 16, .bot_token = "reset_token" };
    discord_ctx_t *ctx = discord_create_with_config(DISCORD_ROLE_CLIENT, &cfg);
    assert(ctx != NULL);

    /* After reset, state should be clean but context still valid */
    discord_reset(ctx);
    assert(discord_gateway_state(ctx) == DISCORD_GW_DISCONNECTED);

    /* Should still be able to use the context */
    assert(discord_create_message(ctx, "123", "after reset") == 0);

    discord_destroy(ctx);
    printf("  test_reset PASSED\n");
}

static void test_set_token(void) {
    printf("  test_set_token...\n");

    discord_ctx_t *ctx = discord_create(DISCORD_ROLE_CLIENT);
    assert(ctx != NULL);

    assert(discord_set_token(ctx, "new_token_456") == 0);

    /* Should be able to use the new token for API calls */
    assert(discord_create_message(ctx, "123", "with new token") == 0);

    discord_destroy(ctx);
    printf("  test_set_token PASSED\n");
}

static void test_event_struct_copy_safety(void) {
    printf("  test_event_struct_copy_safety...\n");

    /* Verify that discord_event_t is self-contained (embedded arrays, not pointers) */
    discord_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = DISCORD_EVENT_MESSAGE_CREATE;
    snprintf(ev.data.message.id, sizeof(ev.data.message.id), "msg123");
    snprintf(ev.data.message.content, sizeof(ev.data.message.content), "hello world");

    /* Copy the event — the embedded arrays should be copied */
    discord_event_t copy = ev;
    assert(strcmp(copy.data.message.id, "msg123") == 0);
    assert(strcmp(copy.data.message.content, "hello world") == 0);

    /* Modifying the copy should not affect the original */
    copy.data.message.id[0] = 'X';
    assert(ev.data.message.id[0] == 'm'); /* original unchanged */

    printf("  test_event_struct_copy_safety PASSED\n");
}

static void test_rate_limit_event_fields(void) {
    printf("  test_rate_limit_event_fields...\n");

    /* Verify rate limit event struct layout and copy safety */
    discord_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = DISCORD_EVENT_RATE_LIMITED;
    ev.data.rate_limit.retry_after_ms = 5000;
    ev.data.rate_limit.is_global = 1;
    snprintf(ev.data.rate_limit.bucket, sizeof(ev.data.rate_limit.bucket),
             "abc123_bucket");

    assert(ev.type == DISCORD_EVENT_RATE_LIMITED);
    assert(ev.data.rate_limit.retry_after_ms == 5000);
    assert(ev.data.rate_limit.is_global == 1);
    assert(strcmp(ev.data.rate_limit.bucket, "abc123_bucket") == 0);

    /* Copy safety */
    discord_event_t copy = ev;
    assert(copy.data.rate_limit.retry_after_ms == 5000);
    assert(copy.data.rate_limit.is_global == 1);
    assert(strcmp(copy.data.rate_limit.bucket, "abc123_bucket") == 0);
    copy.data.rate_limit.bucket[0] = 'Z';
    assert(ev.data.rate_limit.bucket[0] == 'a'); /* original unchanged */

    /* Test per-route (non-global) rate limit */
    memset(&ev, 0, sizeof(ev));
    ev.type = DISCORD_EVENT_RATE_LIMITED;
    ev.data.rate_limit.retry_after_ms = 1200;
    ev.data.rate_limit.is_global = 0;
    assert(ev.data.rate_limit.is_global == 0);
    assert(ev.data.rate_limit.retry_after_ms == 1200);

    printf("  test_rate_limit_event_fields PASSED\n");
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
    test_gateway_state_accessor();
    test_reset();
    test_set_token();
    test_event_struct_copy_safety();
    test_rate_limit_event_fields();

    printf("libdiscord smoke test PASSED.\n");
    return 0;
}
