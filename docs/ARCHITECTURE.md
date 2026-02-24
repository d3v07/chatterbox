# ChatterBox Architecture

## Overview

ChatterBox is a low-latency, multi-user chat server built in C++ using
POSIX IPC primitives. It targets sub-400 ms message round-trips for up
to 64 concurrent users on a single machine.

## Component Map

```
apps/server_main.cpp          Entry point — starts Server, installs signal handlers
apps/client_main.cpp          Entry point — starts Client CLI

src/server/
  server.cpp                  Main server loop, IPC initialisation, connection lifecycle
  connection_manager.cpp      Per-user connection tracking and heartbeat enforcement
  message_router.cpp          Fan-out / unicast routing of chat and system messages
  rate_limiter.cpp            Sliding-window per-user rate limiter
  message_history.cpp         Bounded ring buffer of recent messages with replay
  config_parser.cpp           INI-style config file loader
  signal_handler.cpp          SIGTERM/SIGINT handlers for graceful shutdown
  metrics_exporter.cpp        Stats serialiser (JSON / Prometheus / text)

src/client/
  client.cpp                  Client state machine, IPC connection handling
  input_handler.cpp           Non-blocking stdin reader
  terminal_ui.cpp             ANSI terminal rendering

src/ipc/
  message_queue.cpp           POSIX message queue wrapper with RAII
  shared_memory.cpp           Shared memory segment and ChatSharedMemory layout
  semaphore.cpp               POSIX semaphore wrapper (binary + counting)

src/protocol/
  message.cpp                 Protocol message with 32-byte header and CRC-32 footer
  serializer.cpp              Encode/decode helpers for structured payloads
  timestamp.cpp               Microsecond-resolution timestamp generator

src/sync/
  thread_pool.cpp             Fixed-size work-stealing thread pool
  mutex_guard.cpp             RAII mutex and shared-mutex wrappers
  condition_var.cpp           Condition variable helpers
```

## IPC Layout

```
BASE_IPC_KEY + 0   Server message queue (clients write here to send to server)
BASE_IPC_KEY + 1   Shared memory segment (ChatSharedMemory — online user table)
BASE_IPC_KEY + 2   RW semaphore protecting shared memory
BASE_IPC_KEY + N   Per-client message queues (server writes here to deliver)
```

## Message Lifecycle

1. Client writes `Message` to the server queue.
2. `Server::receiver_loop()` dequeues it and dispatches to thread pool.
3. Thread pool task validates checksum, applies rate limiter, then calls `MessageRouter`.
4. Router looks up destination(s) in the shared user table and writes to their queues.
5. Client polling loop reads from its own queue and renders to the terminal.

## Configuration File (optional)

```ini
[server]
max_users         = 32
thread_pool_size  = 4
log_file          = chatterbox.log

[rate_limit]
max_messages      = 10
window_ms         = 1000
penalty_ms        = 5000

[history]
capacity          = 200
max_age_seconds   = 3600
persist_to_disk   = false
```

Start the server with: `./server --config /path/to/config.ini`

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j$(nproc)
```

Requires a POSIX-compliant OS (Linux, macOS) and a C++17-capable compiler.
