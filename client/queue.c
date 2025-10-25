//
// Created by archuser on 05.10.25.
//


/** This is file which gives an API for queue of messages (read header) **/
#include <stdio.h>
#include "queue.h"

#include <stdlib.h>
#include <string.h>

static msg_queue_item_t *QUEUE_Head = NULL;

void QUEUE_NewMsg(const char *text) {
    if (NULL == text) {
        return;
    }
    size_t len = strlen(text);
    msg_queue_item_t *item = malloc(sizeof(msg_queue_item_t));

    if (NULL == item) return;

    item->payload = malloc(len + 1);
    if (NULL == item->payload) {
        free(item);
        return;
    }
    memcpy(item->payload, text, len + 1);
    item->len = len;
    item->type = MSG_TYPE_TEXT;
    item->next = NULL;

    if (NULL == QUEUE_Head) {
        QUEUE_Head = item;
    } else {
        msg_queue_item_t *cur = QUEUE_Head;
        while (cur->next) cur = cur->next;
        cur->next = item;
    }

}
void QUEUE_NewBinary(const unsigned char *data, size_t len);

msg_queue_item_t* QUEUE_PopItem() {
    msg_queue_item_t* item = QUEUE_Head;
    if (item != NULL) {
        QUEUE_Head = item->next;
    }
    return item;
}






