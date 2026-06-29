/* discord_bot_example.c
 * Pure-C example for libdiscord (dialectic style).
 * All networking / I/O lives in the application; library only emits events.
 *
 * Demonstrates:
 *   - Creating a Discord bot context with token and config
 *   - Preparing a REST API request (create message)
 *   - Establishing Gateway connection (WebSocket)
 *   - Sending identify and heartbeat payloads
 *   - Consuming events from the library
 *
 * Note: This example shows the API usage pattern. In a real application,
 * the caller would handle network I/O (sending output bytes over TCP/TLS,
 * receiving response bytes, and feeding them back into the library).
 */

#include "discord.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static void print_event(const discord_event_t *ev) {
    switch (ev->type) {
        case DISCORD_EVENT_READY:
            printf("  Event: READY user_id=%s session_id=%s\n",
                   ev->data.ready.user_id[0] ? ev->data.ready.user_id : "?",
                   ev->data.ready.session_id[0] ? ev->data.ready.session_id : "?");
            break;
        case DISCORD_EVENT_CONNECTED:
            printf("  Event: CONNECTED (binary channel established)\n");
            break;
        case DISCORD_EVENT_DISCONNECTED:
            printf("  Event: DISCONNECTED\n");
            break;
        case DISCORD_EVENT_HEARTBEAT_REQUEST:
            printf("  Event: HEARTBEAT_REQUEST — send heartbeat now\n");
            break;
        case DISCORD_EVENT_HEARTBEAT_ACK:
            printf("  Event: HEARTBEAT_ACK — server confirmed heartbeat\n");
            break;
        case DISCORD_EVENT_RECONNECT_REQUEST:
            printf("  Event: RECONNECT_REQUEST — server asks us to reconnect\n");
            break;
        case DISCORD_EVENT_MESSAGE_CREATE:
            printf("  Event: MESSAGE_CREATE id=%s channel=%s content=%s\n",
                   ev->data.message.id[0] ? ev->data.message.id : "?",
                   ev->data.message.channel_id[0] ? ev->data.message.channel_id : "?",
                   ev->data.message.content[0] ? ev->data.message.content : "");
            break;
        case DISCORD_EVENT_API_RESPONSE:
            printf("  Event: API_RESPONSE status=%d body_len=%zu%s\n",
                   ev->data.api_response.status_code,
                   ev->data.api_response.body_len,
                   ev->data.api_response.truncated ? " (truncated)" : "");
            if (ev->data.api_response.json_body[0] && ev->data.api_response.body_len > 0) {
                printf("  Body: %.*s\n", (int)ev->data.api_response.body_len,
                       ev->data.api_response.json_body);
            }
            break;
        case DISCORD_EVENT_GUILD_CREATE:
            printf("  Event: GUILD_CREATE\n");
            break;
        case DISCORD_EVENT_INTERACTION_CREATE:
            printf("  Event: INTERACTION_CREATE id=%s type=%d\n",
                   ev->data.interaction.id[0] ? ev->data.interaction.id : "?",
                   ev->data.interaction.type);
            break;
        case DISCORD_EVENT_CHANNEL_CREATE:
            printf("  Event: CHANNEL_CREATE\n");
            break;
        case DISCORD_EVENT_GUILD_MEMBER_ADD:
            printf("  Event: GUILD_MEMBER_ADD\n");
            break;
        case DISCORD_EVENT_ERROR:
            printf("  Event: ERROR: %s\n",
                   ev->error_msg[0] ? ev->error_msg : "unknown");
            break;
        default:
            printf("  Event: type=%d\n", ev->type);
            break;
    }
}

static void print_frames(const uint8_t *buf, int len) {
    int pos = 0;
    while (pos + 9 <= len) {
        uint32_t flen = ((uint32_t)buf[pos] << 16) |
                        ((uint32_t)buf[pos+1] << 8) |
                        (uint32_t)buf[pos+2];
        uint8_t ftype = buf[pos+3];
        uint32_t sid = ((uint32_t)(buf[pos+5] & 0x7f) << 24) |
                       ((uint32_t)buf[pos+6] << 16) |
                       ((uint32_t)buf[pos+7] << 8) |
                       (uint32_t)buf[pos+8];
        const char *type_name = "UNKNOWN";
        switch (ftype) {
            case 0x0: type_name = "DATA"; break;
            case 0x1: type_name = "HEADERS"; break;
            case 0x4: type_name = "SETTINGS"; break;
            case 0x7: type_name = "GOAWAY"; break;
        }
        printf("  Frame: %s stream=%u len=%u\n", type_name, sid, flen);
        pos += 9 + (int)flen;
    }
}

int main(void) {
    printf("=== libdiscord bot example ===\n\n");

    /* Step 1: Create a bot context with token */
    discord_config_t cfg = {
        .event_queue_size = 64,
        .bot_token = "YOUR_BOT_TOKEN_HERE",
        .api_base_url = NULL,   /* use default https://discord.com/api/v10 */
        .gateway_url = NULL,    /* use default wss://gateway.discord.gg/?v=10&encoding=json */
        .intents = 0,           /* 0 = all non-privileged intents */
        .shard_id = 0,
        .num_shards = 0
    };

    discord_ctx_t *bot = discord_create_with_config(DISCORD_ROLE_CLIENT, &cfg);
    if (!bot) {
        fprintf(stderr, "Failed to create bot context\n");
        return 1;
    }

    printf("Bot context created successfully.\n");
    printf("Gateway state: %d\n\n", discord_gateway_state(bot));

    /* Step 2: Prepare a REST API request (create message) */
    printf("--- Creating a message via REST API ---\n");
    int rc = discord_create_message(bot, "123456789", "Hello from libdiscord!");
    if (rc != 0) {
        fprintf(stderr, "Failed to create message request\n");
    }

    /* Consume events */
    discord_event_t ev;
    while (discord_next_event(bot, &ev) == 1) {
        print_event(&ev);
    }

    /* Extract HTTP/2 frames */
    uint8_t send_buf[8192];
    int send_len = discord_get_output(bot, send_buf, sizeof(send_buf));
    if (send_len > 0) {
        printf("\nGenerated HTTP/2 frames (%d bytes):\n", send_len);
        print_frames(send_buf, send_len);
    }

    /* Step 3: Prepare Gateway connection */
    printf("\n--- Establishing Gateway connection ---\n");
    rc = discord_gateway_connect(bot);
    if (rc != 0) {
        fprintf(stderr, "Failed to initiate gateway connect\n");
    }

    while (discord_next_event(bot, &ev) == 1) {
        print_event(&ev);
    }

    send_len = discord_get_output(bot, send_buf, sizeof(send_buf));
    if (send_len > 0) {
        printf("\nGateway HTTP/2 frames (%d bytes):\n", send_len);
        print_frames(send_buf, send_len);
    }

    /* Step 4: Send identify */
    printf("\n--- Sending Gateway identify ---\n");
    rc = discord_gateway_send_identify(bot);
    if (rc != 0) {
        fprintf(stderr, "Failed to send identify\n");
    }

    while (discord_next_event(bot, &ev) == 1) {
        print_event(&ev);
    }

    /* Step 5: Send heartbeat (caller passes monotonic timestamp) */
    printf("\n--- Sending Gateway heartbeat ---\n");
    uint64_t now_ms = 1000; /* In real app, use clock_gettime or equivalent */
    rc = discord_gateway_send_heartbeat(bot, -1, now_ms);
    if (rc != 0) {
        fprintf(stderr, "Failed to send heartbeat\n");
    }

    /* Step 6: Check heartbeat timing */
    printf("\n--- Heartbeat timing check ---\n");
    printf("Should send heartbeat now? %s\n",
           discord_gateway_process_heartbeat(bot, now_ms) ? "yes" : "no");
    printf("Should send after interval? %s\n",
           discord_gateway_process_heartbeat(bot, now_ms + 41250) ? "yes" : "no");

    printf("\n--- Usage notes ---\n");
    printf("In a real application:\n");
    printf("  1. Send output bytes over TLS to Discord's API/Gateway\n");
    printf("  2. Receive response bytes and feed them back via discord_feed_input\n");
    printf("  3. Process events from discord_next_event\n");
    printf("  4. Call discord_gateway_process_heartbeat(ctx, now_ms) periodically\n");
    printf("  5. Use discord_gateway_send_resume to resume after disconnection\n");
    printf("  6. Use discord_gateway_state(ctx) to check connection state\n");

    discord_destroy(bot);
    printf("\nBot example finished.\n");
    return 0;
}
