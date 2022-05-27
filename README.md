# nghttp2-simple-server

Basic HTTP/2 server, using [`nghttp2`](https://github.com/nghttp2/nghttp2/tree/master/lib).

# Build

```bash
$ mkdir build
$ cd build
$ cmake -GNinja ..
$ ninja
```

# Run

In one terminal:

```bash
# Server
$ ./http2server
...
Request recv
...
```

In another terminal:

```bash
$ ./http2client
...
Received response: Hello there
...
```
