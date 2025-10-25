#ifndef QUEUE_H
#define QUEUE_H

/** This is header for queue, which is really simply-designed in aims to avoid the situation then sending buffer is too big
 * for libwebsocket and some messages can be rewritten by others
 **/

#ifdef __cplusplus
extern "C" {
#endif

/* Queue for messages */
typedef enum {
    MSG_TYPE_TEXT,
    MSG_TYPE_BINARY
} message_type_t;

typedef struct msg_queue_item {
    message_type_t type;
    unsigned char *payload;
    size_t len;
    struct msg_queue_item *next;
} msg_queue_item_t;

void QUEUE_NewMsg(const char *text);
void QUEUE_NewBinary(const unsigned char *data, size_t len);
msg_queue_item_t* QUEUE_PopItem();

#ifdef __cplusplus
}
#endif

#endif //QUEUE_H

