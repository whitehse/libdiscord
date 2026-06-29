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
                   ev->data.ready.user_id ? ev->data.ready.user_id : "?",
                   ev->data.ready.session_id ? ev->data.ready.session_id : "?");
            break;
        case DISCORD_EVENT_CONNECTED:
            printf("  Event: CONNECTED (binary channel established)\n");
            break;
        case DISCORD_EVENT_DISCONNECTED:
            printf("  Event: DISCONNECTED\n");
            break;
        case DISCORD_EVENT_MESSAGE_CREATE:
            printf("  Event: MESSAGE_CREATE id=%s channel=%s content=%s\n",
                   ev->data.message.id ? ev->data.message.id : "?",
                   ev->data.message.channel_id ? ev->data.message.channel_id : "?",
                   ev->data.message.content ? ev->data.message.content : "");
            break;
        case DISCORD_EVENT_API_RESPONSE:
            printf("  Event: API_RESPONSE status=%d body_len=%zu\n",
                   ev->data.api_response.status_code,
                   ev->data.api_response.body_len);
            if (ev->data.api_response.json_body && ev->data.api_response.body_len > 0) {
                printf("  Body: %.*s\n", (int)ev->data.api_response.body_len,
                       ev->data.api_response.json_body);
            }
            break;
        case DISCORD_EVENT_GUILD_CREATE:
            printf("  Event: GUILD_CREATE\n");
            break;
        case DISCORD_EVENT_INTERACTION_CREATE:
            printf("  Event: INTERACTION_CREATE\n");
            break;
        case DISCORD_EVENT_ERROR:
            printf("  Event: ERROR: %s\n",
                   ev->error_msg ? ev->error_msg : "unknown");
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
        .gateway_url = NULL     /* use default wss://gateway.discord.gg/?v=10&encoding=json */
    };

    discord_ctx_t *bot = discord_create_with_config(DISCORD_ROLE_CLIENT, &cfg);
    if (!bot) {
        fprintf(stderr, "Failed to create bot context\n");
        return 1;
    }

    printf("Bot context created successfully.\n\n");

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

    /* Step 5: Send heartbeat */
    printf("\n--- Sending Gateway heartbeat ---\n");
    rc = discord_gateway_send_heartbeat(bot, -1);
    if (rc != 0) {
        fprintf(stderr, "Failed to send heartbeat\n");
    }

    printf("\n--- Usage notes ---\n");
    printf("In a real application:\n");
    printf("  1. Send output bytes over TLS to Discord's API/Gateway\n");
    printf("  2. Receive response bytes and feed them back via discord_feed_input\n");
    printf("  3. Process events from discord_next_event\n");
    printf("  4. Call discord_gateway_process_heartbeat periodically to send heartbeats\n");
    printf("  5. Use discord_gateway_send_resume to resume after disconnection\n");

    discord_destroy(bot);
    printf("\nBot example finished.\n");
    return 0;
}
