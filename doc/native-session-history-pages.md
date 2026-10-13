# Bounded session history pages

`GET /v1/sessions/{id}/messages` returns a bounded page from the durable top-level transcript. It accepts `limit` (default 100, range 1–1000) and `before=msg_<sequence>`. The first request returns the latest page; each response keeps messages in chronological order and provides `has_more` plus `next_before`. Pass that boundary unchanged to retrieve the preceding page. Cursors are session-scoped: a boundary from another session or a missing message returns 404. Malformed/duplicate/unknown query parameters and out-of-range limits return 400.

The repository asks xlang3 SQLite for at most `limit + 1` rows, using the session/message index, so page retrieval does not load the whole transcript. Existing `/history` remains available for clients that need the complete array. The native console supports `history-page SESSION LIMIT [BEFORE_MESSAGE_ID]` and returns the same page object as HTTP.

This is a native xMind page contract, not full OpenCode `session.message.list` parity: OpenCode's typed message/part projection, filter, order controls and opaque cursor semantics remain to be matched. The Release `native_http_cli_contract` exercises three pages across a twelve-message fixture, no-skip boundaries, API/CLI equivalence, invalid limits/cursors, missing boundaries and strict query validation.
