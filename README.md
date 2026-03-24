# ChatterBox: High-Performance Terminal Chat

Terminal-based multi-user chat using System V IPC, binary protocol, POSIX threads. Supports 50+ concurrent users with 0.4s latency.

## Build
```bash
mkdir build && cd build && cmake .. && make
./chatterbox_server_app --port 9000 --max-users 50
./chatterbox_client_app --server-key 0x4242 --username alice
```
