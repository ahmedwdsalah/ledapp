# Upload Progress Exact Sync — Audit-First Fix Plan

> **SUPERSEDED (same day):** Phase 0 audit found the real blocker — each device HTTP server is a single-threaded task (`esp_http_server/src/httpd_main.c`), so `/v1/status` **cannot answer at all** while the upload handler runs. All same-port polling designs were impossible. Shipped fix instead: a second HTTP server on port 8081 (own task) serving `/v1/progress`, answered live during transfers (verified: 171813 → 327461 mid-upload). The app polls that and displays exactly `floor(bytes*100/total)`. See git working tree for the shipped diffs.

## Goal
Make the app's upload button percentage and the device screen's "Receiving" percentage show the IDENTICAL integer at every instant during an upload — by first reading every code path that produces a progress number, then making only the listed minimal edits.

## Current context / assumptions

Repo: `/Users/ahmed/ledapp` — Expo SDK 57 app ("Motif") + ESP32-S3 firmware in `hardware/firmware/` (ESP-IDF, Waveshare 480x480 board).

Device is live at `192.168.9.104:8080` (it changed from `.106` after a router reboot — the app re-reads the IP over Bluetooth; out of scope here).

Firmware HTTP contracts (all Bearer-authenticated; token comes from device pairing, stored in NVS — never write the token value into any file in this repo):
- `GET /v1/status` → `{"protocol":1,"deviceId":"33EE30","wifiConnected":true,"generation":0,"state":"playing","uploadBytes":N,"uploadTotal":M}`
- `POST /v1/animation` (Content-Type `application/x-motif-animation`) streams a `.motif` file; device receives to a temp file, validates, applies, responds `202 {"generation":N}`.

Already applied and flashed in this working tree (baseline, do not redo):
- `hardware/firmware/main/motif_http.c`: statics `s_upload_received` / `s_upload_total` updated every recv chunk; reset at handler start and handler end; `status_handler` serves them as `uploadBytes`/`uploadTotal`; the on-screen progress call runs per chunk with `(uint8_t)((uint64_t)received_total * 100u / (uint32_t)req->content_len)`.
- `src/device/motif-device.ts`: during an upload, a 250ms interval polls `/v1/status` (single in-flight request, 2s deadline, monotonic accept — only strictly increasing values are forwarded to `onProgress`).

Known symptoms to eliminate (Ahmed's report):
1. App and screen numbers still differ during upload.
2. Button sometimes starts at 25%, sometimes at 0.
3. App shows "Confirming…" while the screen still says "Receiving".

Root cause of all three: the app's number came from phone-side bytes-sent (races ahead), the screen's from device-side received (slower); plus leftover counters from a previous upload and out-of-order poll responses. The alignment design below removes every way a third number can exist.

## Architecture / proposed approach

Single source of truth = the firmware's `s_upload_received` byte counter (already served by `/v1/status`, already driving the screen). During an upload the app must display exactly `floor(bytes * 100 / total)` as an integer — nothing else, ever. Remaining work: (a) a complete read-audit proving no other number exists anywhere in the chain, (b) three minimal app edits (device-only display, remove dead flag, match the floor rounding), (c) Mac-side invariant probes + one user acceptance pass.

## Phase 0 — Full read audit (no edits)

### T0.1 Dump every progress-related file
```
cd ~/ledapp
wc -l src/device/motif-device.ts src/components/display-screen.tsx \
  hardware/firmware/main/motif_http.c hardware/firmware/main/motif_player.c \
  hardware/firmware/main/motif_player.h
```
Expected: five line counts, no errors.
Then READ EACH FILE FULLY with `read_file` (all pages). Grep alone is what caused the previous misses — do not skip a file.

### T0.2 Fill Appendix A table below (append findings to this plan file)
Answer precisely, pasting the actual code lines:
1. Every call site of `onProgress` in `src/device/motif-device.ts`; every consumer of its value in `src/components/display-screen.tsx` (expected: `setUploadPercent`).
2. The exact conversion line that turns the progress fraction into `uploadPercent` (search `setUploadPercent` in `src/components/display-screen.tsx`; expected forms: `Math.round(p * 100)` or `p * 100`).
3. Confirm in `motif_http.c` that `motif_player_set_receive_progress(...)` is called once per recv chunk (not inside the 64KB `next_progress` gate) and paste the line.
4. In `motif_player.c` paste: the body of `motif_player_set_receive_progress`, the place the percent renders, and the `motif_player_loop` segment where `set_receiving(false)` switches the screen away from "Receiving".
5. Any OTHER reader of `s_receive_percent` / `s_upload_received` (expected: none besides the two above).

Rule: no edit happens until every row is filled with pasted code.

## Phase 1 — Minimal app edits (firmware untouched unless Phase 0 proves otherwise)

### T1.1 `src/device/motif-device.ts` — native progress goes to the log only, never the UI
Replace exactly:
```ts
    const progress = (percent: number) => {
      onProgress?.(percent);
      const bucket = Math.min(10, Math.floor(percent * 10));
      if (bucket > sentBucket) {
        sentBucket = bucket;
        recordConnection('Upload', `Sent ${bucket * 10}%`);
      }
    };
    const nativeProgress = (percent: number) => {
      if (!deviceSeen) progress(percent);
    };
```
with:
```ts
    const progress = (percent: number) => {
      onProgress?.(percent);
    };
    const nativeProgress = (percent: number) => {
      const bucket = Math.min(10, Math.floor(percent * 10));
      if (bucket > sentBucket) {
        sentBucket = bucket;
        recordConnection('Upload', `Sent ${bucket * 10}%`);
      }
    };
```
Result: `onProgress` (hence the button) can only ever receive device-sourced values.

### T1.2 Remove the now-dead `deviceSeen` flag (same file)
- Delete the declaration line: `    let deviceSeen = false;`
- Delete this line in the poll accept-branch: `        deviceSeen = true;`
Verify:
```
grep -n deviceSeen src/device/motif-device.ts
```
Expected: no output.

### T1.3 Match the firmware's floor rounding (same file, poll block)
Replace:
```ts
        const percent = status.uploadBytes / status.uploadTotal;
```
with:
```ts
        const percent = Math.floor((status.uploadBytes * 100) / status.uploadTotal) / 100;
```
Rationale: the firmware screen renders `floor(bytes*100/total)`; pushing the identical integer value through guarantees the app can never differ by one.

### T1.4 Rounding equivalence check (no test framework in this repo — use this one-liner)
```
node -e "const b=388901,t=703252; const f=Math.floor(b*100/t); console.log(f===Math.round((Math.floor(b*100/t)/100)*100))"
```
Expected: `true`.

### T1.5 `src/components/display-screen.tsx` — confirm conversion, change only if Appendix A disagrees
- If Appendix A shows `setUploadPercent(Math.round(p * 100));` → no change.
- Anything else (e.g. `setUploadPercent(p)`) → replace that line with `setUploadPercent(Math.round(p * 100));`.
- Keep the button line unchanged:
  `{uploading ? (uploadPercent >= 100 ? 'Confirming…' : \`Uploading… ${uploadPercent}%\`) : 'Upload to Device'}`
  With a device-sourced percent, `>= 100` now truthfully means "device received every byte".

### T1.6 Static verification
```
cd ~/ledapp && npx tsc --noEmit && pnpm run lint
```
Expected: tsc silent; lint prints `> expo lint` and exits 0.
```
grep -n "onProgress" src/device/motif-device.ts
```
Expected: exactly the intended sites — the `progress` helper call and the `uploadAnimation` parameter.

Do NOT commit. Leave changes in the working tree — Ahmed commits himself.

## Phase 2 — Firmware check + end-to-end verification

### T2.1 Firmware: touch only if Appendix A found a mismatch
Verify in `motif_http.c`: per-chunk `motif_player_set_receive_progress` line + `s_upload_received = 0; s_upload_total = 0;` at BOTH handler start and end. If anything differs, fix, then:
```
cd ~/ledapp/hardware && export IDF_TOOLS_PATH=$PWD/toolchains/.espressif && \
source tools/esp-idf/export.sh && cd firmware && idf.py build && \
idf.py -p /dev/cu.usbmodem31301 flash
```
Expected tail: `Built target flash` / `Hard resetting via RTS pin...` / `Done`, rc=0. If unchanged, do not flash.

### T2.2 Mac-side invariant probe (never touch the phone, Metro, or the dev server)
Use the device token from this session's shell (never write it into files):
```
T=<DEVICE_TOKEN>; IP=192.168.9.104
curl -s --limit-rate 60k --max-time 6 -o /dev/null -X POST http://$IP:8080/v1/animation \
  -H "Authorization: Bearer $T" -H "Content-Type: application/x-motif-animation" \
  --data-binary @assets/device-animations/toyota-ember.motif &
sleep 2;  curl -s -H "Authorization: Bearer $T" http://$IP:8080/v1/status | grep -o '"uploadBytes":[0-9]*,"uploadTotal":[0-9]*'
sleep 2;  curl -s -H "Authorization: Bearer $T" http://$IP:8080/v1/status | grep -o '"uploadBytes":[0-9]*,"uploadTotal":[0-9]*'
sleep 2;  curl -s -H "Authorization: Bearer $T" http://$IP:8080/v1/status | grep -o '"uploadBytes":[0-9]*,"uploadTotal":[0-9]*'
```
Expected: three samples, strictly increasing `uploadBytes`, constant `uploadTotal` (~703252); the transfer aborts itself at 6s (partial is discarded; the on-screen animation is unchanged). Then:
```
curl -s -H "Authorization: Bearer $T" http://$IP:8080/v1/status
```
Expected: `"uploadBytes":0,"uploadTotal":0` with `"state":"playing"` — proves the end-of-handler reset.

### T2.3 Serial cross-check (the same number the screen renders)
During a T2.2 run:
```
python3 /Users/ahmed/.hermes/cache/scratch/noreset_read.py /dev/cu.usbmodem31301 8
```
Expected: `Upload received N/M` lines whose N values line up with the sampled `uploadBytes` (screen percent = `floor(N*100/M)` by construction).

### T2.4 User acceptance (Ahmed only — do not do this step yourself)
1. Ahmed restarts the app.
2. Presses upload.
3. Expected: both numbers identical from the first update; never starts at 25%; never moves backwards; at 100% → "Confirming…" → device plays the new animation.

## Definition of done
- Appendix A fully filled with pasted code.
- Working tree diff = exactly T1.1–T1.3 (+T1.5 only if Appendix A demands it).
- tsc 0 / lint 0; firmware build 0 / flash 0 only if firmware was touched.
- T2.2 and T2.3 outputs match the expected values above.
- Ahmed's acceptance pass shows equal integers.

## Risks / tradeoffs / open questions
- Device-only display: if phone-side polls all fail, the button holds 0% until "Confirming…". Accepted — an honest, quiet number beats a racing one; phone-side numbers remain in the connection log.
- "Confirming…" at device-100% vs the actual HTTP response: seam is ~1s (device validates/applies after the last byte). If Ahmed still flags it, then (and only then) add a phase callback fired after `sendWithRetry` returns — new code, needs explicit approval.
- Poll cadence 250ms with one-in-flight gate; the device proved it answers mid-upload. Slower answers = slower updates, never a mismatch.
- DHCP IP change (.106→.104) already handled by the app's BLE refresh — out of scope.
- No commits by the implementer, ever; Ahmed reviews and commits.

## Appendix A — findings (fill during T0.2; empty until then)
| # | file:line | what it does | consumer | verdict |
|---|---|---|---|---|
| 1 | | onProgress call sites | | |
| 2 | | setUploadPercent conversion | | |
| 3 | | per-chunk receive progress | | |
| 4 | | receiving screen exit | | |
| 5 | | other readers of the counters | | |
