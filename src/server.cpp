#include <chrono>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

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
  std::vector<uint8_t> request_body;

  http2_stream_data(int32_t stream_id) : stream_id(stream_id) {}
};

struct http2_session_data {
  std::list<std::shared_ptr<http2_stream_data>> streams;
  nghttp2_session *session;
  int socket;

  http2_session_data(int socket) : socket(socket) {}
};

static ssize_t read_callback(nghttp2_session *session, int32_t stream_id,
                             uint8_t *buf, size_t length, uint32_t *data_flags,
                             nghttp2_data_source *source, void *user_data) {
  std::cout << "read cb - len: " << length << std::endl;

  auto *stream_data = (http2_stream_data *)nghttp2_session_get_stream_user_data(
      session, stream_id);
  auto &request_body = stream_data->request_body;

  // Echo server

  memcpy(buf, request_body.data(), request_body.size());
  *data_flags |= NGHTTP2_DATA_FLAG_EOF;
  return request_body.size();
}

static int on_request_recv(nghttp2_session *session,
                           http2_session_data *session_data,
                           http2_stream_data *stream_data) {
  int fd;

  std::cout << "Request recv" << std::endl;

  nghttp2_nv hdrs[] = {MAKE_NV(":status", "200")};

  std::string resp = "Hello there";

  nghttp2_data_provider prov;
  prov.source.ptr = (void *)resp.data();
  prov.read_callback = read_callback;

  int rv = nghttp2_submit_response(session, stream_data->stream_id, hdrs,
                                   ARRLEN(hdrs), &prov);
  if (rv != 0) {
    std::cout << "Error sending response" << std::endl;
    return -1;
  }

  return 0;
}

static ssize_t send_callback(nghttp2_session *session, const uint8_t *data,
                             size_t length, int flags, void *user_data) {
  http2_session_data *session_data = (http2_session_data *)user_data;
  std::cout << "Send callback - len: " << length << std::endl;
  send(session_data->socket, data, length, 0);
  return (ssize_t)length;
}

static int on_frame_recv_callback(nghttp2_session *session,
                                  const nghttp2_frame *frame, void *user_data) {
  http2_session_data *session_data = (http2_session_data *)user_data;
  http2_stream_data *stream_data;

  std::cout << "frame recv callback: " << (int)frame->hd.type << std::endl;

  switch (frame->hd.type) {
  case NGHTTP2_DATA:
  case NGHTTP2_HEADERS:
    /* Check that the client request has finished */
    if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
      stream_data = (http2_stream_data *)nghttp2_session_get_stream_user_data(
          session, frame->hd.stream_id);
      /* For DATA and HEADERS frame, this callback may be called after
         on_stream_close_callback. Check that stream still alive. */
      if (!stream_data) {
        return 0;
      }
      return on_request_recv(session, session_data, stream_data);
    }
    break;
  default:
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
  http2_stream_data *stream_data;
  switch (frame->hd.type) {
  case NGHTTP2_HEADERS:
    if (frame->headers.cat != NGHTTP2_HCAT_REQUEST) {
      break;
    }
    stream_data = (http2_stream_data *)nghttp2_session_get_stream_user_data(
        session, frame->hd.stream_id);
    if (!stream_data) { // || stream_data->request_path) {
      break;
    }
    auto hdr = std::string(name, name + namelen);
    std::cout << "Header: " << hdr << ": "
              << std::string(value, value + valuelen) << std::endl;
    break;
  }
  return 0;
}

static int on_begin_headers_callback(nghttp2_session *session,
                                     const nghttp2_frame *frame,
                                     void *user_data) {
  http2_session_data *session_data = (http2_session_data *)user_data;

  if (frame->hd.type != NGHTTP2_HEADERS ||
      frame->headers.cat != NGHTTP2_HCAT_REQUEST) {
    return 0;
  }

  std::cout << "Begin header callback " << std::endl;
  auto stream_data = std::make_shared<http2_stream_data>(frame->hd.stream_id);
  session_data->streams.push_back(stream_data);
  nghttp2_session_set_stream_user_data(session, frame->hd.stream_id,
                                       stream_data.get());
  return 0;
}

static int on_data_callback(nghttp2_session *session, uint8_t flags,
                            int32_t stream_id, const uint8_t *data, size_t len,
                            void *user_data) {
  std::cout << "on_data_callback: " << len << std::endl;

  auto *stream_data = (http2_stream_data *)nghttp2_session_get_stream_user_data(
      session, stream_id);

  stream_data->request_body.insert(stream_data->request_body.end(), data,
                                   data + len);

  std::cout << "Request: "
            << std::string(stream_data->request_body.begin(),
                           stream_data->request_body.end())
            << std::endl;

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

  nghttp2_session_callbacks_set_on_header_callback(callbacks,
                                                   on_header_callback);

  nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks,
                                                            on_data_callback);

  nghttp2_session_callbacks_set_on_stream_close_callback(
      callbacks, on_stream_close_callback);

  nghttp2_session_server_new(&session_data->session, callbacks,
                             session_data.get());

  nghttp2_session_callbacks_del(callbacks);
}

static int send_server_connection_header(
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

int main(int argc, char **argv) {

  int server_fd, new_socket, valread;
  struct sockaddr_in address;
  int opt = 1;
  int addrlen = sizeof(address);

  // Creating socket file descriptor
  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    return 1;
  }

  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                 sizeof(opt))) {
    return 1;
  }
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port);

  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    return 1;
  }

  if (listen(server_fd, 3) < 0) {
    return 1;
  }

  std::cout << "listening on port " << port << "..." << std::endl;

  while (true) {

    if ((new_socket = accept(server_fd, (struct sockaddr *)&address,
                             (socklen_t *)&addrlen)) < 0) {
      perror("accept");
      exit(EXIT_FAILURE);
    }

    auto session = std::make_unique<http2_session_data>(new_socket);
    initialize_nghttp2_session(session);

    std::cout << "http2 session initialised" << std::endl;

    send_server_connection_header(session);

    while (true) {
      if (session_recv(session) == 0) {
        break;
      }
    }
  }

  // curl --http2-prior-knowledge http://localhost:8080 works
  return 0;
}