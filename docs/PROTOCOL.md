# Jackie Source Protocol (JSP), version 1

The protocol Corporal Jackie speaks to its own sources. Stremio add-ons keep
working through the Stremio adapter (`src/sources/StremioAdapter`); JSP is
for sources that want clear failures, bounded waits and plain data. Both are
adapters behind `SourceAdapter`, so the rest of the app does not know which
one answered.

Built in 0.4.1: `JspProtocol` (parsing), `JspClient` (requests) and a dialect switch in `StremioAdapter`, so a Jackie source is listed, ordered, switched on and off and persisted like any add-on.

## What it fixes compared with the Stremio protocol

| Problem in Stremio add-ons | JSP answer |
| --- | --- |
| A failure is a hang, an HTML page or an empty list; the client cannot tell them apart | Every answer is one JSON envelope with `ok` and a typed `error.code` |
| No time limit; one slow add-on stalls Home | The server answers within 8 s or returns `partial: true`; the client ends every request inside one 10 s budget and shows the source's own error line |
| Behavior is hidden in free-form `behaviorHints` | Streams have named, typed fields; unknown fields are ignored, missing required ones reject only that item |
| One URL: when it is down, the source is down | The manifest lists `mirrors`; the client tries them in order and remembers the one that worked |
| No caching rules | Every answer carries `ttl` (seconds) and an `ETag`; on an error the client may show a stale copy marked as such |
| Search, genre and year are `/extra` path segments with per-add-on quirks | One query string, the same names everywhere |
| A stream's trackers, file and size are guessed from a title | `infoHash`, `fileIdx`, `trackers`, `size` are fields |

## Transport

- HTTPS only (HTTP only for `127.0.0.1` during development). GET only.
- No cookies, no client identifiers, no custom headers except `Accept: application/json`
  and `If-None-Match`. The request carries what is searched and the item id, nothing else.
- Base URL = the manifest URL without `/manifest.json`. A language or region
  prefix in the path (`/he`, `/en.tvnca`) is part of the base.

## Envelope

```json
{ "ok": true, "v": 1, "ttl": 600, "partial": false, "data": { } }
{ "ok": false, "v": 1, "error": { "code": "rate_limited", "message": "Slow down", "retryAfter": 30 } }
```

Error codes: `bad_request`, `not_found`, `rate_limited` (with `retryAfter`),
`upstream_down` (the source's own data provider failed), `unavailable`.
HTTP status mirrors the code (400, 404, 429, 502, 503). A client treats any
non-envelope answer as `unavailable`.

## Manifest: `GET /manifest.json`

```json
{ "protocol": 1, "id": "community.wikimaze", "name": "WikiMaze", "version": "1.0.0",
  "mirrors": ["https://wikimaze-seven.vercel.app", "https://backup.example"],
  "resources": ["catalog", "meta", "streams", "subtitles"],
  "types": ["movie", "series"], "idPrefixes": ["tt"],
  "catalogs": [ { "type": "movie", "id": "trending", "name": "Trending",
                  "search": true, "genres": ["action", "drama"], "years": ["2026", "2020s", "Older"] } ] }
```

## Resources

| Request | Data |
| --- | --- |
| `GET /catalog/{type}/{id}?q=&genre=&year=&skip=&limit=` | `{ "items": [Preview], "total": 52 }` |
| `GET /search?q=&type=&genre=&year=&list=&skip=&limit=` | the same, across the source |
| `GET /meta/{type}/{id}` | `Meta` (below) |
| `GET /streams/{type}/{videoId}` | `{ "streams": [Stream] }` |
| `GET /subtitles/{type}/{videoId}` | `{ "subtitles": [{ "url", "lang", "label" }] }` |
| `GET /health` | `{ "ok": true }` for monitors; the client does not call it |

`Preview`: `id`, `type`, `name`, `poster`, `year`. `Meta`: Preview plus
`background`, `description`, `runtime`, `genres`, and for a series `videos`:
`[{ "id", "season", "episode", "title", "released" }]`.

`Stream` (one of two kinds):

```json
{ "kind": "torrent", "infoHash": "…40 hex…", "fileIdx": 3, "trackers": ["udp://…"],
  "name": "Example 4K", "detail": "HDR · 8 GB", "quality": 2160, "size": 8589934592, "seeders": 120, "group": "release-id" }
{ "kind": "http", "url": "https://…", "name": "…", "quality": 1080, "size": 2147483648, "headers": { } }
```

## What the client does (fewer timeouts)

- **One connection pool per source**, so a slow source never holds up the others.
- **One 10 s budget per request**, started on a monotonic clock when the request begins. Every network try gets at most what is left, and the request ends exactly once: with the first good answer, a final error, or "timed out" (or the last good copy, below). When it ends, its other tries are cancelled; other requests to the same source are left alone.
- **Hedged requests**: if the first try has no answer after 2.5 s, a second goes to the next mirror (or the same address again when there is no mirror), still inside the same budget. The first good answer wins. A failed try is replaced at once, not after the timer.
- **Typed errors are read from the HTTP answer**, not only from a 200: a valid envelope on a 4xx or 5xx status counts. `not_found` and `bad_request` are final (a real 404 never asks a mirror, nor the same address again). `rate_limited` keeps that address quiet for its `retryAfter` (5 s when absent, at most 300 s); another mirror is still asked inside the budget, and a later request does not ask the quiet address at all. `upstream_down`, `unavailable`, a transport failure, an HTML page or any body that is not an envelope move on to the next mirror, and the error shown is generic (never the page's text).
- **Answers are kept for their `ttl`** in memory. When every try fails, the last good copy of a **catalog, search or title** (kept up to 24 h) is shown instead of an error, marked as cached; the add-ons page says "Showing cached results" and does not call it a failure. **Streams and subtitles are never shown from an old copy**: their links can be signed and expire.
- **A source that is removed or whose addresses change** loses its saved answers, its remembered mirror and its cooldowns, and requests still out for the old setup end as cancelled and save nothing.
- **The mirror that answered is asked first** next time (memory only, never written to disk).

Address rule, the same for the source's own address, every mirror and every redirect target: `https`, or `http` on `127.0.0.1`/`localhost` for development; a host; no user name or password. The source's own address is checked when its manifest is recognised as Jackie's (install, restore, refresh) and again before each request; a Jackie source that fails it is refused. A path prefix such as `/he` stays part of the base for every resource. Stremio add-ons keep their old rules.

Not used by the client yet (so a source may send them, nothing happens): `partial` (the answer is shown as it is), `total` (paging goes on until a page is empty), the `/search` resource (search goes through `/catalog/{type}/{id}?q=`, with `genre` and `year` in the same request), and `ETag`/`If-None-Match` revalidation (the `ttl` alone decides).

Also:

- A `Stream` goes through the same checks as any other source (`LinkRules`,
  `docs/SECURITY.md`): no file or UNC links, no credentials in URLs, headers
  only for the same host.

## Privacy

Same rules as `docs/PRIVACY.md`: a source learns the title ids and search
words it is asked for and the IP address, nothing else. JSP adds no
identifiers, no accounts and no telemetry.

## Stremio add-ons

A manifest with `protocol: 1` is a Jackie source; any other is a Stremio add-on. Both live in `StremioAdapter`, which
asks each in its own protocol and turns a Jackie answer into the shape the Stremio parsers already read, so the stream,
header and link checks apply to both. WikiMaze will serve both until Stremio use is gone.

## Still to do

- WikiMaze serves JSP next to Stremio (outside this repository). Until then nothing in the app speaks JSP by default.
- Show the protocol on the add-ons page.
