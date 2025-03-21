# TCP Orderbook System

A multi-threaded client-server orderbook implementation using TCP sockets.

## Overview

This project implements a financial orderbook system with the following capabilities:

- TCP client-server architecture
- Multi-threaded server to handle multiple clients concurrently
- Support for various order types (GoodTillCancel, FillAndKill)
- Buy and sell order matching
- Order cancellation and modification
- Real-time trade notifications
- Orderbook status display

## Components

1. **Server**: Handles client connections, processes order requests, maintains the orderbook
2. **Client**: Connects to the server, sends order requests, receives trade notifications
3. **Orderbook**: Core business logic for matching orders
4. **Message Format**: Defines the protocol for client-server communication

## Building the Project

### Prerequisites

- C++17 compatible compiler (GCC 7+ or Clang 5+)
- CMake 3.10 or higher
- POSIX-compatible operating system (Linux, macOS)

### Compile Instructions

```bash
mkdir build
cd build
cmake ..
make
```

This will create two executables:
- `orderbook_server` - The server application
- `orderbook_client` - The client application

## Running the Applications

### Server

```bash
./orderbook_server
```

You will be prompted to enter:
1. Port number (e.g., 9000)
2. Number of worker threads (e.g., 4