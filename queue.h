//
// Created by archuser on 05.10.25.
//

#ifndef QUEUE_H
#define QUEUE_H

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

#endif //QUEUE_H

