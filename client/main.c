#define _GNU_SOURCE /* for asprintf */
#include <libwebsockets.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
/* for camera API */
#include "app.h"

#include "queue.h"
// --- Config ---
#define SECRET_WS_KEY "kdow04sd3"
#define STATUS_SEND_INTERVAL 10
#define RECONNECT_INTERVAL   10
#define MAX_RECONN_ATTEMPTS  -1      // -1 = infinity, 0 = do not reconnect

// --- Global ---
static struct lws_context_creation_info Info;
static struct lws_context *Context = NULL;
static struct lws_client_connect_info Connect_info;
static struct lws *Client_wsi = NULL;

static int reconnect_attempts = 0;
static struct lws_sorted_usec_list sul_reconnect;

/* declaration */
static int connect_to_server(struct lws **wsi);
static void schedule_reconnect(void);
static void sul_reconnect_cb(struct lws_sorted_usec_list *sul);

/** Callback for WebSocket **/
static int callback_client(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len);

typedef struct {
} session_data;
/**
 * @brief Try to connect to server
 * @return 0 success, -1 fail
 */
static int connect_to_server(struct lws **wsi){
    *wsi = lws_client_connect_via_info(&Connect_info);
    if (*wsi != NULL) {
        return 0;
    }
    return -1;
}

/**
 * @brief Plans reconnecting after RECONNECT_INTERVAL seconds
 */
static void schedule_reconnect(void)
{
    if (MAX_RECONN_ATTEMPTS > 0 && reconnect_attempts >= MAX_RECONN_ATTEMPTS) {
        lwsl_err("Max reconnect attempts (%d) reached. Giving up.\n", MAX_RECONN_ATTEMPTS);
        return;
    }

    lwsl_notice("Reconnecting in %d seconds... (attempt %d)\n", RECONNECT_INTERVAL, reconnect_attempts + 1);

    lws_sul_schedule(Context, 0, &sul_reconnect, sul_reconnect_cb, RECONNECT_INTERVAL * LWS_USEC_PER_SEC);
    reconnect_attempts++;
}

/**
 * @brief Callback for timer
 */
static void sul_reconnect_cb(struct lws_sorted_usec_list *sul)
{
    struct lws *new_wsi = lws_client_connect_via_info(&Connect_info);
    if (!new_wsi) {
        lwsl_err("Failed to reconnect\n");
        schedule_reconnect();
    } else {
        Client_wsi = new_wsi;
        lwsl_notice("Reconnected successfully!\n");
        reconnect_attempts = 0;
    }
}

static struct lws_protocols protocols[] = {
    {
        .name = "camera-control",
        .callback = callback_client,
        .rx_buffer_size = 0,
        .id = 0,
        .user = NULL,
        .tx_packet_size = 0
    },
    { NULL, NULL, 0, 0, 0, NULL, 0 }
};

static int callback_client(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len)
{

    switch (reason) {

        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            lwsl_info("Success! Connected to server!\n");
            lws_set_timer_usecs(wsi, STATUS_SEND_INTERVAL * LWS_USEC_PER_SEC);

            char *json = NULL;
            asprintf(&json, "{\"type\": \"onconnection\", \"key\": \"%s\"}", SECRET_WS_KEY);
            QUEUE_NewMsg(json);
            free(json);
            lws_callback_on_writable(wsi);
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE:
            lwsl_info("Got message from server: %.*s\n", (int)len, (char*)in);

            handle_server_command(in, len);
            lws_callback_on_writable(wsi);
            break;

        case LWS_CALLBACK_CLIENT_WRITEABLE:
            ; // this is not useless. If you remove it code will not be compiled. I don't like it too.
            msg_queue_item_t *ItemToSend = QUEUE_PopItem();
            if (NULL == ItemToSend) {
                break;
            }

            /* Unfortunately libwebsocket has a disadvantage - it can not send several messages. This lib
            * would not handle different messages, it just collect it in one buffer so we should do the way it is done here */
            unsigned char *buf = malloc(LWS_PRE + ItemToSend->len);
            if (buf) {
                memcpy(buf + LWS_PRE, ItemToSend->payload, ItemToSend->len);
                int n = lws_write(wsi, buf + LWS_PRE, ItemToSend->len,
                ItemToSend->type == MSG_TYPE_BINARY ? LWS_WRITE_BINARY : LWS_WRITE_TEXT);
                free(buf);

                if (n < 0) {
                    lwsl_err("Write failed\n");
                    // Not sure what t do
                }
            }

            free(ItemToSend->payload);
            free(ItemToSend);

            /* calls it again until messages are over */
            lws_callback_on_writable(wsi);
            break;

        case LWS_CALLBACK_TIMER:
            lwsl_info("Timer fired - sending status...\n");

            get_camera_status(); // goes to queue

            lws_set_timer_usecs(wsi, STATUS_SEND_INTERVAL * LWS_USEC_PER_SEC);
            lws_callback_on_writable(wsi);
            break;

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            lwsl_err("Connection error: %s\n", in ? (char*)in : "unknown");
            schedule_reconnect();
            break;

        case LWS_CALLBACK_CLOSED:
            lwsl_info("WebSocket closed by peer\n");
            schedule_reconnect();
            break;

        case LWS_CALLBACK_WSI_DESTROY:
            lwsl_info("WebSocket WSI destroyed\n");
            schedule_reconnect();
            break;

        default:
            break;
    }
    return 0;
}

static volatile sig_atomic_t force_exit = 0;

static void sigint_handler(int sig) {
    force_exit = 1;
}

int main(void) {
    signal(SIGINT, sigint_handler);

    //lws_set_log_level(LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_INFO, NULL);

    memset(&Info, 0, sizeof(Info));
    Info.port = CONTEXT_PORT_NO_LISTEN;
    Info.protocols = protocols;
    Info.gid = -1;
    Info.uid = -1;
    Info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    Context = lws_create_context(&Info);
    if (!Context) {
        fprintf(stderr, "Error creating websocket context!\n");
        return -1;
    }
    const char *server_ip = "45.151.62.161"; // "localhost";
    const int port = 80;
    const char *path = "/ws/raspberry";

    memset(&Connect_info, 0, sizeof(Connect_info));
    Connect_info.context = Context;
    Connect_info.address = server_ip;
    Connect_info.port = port;
    Connect_info.path = path;
    Connect_info.host = server_ip;
    Connect_info.origin = server_ip;
    Connect_info.protocol = protocols[0].name;
    Connect_info.ssl_connection = 0;

    if (connect_to_server(&Client_wsi) != 0) {
        lwsl_err("Initial connection failed. Will retry via timer...\n");
        schedule_reconnect();
    } else {
        lwsl_info("Initial connection established\n");
    }

    lwsl_notice("Starting event loop...\n");
    while (!force_exit) {
        lws_service(Context, 50); // 50ms timeout
    }

    lwsl_notice("Exiting...\n");
    lws_context_destroy(Context);

    return 0;
}
