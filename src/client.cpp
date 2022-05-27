#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include <list>
#include <memory>
#include <string>

#include <nghttp2/nghttp2.h>

#define ARRLEN(x) (sizeof(x) / sizeof(x[0]))

#define MAKE_NV(NAME, VALUE)                                                   \
  {                                                                            \
    (uint8_t *)NAME, (uint8_t *)VALUE, sizeof(NAME) - 1, sizeof(VALUE) - 1,    \
        NGHTTP2_NV_FLAG_NONE                                                   \
  }

constexpr size_t port = 8080;

struct http2_stream_data {
  int32_t stream_id;

  http2_stream_data(int32_t stream_id) : stream_id(stream_id) {}
};

struct http2_session_data {
  std::list<std::shared_ptr<http2_stream_data>> streams;
  nghttp2_session *session;
  int socket;

  http2_session_data(int socket) : socket(socket) {}
};

static ssize_t send_callback(nghttp2_session *session, const uint8_t *data,
                             size_t length, int flags, void *user_data) {
  http2_session_data *session_data = (http2_session_data *)user_data;
  std::cout << "Send callback - len: " << length << std::endl;
  send(session_data->socket, data, length, 0);
  return (ssize_t)length;
}

static int on_frame_recv_callback(nghttp2_session *session,
                                  const nghttp2_frame *frame, void *user_data) {
  std::cout << "on_frame_recv_callback" << std::endl;
  http2_session_data *session_data = (http2_session_data *)user_data;
  switch (frame->hd.type) {
  case NGHTTP2_HEADERS:
    if (frame->headers.cat == NGHTTP2_HCAT_RESPONSE &&
        session_data->streams.front()->stream_id == frame->hd.stream_id) {
      std::cout << "All headers received" << std::endl;
    }
    break;
  }
  return 0;
}

static int on_stream_close_callback(nghttp2_session *session, int32_t stream_id,
                                    uint32_t error_code, void *user_data) {
  http2_session_data *session_data = (http2_session_data *)user_data;
  http2_stream_data *stream_data;
  (void)error_code;

  stream_data = (http2_stream_data *)nghttp2_session_get_stream_user_data(
      session, stream_id);
  if (!stream_data) {
    return 0;
  }
  std::cout << "Stream close" << std::endl;
  // remove_stream(session_data, stream_data);
  // delete_http2_stream_data(stream_data);
  return 0;
}

static int on_header_callback(nghttp2_session *session,
                              const nghttp2_frame *frame, const uint8_t *name,
                              size_t namelen, const uint8_t *value,
                              size_t valuelen, uint8_t flags, void *user_data) {
  std::cout << "On header callback " << std::endl;
  switch (frame->hd.type) {
  case NGHTTP2_HEADERS:
    if (frame->headers.cat != NGHTTP2_HCAT_RESPONSE) {
      break;
    }
    auto hdr = std::string(name, name + namelen);
    std::cout << "Header: " << hdr << std::endl;
    break;
  }
  return 0;
}

static int on_begin_headers_callback(nghttp2_session *session,
                                     const nghttp2_frame *frame,
                                     void *user_data) {
  std::cout << "Begin header callback " << std::endl;

  if (frame->hd.type != NGHTTP2_HEADERS ||
      frame->headers.cat != NGHTTP2_HCAT_RESPONSE) {
    return 0;
  }

  return 0;
}

static int on_data_chunk_recv_callback(nghttp2_session *session, uint8_t flags,
                                       int32_t stream_id, const uint8_t *data,
                                       size_t len, void *user_data) {
  std::cout << "on_data_chunk_recv_callback " << len << std::endl;

  http2_session_data *session_data = (http2_session_data *)user_data;
  if (session_data->streams.front()->stream_id == stream_id) {
    std::cout << "Received response: " << std::string(data, data + len)
              << std::endl;
  }
  return 0;
}

static void initialize_nghttp2_session(
    const std::unique_ptr<http2_session_data> &session_data) {
  nghttp2_session_callbacks *callbacks;

  nghttp2_session_callbacks_new(&callbacks);

  nghttp2_session_callbacks_set_send_callback(callbacks, send_callback);

  nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks,
                                                       on_frame_recv_callback);

  nghttp2_session_callbacks_set_on_begin_headers_callback(
      callbacks, on_begin_headers_callback);

  nghttp2_session_callbacks_set_on_data_chunk_recv_callback(
      callbacks, on_data_chunk_recv_callback);

  nghttp2_session_callbacks_set_on_header_callback(callbacks,
                                                   on_header_callback);

  nghttp2_session_callbacks_set_on_stream_close_callback(
      callbacks, on_stream_close_callback);

  nghttp2_session_client_new(&session_data->session, callbacks,
                             session_data.get());

  nghttp2_session_callbacks_del(callbacks);
}

static int send_client_connection_header(
    const std::unique_ptr<http2_session_data> &session) {
  nghttp2_settings_entry iv[1] = {
      {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100}};
  std::cout << "submitting settings" << std::endl;
  auto rv = nghttp2_submit_settings(session->session, NGHTTP2_FLAG_NONE, iv,
                                    ARRLEN(iv));
  if (rv != 0) {
    std::cout << "error submitting settings" << std::endl;
    return -1;
  }
  return 0;
}

static int
session_send(const std::unique_ptr<http2_session_data> &session_data) {
  int rv;
  rv = nghttp2_session_send(session_data->session);
  if (rv != 0) {
    return -1;
  }
  return 0;
}

static int
session_recv(const std::unique_ptr<http2_session_data> &session_data) {
  uint8_t data[1024] = {0};
  size_t datalen = read(session_data->socket, data, 1024);

  std::cout << "Read - len: " << datalen << std::endl;

  auto readlen = nghttp2_session_mem_recv(session_data->session, data, datalen);
  if (readlen < 0) {
    return -1;
  }

  if (session_send(session_data) != 0) {
    return -1;
  }
  return datalen;
}

static void
submit_request(const std::unique_ptr<http2_session_data> &session_data) {
  nghttp2_nv hdrs[] = {MAKE_NV(":method", "GET"), MAKE_NV(":scheme", "http"),
                       MAKE_NV(":authority", "localhost:8080"),
                       MAKE_NV(":path", "/")};

  auto stream_data = std::make_shared<http2_stream_data>(0);
  session_data->streams.push_back(stream_data);
  auto stream_id =
      nghttp2_submit_request(session_data->session, nullptr, hdrs, ARRLEN(hdrs),
                             NULL, stream_data.get());
  if (stream_id < 0) {
    std::cout << "Could not submit HTTP request: "
              << nghttp2_strerror(stream_id) << std::endl;
  }

  stream_data->stream_id = stream_id;
  std::cout << "Successfully sent request with stream id: " << stream_id
            << std::endl;
}

int main(int argc, char **argv) {

  int client_fd, new_socket, valread;
  struct sockaddr_in address;
  int opt = 1;
  int addrlen = sizeof(address);

  if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    return 1;
  }

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port);

  if (connect(client_fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
    std::cout << "Connection with server failed" << std::endl;
    return 1;
  }

  std::cout << "Connection with server established" << std::endl;

  auto session = std::make_unique<http2_session_data>(client_fd);
  initialize_nghttp2_session(session);

  std::cout << "http2 session initialised" << std::endl;

  send_client_connection_header(session);
  submit_request(session);
  session_send(session);
  while (true) {
    if (session_recv(session) == 0) {
      break;
    }
  }

  // curl --http2-prior-knowledge http://localhost:8080 works
  return 0;
}