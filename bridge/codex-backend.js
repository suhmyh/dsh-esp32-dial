/*
 * Codex app-server backend.
 *
 * The desktop app does not expose the old DSH HTTP API. Codex provides a
 * local JSON-RPC app-server over stdio instead. This adapter keeps the device
 * protocol unchanged and projects Codex threads/turns into the same compact
 * state frame consumed by the ESP32 app.
 */
import { spawn } from "node:child_process";

const ACTIVE_TURN = "inProgress";

function trimLine(value, max = 40) {
	return String(value ?? "").replace(/\s+/g, " ").trim().slice(0, max);
}

function itemActivity(item) {
	if (!item || typeof item !== "object") return null;
	if (item.type === "commandExecution") {
		return { t: trimLine(item.command, 40) || "执行命令", r: item.status === "inProgress" };
	}
	if (item.type === "fileChange") {
		const change = Array.isArray(item.changes) ? item.changes[0] : null;
		return { t: trimLine(change?.path, 40) || "修改文件", r: false };
	}
	if (item.type === "webSearch") return { t: `搜索 ${trimLine(item.query, 34)}`, r: true };
	if (item.type === "mcpToolCall") return { t: trimLine(`${item.server}/${item.tool}`, 40), r: item.status === "inProgress" };
	if (item.type === "dynamicToolCall") return { t: trimLine(item.tool, 40) || "调用工具", r: item.status === "inProgress" };
	if (item.type === "agentMessage") return { t: "生成回复", r: false };
	if (item.type === "reasoning") return { t: "思考中", r: true };
	return null;
}

function latestTurn(thread) {
	const turns = Array.isArray(thread?.turns) ? thread.turns : [];
	return turns[0] ?? null;
}

function deriveCodexState(threads, previous, now = Date.now()) {
	const items = Array.isArray(threads) ? threads : [];
	const enriched = items.map((thread) => ({ thread, turn: latestTurn(thread) }));
	const active = enriched.filter(({ turn }) => turn?.status === ACTIVE_TURN);
	const selected = active[0] ?? enriched[0] ?? { thread: null, turn: null };
	const running = active.length;
	const wasRunning = (previous?.running ?? 0) > 0;
	let phase = "idle";
	if (running > 0) {
		const runningItem = selected.turn?.items?.find((item) => item?.status === "inProgress");
		phase = runningItem ? "working" : "thinking";
	} else if (wasRunning && now - (previous?.stoppedAt ?? 0) < 4000) {
		phase = "done";
	} else if (selected.turn?.status === "failed") {
		phase = "error";
	}

	const turn = selected.turn;
	const activities = (turn?.items ?? []).map(itemActivity).filter(Boolean).slice(-3);
	const runningItem = activities.find((item) => item.r === true);
	const detail = phase === "thinking" ? "思考中"
		: phase === "done" ? "任务完成"
		: phase === "error" ? "任务失败"
		: runningItem?.t ?? activities.at(-1)?.t ?? "";
	// Keep the product label stable; thread names are user-authored and may be
	// long or contain glyphs the 360px device font cannot render.
	const title = "Codex";
	const turnCount = selected.thread?.turns?.length ?? 0;
	const steps = turn?.items?.length ?? 0;
	return {
		phase,
		title: trimLine(title, 48) || "Codex",
		detail,
		ctx: 0,
		turns: turnCount,
		steps,
		perm: "",
		sessions: items.length,
		running,
		acts: activities,
		stats: "",
		s: null,
		clock: `${String(new Date().getHours()).padStart(2, "0")}:${String(new Date().getMinutes()).padStart(2, "0")}`,
		seq: Number(turn?.id ? 0 : selected.thread?.updatedAt ?? 0),
		stoppedAt: running === 0 && wasRunning ? now : (previous?.stoppedAt ?? 0),
	};
}

class CodexAppServer {
	constructor({ command = "codex", pollMs = 1000, maxThreads = 20 } = {}) {
		this.command = command;
		this.pollMs = pollMs;
		this.maxThreads = maxThreads;
		this.child = null;
		this.buffer = "";
		this.nextId = 1;
		this.pending = new Map();
		this.timer = null;
		this.previous = undefined;
		this.stopped = true;
		this.onState = () => {};
		this.onOnline = () => {};
	}

	start(onState, onOnline) {
		this.stopped = false;
		this.onState = onState;
		this.onOnline = onOnline;
		this.child = spawn(this.command, ["app-server", "--stdio"], { stdio: ["pipe", "pipe", "pipe"] });
		this.child.stdout.on("data", (chunk) => this.onStdout(chunk));
		this.child.stderr.on("data", () => {});
		this.child.on("error", (error) => this.fail(error));
		this.child.on("exit", (code) => {
			if (!this.stopped && code !== 0) this.fail(new Error(`codex app-server exited (${code ?? "signal"})`));
		});
		void this.initialize();
	}

	async initialize() {
		try {
			await this.request("initialize", {
				clientInfo: { name: "dsh-esp32-bridge", version: "1.0.0" },
				capabilities: { experimentalApi: true },
			});
			this.notify("initialized", {});
			this.onOnline(true);
			await this.poll();
			if (this.stopped) return;
			this.timer = setInterval(() => { void this.poll(); }, this.pollMs);
		} catch (error) {
			this.fail(error);
		}
	}

	onStdout(chunk) {
		this.buffer += chunk.toString();
		for (;;) {
			const newline = this.buffer.indexOf("\n");
			if (newline < 0) return;
			const line = this.buffer.slice(0, newline).trim();
			this.buffer = this.buffer.slice(newline + 1);
			if (!line) continue;
			let message;
			try { message = JSON.parse(line); } catch { continue; }
			if (message.id !== undefined && this.pending.has(String(message.id))) {
				const pending = this.pending.get(String(message.id));
				this.pending.delete(String(message.id));
				clearTimeout(pending.timer);
				if (message.error) pending.reject(new Error(message.error.message || "Codex app-server error"));
				else pending.resolve(message.result);
			}
		}
	}

	request(method, params = {}) {
		return new Promise((resolve, reject) => {
			if (!this.child || this.child.stdin.destroyed) return reject(new Error("Codex app-server is not running"));
			const id = String(this.nextId++);
			const timer = setTimeout(() => {
				this.pending.delete(id);
				reject(new Error(`${method} timed out`));
			}, 30000);
			this.pending.set(id, { resolve, reject, timer });
			this.child.stdin.write(`${JSON.stringify({ id: Number(id), method, params })}\n`);
		});
	}

	notify(method, params = {}) {
		if (this.child && !this.child.stdin.destroyed) this.child.stdin.write(`${JSON.stringify({ method, params })}\n`);
	}

	async poll() {
		try {
			const listed = await this.request("thread/list", {
				limit: this.maxThreads,
				archived: false,
				sortKey: "updated_at",
				sortDirection: "desc",
				useStateDbOnly: true,
			});
			const list = Array.isArray(listed?.data) ? listed.data : [];
			const hydrated = await Promise.all(list.map(async (thread) => {
				try {
					const turns = await this.request("thread/turns/list", {
						threadId: thread.id,
						limit: 3,
						itemsView: "summary",
						sortDirection: "desc",
					});
					return { ...thread, turns: turns?.data ?? [] };
				} catch {
					return { ...thread, turns: [] };
				}
			}));
			const next = deriveCodexState(hydrated, this.previous);
			this.previous = next;
			this.onState(next);
			this.onOnline(true);
		} catch (error) {
			this.fail(error);
		}
	}

	fail(error) {
		this.onOnline(false, error);
		this.onState({ t: "state", phase: "error", title: "CODEX OFFLINE", detail: "Check Codex app-server", ctx: 0, turns: 0, steps: 0, perm: "", sessions: 0, running: 0, acts: [], seq: 0 });
	}

	stop() {
		this.stopped = true;
		if (this.timer !== null) clearInterval(this.timer);
		this.timer = null;
		for (const pending of this.pending.values()) { clearTimeout(pending.timer); pending.reject(new Error("stopped")); }
		this.pending.clear();
		this.child?.kill();
		this.child = null;
	}
}

export { CodexAppServer, deriveCodexState };
