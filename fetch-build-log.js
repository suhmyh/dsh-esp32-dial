/**
 * Fetch the failing step's log straight from the Actions API.
 *
 * `gh run view --log` insists on writing a zip into its own cache directory,
 * which the sandbox denies, so this pulls the per-job plain-text log instead and
 * prints only the compiler diagnostics.
 */
import { request } from "node:https";
import { execSync } from "node:child_process";

const REPO = "suhmyh/dsh-esp32-dial";
const RUN_ID = process.argv[2] ?? "31919907635";
const token = execSync("gh auth token", { encoding: "utf8" }).trim();

function api(path, { raw = false } = {}) {
	return new Promise((resolve, reject) => {
		const req = request(
			{
				hostname: "api.github.com",
				path,
				headers: {
					authorization: `Bearer ${token}`,
					"user-agent": "dsh-build-log",
					accept: raw ? "*/*" : "application/vnd.github+json",
					"x-github-api-version": "2022-11-28",
				},
				timeout: 60000,
			},
			(res) => {
				if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
					res.resume();
					// Log downloads redirect to blob storage without auth headers.
					const url = new URL(res.headers.location);
					const sub = request(
						{ hostname: url.hostname, path: url.pathname + url.search, headers: { "user-agent": "dsh-build-log" }, timeout: 60000 },
						(r2) => {
							let body = "";
							r2.setEncoding("utf8");
							r2.on("data", (c) => { body += c; });
							r2.on("end", () => resolve(body));
						},
					);
					sub.on("error", reject);
					sub.end();
					return;
				}
				if (res.statusCode !== 200) { res.resume(); reject(new Error(`HTTP ${res.statusCode} ${path}`)); return; }
				let body = "";
				res.setEncoding("utf8");
				res.on("data", (c) => { body += c; });
				res.on("end", () => resolve(raw ? body : JSON.parse(body)));
			},
		);
		req.on("error", reject);
		req.on("timeout", () => req.destroy(new Error("timeout")));
		req.end();
	});
}

const jobs = await api(`/repos/${REPO}/actions/runs/${RUN_ID}/jobs`);
for (const job of jobs.jobs) {
	if (job.conclusion !== "failure") continue;
	console.log(`\n### job ${job.name} (${job.id}) — ${job.conclusion}`);
	const log = await api(`/repos/${REPO}/actions/jobs/${job.id}/logs`, { raw: true });
	const lines = log.split("\n");

	// Compiler diagnostics are what matter; keep their surrounding context.
	const interesting = [];
	lines.forEach((line, i) => {
		if (/\berror\b|\bError\b|fatal error|undefined reference|No such file/.test(line)) {
			interesting.push({ i, line });
		}
	});
	if (interesting.length === 0) {
		console.log("(no error lines matched — printing tail)");
		console.log(lines.slice(-40).join("\n"));
	} else {
		const shown = new Set();
		for (const { i } of interesting.slice(0, 40)) {
			for (let j = Math.max(0, i - 2); j <= Math.min(lines.length - 1, i + 2); j++) {
				if (!shown.has(j)) { shown.add(j); console.log(lines[j].replace(/^\S+\s/, "")); }
			}
			console.log("  ---");
		}
	}
}