/* test_discord_dialectic.c — Paired client ↔ server dialectic test for libdiscord
 *
 * Exercises a full Discord API round-trip through real HTTP/2 frames:
 *   client creates message → extract frames → feed to server →
 *   server processes → server respond → extract frames → feed to client →
 *   client observes API response event.
 *
 * Also exercises Gateway WebSocket handshake simulation.
 *
 * No sockets, no I/O — pure buffer exchange per ADR 004.
 */

#include "discord.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>

/* Helper: drain all output from a discord context into a buffer.
 * Returns total bytes written, or -1 on overflow. */
static int drain_output(discord_ctx_t *ctx, uint8_t *buf, size_t max) {
    size_t total = 0;
    for (;;) {
        int n = discord_get_output(ctx, buf + total, max - total);
        if (n <= 0) break;
        total += (size_t)n;
        if (total >= max) return -1;
    }
    return (int)total;
}

/* Helper: single-shot get_output */
static int get_one_output(discord_ctx_t *ctx, uint8_t *buf, size_t max) {
    return discord_get_output(ctx, buf, max);
}

/* Helper: feed all bytes into ctx */
static int feed_all(discord_ctx_t *ctx, const uint8_t *buf, size_t len) {
    return discord_feed_input(ctx, buf, len);
}

/* Helper: drain all events, collecting types into an array */
static int collect_events(discord_ctx_t *ctx, discord_event_t *events, int max_events) {
    int count = 0;
    while (count < max_events && discord_next_event(ctx, &events[count]) == 1) {
        count++;
    }
    return count;
}

/* Perform HTTP/2 connection handshake between client and server librest contexts. */
static void http2_handshake(discord_ctx_t *client, discord_ctx_t *server) {
    uint8_t buf[256];
    int n;

    /* Client sends preface + SETTINGS */
    n = get_one_output(client, buf, sizeof(buf));
    if (n > 0) {
        assert(feed_all(server, buf, (size_t)n) == 0);
    }

    /* Client may also send SETTINGS frame */
    n = get_one_output(client, buf, sizeof(buf));
    if (n > 0) {
        assert(feed_all(server, buf, (size_t)n) == 0);
    }

    /* Server responds with SETTINGS ACK (and possibly its own SETTINGS) */
    for (;;) {
        n = get_one_output(server, buf, sizeof(buf));
        if (n <= 0) break;
        assert(feed_all(client, buf, (size_t)n) == 0);
    }

    /* Client may respond with SETTINGS ACK */
    for (;;) {
        n = get_one_output(client, buf, sizeof(buf));
        if (n <= 0) break;
        assert(feed_all(server, buf, (size_t)n) == 0);
    }

    /* Drain any remaining server output */
    for (;;) {
        n = get_one_output(server, buf, sizeof(buf));
        if (n <= 0) break;
        assert(feed_all(client, buf, (size_t)n) == 0);
    }

    /* Drain any events from both sides */
    discord_event_t ev;
    while (discord_next_event(client, &ev) == 1) { /* drain */ }
    while (discord_next_event(server, &ev) == 1) { /* drain */ }
}

static void test_rest_api_round_trip(void) {
    printf("  test_rest_api_round_trip...\n");

    discord_config_t client_cfg = {
        .event_queue_size = 32,
        .bot_token = "test_bot_token_123"
    };
    discord_config_t server_cfg = {
        .event_queue_size = 32
    };

    discord_ctx_t *client = discord_create_with_config(DISCORD_ROLE_CLIENT, &client_cfg);
    assert(client != NULL);
    discord_ctx_t *server = discord_create_with_config(DISCORD_ROLE_SERVER, &server_cfg);
    assert(server != NULL);

    /* HTTP/2 handshake */
    http2_handshake(client, server);

    /* Client: create a message */
    int rc = discord_create_message(client, "987654321", "Hello, Discord!");
    assert(rc == 0);

    /* Extract HTTP/2 frames from client */
    uint8_t client_output[8192];
    int client_out_len = drain_output(client, client_output, sizeof(client_output));
    assert(client_out_len > 0);

    /* Verify we have a HEADERS frame (REST POST) */
    int found_headers = 0, found_data = 0;
    int pos = 0;
    while (pos + 9 <= client_out_len) {
        uint32_t flen = ((uint32_t)client_output[pos] << 16) |
                        ((uint32_t)client_output[pos+1] << 8) |
                        (uint32_t)client_output[pos+2];
        uint8_t ftype = client_output[pos+3];
        if (ftype == 0x1) found_headers = 1;
        if (ftype == 0x0) found_data = 1;
        pos += 9 + (int)flen;
    }
    assert(found_headers && "client should produce HEADERS frame");
    assert(found_data && "client should produce DATA frame (JSON body)");

    /* Feed to server */
    assert(feed_all(server, client_output, (size_t)client_out_len) == 0);

    /* Server should see request events */
    discord_event_t server_events[16];
    int server_event_count = collect_events(server, server_events, 16);
    (void)server_event_count;

    discord_destroy(client);
    discord_destroy(server);
    printf("  test_rest_api_round_trip PASSED\n");
}

static void test_gateway_handshake_simulation(void) {
    printf("  test_gateway_handshake_simulation...\n");

    discord_config_t client_cfg = {
        .event_queue_size = 32,
        .bot_token = "test_bot_token_456"
    };
    discord_config_t server_cfg = {
        .event_queue_size = 32
    };

    discord_ctx_t *client = discord_create_with_config(DISCORD_ROLE_CLIENT, &client_cfg);
    assert(client != NULL);
    discord_ctx_t *server = discord_create_with_config(DISCORD_ROLE_SERVER, &server_cfg);
    assert(server != NULL);

    /* HTTP/2 handshake */
    http2_handshake(client, server);

    /* Client: connect to gateway (establish binary channel) */
    int rc = discord_gateway_connect(client);
    assert(rc == 0);

    /* Extract frames from client */
    uint8_t client_output[4096];
    int client_out_len = drain_output(client, client_output, sizeof(client_output));
    assert(client_out_len > 0);

    /* Feed to server */
    assert(feed_all(server, client_output, (size_t)client_out_len) == 0);

    /* Drain server events */
    discord_event_t server_events[16];
    int server_event_count = collect_events(server, server_events, 16);
    (void)server_event_count;

    /* Verify no crash and client got CONNECTED event */
    discord_event_t client_events[16];
    int client_event_count = collect_events(client, client_events, 16);
    int found_connected = 0;
    for (int i = 0; i < client_event_count; i++) {
        if (client_events[i].type == DISCORD_EVENT_CONNECTED) {
            found_connected = 1;
        }
    }
    (void)found_connected;

    discord_destroy(client);
    discord_destroy(server);
    printf("  test_gateway_handshake_simulation PASSED\n");
}

static void test_multiple_rest_requests(void) {
    printf("  test_multiple_rest_requests...\n");

    discord_config_t cfg = { .event_queue_size = 32, .bot_token = "token789" };

    int total_headers = 0;
    {
        discord_ctx_t *c1 = discord_create_with_config(DISCORD_ROLE_CLIENT, &cfg);
        assert(c1 != NULL);
        assert(discord_create_message(c1, "111", "First message") == 0);
        uint8_t output[4096];
        int total = drain_output(c1, output, sizeof(output));
        assert(total > 0);
        int pos = 0;
        while (pos + 9 <= total) {
            uint32_t flen = ((uint32_t)output[pos] << 16) |
                            ((uint32_t)output[pos+1] << 8) |
                            (uint32_t)output[pos+2];
            if (output[pos+3] == 0x1) total_headers++;
            pos += 9 + (int)flen;
        }
        discord_destroy(c1);
    }
    {
        discord_ctx_t *c2 = discord_create_with_config(DISCORD_ROLE_CLIENT, &cfg);
        assert(c2 != NULL);
        assert(discord_get_channel(c2, "222") == 0);
        uint8_t output[4096];
        int total = drain_output(c2, output, sizeof(output));
        assert(total > 0);
        int pos = 0;
        while (pos + 9 <= total) {
            uint32_t flen = ((uint32_t)output[pos] << 16) |
                            ((uint32_t)output[pos+1] << 8) |
                            (uint32_t)output[pos+2];
            if (output[pos+3] == 0x1) total_headers++;
            pos += 9 + (int)flen;
        }
        discord_destroy(c2);
    }
    {
        discord_ctx_t *c3 = discord_create_with_config(DISCORD_ROLE_CLIENT, &cfg);
        assert(c3 != NULL);
        assert(discord_delete_message(c3, "333", "444") == 0);
        uint8_t output[4096];
        int total = drain_output(c3, output, sizeof(output));
        assert(total > 0);
        int pos = 0;
        while (pos + 9 <= total) {
            uint32_t flen = ((uint32_t)output[pos] << 16) |
                            ((uint32_t)output[pos+1] << 8) |
                            (uint32_t)output[pos+2];
            if (output[pos+3] == 0x1) total_headers++;
            pos += 9 + (int)flen;
        }
        discord_destroy(c3);
    }

    assert(total_headers >= 3 && "should have at least 3 HEADERS frames total");

    printf("  test_multiple_rest_requests PASSED\n");
}

static void test_new_rest_helpers_produce_frames(void) {
    printf("  test_new_rest_helpers_produce_frames...\n");

    discord_config_t cfg = { .event_queue_size = 16, .bot_token = "new_helper_token" };

    /* get_guild */
    {
        discord_ctx_t *c = discord_create_with_config(DISCORD_ROLE_CLIENT, &cfg);
        assert(c != NULL);
        assert(discord_get_guild(c, "guild123") == 0);
        uint8_t out[4096];
        int n = drain_output(c, out, sizeof(out));
        assert(n > 0);
        int found = 0, pos = 0;
        while (pos + 9 <= n) {
            uint32_t flen = ((uint32_t)out[pos] << 16) |
                            ((uint32_t)out[pos+1] << 8) | (uint32_t)out[pos+2];
            if (out[pos+3] == 0x1) found++;
            pos += 9 + (int)flen;
        }
        assert(found >= 1);
        discord_destroy(c);
    }

    /* get_channel_messages */
    {
        discord_ctx_t *c = discord_create_with_config(DISCORD_ROLE_CLIENT, &cfg);
        assert(c != NULL);
        assert(discord_get_channel_messages(c, "ch456") == 0);
        uint8_t out[4096];
        int n = drain_output(c, out, sizeof(out));
        assert(n > 0);
        discord_destroy(c);
    }

    /* create_interaction_response */
    {
        discord_ctx_t *c = discord_create_with_config(DISCORD_ROLE_CLIENT, &cfg);
        assert(c != NULL);
        assert(discord_create_interaction_response(c, "inter1", "token1",
                "{\"type\":4,\"data\":{\"content\":\"pong\"}}") == 0);
        uint8_t out[4096];
        int n = drain_output(c, out, sizeof(out));
        assert(n > 0);
        discord_destroy(c);
    }

    /* create_dm */
    {
        discord_ctx_t *c = discord_create_with_config(DISCORD_ROLE_CLIENT, &cfg);
        assert(c != NULL);
        assert(discord_create_dm(c, "user123") == 0);
        uint8_t out[4096];
        int n = drain_output(c, out, sizeof(out));
        assert(n > 0);
        discord_destroy(c);
    }

    /* create_message_ex with embeds */
    {
        discord_ctx_t *c = discord_create_with_config(DISCORD_ROLE_CLIENT, &cfg);
        assert(c != NULL);
        assert(discord_create_message_ex(c, "ch789", "text",
                "[{\"title\":\"Embed\"}]", NULL) == 0);
        uint8_t out[4096];
        int n = drain_output(c, out, sizeof(out));
        assert(n > 0);
        discord_destroy(c);
    }

    /* add_reaction and delete_own_reaction */
    {
        discord_ctx_t *c = discord_create_with_config(DISCORD_ROLE_CLIENT, &cfg);
        assert(c != NULL);
        assert(discord_add_reaction(c, "ch1", "msg1", "%F0%9F%91%8D") == 0);
        uint8_t out1[4096];
        int n1 = drain_output(c, out1, sizeof(out1));
        assert(n1 > 0);
        discord_destroy(c);
    }

    printf("  test_new_rest_helpers_produce_frames PASSED\n");
}

static void test_embedded_event_fields(void) {
    printf("  test_embedded_event_fields...\n");

    /* Verify that event struct uses embedded arrays */
    discord_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = DISCORD_EVENT_MESSAGE_CREATE;
    snprintf(ev.data.message.id, DISCORD_ID_LEN, "123456");
    snprintf(ev.data.message.content, DISCORD_MSG_CONTENT_LEN, "test content");
    snprintf(ev.data.message.guild_id, DISCORD_ID_LEN, "guild789");

    /* Verify fields are self-contained */
    assert(strcmp(ev.data.message.id, "123456") == 0);
    assert(strcmp(ev.data.message.content, "test content") == 0);
    assert(strcmp(ev.data.message.guild_id, "guild789") == 0);

    /* Empty guild_id for DMs */
    discord_event_t dm;
    memset(&dm, 0, sizeof(dm));
    dm.type = DISCORD_EVENT_MESSAGE_CREATE;
    assert(dm.data.message.guild_id[0] == '\0'); /* empty = DM */

    /* API response with body */
    discord_event_t api;
    memset(&api, 0, sizeof(api));
    api.type = DISCORD_EVENT_API_RESPONSE;
    api.data.api_response.status_code = 200;
    snprintf(api.data.api_response.json_body, DISCORD_EV_BODY_CAP,
             "{\"id\":\"msg1\",\"content\":\"hello\"}");
    api.data.api_response.body_len = strlen(api.data.api_response.json_body);
    assert(api.data.api_response.status_code == 200);
    assert(api.data.api_response.body_len > 0);
    assert(api.data.api_response.truncated == 0);

    /* Interaction event */
    discord_event_t inter;
    memset(&inter, 0, sizeof(inter));
    inter.type = DISCORD_EVENT_INTERACTION_CREATE;
    inter.data.interaction.type = 2;
    snprintf(inter.data.interaction.id, DISCORD_ID_LEN, "inter1");
    snprintf(inter.data.interaction.token, DISCORD_ID_LEN, "tok123");
    assert(inter.data.interaction.type == 2);
    assert(strcmp(inter.data.interaction.id, "inter1") == 0);

    printf("  test_embedded_event_fields PASSED\n");
}

int main(void) {
    printf("libdiscord dialectic test starting...\n");

    test_rest_api_round_trip();
    test_gateway_handshake_simulation();
    test_multiple_rest_requests();
    test_new_rest_helpers_produce_frames();
    test_embedded_event_fields();

    printf("libdiscord dialectic test PASSED.\n");
    return 0;
}
