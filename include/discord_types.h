#ifndef DISCORD_TYPES_H
#define DISCORD_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Discord permission flags (future use) */
typedef enum {
    DISCORD_PERM_CREATE_INSTANT_INVITE = (1ULL << 0),
    DISCORD_PERM_KICK_MEMBERS          = (1ULL << 1),
    DISCORD_PERM_BAN_MEMBERS           = (1ULL << 2),
    DISCORD_PERM_ADMINISTRATOR         = (1ULL << 3),
    DISCORD_PERM_MANAGE_CHANNELS       = (1ULL << 4),
    DISCORD_PERM_MANAGE_GUILD          = (1ULL << 5),
    DISCORD_PERM_ADD_REACTIONS         = (1ULL << 6),
    DISCORD_PERM_VIEW_AUDIT_LOG        = (1ULL << 7),
    DISCORD_PERM_SEND_MESSAGES         = (1ULL << 11),
    DISCORD_PERM_MANAGE_MESSAGES       = (1ULL << 13),
    DISCORD_PERM_EMBED_LINKS           = (1ULL << 14),
    DISCORD_PERM_ATTACH_FILES          = (1ULL << 15),
    DISCORD_PERM_READ_MESSAGE_HISTORY  = (1ULL << 16),
    DISCORD_PERM_MENTION_EVERYONE      = (1ULL << 18),
    DISCORD_PERM_USE_EXTERNAL_EMOJIS   = (1ULL << 18),
    DISCORD_PERM_CONNECT               = (1ULL << 20),
    DISCORD_PERM_SPEAK                 = (1ULL << 21),
    DISCORD_PERM_MANAGE_ROLES          = (1ULL << 28),
    DISCORD_PERM_MANAGE_WEBHOOKS       = (1ULL << 29),
} discord_permission_t;

/* Discord channel types */
typedef enum {
    DISCORD_CHANNEL_GUILD_TEXT          = 0,
    DISCORD_CHANNEL_DM                  = 1,
    DISCORD_CHANNEL_GUILD_VOICE         = 2,
    DISCORD_CHANNEL_GROUP_DM            = 3,
    DISCORD_CHANNEL_GUILD_CATEGORY      = 4,
    DISCORD_CHANNEL_GUILD_ANNOUNCEMENT  = 5,
    DISCORD_CHANNEL_GUILD_STORE         = 6,
    DISCORD_CHANNEL_GUILD_STAGE_VOICE   = 13,
    DISCORD_CHANNEL_GUILD_FORUM         = 15,
} discord_channel_type_t;

/* Discord message types */
typedef enum {
    DISCORD_MSG_DEFAULT                     = 0,
    DISCORD_MSG_RECIPIENT_ADD               = 1,
    DISCORD_MSG_RECIPIENT_REMOVE            = 2,
    DISCORD_MSG_CALL                        = 3,
    DISCORD_MSG_CHANNEL_NAME_CHANGE         = 4,
    DISCORD_MSG_CHANNEL_ICON_CHANGE         = 5,
    DISCORD_MSG_CHANNEL_PINNED_MESSAGE      = 6,
    DISCORD_MSG_GUILD_MEMBER_JOIN           = 7,
    DISCORD_MSG_USER_PREMIUM_GUILD_SUB      = 8,
    DISCORD_MSG_USER_PREMIUM_GUILD_TIER_1   = 9,
    DISCORD_MSG_USER_PREMIUM_GUILD_TIER_2   = 10,
    DISCORD_MSG_USER_PREMIUM_GUILD_TIER_3   = 11,
    DISCORD_MSG_CHANNEL_FOLLOW_ADD          = 12,
    DISCORD_MSG_GUILD_DISCOVERY_DISQUALIFIED = 14,
    DISCORD_MSG_GUILD_DISCOVERY_REQUALIFIED  = 15,
    DISCORD_MSG_REPLY                       = 19,
    DISCORD_MSG_APPLICATION_COMMAND         = 20,
    DISCORD_MSG_THREAD_STARTER_MESSAGE      = 21,
    DISCORD_MSG_CHAT_INPUT_COMMAND          = 20,
    DISCORD_MSG_CONTEXT_MENU_COMMAND        = 23,
} discord_message_type_t;

/* Discord Gateway intents (bitfield for identify payload) */
typedef enum {
    DISCORD_INTENT_GUILDS                  = (1 << 0),
    DISCORD_INTENT_GUILD_MEMBERS           = (1 << 1),
    DISCORD_INTENT_GUILD_MODERATION        = (1 << 2),
    DISCORD_INTENT_GUILD_EXPRESSIONS       = (1 << 3),
    DISCORD_INTENT_GUILD_INTEGRATIONS      = (1 << 4),
    DISCORD_INTENT_GUILD_WEBHOOKS          = (1 << 5),
    DISCORD_INTENT_GUILD_INVITES           = (1 << 6),
    DISCORD_INTENT_GUILD_VOICE_STATES      = (1 << 7),
    DISCORD_INTENT_GUILD_PRESENCES         = (1 << 8),
    DISCORD_INTENT_GUILD_MESSAGES          = (1 << 9),
    DISCORD_INTENT_GUILD_MESSAGE_REACTIONS = (1 << 10),
    DISCORD_INTENT_GUILD_MESSAGE_TYPING    = (1 << 11),
    DISCORD_INTENT_DIRECT_MESSAGES         = (1 << 12),
    DISCORD_INTENT_DIRECT_MESSAGE_REACTIONS = (1 << 13),
    DISCORD_INTENT_DIRECT_MESSAGE_TYPING   = (1 << 14),
    DISCORD_INTENT_MESSAGE_CONTENT         = (1 << 15),
    DISCORD_INTENT_GUILD_SCHEDULED_EVENTS  = (1 << 16),
    DISCORD_INTENT_AUTO_MODERATION_CONFIGURATION = (1 << 20),
    DISCORD_INTENT_AUTO_MODERATION_EXECUTION     = (1 << 21),
} discord_intent_t;

#ifdef __cplusplus
}
#endif

#endif /* DISCORD_TYPES_H */
