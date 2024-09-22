# Chat Server

This project implements a chat server (`chatServer.c`) designed to handle multiple client connections concurrently. It utilizes a connection pool to manage incoming sockets, facilitating communication between clients. The server transforms messages to uppercase before broadcasting them to other connected clients.

---

**Description:**

The chat server serves as a communication hub for clients, allowing them to send and receive messages. It handles incoming connections, reads data from clients, and broadcasts messages to all other connected clients using a non-blocking I/O model. The server can be gracefully terminated with a signal.

---

**Project Structure:**

1. **`chatServer.c`:**
   - Implements the chat server functionality.
   - Accepts command-line arguments for configuration:
     - Port number: Specifies the port on which the server listens for incoming connections.

2. **Connection Pool Management:**
   - Manages client connections through a structure that maintains active sockets and their associated messages.
   - Provides functions for adding and removing connections, as well as for reading and writing messages.

---

**Usage:**

1. Compile the project using a C compiler:
   ```bash
   gcc chatServer.c -o chatServer
   ```
2. Run the compiled executable with command-line arguments:
   ```bash
   ./chatServer <port>
   ```
   - Replace `<port>` with the desired port number.
   - Example: `./chatServer 8080`

3. Connect clients to the server using a telnet client or any other socket-based client implementation.

---

**Features:**

- Supports multiple concurrent client connections.
- Messages are transformed to uppercase before broadcasting.
- Non-blocking socket operations for efficient communication.
- Graceful server shutdown with signal handling.
- Connection pool for managing client sockets and messages.
