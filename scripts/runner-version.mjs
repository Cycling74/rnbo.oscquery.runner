#!/usr/bin/env node
// Runner versioning: one source of truth for the runner version.
//
// The runner version is `<rnbo version>-<app version>`, for example 1.4.6-11.
// The app version lives in the `runner-version` file at the repo root. This
// script checks that a version is allowed (never going backwards), lists what
// is known, stamps the changelog and creates tags.
//
//   runner-version.mjs check --rnbo <v> [--prerelease beta.N] [--tag <tag>] [--published -] [--force]
//   runner-version.mjs list [--published -]
//   runner-version.mjs release <rnbo version>... [--beta] [--at <existing tag>] [--published -]
//   runner-version.mjs notes --tag <tag>
//
// Tags:    v1.4.6-11 (stable), v1.4.6-11-beta.1 (app beta)
// Debian:  1.4.6-11,           1.4.6-11~beta.1
//
// `--published -` reads additional known versions (one Debian version per
// line) from stdin. The RUNNER_APT_BASE environment variable overrides the base
// URL of the public apt repository the main index is read from.
//
// No dependencies, node built-ins only. The pure functions are exported for
// scripts/runner-version.test.mjs, the CLI at the bottom is a thin wrapper.

import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';

export const TAG_REGEX = /^v(.+)-([0-9]+)(-(beta\.[0-9]+))?$/;
export const APT_BASE = 'https://c74-apt.nyc3.digitaloceanspaces.com/raspbian';
export const APT_SUITE = 'bookworm';
export const APT_ARCHES = ['armhf', 'arm64'];
export const PACKAGE_NAME = 'rnbooscquery';

//// dpkg version ordering, a port of verrevcmp() from dpkg's lib/dpkg/version.c

const isDigit = (c) => c !== undefined && c >= '0' && c <= '9';
const isAlpha = (c) => c !== undefined && /[A-Za-z]/.test(c);

function order(c) {
	if (c === undefined || isDigit(c)) return 0;
	if (isAlpha(c)) return c.charCodeAt(0);
	if (c === '~') return -1;
	return c.charCodeAt(0) + 256;
}

function verrevcmp(a, b) {
	let i = 0;
	let j = 0;
	while (i < a.length || j < b.length) {
		let firstDiff = 0;
		while ((i < a.length && !isDigit(a[i])) || (j < b.length && !isDigit(b[j]))) {
			const ac = order(a[i]);
			const bc = order(b[j]);
			if (ac !== bc) return ac - bc;
			i++;
			j++;
		}
		while (a[i] === '0') i++;
		while (b[j] === '0') j++;
		while (isDigit(a[i]) && isDigit(b[j])) {
			if (!firstDiff) firstDiff = a.charCodeAt(i) - b.charCodeAt(j);
			i++;
			j++;
		}
		if (isDigit(a[i])) return 1;
		if (isDigit(b[j])) return -1;
		if (firstDiff) return firstDiff;
	}
	return 0;
}

function splitDebian(v) {
	let epoch = 0;
	let rest = v;
	const e = /^(\d+):(.*)$/.exec(v);
	if (e) {
		epoch = parseInt(e[1], 10);
		rest = e[2];
	}
	const dash = rest.lastIndexOf('-');
	if (dash < 0) return { epoch, upstream: rest, revision: '' };
	return { epoch, upstream: rest.slice(0, dash), revision: rest.slice(dash + 1) };
}

// negative, zero or positive, like dpkg --compare-versions
export function compareVersions(a, b) {
	const x = splitDebian(a);
	const y = splitDebian(b);
	if (x.epoch !== y.epoch) return x.epoch < y.epoch ? -1 : 1;
	const u = verrevcmp(x.upstream, y.upstream);
	if (u) return u < 0 ? -1 : 1;
	const r = verrevcmp(x.revision, y.revision);
	return r < 0 ? -1 : r > 0 ? 1 : 0;
}

//// parsing

// Semver shaped, X.Y.Z optionally followed by -ident(.ident)* with non-empty identifiers.
// `1.4.6` -> stable, `1.5.0-dev.150` -> channel `dev`, `1.4.5-alpha.0` -> channel `alpha`
export function parseRnbo(lib) {
	const m = /^(\d+\.\d+\.\d+)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$/.exec(lib);
	if (!m) return null;
	const pre = m[2] || null;
	const channel = pre ? ((/^[A-Za-z]+/.exec(pre) || [null])[0]) : null;
	return { base: m[1], prerelease: pre, channel, stable: pre === null };
}

export function formatVersion({ rnbo, n, prerelease }) {
	return `${rnbo}-${n}${prerelease ? `~${prerelease}` : ''}`;
}

export function tagName({ rnbo, n, prerelease }) {
	return `v${rnbo}-${n}${prerelease ? `-${prerelease}` : ''}`;
}

// null for tags that are not runner release tags (rnbo_v1.4.6, update_v0.4.1-1, ...)
export function parseTag(tag) {
	const m = TAG_REGEX.exec(tag);
	if (!m) return null;
	const info = { tag, rnbo: m[1], n: parseInt(m[2], 10), prerelease: m[4] || null };
	info.debian = formatVersion(info);
	return info;
}

// `1.4.6-11~beta.1` -> lib 1.4.6, n 11, prerelease beta.1.
// A version without a trailing numeric segment (1.5.0-dev.124, 1.4.5, 1.4.5-alpha.0) is legacy: n is null.
export function parseDebianVersion(debian) {
	const v = debian.trim();
	const m = /^(.+)-(\d+)(?:~(.+))?$/.exec(v);
	if (m) return { debian: v, lib: m[1], n: parseInt(m[2], 10), prerelease: m[3] || null, legacy: false };
	return { debian: v, lib: v, n: null, prerelease: null, legacy: true };
}

// one Debian version per line, blank lines ignored. Anything else is an error, a
// feed that is not a list of versions must not be silently accepted.
export function parseVersionLines(text) {
	const versions = [];
	text.split('\n').forEach((raw, i) => {
		const l = raw.trim();
		if (!l) return;
		if (!/^[0-9][A-Za-z0-9.+~-]*$/.test(l)) {
			throw new Error(`published versions, line ${i + 1}: '${l}' is not a Debian version (expected one version per line, like 1.4.6-11)`);
		}
		versions.push(l);
	});
	return versions;
}

// is this text a Debian Packages index, at least one `Package:` stanza (HTML or an empty body is not)
export function isPackagesIndex(text) {
	return /^Package:[ \t]*\S/m.test(text);
}

// the versions of the rnbooscquery package out of a Debian Packages index
export function parsePackagesIndex(text, pkg = PACKAGE_NAME) {
	const versions = [];
	for (const stanza of text.split(/\r?\n\s*\r?\n/)) {
		let name = null;
		let version = null;
		for (const line of stanza.split(/\r?\n/)) {
			const m = /^(Package|Version):\s*(.+?)\s*$/.exec(line);
			if (!m) continue;
			if (m[1] === 'Package') name = m[2];
			else version = m[2];
		}
		if (name === pkg && version) versions.push(version);
	}
	return versions;
}

//// known versions

// sources: { tags: [tag name], published: [debian version] }. Each entry carries
// `tag` (name of the tag, or null) and `published` (in apt or stdin)
export function buildKnown({ tags = [], published = [] }) {
	const map = new Map();
	const entry = (parsed) => {
		if (!map.has(parsed.debian)) map.set(parsed.debian, { ...parsed, tag: null, published: false });
		return map.get(parsed.debian);
	};
	for (const t of tags) {
		const info = parseTag(t);
		if (!info) continue;
		const e = entry({ debian: info.debian, lib: info.rnbo, n: info.n, prerelease: info.prerelease, legacy: false });
		e.tag = t;
	}
	for (const v of published) {
		entry(parseDebianVersion(v)).published = true;
	}
	return [...map.values()];
}

// with --tag the tag itself is expected to exist, it does not count as already known
export function excludeTag(known, tag) {
	return known
		.map((e) => (e.tag === tag ? { ...e, tag: null } : e))
		.filter((e) => e.tag !== null || e.published);
}

const stableN = (known) => {
	let max = null;
	for (const e of known) {
		const r = parseRnbo(e.lib);
		if (e.n === null || e.prerelease || !r || !r.stable) continue;
		if (max === null || e.n > max.n) max = { n: e.n, versions: [] };
		if (e.n === max.n) max.versions.push(e.debian);
	}
	return max;
};

const where = (e) => [e.tag ? `tag ${e.tag}` : null, e.published ? 'apt' : null].filter(Boolean).join(' and ');

// candidate: { rnbo, n, prerelease }. Returns a list of failures { rule, version, message }.
// Optional: fileN (the runner-version file) and tagN, to evaluate rule 6;
// changelog (CHANGELOG.md text) to require a stamped section for a stable tag.
export function evaluateRules({ candidate, known, fileN, tagN, changelog }) {
	const failures = [];
	const fail = (rule, version, message) => failures.push({ rule, version, message });
	const c = { ...candidate, debian: formatVersion(candidate) };
	const rnbo = parseRnbo(c.rnbo);
	if (!rnbo) fail('input', c.debian, `'${c.rnbo}' is not a valid RNBO version`);

	// 1. the exact version must not exist
	for (const e of known) {
		if (e.debian === c.debian) fail('rule 1', e.debian, `${e.debian} already exists (${where(e)})`);
	}

	// 2. strictly greater than everything with the same RNBO library version
	for (const e of known) {
		if (e.lib !== c.rnbo || e.debian === c.debian) continue;
		if (compareVersions(c.debian, e.debian) <= 0) {
			fail('rule 2', e.debian, `${c.debian} is not greater than the existing ${e.debian} (${where(e)})`);
		}
	}

	// 3. N never goes below the highest stable N published for any RNBO version
	const stable = stableN(known);
	if (stable && c.n < stable.n) {
		fail('rule 3', stable.versions.join(', '), `app version ${c.n} is lower than the highest stable app version ${stable.n} (${stable.versions.join(', ')})`);
	}

	// 4. on a prerelease RNBO channel N never goes below the highest N on that channel
	if (rnbo && rnbo.channel) {
		let max = null;
		for (const e of known) {
			const r = parseRnbo(e.lib);
			if (e.n === null || !r || r.channel !== rnbo.channel) continue;
			if (max === null || e.n > max.n) max = { n: e.n, versions: [] };
			if (e.n === max.n) max.versions.push(e.debian);
		}
		if (max && c.n < max.n) {
			fail('rule 4', max.versions.join(', '), `app version ${c.n} is lower than the highest app version ${max.n} on the ${rnbo.channel} channel (${max.versions.join(', ')})`);
		}
	}

	// 5. an app beta must be newer than the highest stable N
	if (c.prerelease && stable && c.n <= stable.n) {
		fail('rule 5', stable.versions.join(', '), `${c.debian} is a ${c.prerelease} of app version ${c.n}, which must be greater than the highest stable app version ${stable.n} (${stable.versions.join(', ')})`);
	}

	// 6. the N in the tag is the N in the runner-version file at the tagged commit
	if (tagN !== undefined && fileN !== undefined && tagN !== fileN) {
		fail('rule 6', c.debian, `the tag says app version ${tagN} but runner-version holds ${fileN}`);
	}

	// a stable tag needs a stamped changelog section
	if (changelog !== undefined && changelog !== null && tagN !== undefined && !c.prerelease && !hasStampedSection(changelog, c.n)) {
		fail('changelog', c.debian, `CHANGELOG.md has no stamped section for app version ${c.n} (expected a heading like '* *${c.n}* — <date> — RNBO ${c.rnbo}')`);
	}
	return failures;
}

// next unused beta number for this rnbo version and app version
export function nextBetaNumber(known, rnbo, n) {
	let max = 0;
	for (const e of known) {
		if (e.lib !== rnbo || e.n !== n || !e.prerelease) continue;
		const m = /^beta\.(\d+)$/.exec(e.prerelease);
		if (m) max = Math.max(max, parseInt(m[1], 10));
	}
	return max + 1;
}

//// list

export function buildListRows(known) {
	return [...known]
		.sort((a, b) => compareVersions(a.debian, b.debian))
		.map((e) => {
			let note = '';
			const isStable = !e.prerelease && !e.legacy;
			if (e.tag && !e.published) note = e.prerelease ? 'beta, tag only' : 'tagged but unpublished';
			else if (e.published && !e.tag && !e.legacy) note = 'published, no tag';
			return {
				version: e.debian,
				app: e.n === null ? '-' : String(e.n),
				rnbo: e.lib,
				prerelease: e.prerelease || '-',
				tag: e.tag ? 'yes' : '-',
				main: e.published ? 'yes' : '-',
				note,
				unpublished: isStable && !!e.tag && !e.published,
			};
		});
}

export function formatListTable(rows) {
	const cols = [
		['VERSION', 'version'],
		['APP', 'app'],
		['RNBO', 'rnbo'],
		['PRE', 'prerelease'],
		['TAG', 'tag'],
		['MAIN', 'main'],
		['NOTE', 'note'],
	];
	const widths = cols.map(([h, k]) => Math.max(h.length, ...rows.map((r) => r[k].length)));
	const line = (get) => cols.map(([h, k], i) => get(h, k).padEnd(widths[i])).join('  ').trimEnd();
	return [line((h) => h), ...rows.map((r) => line((h, k) => r[k]))].join('\n');
}

//// changelog
//
// Top level bullets are releases: `* *current*`, `* *11* — 2026-11-20 — RNBO 1.4.5, 1.4.6`
// and legacy `* *1.4.5-9*`. Indented lines below them are the entries.

const HEADING = /^\* \*([^*]+)\*(.*)$/;

export function parseChangelog(text) {
	const lines = text.split('\n');
	const sections = [];
	lines.forEach((l, i) => {
		const m = HEADING.exec(l);
		if (!m) return;
		if (sections.length) sections[sections.length - 1].end = i;
		sections.push({ label: m[1], rest: m[2], start: i, end: lines.length });
	});
	return { lines, sections };
}

const findStamped = (cl, n) => cl.sections.find((s) => /^\d+$/.test(s.label) && parseInt(s.label, 10) === n);
const findCurrent = (cl) => cl.sections.find((s) => s.label === 'current');
const bodyLines = (cl, s) => {
	const body = cl.lines.slice(s.start + 1, s.end);
	while (body.length && body[body.length - 1].trim() === '') body.pop();
	return body;
};

export function hasStampedSection(text, n) {
	return !!findStamped(parseChangelog(text), n);
}

// does the `*current*` section have any entries
export function currentHasEntries(text) {
	const cl = parseChangelog(text);
	const cur = findCurrent(cl);
	return !!cur && bodyLines(cl, cur).some((l) => l.trim() !== '');
}

// rename `*current*` to the stamped heading and put a fresh empty `*current*` above it
export function stampChangelog(text, { n, date, rnbos }) {
	const cl = parseChangelog(text);
	const cur = findCurrent(cl);
	if (!cur) throw new Error('CHANGELOG.md has no `* *current*` section');
	const lines = [...cl.lines];
	lines.splice(cur.start, 1, '* *current*', `* *${n}* — ${date} — RNBO ${rnbos.join(', ')}`);
	return lines.join('\n');
}

// add another RNBO version to the heading of an already stamped app version
export function addRnboToHeading(text, n, rnbo) {
	const cl = parseChangelog(text);
	const s = findStamped(cl, n);
	if (!s) throw new Error(`CHANGELOG.md has no stamped section for app version ${n}`);
	const m = /^(.* — RNBO )(.+)$/.exec(s.rest);
	if (!m) throw new Error(`cannot find the RNBO list in the heading of app version ${n}: '* *${s.label}*${s.rest}'`);
	const existing = m[2].split(',').map((v) => v.trim());
	if (existing.includes(rnbo)) throw new Error(`the heading of app version ${n} already lists RNBO ${rnbo}`);
	const lines = [...cl.lines];
	lines[s.start] = `* *${s.label}*${m[1]}${[...existing, rnbo].join(', ')}`;
	return lines.join('\n');
}

// body of the section for a tag, dedented so it renders as a list. null when there is no section.
export function notesForTag(text, tag) {
	const cl = parseChangelog(text);
	let s;
	if (tag.prerelease) {
		s = findCurrent(cl);
	} else {
		s = findStamped(cl, tag.n) || cl.sections.find((x) => x.label === `${tag.rnbo}-${tag.n}`);
	}
	if (!s) return null;
	const body = bodyLines(cl, s);
	const indent = Math.min(...body.filter((l) => l.trim() !== '').map((l) => /^ */.exec(l)[0].length));
	const cut = Number.isFinite(indent) ? indent : 0;
	return body.map((l) => l.slice(Math.min(cut, /^ */.exec(l)[0].length))).join('\n');
}

//// apt

export function aptIndexUrls(base, arch) {
	const root = `${base.replace(/\/+$/, '')}/dists/${APT_SUITE}/main/binary-${arch}`;
	return [`${root}/Packages`, `${root}/Packages.gz`];
}

// Versions of rnbooscquery in the apt main index. Per arch: 403/404 means the arch
// is absent, a body that is a Packages index is read, anything else (5xx, network
// error, a body that is not an index) is an error. At least one arch must be readable.
export async function fetchAptVersions(base = APT_BASE, fetchImpl = fetch) {
	const versions = new Set();
	const errors = [];
	const absent = [];
	let readable = 0;
	for (const arch of APT_ARCHES) {
		let got = false;
		for (const url of aptIndexUrls(base, arch)) {
			let res;
			try {
				res = await fetchImpl(url, { signal: AbortSignal.timeout(30000) });
			} catch (e) {
				errors.push(`${url}: ${e.message}`);
				break;
			}
			if (res.status === 403 || res.status === 404) {
				absent.push(`${url}: HTTP ${res.status}`);
				continue;
			}
			if (!res.ok) {
				errors.push(`${url}: HTTP ${res.status}`);
				break;
			}
			let text;
			try {
				let buf = Buffer.from(await res.arrayBuffer());
				if (url.endsWith('.gz')) buf = zlib.gunzipSync(buf);
				text = buf.toString('utf8');
			} catch (e) {
				errors.push(`${url}: ${e.message}`);
				break;
			}
			if (!isPackagesIndex(text)) {
				errors.push(`${url}: HTTP 200 but the body is not a Packages index`);
				break;
			}
			for (const v of parsePackagesIndex(text)) versions.add(v);
			got = true;
			break;
		}
		if (got) readable++;
	}
	if (errors.length || !readable) {
		throw new Error(`could not read the apt main index from ${base}:\n  ${[...errors, ...(readable ? [] : absent)].join('\n  ')}`);
	}
	return [...versions];
}

//// git and files (CLI side)

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '..');

function git(args, { allowFail = false } = {}) {
	try {
		return execFileSync('git', args, { cwd: root, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] });
	} catch (e) {
		if (allowFail) return null;
		throw new Error(`git ${args.join(' ')} failed: ${(e.stderr || e.message).toString().trim()}`);
	}
}

const tagExists = (tag) => git(['rev-parse', '-q', '--verify', `refs/tags/${tag}`], { allowFail: true }) !== null;

function parseN(text, what) {
	const v = text.trim();
	if (!/^\d+$/.test(v)) throw new Error(`${what} must contain a single integer, found '${v}'`);
	return parseInt(v, 10);
}

function readRunnerVersionFile() {
	return parseN(fs.readFileSync(path.join(root, 'runner-version'), 'utf8'), 'runner-version');
}

// the app version at a tag: runner-version, or the old hard coded CMakeLists.txt value for tags before the file existed
function readRunnerVersionAt(tag) {
	if (!tagExists(tag)) return readRunnerVersionFile();
	const f = git(['show', `${tag}:runner-version`], { allowFail: true });
	if (f !== null) return parseN(f, `runner-version at ${tag}`);
	const cm = git(['show', `${tag}:CMakeLists.txt`], { allowFail: true });
	const m = cm && /^set\(RUNNER_VERSION\s+(\d+)\)/m.exec(cm);
	if (m) return parseInt(m[1], 10);
	throw new Error(`cannot find the app version at ${tag}`);
}

function readChangelogAt(tag) {
	if (tag && tagExists(tag)) {
		const c = git(['show', `${tag}:CHANGELOG.md`], { allowFail: true });
		return c === null ? '' : c;
	}
	return fs.readFileSync(path.join(root, 'CHANGELOG.md'), 'utf8');
}

const readChangelog = () => fs.readFileSync(path.join(root, 'CHANGELOG.md'), 'utf8');
const writeChangelog = (text) => fs.writeFileSync(path.join(root, 'CHANGELOG.md'), text);

// the checks only see local tags, a shallow repository has an incomplete history
function requireFullHistory({ force = false } = {}) {
	if ((git(['rev-parse', '--is-shallow-repository'], { allowFail: true }) || '').trim() !== 'true') return;
	const msg = 'this is a shallow repository, the version checks need all tags: fetch full history and tags first (git fetch --unshallow --tags)';
	if (!force) throw new Error(msg);
	console.error(`warning (--force): ${msg}`);
}

const gitTags = () => git(['tag', '--list']).split('\n').map((t) => t.trim()).filter(Boolean);

function readStdinVersions() {
	return parseVersionLines(fs.readFileSync(0, 'utf8'));
}

// apt main (unless it fails and force is set) plus anything on stdin
async function gatherPublished({ stdin, force }) {
	const published = [];
	const warnings = [];
	try {
		published.push(...(await fetchAptVersions(process.env.RUNNER_APT_BASE || APT_BASE)));
	} catch (e) {
		if (!force) throw e;
		warnings.push(`warning (--force): ${e.message}`);
	}
	if (stdin) published.push(...readStdinVersions());
	return { published, warnings };
}

function publishedFlag(values) {
	if (values.published === undefined) return false;
	if (values.published !== '-') throw new Error(`--published only accepts '-' (read versions from stdin), got '${values.published}'`);
	return true;
}

function printFailures(failures, force) {
	for (const f of failures) {
		console.error(`${force ? 'warning' : 'FAIL'} ${f.rule}: ${f.message}`);
	}
}

//// commands

const today = (d = new Date()) => `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`;

async function cmdCheck(argv) {
	const { values } = parseArgs({
		args: argv,
		options: {
			rnbo: { type: 'string' },
			prerelease: { type: 'string' },
			tag: { type: 'string' },
			published: { type: 'string' },
			force: { type: 'boolean', default: false },
		},
		allowPositionals: false,
	});
	const stdin = publishedFlag(values);
	if (values.prerelease !== undefined && !/^beta\.[0-9]+$/.test(values.prerelease)) {
		throw new Error(`--prerelease must look like beta.1, got '${values.prerelease}'`);
	}

	let candidate;
	let tagN;
	if (values.tag) {
		const info = parseTag(values.tag);
		if (!info) throw new Error(`'${values.tag}' is not a runner release tag (expected ${TAG_REGEX})`);
		if (values.rnbo !== undefined && values.rnbo !== info.rnbo) {
			throw new Error(`--rnbo ${values.rnbo} disagrees with the tag ${values.tag} (RNBO ${info.rnbo})`);
		}
		if (values.prerelease !== undefined && values.prerelease !== info.prerelease) {
			throw new Error(`--prerelease ${values.prerelease} disagrees with the tag ${values.tag} (prerelease ${info.prerelease || 'none'})`);
		}
		candidate = { rnbo: info.rnbo, n: info.n, prerelease: info.prerelease };
		tagN = info.n;
	} else {
		if (!values.rnbo) throw new Error('check needs --rnbo <version> or --tag <tag>');
		candidate = { rnbo: values.rnbo, n: readRunnerVersionFile(), prerelease: values.prerelease || null };
	}

	requireFullHistory({ force: values.force });
	const { published, warnings } = await gatherPublished({ stdin, force: values.force });
	warnings.forEach((w) => console.error(w));
	let known = buildKnown({ tags: gitTags(), published });
	if (values.tag) known = excludeTag(known, values.tag);

	const failures = evaluateRules({
		candidate,
		known,
		fileN: values.tag ? readRunnerVersionAt(values.tag) : undefined,
		tagN,
		changelog: values.tag ? readChangelogAt(values.tag) : undefined,
	});
	const version = formatVersion(candidate);
	if (!failures.length) {
		console.log(`ok: ${version} is allowed`);
		return 0;
	}
	printFailures(failures, values.force);
	if (values.force) {
		console.error(`--force: ignoring ${failures.length} failure(s) for ${version}`);
		return 0;
	}
	console.error(`${version} is not allowed (${failures.length} failure(s))`);
	return 1;
}

async function cmdList(argv) {
	const { values } = parseArgs({ args: argv, options: { published: { type: 'string' } }, allowPositionals: false });
	const stdin = publishedFlag(values);
	const { published } = await gatherPublished({ stdin, force: false });
	const rows = buildListRows(buildKnown({ tags: gitTags(), published }));
	console.log(formatListTable(rows));
	const un = rows.filter((r) => r.unpublished);
	if (un.length) {
		console.log(`\ntagged but not published: ${un.map((r) => r.version).join(', ')}`);
	}
	console.log(`\napp version in development (runner-version): ${readRunnerVersionFile()}`);
	return 0;
}

function requireCleanTree() {
	// untracked files are ignored, except the two that release edits
	for (const f of ['runner-version', 'CHANGELOG.md']) {
		if (git(['ls-files', '--error-unmatch', f], { allowFail: true }) === null || git(['cat-file', '-e', `HEAD:${f}`], { allowFail: true }) === null) {
			throw new Error(`${f} is not tracked in HEAD, commit it first`);
		}
	}
	const status = git(['status', '--porcelain', '--untracked-files=no']);
	if (status.trim()) throw new Error(`the working tree is not clean, commit or stash first:\n${status.trimEnd()}`);
}

async function cmdRelease(argv) {
	const { values, positionals } = parseArgs({
		args: argv,
		options: {
			beta: { type: 'boolean', default: false },
			at: { type: 'string' },
			published: { type: 'string' },
		},
		allowPositionals: true,
	});
	const stdin = publishedFlag(values);
	if (!positionals.length) throw new Error('release needs at least one RNBO version');
	if (values.beta && values.at) throw new Error('--beta and --at cannot be combined');
	if (new Set(positionals).size !== positionals.length) throw new Error('duplicate RNBO version');
	for (const v of positionals) {
		if (!parseRnbo(v)) throw new Error(`'${v}' is not a valid RNBO version`);
	}
	requireCleanTree();
	requireFullHistory();

	const { published } = await gatherPublished({ stdin, force: false });
	const known = buildKnown({ tags: gitTags(), published });
	const changelog = readChangelog();

	let n;
	let atCommit = null;
	if (values.at) {
		if (!tagExists(values.at)) throw new Error(`--at ${values.at}: no such tag`);
		atCommit = git(['rev-list', '-n', '1', values.at]).trim();
		n = readRunnerVersionAt(values.at);
	} else {
		n = readRunnerVersionFile();
	}

	// plan: everything is checked before anything is changed
	const plan = positionals.map((rnbo) => ({
		rnbo,
		n,
		prerelease: values.beta ? `beta.${nextBetaNumber(known, rnbo, n)}` : null,
	}));
	let bad = 0;
	for (const c of plan) {
		const failures = evaluateRules({ candidate: c, known });
		if (failures.length) {
			console.error(`${formatVersion(c)}:`);
			printFailures(failures, false);
			bad += failures.length;
		}
	}
	if (bad) throw new Error(`${bad} check failure(s), nothing was changed`);

	if (!values.beta && !values.at) {
		if (!currentHasEntries(changelog)) throw new Error('the `* *current*` section of CHANGELOG.md has no entries, nothing to release');
		if (hasStampedSection(changelog, n)) throw new Error(`CHANGELOG.md already has a stamped section for app version ${n}`);
	}
	if (values.at) {
		if (!hasStampedSection(changelog, n)) throw new Error(`CHANGELOG.md has no stamped section for app version ${n} to add the RNBO version to`);
		if (!hasStampedSection(readChangelogAt(values.at), n)) {
			console.error(`warning: CHANGELOG.md at ${values.at} has no stamped section for app version ${n}, the build for the new tag(s) will fail the changelog check`);
		}
	}

	// the tag names and the new text are validated before anything is changed
	const tags = plan.map((c) => tagName(c));
	for (const t of tags) {
		if (git(['check-ref-format', `refs/tags/${t}`], { allowFail: true }) === null) throw new Error(`'${t}' is not a valid tag name, nothing was changed`);
		if (tagExists(t)) throw new Error(`tag ${t} already exists, nothing was changed`);
	}
	let newChangelog = null;
	if (values.at) {
		newChangelog = changelog;
		for (const c of plan) newChangelog = addRnboToHeading(newChangelog, n, c.rnbo);
	} else if (!values.beta) {
		newChangelog = stampChangelog(changelog, { n, date: today(), rnbos: positionals });
	}

	const startHead = git(['rev-parse', 'HEAD']).trim();
	const createdTags = [];
	let committed = 0;
	const tag = (t, commit) => {
		git(commit ? ['tag', t, commit] : ['tag', t]);
		createdTags.push(t);
	};
	const commit = (file, text, message) => {
		fs.writeFileSync(path.join(root, file), text);
		git(['add', file]);
		git(['commit', '-m', message]);
		committed++;
	};
	try {
		if (values.beta) {
			for (const t of tags) tag(t);
		} else if (values.at) {
			commit('CHANGELOG.md', newChangelog, `Add RNBO ${positionals.join(', ')} to the runner ${n} changelog`);
			for (const t of tags) tag(t, atCommit);
		} else {
			commit('CHANGELOG.md', newChangelog, `Release runner ${n} for RNBO ${positionals.join(', ')}`);
			for (const t of tags) tag(t);
			commit('runner-version', `${n + 1}\n`, `Start runner ${n + 1}`);
		}
	} catch (e) {
		console.error(`release failed part way: ${e.message}`);
		console.error(`created ${committed} commit(s) and ${createdTags.length} tag(s)${createdTags.length ? `: ${createdTags.join(', ')}` : ''}`);
		console.error('to undo (nothing was pushed):');
		if (createdTags.length) console.error(`  git tag -d ${createdTags.join(' ')}`);
		if (committed) console.error(`  git reset --hard ${startHead}`);
		else if (!values.beta) console.error('  git checkout -- CHANGELOG.md runner-version');
		throw new Error('release incomplete, see above');
	}

	const branch = git(['rev-parse', '--abbrev-ref', 'HEAD']).trim();
	console.log(`created ${tags.join(', ')}`);
	console.log('nothing was pushed, to publish run:');
	console.log(`  git push origin ${branch} ${tags.join(' ')}`);
	return 0;
}

async function cmdNotes(argv) {
	const { values } = parseArgs({ args: argv, options: { tag: { type: 'string' } }, allowPositionals: false });
	if (!values.tag) throw new Error('notes needs --tag <tag>');
	const info = parseTag(values.tag);
	if (!info) throw new Error(`'${values.tag}' is not a runner release tag`);
	const notes = notesForTag(readChangelogAt(values.tag), info);
	if (notes === null) {
		console.error(`no changelog section found for ${values.tag}`);
		return 1;
	}
	console.log(notes);
	return 0;
}

const USAGE = `usage:
  runner-version.mjs check --rnbo <version> [--prerelease beta.N] [--tag <tag>] [--published -] [--force]
  runner-version.mjs list [--published -]
  runner-version.mjs release <rnbo version>... [--beta] [--at <existing tag>] [--published -]
  runner-version.mjs notes --tag <tag>`;

export async function main(argv) {
	const [cmd, ...rest] = argv;
	const commands = { check: cmdCheck, list: cmdList, release: cmdRelease, notes: cmdNotes };
	if (!commands[cmd]) {
		console.error(USAGE);
		return 2;
	}
	try {
		return await commands[cmd](rest);
	} catch (e) {
		console.error(`error: ${e.message}`);
		return 1;
	}
}

if (process.argv[1] && fs.realpathSync(process.argv[1]) === fileURLToPath(import.meta.url)) {
	process.exitCode = await main(process.argv.slice(2));
}
