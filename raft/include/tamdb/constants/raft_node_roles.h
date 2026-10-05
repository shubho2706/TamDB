#pragma once

namespace tamdb {

/** Raft node states — each node is in exactly one state at any time. */
enum RaftNodeRole {
    FOLLOWER,   /** Default state. Listens for heartbeats, votes when asked. */
    CANDIDATE,  /** Seeking election. Requests votes from peers. */
    LEADER      /** Won election. Sends heartbeats, replicates log entries. */
};
}
