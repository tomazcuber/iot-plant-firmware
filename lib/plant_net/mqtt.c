#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>

#include <plant/net.h>

LOG_MODULE_DECLARE(plant_net);

#define MQTT_PORT_STR_LEN 6

/* One client per device (SM-41). Buffers are sized by Kconfig. */
static struct mqtt_client client;
static struct sockaddr_storage broker_addr;
static uint8_t rx_buf[CONFIG_PLANT_NET_MQTT_BUFFER_SIZE];
static uint8_t tx_buf[CONFIG_PLANT_NET_MQTT_BUFFER_SIZE];
static bool connected;
static bool connack_seen;
static int connack_result;
/* mqtt_client keeps pointers to these, so they must outlive the call. */
static char client_id_copy[64];

static void mqtt_evt_handler(struct mqtt_client *c, const struct mqtt_evt *evt)
{
	ARG_UNUSED(c);

	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		connack_result = evt->result;
		connack_seen = true;
		connected = (evt->result == 0);
		break;
	case MQTT_EVT_DISCONNECT:
		connected = false;
		break;
	case MQTT_EVT_PINGRESP:
	default:
		break;
	}
}

static int resolve_broker(const char *host)
{
	struct zsock_addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	struct zsock_addrinfo *res = NULL;
	char port[MQTT_PORT_STR_LEN];
	int ret;

	snprintf(port, sizeof(port), "%d", CONFIG_PLANT_NET_MQTT_PORT);
	ret = zsock_getaddrinfo(host, port, &hints, &res);
	if (ret != 0 || res == NULL) {
		LOG_ERR("getaddrinfo(%s) failed (%d)", host, ret);
		return -EHOSTUNREACH;
	}
	memcpy(&broker_addr, res->ai_addr, res->ai_addrlen);
	zsock_freeaddrinfo(res);
	return 0;
}

static int drop_connection(void)
{
	connected = false;
	mqtt_abort(&client);
	return -ENOTCONN;
}

static int wait_socket(int32_t timeout_ms)
{
	struct zsock_pollfd fds = {
		.fd = client.transport.tcp.sock,
		.events = ZSOCK_POLLIN,
	};
	int ret = zsock_poll(&fds, 1, timeout_ms);

	if (ret < 0) {
		return -errno;
	}
	if (ret == 0) {
		return -EAGAIN;
	}
	if (fds.revents & (ZSOCK_POLLERR | ZSOCK_POLLHUP | ZSOCK_POLLNVAL)) {
		return -ENOTCONN;
	}
	return 0;
}

int plant_mqtt_connect(const char *broker_host, const char *client_id,
		       int32_t timeout_ms)
{
	int64_t deadline;
	int ret;

	if (broker_host == NULL || client_id == NULL || timeout_ms <= 0 ||
	    strlen(client_id) >= sizeof(client_id_copy)) {
		return -EINVAL;
	}
	if (connected) {
		return -EALREADY;
	}

	deadline = k_uptime_get() + timeout_ms;

	ret = resolve_broker(broker_host);
	if (ret < 0) {
		return ret;
	}

	strcpy(client_id_copy, client_id);
	mqtt_client_init(&client);
	client.broker = &broker_addr;
	client.evt_cb = mqtt_evt_handler;
	client.client_id.utf8 = (uint8_t *)client_id_copy;
	client.client_id.size = strlen(client_id_copy);
	client.password = NULL;
	client.user_name = NULL;
	client.protocol_version = MQTT_VERSION_3_1_1;
	client.clean_session = 1;
	client.keepalive = CONFIG_MQTT_KEEPALIVE;
	client.rx_buf = rx_buf;
	client.rx_buf_size = sizeof(rx_buf);
	client.tx_buf = tx_buf;
	client.tx_buf_size = sizeof(tx_buf);
	client.transport.type = MQTT_TRANSPORT_NON_SECURE;

	connack_seen = false;
	connack_result = -1;
	connected = false;

	/* Opens the TCP socket, connects and sends CONNECT. */
	ret = mqtt_connect(&client);
	if (ret < 0) {
		LOG_ERR("mqtt_connect failed (%d)", ret);
		return ret;
	}

	while (!connack_seen) {
		int64_t left = deadline - k_uptime_get();

		if (left <= 0) {
			ret = -ETIMEDOUT;
			goto fail;
		}
		ret = wait_socket((int32_t)left);
		if (ret == -EAGAIN) {
			ret = -ETIMEDOUT;
			goto fail;
		}
		if (ret < 0) {
			goto fail;
		}
		ret = mqtt_input(&client);
		if (ret < 0) {
			goto fail;
		}
	}

	if (!connected) {
		LOG_ERR("CONNACK refused (%d)", connack_result);
		ret = -ECONNREFUSED;
		goto fail;
	}
	return 0;

fail:
	mqtt_abort(&client);
	connected = false;
	return ret;
}

int plant_mqtt_publish(const char *topic, const char *payload, size_t len)
{
	struct mqtt_publish_param param = {0};

	if (topic == NULL || (payload == NULL && len > 0)) {
		return -EINVAL;
	}
	if (!connected) {
		return -ENOTCONN;
	}

	param.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE; /* FR-MA-11 */
	param.message.topic.topic.utf8 = (const uint8_t *)topic;
	param.message.topic.topic.size = strlen(topic);
	param.message.payload.data = (uint8_t *)payload;
	param.message.payload.len = len;
	param.message_id = 0;
	param.dup_flag = 0;
	param.retain_flag = 0;

	return mqtt_publish(&client, &param);
}

int plant_mqtt_poll(int32_t timeout_ms)
{
	int32_t wait = timeout_ms;
	int left;
	int ret;

	if (timeout_ms < 0) {
		return -EINVAL;
	}
	if (!connected) {
		return -ENOTCONN;
	}

	/* Never sleep past the moment a PINGREQ is due. */
	left = mqtt_keepalive_time_left(&client);
	if (left >= 0 && left < wait) {
		wait = left;
	}

	ret = wait_socket(wait);
	if (ret == 0) {
		ret = mqtt_input(&client);
		if (ret < 0) {
			return drop_connection();
		}
	} else if (ret != -EAGAIN) {
		return drop_connection();
	}

	/* A PINGREQ still unanswered when the next one is due means the link is
	 * silently dead (the write alone only reaches the TCP buffer). */
	if (client.unacked_ping > 0 && mqtt_keepalive_time_left(&client) == 0) {
		return drop_connection();
	}

	ret = mqtt_live(&client);
	if (ret == -EAGAIN) {
		/* Keepalive not due yet. */
		ret = 0;
	}
	if (ret < 0) {
		return drop_connection();
	}
	return connected ? 0 : -ENOTCONN;
}

int plant_mqtt_disconnect(void)
{
	int ret;

	if (!connected) {
		return -ENOTCONN;
	}
	ret = mqtt_disconnect(&client, NULL);
	connected = false;
	if (ret < 0) {
		/* Make sure the socket is released even if DISCONNECT failed. */
		mqtt_abort(&client);
		return ret;
	}
	return 0;
}
