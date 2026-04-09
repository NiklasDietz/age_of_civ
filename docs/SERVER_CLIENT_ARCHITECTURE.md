# Server/Client Architecture

## Overview

The game uses a server/client split even for single player. This means:

- **Game logic code never touches rendering**
- **Rendering code never directly modifies game state**
- **All player actions go through Commands**
- **The same codebase works for single player and multiplayer**

## Architecture

```
┌─────────────────────────────────────────────────┐
│                  GameServer                      │
│  - Owns ECS World, HexGrid, all game state      │
│  - Runs TurnProcessor::processTurn() each turn   │
│  - Validates incoming commands                   │
│  - Executes valid commands                       │
│  - Produces GameStateSnapshot each turn          │
│  - Manages AI controllers                        │
└────────────────────┬────────────────────────────┘
                     │
              Transport Layer
              (ITransport interface)
                     │
        ┌────────────┴────────────┐
        │                         │
  LocalTransport            NetworkTransport
  (in-process, zero-cost)   (TCP/UDP, future)
        │                         │
        └────────────┬────────────┘
                     │
┌────────────────────┴────────────────────────────┐
│                  GameClient                      │
│  - Sends Commands to server                      │
│  - Receives GameStateSnapshot from server        │
│  - Renders the world (map, units, UI)            │
│  - Handles player input -> converts to Commands  │
│  - Camera, selection, screens are client-only    │
└─────────────────────────────────────────────────┘
```

## Communication Model

### Why this approach?

**Command-based** (not state-sync): The client sends small command messages
("move unit X to tile Y"), not the full game state. The server validates
and executes them. This is efficient and prevents cheating.

**Snapshot-based updates**: After each turn, the server sends a snapshot
of visible game state to each client. This is simpler than delta-encoding
and works well for turn-based games where updates happen once per turn,
not 60 times per second.

**No third-party networking library needed yet**: For single player, the
LocalTransport is just function calls. For multiplayer, the NetworkTransport
will use raw POSIX sockets (TCP for commands, optionally UDP for voice
chat). No need for Boost.Asio, ENet, or similar until we actually
implement multiplayer. The interface is ready for any backend.

### Why not ENet/Boost.Asio/gRPC?

- **ENet**: Good for real-time games (FPS, RTS). Overkill for turn-based.
  We don't need sub-frame latency or unreliable channels.
- **Boost.Asio**: Heavy dependency for a simple request/response pattern.
  Raw sockets are sufficient for turn-based command exchange.
- **gRPC/Protobuf**: Too much ceremony for game commands. Our commands
  are already a variant type that can be trivially serialized.

When multiplayer is implemented, the simplest approach is:
1. TCP socket per client
2. Length-prefixed binary messages (same format as save file sections)
3. Server broadcasts turn results to all clients after processing

This can be done in ~500 lines of socket code without any library.

## Data Flow

### Single Player Turn

```
1. Player clicks "Move Unit" on the map
2. Client creates MoveUnitCommand{unitId, destination}
3. Client calls transport->sendCommand(command)
4. LocalTransport delivers to GameServer::receiveCommand()
5. Server validates: is it this player's unit? Is the move legal?
6. Server stores command in pending buffer
7. Player clicks "End Turn"
8. Client sends EndTurnCommand
9. Server sees all players ready
10. Server runs TurnProcessor::processTurn()
11. Server creates GameStateSnapshot (visible state for this player)
12. Server sends snapshot via transport
13. Client receives snapshot, updates its local view
14. Client renders the new state
```

### Multiplayer Turn (future)

```
1-6: Same as single player, but command goes over TCP
7-8: Same, but EndTurnCommand goes over TCP
9: Server waits for ALL clients to send EndTurn
10-11: Same
12: Server sends DIFFERENT snapshots to each client (fog of war)
13-14: Same
```

The game logic (steps 5, 10, 11) is **identical** in both cases.

## Key Types

### ITransport (interface)

```cpp
class ITransport {
    // Client -> Server
    void sendCommand(PlayerId player, GameCommand command);

    // Server -> Client
    void sendSnapshot(PlayerId player, GameStateSnapshot snapshot);

    // Server: get pending commands
    vector<pair<PlayerId, GameCommand>> receivePendingCommands();

    // Client: get latest snapshot
    optional<GameStateSnapshot> receiveSnapshot();
};
```

### LocalTransport (single player)

Just moves data between server and client in the same process:
- `sendCommand()` pushes to an internal queue
- `receivePendingCommands()` drains the queue
- Zero allocation, zero copy, zero latency

### GameStateSnapshot

Contains everything the client needs to render:
- Visible tiles (terrain, features, improvements, resources, owners)
- Visible units (position, type, HP, owner)
- Player's cities (name, population, production, yields)
- Diplomatic state (who's at war, alliances)
- Economy summary (GDP, treasury, inflation)
- Turn number, current phase
- Notifications/events that happened this turn

The snapshot is **per-player** -- each player only sees tiles within
their fog of war. This prevents information cheating in multiplayer.

### GameServer

```cpp
class GameServer {
    World m_world;
    HexGrid m_grid;
    TurnProcessor m_turnProcessor;
    vector<AIController> m_aiControllers;
    ITransport* m_transport;

    void tick();  // Called each frame or each turn
    void processReceivedCommands();
    void executeTurn();
    void broadcastSnapshots();
};
```

### GameClient

```cpp
class GameClient {
    ITransport* m_transport;
    PlayerId m_localPlayer;

    // Local render state (rebuilt from snapshots)
    RenderState m_renderState;

    void submitCommand(GameCommand cmd);
    void applySnapshot(GameStateSnapshot snapshot);
    void render();
};
```

## What Changes in Application.cpp

Currently, Application.cpp does everything:
- Owns the World
- Handles input
- Modifies game state directly
- Renders

After the refactor:
- Application.cpp owns a GameClient and a GameServer
- Input -> Commands -> Client -> Transport -> Server
- Server processes turns -> Snapshots -> Transport -> Client
- Client updates render state from snapshots
- Application.cpp only renders and handles input

The key insight: **Application.cpp becomes thin**. It's just the glue
between the window system and the GameClient.
