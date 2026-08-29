/**
 * End-to-end self-test for the ESP32 bridge — runs with no hardware attached.
 *
 * A fake device speaks the real wire protocol over a real WebSocket to a real
 * bridge process, which talks to the real DSH on loopback. That covers every
 * layer the firmware will exercise except the firmware itself, so when the board
 * is flashed, a failure is known to be in firmware rather than in the host half.
 *
 * The bridge must already be running; this test does not spawn it, because the
 * sandbox blocks piped-stdio child processes. Start it first:
 *
 *   $env:BRIDGE_PORT=3099
 *   $env:BRIDGE_STATE_DIR='<workspace>\dsh-esp32\bridge\.selftest-state'
 *   node bridge.js
 *
 * then run this with the same BRIDGE_PORT / BRIDGE_STATE_DIR.
 *
 * Checks, in order:
 *   1. bridge serves /status and reached DSH
 *   2. an upgrade with a bad token is refused (a LAN peer must not connect)
 *   3. an upgrade with the right token completes the RFC6455 handshake
 *   4. `hello` is answered with an immediate `state` frame
 *   5. that frame carries live DSH values (real session count, real ctx%)
 *   6. `ping` is answered with `pong`
 *   7. a published `ask` reaches the device with routable options
 *   8. an `answer` frame is accepted and clears the pending ask
 *   9. unknown frames are tolerated rather than fatal
 *  10. the status page reflects the attached device
 */

import { connect } from "node:net";
import { randomUUID } from "node:crypto";
import { request } from "node:http";
import { readFileSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const STATE_DIR = process.env.BRIDGE_STATE_DIR ?? join(HERE, ".selftest-state");
const PORT = Number(process.env.BRIDGE_PORT ?? 3099);

const results = [];
/** Record one check; `detail` is shown for both outcomes to aid diagnosis. */
function check(name, passed, detail = "") {
	results.push({ name, passed, detail });
	const mark = passed ? "PASS" : "FAIL";
	console.log(`  [${mark}] ${name}${detail === "" ? "" : ` — ${detail}`}`);
}

// ─────────────────────────────────────────────────── websocket client (device)

const OP = { text: 0x1, close: 0x8 };

/** Encode a masked client→server text frame (clients MUST mask). */
function clientFrame(text) {
	const payload = Buffer.from(text, "utf8");
	const mask = Buffer.from([1, 2, 3, 4]);
	const masked = Buffer.from(payload);
	for (let i = 0; i < masked.length; i += 1) masked[i] ^= mask[i & 3];
	let header;
	if (payload.length < 126) {
		header = Buffer.from([0x80 | OP.text, 0x80 | payload.length]);
	} else {
		header = Buffer.alloc(4);
		header[0] = 0x80 | OP.text;
		header[1] = 0x80 | 126;
		header.writeUInt16BE(payload.length, 2);
	}
	return Buffer.concat([header, mask, masked]);
}

/** A fake ESP32: connects, sends protocol frames, collects what comes back. */
class FakeDevice {
	constructor(port, token) {
		this.port = port;
		this.token = token;
		this.frames = [];
		this.buffer = Buffer.alloc(0);
		this.handshaken = false;
		this.handshakeStatus = "";
	}

	connect() {
		return new Promise((resolve, reject) => {
			const key = Buffer.from(randomUUID().replace(/-/g, "").slice(0, 16)).toString("base64");
			this.socket = connect({ host: "127.0.0.1", port: this.port }, () => {
				this.socket.write(
					`GET /dev?token=${this.token} HTTP/1.1\r\n` +
					`Host: 127.0.0.1:${this.port}\r\n` +
					"Upgrade: websocket\r\nConnection: Upgrade\r\n" +
					`Sec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\n\r\n`,
				);
			});
			this.socket.on("data", (chunk) => {
				this.buffer = Buffer.concat([this.buffer, chunk]);
				if (!this.handshaken) {
					const end = this.buffer.indexOf("\r\n\r\n");
					if (end === -1) return;
					this.handshakeStatus = this.buffer.slice(0, end).toString("utf8").split("\r\n")[0];
					this.buffer = this.buffer.slice(end + 4);
					if (!this.handshakeStatus.includes("101")) { reject(new Error(this.handshakeStatus)); return; }
					this.handshaken = true;
					resolve();
				}
				this.drain();
			});
			this.socket.on("error", reject);
			setTimeout(() => reject(new Error("connect timeout")), 5000);
		});
	}

	/** Read server frames (unmasked) out of the buffer. */
	drain() {
		for (;;) {
			if (this.buffer.length < 2) return;
			const opcode = this.buffer[0] & 0x0f;
			let length = this.buffer[1] & 0x7f;
			let offset = 2;
			if (length === 126) {
				if (this.buffer.length < 4) return;
				length = this.buffer.readUInt16BE(2);
				offset = 4;
			} else if (length === 127) {
				if (this.buffer.length < 10) return;
				length = Number(this.buffer.readBigUInt64BE(2));
				offset = 10;
			}
			if (this.buffer.length < offset + length) return;
			const payload = this.buffer.slice(offset, offset + length).toString("utf8");
			this.buffer = this.buffer.slice(offset + length);
			if (opcode === OP.text) {
				try { this.frames.push(JSON.parse(payload)); } catch { /* ignore non-JSON */ }
			}
			if (opcode === OP.close) return;
		}
	}

	send(object) { this.socket.write(clientFrame(JSON.stringify(object))); }

	/** Wait for the first frame matching `t`, or reject on timeout. */
	async expect(type, timeoutMs = 4000) {
		const deadline = Date.now() + timeoutMs;
		for (;;) {
			const found = this.frames.find((f) => f.t === type);
			if (found !== undefined) return found;
			if (Date.now() > deadline) throw new Error(`no "${type}" frame within ${timeoutMs}ms (saw: ${this.frames.map((f) => f.t).join(", ") || "nothing"})`);
			await new Promise((r) => setTimeout(r, 100));
		}
	}

	close() { this.socket?.destroy(); }
}

/** GET the bridge status page. */
function status(port) {
	return new Promise((resolve, reject) => {
		const req = request({ hostname: "127.0.0.1", port, path: "/status", timeout: 4000 }, (res) => {
			const chunks = [];
			res.on("data", (c) => chunks.push(c));
			res.on("end", () => {
				try { resolve(JSON.parse(Buffer.concat(chunks).toString("utf8"))); }
				catch (error) { reject(error); }
			});
		});
		req.on("error", reject);
		req.on("timeout", () => { req.destroy(new Error("timeout")); });
		req.end();
	});
}

/** Try an upgrade with a deliberately wrong token; expect a refusal. */
function badTokenAttempt(port) {
	return new Promise((resolve) => {
		const socket = connect({ host: "127.0.0.1", port }, () => {
			socket.write(
				"GET /dev?token=0000000000000000000000000000dead HTTP/1.1\r\n" +
				`Host: 127.0.0.1:${port}\r\n` +
				"Upgrade: websocket\r\nConnection: Upgrade\r\n" +
				"Sec-WebSocket-Key: AAAAAAAAAAAAAAAAAAAAAA==\r\nSec-WebSocket-Version: 13\r\n\r\n",
			);
		});
		let text = "";
		socket.on("data", (c) => { text += c.toString("utf8"); });
		socket.on("close", () => resolve(text.split("\r\n")[0] ?? "(closed)"));
		socket.on("error", () => resolve("(error)"));
		setTimeout(() => { socket.destroy(); resolve(text.split("\r\n")[0] ?? "(timeout)"); }, 3000);
	});
}

// ─────────────────────────────────────────────────────────────────────── run

console.log("═".repeat(70));
console.log("DSH ↔ ESP32 bridge self-test (no hardware required)");
console.log("═".repeat(70));
console.log(`target: 127.0.0.1:${PORT}, state dir: ${STATE_DIR}`);

let token = "";
try {
	token = readFileSync(join(STATE_DIR, "device-token.txt"), "utf8").trim();
} catch {
	console.log("  ── cannot read device-token.txt: bridge not started or state dir not shared");
}

console.log("\n▸ checks\n");

// 1 — status page
try {
	const s = await status(PORT);
	check("bridge serves /status", s.bridge === "dsh-esp32-bridge", `devices=${s.devices} dshOnline=${s.dshOnline}`);
	const backendOnline = s.backend === "codex" ? s.codexOnline === true : s.dshOnline === true;
	check("bridge reached desktop backend", backendOnline,
		backendOnline ? `${s.backend ?? "dsh"} backend answered` : "desktop backend unreachable — start Codex or set BRIDGE_BACKEND=dsh");
} catch (error) {
	check("bridge serves /status", false, error.message);
	check("bridge reached desktop backend", false, "status page unavailable");
}

// 2 — token discovered, and wrong tokens refused
check("device token available", /^[0-9a-f]{32}$/.test(token), token === "" ? "not found" : `${token.slice(0, 8)}…`);
const refusal = await badTokenAttempt(PORT);
check("bad token refused", refusal.includes("401"), refusal);

// 3-9 — the device conversation
const device = new FakeDevice(PORT, token);
try {
	await device.connect();
	check("handshake with valid token", device.handshakeStatus.includes("101"), device.handshakeStatus);

	device.send({ t: "hello", fw: "selftest/0.5.0", board: "ESP32-S3-Touch-LCD-1.85B", battery: 87, charging: false });
	const state = await device.expect("state");
	check("hello answered with state frame", true, `phase=${state.phase}`);

	const liveish = typeof state.ctx === "number" && typeof state.sessions === "number" && typeof state.phase === "string";
	check("state frame carries dial fields", liveish, `phase=${state.phase} ctx=${state.ctx}% sessions=${state.sessions} running=${state.running} perm=${state.perm || "—"}`);
	check("state reflects real DSH data", state.sessions > 0, state.sessions > 0 ? `${state.sessions} real sessions, title="${state.title}"` : "no sessions seen");

	device.frames.length = 0;
	device.send({ t: "ping", battery: 86, charging: true });
	const pong = await device.expect("pong");
	check("ping answered with pong", typeof pong.at === "number", `at=${pong.at}`);

	// 7 — an ask must reach the device. Drive it through the bridge's own HTTP
	// face so the test exercises the same publish path a DSH frame would.
	device.frames.length = 0;
	const asked = await new Promise((resolve) => {
		const body = JSON.stringify({
			kind: "approval",
			title: "执行命令？",
			body: "Remove-Item -Recurse D:\\build",
			options: [{ id: "allow", label: "允许" }, { id: "deny", label: "拒绝" }],
		});
		const req = request({
			hostname: "127.0.0.1", port: PORT, path: "/test/ask", method: "POST",
			headers: { "content-type": "application/json", "content-length": Buffer.byteLength(body) },
			timeout: 4000,
		}, (res) => { res.resume(); resolve(res.statusCode === 200); });
		req.on("error", () => resolve(false));
		req.on("timeout", () => { req.destroy(); resolve(false); });
		req.write(body);
		req.end();
	});

	if (asked) {
		const ask = await device.expect("ask");
		check("ask frame reaches device", typeof ask.id === "string" && Array.isArray(ask.options), `id=${ask.id} options=${ask.options?.map((o) => o.label).join("/")}`);
		const beep = device.frames.find((f) => f.t === "beep");
		check("ask is accompanied by a beep", beep !== undefined, beep === undefined ? "no beep frame" : `kind=${beep.kind}`);

		device.send({ t: "answer", id: ask.id, choice: "deny" });
		await new Promise((r) => setTimeout(r, 800));
		const after = await status(PORT);
		check("answer clears the pending ask", after.pendingAsks === 0, `pendingAsks=${after.pendingAsks}`);
	} else {
		check("ask frame reaches device", false, "bridge has no /test/ask endpoint");
		check("ask is accompanied by a beep", false, "skipped");
		check("answer clears the pending ask", false, "skipped");
	}

	// 9 — a frame the bridge does not know must not kill it
	device.send({ t: "definitely-not-a-real-frame", junk: true });
	await new Promise((r) => setTimeout(r, 500));
	const alive = await status(PORT).then(() => true).catch(() => false);
	check("unknown frame tolerated", alive, alive ? "bridge still serving" : "bridge died");

	// 10 — the status page sees the device
	const withDevice = await status(PORT);
	check("status page counts the device", withDevice.devices >= 1, `devices=${withDevice.devices}`);
} catch (error) {
	check("device conversation", false, error.message);
}

device.close();
await new Promise((r) => setTimeout(r, 400));

// ───────────────────────────────────────────────────────────────────── report

const passed = results.filter((r) => r.passed).length;
console.log(`\n${"═".repeat(70)}`);
console.log(`RESULT: ${passed}/${results.length} passed`);
console.log("═".repeat(70));
for (const r of results.filter((r) => !r.passed)) {
	console.log(`  FAILED: ${r.name} — ${r.detail}`);
}
process.exit(passed === results.length ? 0 : 1);
