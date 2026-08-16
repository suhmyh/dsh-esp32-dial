/**
 * dsh-esp32-bridge — host-side bridge between DSH and an ESP32 status dial.
 *
 * Shape
 * -----
 *              DSH (127.0.0.1:3080)                    ESP32 (WiFi)
 *                    │                                      │
 *        POST /api/session.list  ◄── poll ──┐                │
 *        WS /api/events.mux      ◄── live ──┤                │
 *                                            │                │
 *                                     [ this bridge ]  ◄── WS ─┘
 *                                        :3082/dev
 *
 * Why a bridge at all: DSH speaks Typert RPC (envelope-wrapped, WebSocket mux
 * for pushes) and its trust fence only accepts loopback callers with no browser
 * Origin. An ESP32 cannot satisfy that, and should not have to parse DSH's event
 * protocol either. The bridge owns both hard parts and hands the device one flat
 * frame per state — see 协议规范.md for the wire contract.
 *
 * Data honesty: the device is told what the bridge actually knows. When the DSH
 * link drops, the bridge stops publishing derived numbers rather than repeating
 * the last ones, so a dial can show `offline` instead of stale confidence.
 */

import { createServer } from "node:http";
import { createHash, randomUUID, randomBytes, timingSafeEqual } from "node:crypto";
import { request as httpRequest } from "node:http";
import { connect } from "node:net";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { homedir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

// ─────────────────────────────────────────────────────────────── configuration

const CONFIG = {
	/** Port the device connects to. 3080 = DSH, 3081 = remote gateway. */
	listenPort: Number(process.env.BRIDGE_PORT ?? 3082),
	/** DSH's loopback web server. */
	dshHost: "127.0.0.1",
	dshPort: Number(process.env.DSH_PORT ?? 3080),
	/** How often to poll session.list while a device is attached. */
	pollMs: 1000,
	/** A state frame older than this is `stale` on the device. */
	freshnessMs: 3000,
	/** `done` is shown for this long after a run finishes, then falls to idle. */
	doneHoldMs: 4000,
};

/**
 * Bridge state directory.
 *
 * Defaults next to the remote gateway's state under `$DSH_HOME`. `BRIDGE_STATE_DIR`
 * overrides it so the bridge can run under a restricted file policy (a sandboxed
 * test run keeps its token inside the workspace instead of failing to persist).
 */
function dataDir() {
	const override = process.env.BRIDGE_STATE_DIR;
	if (override !== undefined && override !== "") return override;
	const home = process.env.DSH_HOME ?? join(homedir(), ".dsh");
	return join(home, "esp32-bridge");
}

/**
 * Load or mint the pre-shared device token.
 *
 * A LAN is not a trust boundary: without this, any host on the subnet could read
 * session titles and answer approval prompts. The token goes into the firmware
 * config once and is compared in constant time on every upgrade.
 */
function loadToken() {
	const path = join(dataDir(), "device-token.txt");
	try {
		const existing = readFileSync(path, "utf8").trim();
		if (/^[0-9a-f]{32,}$/.test(existing)) return existing;
	} catch {
		// fall through and mint
	}
	const token = randomBytes(16).toString("hex");
	try {
		mkdirSync(dataDir(), { recursive: true, mode: 0o700 });
		writeFileSync(path, `${token}\n`, { mode: 0o600 });
		log(`minted device token → ${path}`);
	} catch (error) {
		log(`WARN could not persist token (${String(error)}); using in-memory token`);
	}
	return token;
}

const TOKEN = loadToken();

/** Timestamped line log; the bridge is meant to be watched in a terminal. */
function log(message) {
	const at = new Date().toISOString().slice(11, 19);
	console.log(`[${at}] ${message}`);
}

// ────────────────────────────────────────────────────────────────── DSH client

/**
 * One unary DSH RPC call.
 *
 * The envelope is mandatory in both places: `method` must appear in the URL and
 * in the body, and `type` must be `client-request`, or DSH answers `bad-request`.
 *
 * @returns the unwrapped `result.value`.
 * @throws when transport fails or DSH returns `ok: false`.
 */
function dshCall(method, payload = {}) {
	return new Promise((resolve, reject) => {
		const body = JSON.stringify({ type: "client-request", rpcId: randomUUID(), method, payload });
		const req = httpRequest({
			hostname: CONFIG.dshHost,
			port: CONFIG.dshPort,
			path: `/api/${method}`,
			method: "POST",
			headers: { "content-type": "application/json", "content-length": Buffer.byteLength(body) },
			timeout: 8000,
		}, (res) => {
			const chunks = [];
			res.on("data", (c) => chunks.push(c));
			res.on("end", () => {
				const text = Buffer.concat(chunks).toString("utf8");
				if (res.statusCode !== 200) { reject(new Error(`HTTP ${res.statusCode}`)); return; }
				let parsed;
				try { parsed = JSON.parse(text); } catch { reject(new Error("non-JSON response")); return; }
				if (parsed?.result?.ok === true) resolve(parsed.result.value);
				else reject(new Error(parsed?.result?.error?.code ?? "rpc-failed"));
			});
		});
		req.on("error", reject);
		req.on("timeout", () => { req.destroy(new Error("timeout")); });
		req.write(body);
		req.end();
	});
}

// ────────────────────────────────────────────────────── minimal WebSocket layer

/** RFC6455 opcodes used here. */
const OP = { text: 0x1, close: 0x8, ping: 0x9, pong: 0xa };

/**
 * Encode one server→client text frame (server frames are never masked).
 */
function encodeFrame(text, opcode = OP.text) {
	const payload = Buffer.from(text, "utf8");
	const length = payload.length;
	let header;
	if (length < 126) {
		header = Buffer.from([0x80 | opcode, length]);
	} else if (length < 65536) {
		header = Buffer.alloc(4);
		header[0] = 0x80 | opcode;
		header[1] = 126;
		header.writeUInt16BE(length, 2);
	} else {
		header = Buffer.alloc(10);
		header[0] = 0x80 | opcode;
		header[1] = 127;
		header.writeBigUInt64BE(BigInt(length), 2);
	}
	return Buffer.concat([header, payload]);
}

/**
 * Incremental parser for client→server frames (always masked).
 *
 * Written by hand so the bridge has no npm dependency: the profile has no `ws`
 * package, and a dependency-free bridge can be copied next to the firmware and
 * run with a bare `node`.
 *
 * @param onText - called with each complete text payload.
 * @returns a `push(chunk)` function and a `close` flag holder.
 */
function createFrameReader(onText, onClose) {
	let buffer = Buffer.alloc(0);
	return (chunk) => {
		buffer = Buffer.concat([buffer, chunk]);
		for (;;) {
			if (buffer.length < 2) return;
			const opcode = buffer[0] & 0x0f;
			const masked = (buffer[1] & 0x80) !== 0;
			let length = buffer[1] & 0x7f;
			let offset = 2;
			if (length === 126) {
				if (buffer.length < 4) return;
				length = buffer.readUInt16BE(2);
				offset = 4;
			} else if (length === 127) {
				if (buffer.length < 10) return;
				length = Number(buffer.readBigUInt64BE(2));
				offset = 10;
			}
			const maskLength = masked ? 4 : 0;
			if (buffer.length < offset + maskLength + length) return;
			const mask = masked ? buffer.slice(offset, offset + 4) : undefined;
			const raw = buffer.slice(offset + maskLength, offset + maskLength + length);
			buffer = buffer.slice(offset + maskLength + length);
			if (mask !== undefined) {
				for (let i = 0; i < raw.length; i += 1) raw[i] ^= mask[i & 3];
			}
			if (opcode === OP.text) onText(raw.toString("utf8"));
			else if (opcode === OP.close) { onClose(); return; }
			// ping/pong from the device are handled by the app-level heartbeat.
		}
	};
}

// ──────────────────────────────────────────────────────────────── dial state

/**
 * Live facts learned from the DSH event stream, merged into each derived state.
 *
 * Polling `session.list` gives durable projections but not the moment-to-moment
 * activity a dial should show. The observed `session/jobs` frame carries exactly
 * that: the command DSH is running right now. Holding it here lets `detail` name
 * a real action instead of a generic phase word.
 */
const liveFacts = {
	/** Running background jobs, newest label wins for `detail`. */
	jobs: [],
	/** When the jobs list last changed, for freshness decisions. */
	jobsAt: 0,
};

/**
 * Derive the dial's view of DSH from a `session.list` snapshot.
 *
 * All business judgement lives here rather than in firmware: the device renders
 * what it is told. `phase` is deliberately coarse — five states a glance can
 * distinguish — and the numeric fields are the few a 360×360 round screen can
 * show without becoming a spreadsheet.
 */
function deriveState(snapshot, previous) {
	const items = Array.isArray(snapshot?.items) ? snapshot.items : [];
	const running = items.filter((s) => s.running === true);
	const active = running[0] ?? items.find((s) => s.blank === false) ?? items[0];
	const values = active?.projections?.values ?? {};

	const pressure = values.contextPressure;
	const ctxPercent = pressure?.contextWindow > 0
		? Math.min(100, Math.round((pressure.pressureTokens / pressure.contextWindow) * 100))
		: 0;

	// `done` is an edge, not a state DSH reports: it is "was running, no longer".
	const wasRunning = previous?.running > 0;
	const nowRunning = running.length;
	let phase;
	if (pendingAsks.size > 0) phase = "waiting";
	else if (nowRunning > 0) phase = "working";
	else if (wasRunning && Date.now() - (previous?.stoppedAt ?? 0) < CONFIG.doneHoldMs) phase = "done";
	else phase = "idle";

	const goal = values.goal;
	const todos = values.todos;
	// Prefer the most specific thing we can name as the current activity. A
	// running job is the most concrete: it is what the machine is doing now.
	const runningJob = liveFacts.jobs.find((j) => j.status === "running");
	const detail = runningJob !== undefined ? firstLine(runningJob.label, runningJob.kind)
		: goal?.objective !== undefined ? String(goal.objective).slice(0, 60)
		: Array.isArray(todos) ? `${todos.filter((t) => t.status === "completed").length}/${todos.length} 任务`
		: values.plan?.active === true ? "计划中"
		: values.subagent !== null && values.subagent !== undefined ? "子智能体运行中"
		: "";

	return {
		phase,
		title: String(values.title ?? "").slice(0, 48),
		detail,
		ctx: ctxPercent,
		turns: values.sessionStats?.turns ?? 0,
		steps: values.sessionStats?.steps ?? 0,
		perm: values.permissions?.currentValue ?? "",
		sessions: items.length,
		running: nowRunning,
		seq: active?.projections?.asOfSeq ?? 0,
		// carried for the next derivation, not sent to the device
		stoppedAt: nowRunning === 0 && wasRunning ? Date.now() : (previous?.stoppedAt ?? 0),
	};
}

/**
 * Reduce a job label to one dial-sized line.
 *
 * Job labels are whole shell commands — multi-line, path-heavy, far wider than a
 * 360×360 round screen. The first non-empty line, trimmed, is what a glance can
 * actually use; the job kind is a readable fallback when the label is unusable.
 */
function firstLine(label, kind) {
	const line = String(label ?? "")
		.split("\n")
		.map((l) => l.trim())
		.find((l) => l !== "" && !l.startsWith("$env:") && !l.startsWith("#"));
	if (line === undefined || line === "") return String(kind ?? "");
	return line.slice(0, 40);
}

/** Strip bookkeeping fields before sending to the device. */
function toFrame(state) {
	const { stoppedAt, ...wire } = state;
	return { t: "state", ...wire };
}

// ─────────────────────────────────────────────────────────────────── the bridge

/** Connected devices; a Set because several dials may watch one DSH. */
const devices = new Set();

/** Pending asks by id, so an `answer` frame can be routed back to DSH. */
const pendingAsks = new Map();

/** Latest derived state, and whether DSH is currently reachable. */
let lastState;
let dshOnline = false;

/** Send one JSON frame to one device. */
function send(device, frame) {
	try {
		device.socket.write(encodeFrame(JSON.stringify(frame)));
	} catch {
		devices.delete(device);
	}
}

/** Send one JSON frame to every attached device. */
function broadcast(frame) {
	for (const device of devices) send(device, frame);
}

/**
 * Poll DSH and publish a state frame when something a dial can see has changed.
 *
 * Publishing on change (rather than every tick) keeps a battery-powered device's
 * radio quiet; the device's own freshness timer decides when silence becomes
 * `stale`, which is why every frame carries the numbers it needs to make that
 * call without asking.
 */
async function tick() {
	let snapshot;
	try {
		snapshot = await dshCall("session.list");
		if (!dshOnline) { dshOnline = true; log("DSH link up"); }
	} catch (error) {
		if (dshOnline) {
			dshOnline = false;
			log(`DSH link down (${error.message})`);
			// Honesty: say the link is gone instead of repeating stale numbers.
			broadcast({ t: "state", phase: "error", title: "DSH 离线", detail: "", ctx: 0, turns: 0, steps: 0, perm: "", sessions: 0, running: 0, seq: 0 });
		}
		return;
	}

	const next = deriveState(snapshot, lastState);
	const changed = lastState === undefined ||
		next.phase !== lastState.phase ||
		next.title !== lastState.title ||
		next.detail !== lastState.detail ||
		next.ctx !== lastState.ctx ||
		next.steps !== lastState.steps ||
		next.running !== lastState.running ||
		next.perm !== lastState.perm;

	lastState = next;
	if (changed) {
		broadcast(toFrame(next));
		log(`state → ${next.phase} | ${next.title || "(no title)"} | ctx ${next.ctx}% | ${next.running}/${next.sessions} running`);
	}
	if (next.phase === "done") broadcast({ t: "beep", kind: "done" });
}

/**
 * Handle one decoded frame from a device.
 *
 * `answer` is the frame that justifies the hardware: a physical button ending a
 * prompt without the user finding a window. It is forwarded to DSH's `respond`
 * endpoint; anything else is either telemetry or a control verb.
 */
async function onDeviceFrame(device, text) {
	let frame;
	try { frame = JSON.parse(text); } catch { return; }

	switch (frame.t) {
		case "hello":
			device.info = { fw: frame.fw, board: frame.board };
			log(`device hello: ${frame.board ?? "?"} fw=${frame.fw ?? "?"} battery=${frame.battery ?? "?"}%`);
			// Give a new device the current picture immediately.
			if (lastState !== undefined) send(device, toFrame(lastState));
			else send(device, { t: "state", phase: "idle", title: "", detail: "", ctx: 0, turns: 0, steps: 0, perm: "", sessions: 0, running: 0, seq: 0 });
			break;

		case "ping":
			device.battery = frame.battery;
			device.charging = frame.charging;
			device.lastSeen = Date.now();
			send(device, { t: "pong", at: Date.now() });
			break;

		case "answer": {
			const ask = pendingAsks.get(frame.id);
			if (ask === undefined) { log(`answer for unknown ask ${frame.id}`); return; }
			pendingAsks.delete(frame.id);
			log(`answer: ${frame.id} → ${frame.choice}`);
			try {
				await dshCall("respond", { ...ask.respondPayload, choice: frame.choice });
			} catch (error) {
				log(`WARN respond failed: ${error.message}`);
			}
			break;
		}

		case "cmd":
			log(`device command: ${frame.action}`);
			// Control verbs need endpoints this DSH build does not expose yet
			// (no jobs.* / session.cancel); recorded so the firmware path is
			// testable and the host side can be filled in once they exist.
			break;

		case "sensor":
			device.battery = frame.battery;
			device.charging = frame.charging;
			break;

		default:
			log(`unknown device frame: ${String(frame.t).slice(0, 24)}`);
	}
}

/**
 * Publish an ask to every dial. Called when DSH pushes an approval or question
 * frame; also used by the self-test to exercise the waiting path.
 */
function publishAsk({ kind, title, body, options, respondPayload, ttlMs = 120000 }) {
	const id = `ask-${randomBytes(3).toString("hex")}`;
	pendingAsks.set(id, { respondPayload: respondPayload ?? {}, at: Date.now() });
	broadcast({
		t: "ask",
		id,
		kind,
		title,
		body,
		options,
		expiresAt: Date.now() + ttlMs,
	});
	broadcast({ t: "beep", kind: "waiting" });
	log(`ask published: ${id} (${kind}) ${title}`);
	return id;
}

// ───────────────────────────────────────────────────────── DSH event listener

/**
 * Keep a live WebSocket on DSH's mux stream.
 *
 * Approvals cannot be polled — this build exposes `respond` but no
 * `approvals.list` — so the only way to know a session is waiting on a human is
 * to hold this stream open. Unknown frame types are logged rather than dropped
 * silently: the approval frame's exact name still has to be confirmed against a
 * live prompt, and a census in the log is how that gets confirmed.
 */
function watchDshEvents() {
	const seen = new Set();
	let backoff = 1000;

	const open = () => {
		const key = Buffer.from(randomUUID().replace(/-/g, "").slice(0, 16)).toString("base64");
		const socket = connect({ host: CONFIG.dshHost, port: CONFIG.dshPort }, () => {
			socket.write(
				"GET /api/events.mux HTTP/1.1\r\n" +
				`Host: ${CONFIG.dshHost}:${CONFIG.dshPort}\r\n` +
				"Upgrade: websocket\r\nConnection: Upgrade\r\n" +
				`Sec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\n\r\n`,
			);
		});

		let handshaken = false;
		let buffer = Buffer.alloc(0);
		let read;

		socket.on("data", (chunk) => {
			if (!handshaken) {
				buffer = Buffer.concat([buffer, chunk]);
				const end = buffer.indexOf("\r\n\r\n");
				if (end === -1) return;
				const status = buffer.slice(0, end).toString("utf8").split("\r\n")[0];
				const rest = buffer.slice(end + 4);
				buffer = Buffer.alloc(0);
				if (!status.includes("101")) { log(`event stream handshake failed: ${status}`); socket.destroy(); return; }
				handshaken = true;
				backoff = 1000;
				log("event stream attached");
				read = createFrameReader(onDshFrame, () => socket.destroy());
				if (rest.length > 0) read(rest);
				return;
			}
			read?.(chunk);
		});

		const retry = () => {
			socket.destroy();
			setTimeout(open, backoff);
			backoff = Math.min(backoff * 2, 30000);
		};
		socket.on("error", retry);
		socket.on("close", retry);
	};

	/** Inspect one DSH frame; react to the ones a dial cares about. */
	const onDshFrame = (text) => {
		let frame;
		try { frame = JSON.parse(text); } catch { return; }
		const method = frame.method ?? frame.type ?? "?";
		const payload = frame.payload ?? {};

		// Census: name each frame type once.
		if (!seen.has(method)) {
			seen.add(method);
			log(`event frame type observed: ${method}`);
		}

		// ── session/jobs — running commands → liveFacts.jobs, then re-derive.
		if (method === "session/jobs") {
			const jobs = Array.isArray(payload.jobs) ? payload.jobs : [];
			liveFacts.jobs = jobs;
			liveFacts.jobsAt = Date.now();
			// Re-derive state from the last known snapshot, enriched with the
			// live jobs list, and push if changed.
			if (lastState !== undefined) {
				const next = { ...lastState };
				const runningJob = jobs.find((j) => j.status === "running");
				next.detail = runningJob !== undefined ? firstLine(runningJob.label, runningJob.kind)
					: lastState.detail !== "" && lastState.phase === "working" ? "等待中"
					: "";
				if (next.detail !== lastState.detail) {
					lastState = next;
					broadcast(toFrame(next));
				}
			}
			return;
		}

		// ── session/projection — live projection push → re-derive from it.
		if (method === "session/projection") {
			if (payload.values !== undefined) {
				const snapshot = { items: [{ projections: { values: payload.values } }] };
				const next = deriveState(snapshot, lastState);
				next.sessions = lastState?.sessions ?? 1;
				next.running = lastState?.running ?? 0;
				const changed = lastState === undefined ||
					next.phase !== lastState.phase ||
					next.title !== lastState.title ||
					next.ctx !== lastState.ctx ||
					next.steps !== lastState.steps;
				if (changed) {
					lastState = next;
					broadcast(toFrame(next));
				}
			}
			return;
		}

		// Approval / question frames are the reason the stream is held open.
		if (/approv|permission|user-question|userQuestion|ask/i.test(method)) {
			log(`ASK-SHAPED FRAME: ${method} → ${JSON.stringify(payload).slice(0, 400)}`);
			const p = payload;
			publishAsk({
				kind: /question/i.test(method) ? "question" : "approval",
				title: String(p.title ?? p.question ?? "需要你确认"),
				body: String(p.body ?? p.command ?? p.detail ?? "").slice(0, 160),
				options: Array.isArray(p.options) && p.options.length > 0
					? p.options.slice(0, 3).map((o, i) => ({
						id: String(o.id ?? o.value ?? i),
						label: String(o.label ?? o.name ?? o.value ?? `选项${i + 1}`).slice(0, 8),
						style: i === 0 ? "primary" : "danger",
					}))
					: [
						{ id: "allow", label: "允许", style: "primary" },
						{ id: "deny", label: "拒绝", style: "danger" },
					],
				respondPayload: { rpcId: frame.rpcId, method },
			});
			return;
		}

		// ── session/event — the tool-call / message event stream. Payload
		// shape varies; log the first few to confirm the fields a dial could
		// use for an even more precise `detail`.
		if (method === "session/event" && seen.size < 10) {
			log(`session/event: ${JSON.stringify(payload).slice(0, 300)}`);
		}
	};

	open();
}

// ───────────────────────────────────────────────────────────────── HTTP server

const server = createServer((req, res) => {
	// A tiny status page: proves the bridge is alive without a device attached.
	if (req.url === "/" || req.url === "/status") {
		const body = JSON.stringify({
			bridge: "dsh-esp32-bridge",
			dshOnline,
			devices: devices.size,
			pendingAsks: pendingAsks.size,
			state: lastState === undefined ? null : toFrame(lastState),
		}, null, 2);
		res.writeHead(200, { "content-type": "application/json; charset=utf-8" });
		res.end(body);
		return;
	}
	// /test/ask — inject an ask (used by the self-test harness; otherwise
	// asks come from the DSH event stream).
	if (req.url === "/test/ask" && req.method === "POST") {
		const chunks = [];
		req.on("data", (c) => chunks.push(c));
		req.on("end", () => {
			try {
				const p = JSON.parse(Buffer.concat(chunks).toString("utf8"));
				const id = publishAsk({
					kind: p.kind ?? "approval",
					title: p.title ?? "需要你确认",
					body: p.body ?? "",
					options: p.options ?? [{ id: "allow", label: "允许", style: "primary" }, { id: "deny", label: "拒绝", style: "danger" }],
					respondPayload: p.respondPayload ?? {},
				});
				res.writeHead(200, { "content-type": "application/json" });
				res.end(JSON.stringify({ ok: true, askId: id }));
			} catch (error) {
				res.writeHead(400, { "content-type": "application/json" });
				res.end(JSON.stringify({ ok: false, error: error.message }));
			}
		});
		return;
	}
	res.writeHead(404, { "content-type": "text/plain" });
	res.end("not found");
});

server.on("upgrade", (req, socket, head) => {
	const url = new URL(req.url ?? "/", "http://bridge");
	if (url.pathname !== "/dev") {
		socket.write("HTTP/1.1 404 Not Found\r\n\r\n");
		socket.destroy();
		return;
	}

	// Constant-time token comparison; a LAN peer must not be able to probe it.
	const supplied = Buffer.from(url.searchParams.get("token") ?? "");
	const expected = Buffer.from(TOKEN);
	const ok = supplied.length === expected.length && timingSafeEqual(supplied, expected);
	if (!ok) {
		log(`device rejected: bad token from ${req.socket.remoteAddress}`);
		socket.write("HTTP/1.1 401 Unauthorized\r\n\r\n");
		socket.destroy();
		return;
	}

	const key = req.headers["sec-websocket-key"];
	if (typeof key !== "string") {
		socket.write("HTTP/1.1 400 Bad Request\r\n\r\n");
		socket.destroy();
		return;
	}
	// RFC6455 handshake accept value.
	const accept = createHash("sha1")
		.update(`${key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`)
		.digest("base64");

	socket.write(
		"HTTP/1.1 101 Switching Protocols\r\n" +
		"Upgrade: websocket\r\nConnection: Upgrade\r\n" +
		`Sec-WebSocket-Accept: ${accept}\r\n\r\n`,
	);
	socket.setNoDelay(true);

	const device = { socket, addr: req.socket.remoteAddress, lastSeen: Date.now() };
	devices.add(device);
	log(`device attached from ${device.addr} (${devices.size} total)`);

	const read = createFrameReader(
		(text) => { void onDeviceFrame(device, text); },
		() => { devices.delete(device); socket.destroy(); },
	);
	socket.on("data", (chunk) => { if (head?.length) { read(head); head = undefined; } read(chunk); });
	socket.on("error", () => devices.delete(device));
	socket.on("close", () => {
		devices.delete(device);
		log(`device detached (${devices.size} remaining)`);
	});
});

function startBridge() {
	server.listen(CONFIG.listenPort, "0.0.0.0", () => {
		log(`bridge listening on ws://0.0.0.0:${CONFIG.listenPort}/dev`);
		log(`device token: ${TOKEN}`);
		log(`DSH target: http://${CONFIG.dshHost}:${CONFIG.dshPort}`);
		log(`status page: http://127.0.0.1:${CONFIG.listenPort}/status`);
	});

	setInterval(() => { void tick(); }, CONFIG.pollMs);
	void tick();
	watchDshEvents();
}

// If run as a script (node bridge.js), start the server.
if (process.argv[1] === fileURLToPath(import.meta.url)) {
	startBridge();
}


// Expose the ask publisher for the self-test harness.
export { publishAsk, deriveState, CONFIG };
