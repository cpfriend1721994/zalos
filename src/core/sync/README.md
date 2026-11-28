# Phone history sync

`MobileHistorySync` copies message history from the user's phone into the offline store. The phone
does the work; the desktop only asks, downloads and imports. There are two transfer protocols.

## v1: full backup

1. The desktop sends `pull_mobile_msg` with a fresh public key.
2. The user accepts on the phone, which packs its whole message database into one encrypted
   SQLite backup and uploads it.
3. The desktop downloads the backup, decrypts it and imports every conversation.

v1 is used for the first sync on a device. It is one large transfer and has no per-conversation
progress.

## v2: sessions over the realtime socket

v2 is driven by three websocket commands:

| cmd | meaning                                    |
| --- | ------------------------------------------ |
| 590 | request a session (a list of queries)      |
| 591 | release a session                          |
| 592 | wake the phone up for an open session      |

A sync runs in two phases:

1. **Conversation phase.** One session asks for the conversation list. Each entry carries the
   conversation id, type, last message timestamp and last message id.
2. **Message phase.** Conversations are sorted newest first and requested 30 per session. Each
   query is `{partition, from, to, limit}`, where `partition` is `group/<id>` or `oneone/<id>`.
   The phone answers with encrypted batches that are downloaded first and imported afterwards.

Sessions stay open until the sync ends and are released together.

## Incremental sync

The goal is that only the first sync is large.

- **Up-to-date conversations are skipped.** A conversation is skipped when its last message id is
  already in the store, or when its checkpoint is at or past its last timestamp.
- **Checkpoints.** `sync_checkpoints(partition, synced_to)` stores, per partition, the timestamp a
  v2 sync has fully covered. A checkpoint is written only after a session was fully received and
  imported. Sessions cut short by an idle timeout and conversations whose id could not be decrypted
  get none. When a checkpoint exists, the next query starts `from` it.
- **Routing.** After any successful import, the store meta key `phone_history_synced` is set, and
  from then on "sync again" goes straight to v2. If the phone does not even return the conversation
  list, `phone_incremental_failed` is set and the next sync uses v1 again. A stall later on keeps
  the next sync on v2, because a v1 backup covers only part of the conversations the phone lists
  (in one test, 919 of 2,707) and v2 fills the rest over several syncs.
  Both keys and all checkpoints are cleared with the cache when the account changes.

## When the phone stops answering

The server normally acknowledges a 590 request within a second. Partway through a long v2 sync it
can stop doing so, while still acknowledging 591 and 592; this has been observed after anywhere
from 7 to 32 sessions and lasted more than two hours. The cause is not known. The desktop handles
two cases:

- **No acknowledgement within 10 seconds, or a non-zero `err`.** The request is not retried, as in
  the official desktop client.
- **Acknowledged, but the phone sends nothing.** The desktop wakes the phone once (cmd 592) and asks
  again.

Either way, if earlier message sessions finished, the sync ends as a success with a note to sync
again. Everything imported so far is kept, along with its checkpoints, so the next run continues
where this one stopped. If no message session finished, the sync fails.

## Known limits

- A conversation whose last message is stored is treated as complete. A gap further back, for
  example from a long offline period, is not detected.
- A message recalled on the phone after its range was synced is not picked up by a later sync.
  Recalls that happen while the desktop is online still arrive through the realtime channel.

## Debugging

- `--force-sync-v2` (or `ZALOS_FORCE_SYNC_V2=1`) skips v1 and always runs a full v2 sync.
- `logs/mobile-sync.log` records each session (`v2 590 sent`, `v2 session done`), the conversation
  filter (`v2 conversations: listed=… to_fetch=… up_to_date=…`) and early stops
  (`v2 stopped early`).
- `logs/errors.log` records the server acknowledgements for cmd 590/591/592; `mobile-sync.log`
  records `v2 590 ack err=…` and `v2 590 no ack within …`.
