/* 
 * Copyright (c) 2014 Scott Vokes <vokes.s@gmail.com>
 *  
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *  
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <err.h>
#include <sys/types.h>

#include "mosquitto.h"

/* The linked code creates a client that connects to a broker at
 * localhost:1883, subscribes to the topics "tick", "control/#{PID}",
 * and "control/all", and publishes its process ID and uptime (in
 * seconds) on "tock/#{PID}" every time it gets a "tick" message. If the
 * message "halt" is sent to either "control" endpoint, it will
 * disconnect, free its resources, and exit. */

#ifdef DEBUG
#define LOG(...) do { printf(__VA_ARGS__); } while (0)
#else
#define LOG(...)
#endif

/* How many seconds the broker should wait between sending out
 * keep-alive messages. */
#define KEEPALIVE_SECONDS 60

/* Hostname and port for the MQTT broker. */
#define BROKER_HOSTNAME "localhost"
#define BROKER_PORT	1883
#define PUBLISH_TOPIC	"test_topic"
#define PUBLISH_MSG_QOS 0
static bool connected = true;
static int mid_sent = 0;

struct client_info {
    struct mosquitto *m;
    pid_t pid;
    uint32_t tick_ct;
};

static void die(const char *msg);
static struct mosquitto *init(struct client_info *info);
static bool set_callbacks(struct mosquitto *m);
static bool connect(struct mosquitto *m);
static int run_loop(struct client_info *info);

int main(int argc, char **argv) {
    pid_t pid = getpid();

    struct client_info info;
    memset(&info, 0, sizeof(info));
    info.pid = pid;

    struct mosquitto *m = init(&info);
    if (m == NULL) { die("init() failure\n"); }
    info.m = m;

    if (!set_callbacks(m)) { die("set_callbacks() failure\n"); }

    /* client_opts_set: set up the configuration */

    if (!connect(m)) { die("connect() failure\n"); }

    return run_loop(&info);
}

/* Fail with an error message. */
static void die(const char *msg) {
    fprintf(stderr, "%s", msg);
    exit(1);
}

/* Initialize a mosquitto client. */
static struct mosquitto *init(struct client_info *info) {
    void *udata = (void *)info;
    size_t buf_sz = 32;
    char buf[buf_sz];
    if (buf_sz < snprintf(buf, buf_sz, "client_%d", info->pid)) {
        return NULL;            /* snprintf buffer failure */
    }

    mosquitto_lib_init();

    /* Create a new mosquitto client, with the name "client_#{PID}". */
    struct mosquitto *m = mosquitto_new(buf, true, udata);

    return m;
}

/* Callback for successful connection: add subscriptions. */
static void on_connect(struct mosquitto *m, void *udata, int res) {
    int rc = MOSQ_ERR_SUCCESS;
    if (res == 0) {             /* success */
	LOG("-- connect successfully\n");
    } else {
        die("connection refused\n");
    }
}

/* A message was successfully published. */
static void on_publish(struct mosquitto *m, void *udata, int m_id) {
    LOG("-- published successfully\n");
}

static bool match(const char *topic, const char *key) {
    return 0 == strncmp(topic, key, strlen(key));
}

/* Handle a message that just arrived via one of the subscriptions. */
static void on_message(struct mosquitto *m, void *udata,
                       const struct mosquitto_message *msg) {
    if (msg == NULL) { return; }
    LOG("-- got message @ %s: (%d, QoS %d, %s) '%s'\n",
        msg->topic, msg->payloadlen, msg->qos, msg->retain ? "R" : "!r",
        msg->payload);

    struct client_info *info = (struct client_info *)udata;

#if 0
    if (match(msg->topic, "tick")) {
        if (0 == strncmp(msg->payload, "tick", msg->payloadlen)) {
        } else {
            LOG("invalid 'tick' message\n");
        }
    } else if (match(msg->topic, "control/")) {
        /* This will cover both "control/all" and "control/$(PID)".
         * We won'st see "control/$(OTHER_PID)" because we won't be
         * subscribed to them.*/
        if (0 == strncmp(msg->payload, "halt", msg->payloadlen)) {
            LOG("*** halt\n");
            (void)mosquitto_disconnect(m);
        }
    }
#endif
}

/* Successful subscription hook. */
static void on_subscribe(struct mosquitto *m, void *udata, int mid,
                         int qos_count, const int *granted_qos) {
    LOG("-- subscribed successfully\n");
}

static void on_disconnect(struct mosquitto *m, void *udata, int res)
{
    LOG("-- disconnected callback\n");
    connected = false;
}

/* Register the callbacks that the mosquitto connection will use. */
static bool set_callbacks(struct mosquitto *m) {
    mosquitto_connect_callback_set(m, on_connect);
    mosquitto_publish_callback_set(m, on_publish);
    mosquitto_subscribe_callback_set(m, on_subscribe);
    mosquitto_message_callback_set(m, on_message);
    mosquitto_disconnect_callback_set(m, on_disconnect);
    return true;
}

/* Connect to the network. */
static bool connect(struct mosquitto *m) {
    int res = mosquitto_connect(m, BROKER_HOSTNAME, BROKER_PORT, KEEPALIVE_SECONDS);
    return res == MOSQ_ERR_SUCCESS;
}

/* Loop until it is explicitly halted or the network is lost, then clean up. */
static int run_loop(struct client_info *info) {
    int res = MOSQ_ERR_SUCCESS;
    int i = 0;
    char buf[1024];

    memset(buf, 0, sizeof(buf));
    do {
	res = mosquitto_loop(info->m, -1, 1 /* unused */);
	if (res != MOSQ_ERR_SUCCESS) break;
	sleep(5);
	memset(buf, 0, sizeof(buf));
	sprintf(buf, "test_msg#%d\n", i++);
	res = mosquitto_publish(info->m, &mid_sent, PUBLISH_TOPIC,
		      strlen(buf), buf, PUBLISH_MSG_QOS, 0);
    }while(res == MOSQ_ERR_SUCCESS && connected);

    mosquitto_destroy(info->m);
    (void)mosquitto_lib_cleanup();

    if (res == MOSQ_ERR_SUCCESS) {
        return 0;
    } else {
        return 1;
    }
}
