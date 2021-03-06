#include "esp8266.h"
#include "sscp.h"

static void dns_cb(const char *name, ip_addr_t *ipaddr, void *arg);
static void tcp_connect_cb(void *arg);
static void tcp_discon_cb(void *arg);
static void tcp_recv_cb(void *arg, char *data, unsigned short len);
static void tcp_sent_cb(void *arg);
static void tcp_recon_cb(void *arg, sint8 errType);

static void send_handler(sscp_hdr *hdr, int size);
static void recv_handler(sscp_hdr *hdr, int size);
static void close_handler(sscp_hdr *hdr);

static sscp_dispatch tcpDispatch = {
    .path = NULL,
    .send = send_handler,
    .recv = recv_handler,
    .close = close_handler
};

void ICACHE_FLASH_ATTR tcp_do_connect(int argc, char *argv[])
{
    sscp_connection *c;
    struct espconn *conn;
    ip_addr_t ipAddr;

    if (argc != 3) {
        sscp_sendResponse("E,%d", SSCP_ERROR_WRONG_ARGUMENT_COUNT);
        return;
    }
    
    // allocate a connection
    if (!(c = sscp_allocate_connection(TYPE_TCP_CONNECTION, &tcpDispatch))) {
        sscp_sendResponse("E,%d", SSCP_ERROR_NO_FREE_CONNECTION);
        return;
    }
    conn = &c->d.tcp.conn;

    os_memset(&c->d.tcp, 0, sizeof(c->d.tcp));
    conn->type = ESPCONN_TCP;
    conn->state = ESPCONN_NONE;
    conn->proto.tcp = &c->d.tcp.tcp;
    conn->proto.tcp->remote_port = atoi(argv[2]);
    conn->reverse = (void *)c;

    espconn_regist_connectcb(conn, tcp_connect_cb);
    espconn_regist_reconcb(conn, tcp_recon_cb);

    if (isdigit((int)*argv[1]))
        ipAddr.addr = ipaddr_addr(argv[1]);
    else {
        switch (espconn_gethostbyname(conn, argv[1], &ipAddr, dns_cb)) {
        case ESPCONN_OK:
            // connect below
            break;
        case ESPCONN_INPROGRESS:
            // response is sent by tcp_connect_cb or tcp_recon_cb
            os_printf("TCP: looking up '%s'\n", argv[1]);
            return;
        default:
            sscp_sendResponse("E,%d", SSCP_ERROR_LOOKUP_FAILED);
            return;
        }
    }
    
    memcpy(conn->proto.tcp->remote_ip, &ipAddr.addr, 4);

    if (espconn_connect(conn) != ESPCONN_OK) {
        sscp_sendResponse("E,%d", SSCP_ERROR_CONNECT_FAILED);
        return;
    }

    // response is sent by tcp_connect_cb or tcp_recon_cb
    c->d.tcp.state = TCP_STATE_CONNECTING;
}

static void ICACHE_FLASH_ATTR dns_cb(const char *name, ip_addr_t *ipaddr, void *arg)
{
    struct espconn *conn = (struct espconn *)arg;
    sscp_connection *c = (sscp_connection *)conn->reverse;

    if (!ipaddr) {
        os_printf("TCP: no IP address found for '%s'\n", name);
        sscp_sendResponse("E,%d", SSCP_ERROR_LOOKUP_FAILED);
        return;
    }
    
    os_printf("TCP: found IP address %d.%d.%d.%d for '%s'\n",
                *((uint8 *)&ipaddr->addr),
                *((uint8 *)&ipaddr->addr + 1),
                *((uint8 *)&ipaddr->addr + 2),
                *((uint8 *)&ipaddr->addr + 3),
                name);
                
    os_memcpy(conn->proto.tcp->remote_ip, &ipaddr->addr, 4);
    
    if (espconn_connect(conn) != ESPCONN_OK) {
        sscp_close_connection(c);
        sscp_sendResponse("E,%d", SSCP_ERROR_CONNECT_FAILED);
    }
    
    // response is sent by tcp_connect_cb or tcp_recon_cb
    c->d.tcp.state = TCP_STATE_CONNECTING;
}

static void ICACHE_FLASH_ATTR tcp_connect_cb(void *arg)
{
    struct espconn *conn = (struct espconn *)arg;
    sscp_connection *c = (sscp_connection *)conn->reverse;

    espconn_regist_disconcb(conn, tcp_discon_cb);
    espconn_regist_recvcb(conn, tcp_recv_cb);
    espconn_regist_sentcb(conn, tcp_sent_cb);

    c->d.tcp.state = TCP_STATE_CONNECTED;
    sscp_sendResponse("S,%d", c->hdr.handle);
}

static void ICACHE_FLASH_ATTR tcp_discon_cb(void *arg)
{
    struct espconn *conn = (struct espconn *)arg;
    sscp_connection *c = (sscp_connection *)conn->reverse;
    c->flags |= CONNECTION_TERM;
    os_printf("TCP: %d disconnected\n", c->hdr.handle);
    c->d.tcp.state = TCP_STATE_IDLE;
}

static void ICACHE_FLASH_ATTR tcp_recv_cb(void *arg, char *data, unsigned short len)
{
    struct espconn *conn = (struct espconn *)arg;
    sscp_connection *c = (sscp_connection *)conn->reverse;
    os_printf("TCP: %d received %d bytes\n", c->hdr.handle, len);
    if (!(c->flags & CONNECTION_RXFULL)) {
        if (len > SSCP_RX_BUFFER_MAX)
            len = SSCP_RX_BUFFER_MAX;
        os_memcpy(c->rxBuffer, data, len);
        c->rxCount = len;
        c->rxIndex = 0;
        c->flags |= CONNECTION_RXFULL;
    }
}

static void ICACHE_FLASH_ATTR tcp_recon_cb(void *arg, sint8 errType)
{
    struct espconn *conn = (struct espconn *)arg;
    sscp_connection *c = (sscp_connection *)conn->reverse;

    c->d.tcp.state = TCP_STATE_IDLE;
    sscp_sendResponse("E,%d", SSCP_ERROR_DISCONNECTED);
}

static void ICACHE_FLASH_ATTR tcp_sent_cb(void *arg)
{
    struct espconn *conn = (struct espconn *)arg;
    sscp_connection *c = (sscp_connection *)conn->reverse;
    c->flags &= ~CONNECTION_TXFULL;
    sscp_sendResponse("S,0");
}

static void ICACHE_FLASH_ATTR send_cb(void *data, int count)
{
    sscp_connection *c = (sscp_connection *)data;
    struct espconn *conn = &c->d.tcp.conn;
    if (espconn_send(conn, (uint8 *)c->txBuffer, count) != ESPCONN_OK) {
        c->flags &= ~CONNECTION_TXFULL;
        sscp_sendResponse("E,%d", SSCP_ERROR_SEND_FAILED);
    }
}

static void ICACHE_FLASH_ATTR send_handler(sscp_hdr *hdr, int size)
{
    sscp_connection *c = (sscp_connection *)hdr;
    
    if (c->d.tcp.state != TCP_STATE_CONNECTED) {
        sscp_sendResponse("E,%d", SSCP_ERROR_INVALID_STATE);
        return;
    }
    
    if (size == 0)
        sscp_sendResponse("S,0");
    else {
        // response is sent by tcp_sent_cb
        sscp_capturePayload(c->txBuffer, size, send_cb, c);
        c->flags |= CONNECTION_TXFULL;
    }
}

static void ICACHE_FLASH_ATTR recv_handler(sscp_hdr *hdr, int size)
{
    sscp_connection *connection = (sscp_connection *)hdr;
    
    if (!(connection->flags & CONNECTION_RXFULL)) {
        sscp_sendResponse("S,0");
        return;
    }

    if (connection->rxIndex + size > connection->rxCount)
        size = connection->rxCount - connection->rxIndex;

    sscp_sendResponse("S,%d", size);
    if (size > 0) {
        sscp_sendPayload(connection->rxBuffer + connection->rxIndex, size);
        connection->rxIndex += size;
    }
    
    if (connection->rxIndex >= connection->rxCount)
        connection->flags &= ~CONNECTION_RXFULL;
}

static void ICACHE_FLASH_ATTR close_handler(sscp_hdr *hdr)
{
    sscp_connection *connection = (sscp_connection *)hdr;
    struct espconn *conn = &connection->d.tcp.conn;
    if (conn)
        espconn_disconnect(conn);
}
