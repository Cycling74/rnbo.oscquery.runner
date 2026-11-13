// node --test scripts/
import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import zlib from 'node:zlib';

import {
	addRnboToHeading,
	buildKnown,
	buildListRows,
	compareVersions,
	currentHasEntries,
	evaluateRules,
	excludeTag,
	fetchAptVersions,
	isPackagesIndex,
	formatListTable,
	formatVersion,
	hasStampedSection,
	nextBetaNumber,
	notesForTag,
	parseDebianVersion,
	parsePackagesIndex,
	parseRnbo,
	parseTag,
	parseVersionLines,
	stampChangelog,
	tagName,
} from './runner-version.mjs';

const lt = (a, b) => {
	assert.ok(compareVersions(a, b) < 0, `${a} < ${b}`);
	assert.ok(compareVersions(b, a) > 0, `${b} > ${a}`);
};

test('dpkg ordering', () => {
	lt('1.4.6-10', '1.4.6-11~beta.1');
	lt('1.4.6-11~beta.1', '1.4.6-11~beta.2');
	lt('1.4.6-11~beta.2', '1.4.6-11');
	lt('1.4.6-10', '1.4.6-11');
	lt('1.5.0-dev.150', '1.5.0-dev.150-11');
	lt('1.5.0-dev.149-11', '1.5.0-dev.150-11');
	lt('1.4.6-11~beta.2', '1.4.6-11~beta.10');
	lt('1.4.6-9', '1.4.6-10');
	lt('1.4.5', '1.4.5-9');
	lt('1.4.5-9', '1.4.6-9');
	lt('1.0~rc1-1', '1.0-1');
	lt('1.0', '1.0a');
	lt('1:0.1', '2:0.0');
	assert.equal(compareVersions('1.4.6-11', '1.4.6-11'), 0);
	assert.equal(compareVersions('1.04-1', '1.4-1'), 0);
});

test('tag parsing', () => {
	assert.deepEqual(parseTag('v1.4.6-11'), { tag: 'v1.4.6-11', rnbo: '1.4.6', n: 11, prerelease: null, debian: '1.4.6-11' });
	assert.deepEqual(parseTag('v1.4.6-11-beta.1'), { tag: 'v1.4.6-11-beta.1', rnbo: '1.4.6', n: 11, prerelease: 'beta.1', debian: '1.4.6-11~beta.1' });
	const dev = parseTag('v1.5.0-dev.150-11');
	assert.equal(dev.rnbo, '1.5.0-dev.150');
	assert.equal(dev.n, 11);
	assert.equal(dev.debian, '1.5.0-dev.150-11');
	const devBeta = parseTag('v1.5.0-dev.150-11-beta.2');
	assert.equal(devBeta.rnbo, '1.5.0-dev.150');
	assert.equal(devBeta.prerelease, 'beta.2');
	assert.equal(parseTag('v1.4.0-dev.0-2').rnbo, '1.4.0-dev.0');
	for (const ignored of ['rnbo_v1.4.6', 'update_v0.4.1-1', 'v1.4.6', 'v1.4.6-beta.1', 'v1.4.6-11-alpha.1', '1.4.6-11']) {
		assert.equal(parseTag(ignored), null, ignored);
	}
	assert.equal(tagName({ rnbo: '1.4.6', n: 11, prerelease: 'beta.1' }), 'v1.4.6-11-beta.1');
});

test('debian string parsing', () => {
	assert.deepEqual(parseDebianVersion('1.4.6-11'), { debian: '1.4.6-11', lib: '1.4.6', n: 11, prerelease: null, legacy: false });
	assert.deepEqual(parseDebianVersion('1.4.6-11~beta.1'), { debian: '1.4.6-11~beta.1', lib: '1.4.6', n: 11, prerelease: 'beta.1', legacy: false });
	const dashed = parseDebianVersion('1.4.0-dev.0-2');
	assert.equal(dashed.lib, '1.4.0-dev.0');
	assert.equal(dashed.n, 2);
	for (const legacy of ['1.5.0-dev.124', '1.4.5', '1.4.5-alpha.0']) {
		const p = parseDebianVersion(legacy);
		assert.equal(p.legacy, true, legacy);
		assert.equal(p.n, null);
		assert.equal(p.lib, legacy);
	}
	assert.equal(formatVersion({ rnbo: '1.4.6', n: 11, prerelease: 'beta.1' }), '1.4.6-11~beta.1');
});

test('rnbo version parsing', () => {
	assert.deepEqual(parseRnbo('1.4.6'), { base: '1.4.6', prerelease: null, channel: null, stable: true });
	assert.equal(parseRnbo('1.5.0-dev.150').channel, 'dev');
	assert.equal(parseRnbo('1.5.0-dev.150').stable, false);
	assert.equal(parseRnbo('1.4.5-alpha.0').channel, 'alpha');
	assert.equal(parseRnbo('nonsense'), null);
	for (const bad of ['1.4.7-dev..1', '1.4.7-', '1.4.7-.dev', '1.4.7-dev.', '1.4', '1.4.7.1', 'v1.4.7', '1.4.7-dev 1', "1.4.7-dev';x"]) {
		assert.equal(parseRnbo(bad), null, bad);
	}
	assert.equal(parseRnbo('1.4.0-dev.0').channel, 'dev');
});

test('version lines and Packages index', () => {
	assert.deepEqual(parseVersionLines('1.4.6-9\n\n  1.4.5-9  \n'), ['1.4.6-9', '1.4.5-9']);
	assert.deepEqual(parseVersionLines(''), []);
	assert.deepEqual(parseVersionLines('\n  \n'), []);
	assert.deepEqual(parseVersionLines('1.5.0-dev.150-12\n1.4.6-11~beta.1\n'), ['1.5.0-dev.150-12', '1.4.6-11~beta.1']);
	assert.throws(() => parseVersionLines('1.4.6-9\nVersion: 1.5.0-dev.150-12\n'), /line 2: 'Version: 1\.5\.0-dev\.150-12'/);
	assert.throws(() => parseVersionLines('rnbooscquery_1.4.6-9_armhf.deb\n'), /line 1/);
	assert.throws(() => parseVersionLines('<html>\n'), /not a Debian version/);
	assert.throws(() => parseVersionLines('1.4.6 -9\n'), /line 1/);
	assert.equal(isPackagesIndex('Package: rnbooscquery\nVersion: 1.4.6-9\n'), true);
	assert.equal(isPackagesIndex('<html><body>Package: nope</body></html>'), false);
	assert.equal(isPackagesIndex(''), false);
	const index = [
		'Package: rnbo-runner-panel', 'Version: 2.4.1', '',
		'Package: rnbooscquery', 'Version: 1.4.6-9', 'Architecture: armhf', '',
		'Package: rnbooscquery', 'Version: 1.4.5', '',
	].join('\n');
	assert.deepEqual(parsePackagesIndex(index), ['1.4.6-9', '1.4.5']);
});

// what is known in the real repo, more or less
const base = () => buildKnown({
	tags: ['v1.4.4-8', 'v1.4.4-9', 'v1.4.5-9', 'v1.4.5-10', 'v1.4.6-9', 'rnbo_v1.4.6', 'update_v0.4.1-1'],
	published: ['1.4.4-8', '1.4.4-9', '1.4.5-9', '1.4.6-9', '1.4.5', '1.3.2-2'],
});
const rules = (candidate, known = base(), extra = {}) => evaluateRules({ candidate, known, ...extra });
const ruleNames = (failures) => failures.map((f) => f.rule);

test('known versions merge tags and published', () => {
	const known = base();
	const byVersion = Object.fromEntries(known.map((e) => [e.debian, e]));
	assert.equal(byVersion['1.4.5-10'].tag, 'v1.4.5-10');
	assert.equal(byVersion['1.4.5-10'].published, false);
	assert.equal(byVersion['1.4.6-9'].published, true);
	assert.equal(byVersion['1.4.5'].legacy, true);
	assert.equal(byVersion['rnbo_v1.4.6'], undefined);
});

test('rules pass for the next release', () => {
	assert.deepEqual(rules({ rnbo: '1.4.6', n: 11, prerelease: null }), []);
	assert.deepEqual(rules({ rnbo: '1.4.6', n: 11, prerelease: 'beta.1' }), []);
});

test('rule 1: the exact version must not exist', () => {
	assert.ok(ruleNames(rules({ rnbo: '1.4.6', n: 9, prerelease: null })).includes('rule 1'));
	// tagged but not published
	const f = rules({ rnbo: '1.4.5', n: 10, prerelease: null });
	assert.ok(f.some((x) => x.rule === 'rule 1' && x.version === '1.4.5-10'));
	// published only, with the tag excluded
	const known = excludeTag(buildKnown({ tags: ['v1.4.6-11'], published: ['1.4.6-11'] }), 'v1.4.6-11');
	const g = evaluateRules({ candidate: { rnbo: '1.4.6', n: 11, prerelease: null }, known });
	assert.ok(g.some((x) => x.rule === 'rule 1' && x.version === '1.4.6-11'));
});

test('rule 1: the tag itself is excluded with --tag', () => {
	const known = excludeTag(buildKnown({ tags: ['v1.4.6-11'], published: ['1.4.6-9'] }), 'v1.4.6-11');
	assert.deepEqual(evaluateRules({ candidate: { rnbo: '1.4.6', n: 11, prerelease: null }, known }), []);
	// without the exclusion it would fail
	const again = buildKnown({ tags: ['v1.4.6-11'], published: ['1.4.6-9'] });
	assert.ok(ruleNames(evaluateRules({ candidate: { rnbo: '1.4.6', n: 11, prerelease: null }, known: again })).includes('rule 1'));
});

test('rule 2: strictly greater than the same RNBO version', () => {
	const known = buildKnown({ tags: ['v1.4.6-9', 'v1.4.6-11-beta.2'] });
	const f = evaluateRules({ candidate: { rnbo: '1.4.6', n: 11, prerelease: 'beta.1' }, known });
	assert.ok(f.some((x) => x.rule === 'rule 2' && x.version === '1.4.6-11~beta.2'));
	// a stable release is above its betas
	assert.ok(!ruleNames(evaluateRules({ candidate: { rnbo: '1.4.6', n: 11, prerelease: null }, known })).includes('rule 2'));
	// below an existing stable of the same rnbo
	const g = evaluateRules({ candidate: { rnbo: '1.4.6', n: 8, prerelease: null }, known });
	assert.ok(g.some((x) => x.rule === 'rule 2' && x.version === '1.4.6-9'));
	// a legacy bare version of the same RNBO version is below
	const legacy = buildKnown({ published: ['1.4.5'] });
	assert.deepEqual(evaluateRules({ candidate: { rnbo: '1.4.5', n: 1, prerelease: null }, known: legacy }), []);
	// dev versions: 1.5.0-dev.150 alone is below 1.5.0-dev.150-11
	const dev = buildKnown({ published: ['1.5.0-dev.150'] });
	assert.deepEqual(evaluateRules({ candidate: { rnbo: '1.5.0-dev.150', n: 11, prerelease: null }, known: dev }), []);
	// other RNBO versions do not matter for rule 2
	assert.ok(!ruleNames(rules({ rnbo: '1.4.7', n: 10, prerelease: null })).includes('rule 2'));
});

test('rule 3: N never goes below the highest stable N', () => {
	const known = buildKnown({ published: ['1.4.5-9', '1.4.6-9', '1.4.6-10'] });
	assert.deepEqual(evaluateRules({ candidate: { rnbo: '1.4.5', n: 10, prerelease: null }, known }), []);
	const f = evaluateRules({ candidate: { rnbo: '1.4.7', n: 9, prerelease: null }, known });
	assert.deepEqual(ruleNames(f), ['rule 3']);
	assert.equal(f[0].version, '1.4.6-10');
	// the real repo: 1.4.6 at app version 9 or lower
	assert.ok(ruleNames(rules({ rnbo: '1.4.8', n: 9, prerelease: null })).includes('rule 3'));
	// dev and beta versions do not raise the stable bar
	const devKnown = buildKnown({ published: ['1.4.5-9', '1.5.0-dev.150-20', '1.4.8-30~beta.1'] });
	assert.deepEqual(evaluateRules({ candidate: { rnbo: '1.4.6', n: 10, prerelease: null }, known: devKnown }), []);
});

test('rule 4: N never goes below the highest N on the same channel', () => {
	const known = buildKnown({ published: ['1.4.5-9', '1.5.0-dev.149-10'] });
	const f = evaluateRules({ candidate: { rnbo: '1.5.0-dev.150', n: 9, prerelease: null }, known });
	assert.ok(f.some((x) => x.rule === 'rule 4' && x.version === '1.5.0-dev.149-10'));
	assert.deepEqual(evaluateRules({ candidate: { rnbo: '1.5.0-dev.150', n: 10, prerelease: null }, known }), []);
	// another channel is not affected
	const alpha = buildKnown({ published: ['1.5.0-dev.149-10'] });
	assert.deepEqual(evaluateRules({ candidate: { rnbo: '1.5.0-alpha.1', n: 3, prerelease: null }, known: alpha }), []);
	const alphaKnown = buildKnown({ published: ['1.5.0-alpha.1-5'] });
	assert.ok(ruleNames(evaluateRules({ candidate: { rnbo: '1.5.0-alpha.2', n: 4, prerelease: null }, known: alphaKnown })).includes('rule 4'));
	// the issue example: 1.5.0-dev.149-11 is known, candidate dev.150 at 10
	const pub = buildKnown({ tags: ['v1.4.5-10'], published: ['1.5.0-dev.149-11'] });
	assert.ok(ruleNames(evaluateRules({ candidate: { rnbo: '1.5.0-dev.150', n: 10, prerelease: null }, known: pub })).includes('rule 4'));
	assert.deepEqual(evaluateRules({ candidate: { rnbo: '1.5.0-dev.150', n: 11, prerelease: null }, known: pub }), []);
});

test('rule 5: an app beta must be above the highest stable N', () => {
	const f = rules({ rnbo: '1.4.6', n: 10, prerelease: 'beta.1' });
	assert.ok(f.some((x) => x.rule === 'rule 5'));
	assert.ok(ruleNames(rules({ rnbo: '1.4.7', n: 10, prerelease: 'beta.1' })).includes('rule 5'));
	assert.deepEqual(rules({ rnbo: '1.4.7', n: 11, prerelease: 'beta.1' }), []);
	// a stable at the same N is fine, only betas are held to this
	assert.ok(!ruleNames(rules({ rnbo: '1.4.7', n: 10, prerelease: null })).includes('rule 5'));
});

test('rule 6: the tag N matches the runner-version file', () => {
	const cand = { rnbo: '1.4.6', n: 12, prerelease: null };
	const f = rules(cand, base(), { tagN: 12, fileN: 11 });
	assert.ok(f.some((x) => x.rule === 'rule 6'));
	assert.ok(!ruleNames(rules({ ...cand, n: 11 }, base(), { tagN: 11, fileN: 11, changelog: '* *current*\n* *11* — 2026-11-20 — RNBO 1.4.6\n    * x' })).length);
});

test('stable tag needs a stamped changelog section', () => {
	const cand = { rnbo: '1.4.6', n: 11, prerelease: null };
	const without = rules(cand, base(), { tagN: 11, fileN: 11, changelog: '* *current*\n    * x\n' });
	assert.deepEqual(ruleNames(without), ['changelog']);
	const withSection = rules(cand, base(), { tagN: 11, fileN: 11, changelog: '* *current*\n* *11* — 2026-11-20 — RNBO 1.4.6\n    * x\n' });
	assert.deepEqual(withSection, []);
	// betas do not need one
	const beta = rules({ ...cand, prerelease: 'beta.1' }, base(), { tagN: 11, fileN: 11, changelog: '* *current*\n    * x\n' });
	assert.deepEqual(beta, []);
});

test('next beta number', () => {
	const known = buildKnown({ tags: ['v1.4.6-11-beta.1', 'v1.4.6-11-beta.2', 'v1.4.6-12-beta.7', 'v1.4.5-11-beta.4'] });
	assert.equal(nextBetaNumber(known, '1.4.6', 11), 3);
	assert.equal(nextBetaNumber(known, '1.4.6', 13), 1);
	assert.equal(nextBetaNumber(known, '1.4.7', 11), 1);
});

test('list rows flag tagged but unpublished stable versions', () => {
	const rows = buildListRows(buildKnown({
		tags: ['v1.4.5-9', 'v1.4.5-10', 'v1.4.6-11-beta.1'],
		published: ['1.4.5-9', '1.4.5', '1.4.7-9'],
	}));
	assert.deepEqual(rows.map((r) => r.version), ['1.4.5', '1.4.5-9', '1.4.5-10', '1.4.6-11~beta.1', '1.4.7-9']);
	const by = Object.fromEntries(rows.map((r) => [r.version, r]));
	assert.equal(by['1.4.5-10'].unpublished, true);
	assert.equal(by['1.4.5-10'].note, 'tagged but unpublished');
	assert.equal(by['1.4.5-9'].unpublished, false);
	assert.equal(by['1.4.6-11~beta.1'].unpublished, false);
	assert.equal(by['1.4.6-11~beta.1'].note, 'beta, tag only');
	assert.equal(by['1.4.5'].app, '-');
	assert.equal(by['1.4.7-9'].note, 'published, no tag');
	assert.match(formatListTable(rows), /^VERSION/);
});

const CHANGELOG = [
	'# RNBO Runner Changes',
	'',
	'* *current*',
	'    * first change',
	'        * detail',
	'    * second change',
	'      that wraps',
	'* *10* — 2026-07-27 — RNBO 1.4.5',
	'    * ten change',
	'* *1.4.5-9*',
	'    * legacy change',
	'',
].join('\n');

test('changelog: current entries', () => {
	assert.equal(currentHasEntries(CHANGELOG), true);
	assert.equal(currentHasEntries('# x\n\n* *current*\n* *10* — 2026-07-27 — RNBO 1.4.5\n    * a\n'), false);
	assert.equal(currentHasEntries('# x\n\n* *current*\n\n'), false);
	assert.equal(currentHasEntries('# x\n\n* *10* — 2026-07-27 — RNBO 1.4.5\n    * a\n'), false);
});

test('changelog: stamp', () => {
	const stamped = stampChangelog(CHANGELOG, { n: 11, date: '2026-11-20', rnbos: ['1.4.5', '1.4.6'] });
	assert.equal(stamped, [
		'# RNBO Runner Changes',
		'',
		'* *current*',
		'* *11* — 2026-11-20 — RNBO 1.4.5, 1.4.6',
		'    * first change',
		'        * detail',
		'    * second change',
		'      that wraps',
		'* *10* — 2026-07-27 — RNBO 1.4.5',
		'    * ten change',
		'* *1.4.5-9*',
		'    * legacy change',
		'',
	].join('\n'));
	assert.equal(currentHasEntries(stamped), false);
	assert.equal(hasStampedSection(stamped, 11), true);
	assert.equal(hasStampedSection(CHANGELOG, 11), false);
	assert.equal(hasStampedSection(CHANGELOG, 10), true);
	assert.throws(() => stampChangelog('# x\n', { n: 1, date: 'd', rnbos: ['1.0.0'] }));
});

test('changelog: add an RNBO version to a stamped heading', () => {
	const out = addRnboToHeading(CHANGELOG, 10, '1.4.6');
	assert.match(out, /^\* \*10\* — 2026-07-27 — RNBO 1\.4\.5, 1\.4\.6$/m);
	assert.throws(() => addRnboToHeading(CHANGELOG, 10, '1.4.5'), /already lists/);
	assert.throws(() => addRnboToHeading(CHANGELOG, 12, '1.4.5'), /no stamped section/);
});

test('changelog: notes', () => {
	const stamped = stampChangelog(CHANGELOG, { n: 11, date: '2026-11-20', rnbos: ['1.4.6'] });
	assert.equal(notesForTag(stamped, parseTag('v1.4.6-11')), [
		'* first change',
		'    * detail',
		'* second change',
		'  that wraps',
	].join('\n'));
	assert.equal(notesForTag(stamped, parseTag('v1.4.5-10')), '* ten change');
	// legacy heading by rnbo version and app version
	assert.equal(notesForTag(stamped, parseTag('v1.4.5-9')), '* legacy change');
	assert.equal(notesForTag(stamped, parseTag('v1.4.6-9')), null);
	assert.equal(notesForTag(stamped, parseTag('v1.4.6-12')), null);
	// beta tags use the current section
	assert.equal(notesForTag(CHANGELOG, parseTag('v1.4.6-11-beta.1')), notesForTag(CHANGELOG, parseTag('v1.4.6-11-beta.2')));
	assert.match(notesForTag(CHANGELOG, parseTag('v1.4.6-11-beta.1')), /^\* first change/);
	assert.equal(notesForTag('# x\n* *10* — d — RNBO 1\n    * a\n', parseTag('v1.4.6-11-beta.1')), null);
});

const INDEX = 'Package: rnbooscquery\nVersion: 1.4.6-9\n\nPackage: rnbooscquery\nVersion: 1.4.5-9\n';

// serve each url path suffix with a handler (status, body), default 403
async function withRepo(routes, fn) {
	const hits = [];
	const server = http.createServer((req, res) => {
		hits.push(req.url);
		const key = Object.keys(routes).find((k) => req.url.endsWith(k));
		const [status, body] = key ? routes[key] : [403, ''];
		res.statusCode = status;
		res.end(body);
	});
	await new Promise((r) => server.listen(0, '127.0.0.1', r));
	try {
		await fn(`http://127.0.0.1:${server.address().port}/repo/`, hits);
	} finally {
		server.close();
	}
}

test('apt index fetch: an absent arch plus a good arch passes, Packages.gz is the fallback', async () => {
	await withRepo({ '/binary-armhf/Packages': [404, ''], '/binary-armhf/Packages.gz': [200, zlib.gzipSync(INDEX)] }, async (base, hits) => {
		assert.deepEqual((await fetchAptVersions(base)).sort(), ['1.4.5-9', '1.4.6-9']);
		assert.ok(hits.every((u) => u.startsWith('/repo/dists/bookworm/main/binary-')));
		assert.ok(hits.some((u) => u.endsWith('/binary-arm64/Packages.gz')));
	});
	// both arches readable, versions merged
	await withRepo({ '/binary-armhf/Packages': [200, INDEX], '/binary-arm64/Packages': [200, 'Package: rnbooscquery\nVersion: 1.4.7-9\n'] }, async (base) => {
		assert.deepEqual((await fetchAptVersions(base)).sort(), ['1.4.5-9', '1.4.6-9', '1.4.7-9']);
	});
});

test('apt index fetch: a 5xx on one arch fails even if the other is fine', async () => {
	await withRepo({ '/binary-armhf/Packages': [200, INDEX], '/binary-arm64/Packages': [500, 'oops'] }, async (base) => {
		await assert.rejects(fetchAptVersions(base), /binary-arm64\/Packages: HTTP 500/);
	});
});

test('apt index fetch: a 200 that is not an index fails', async () => {
	await withRepo({ '/binary-armhf/Packages': [200, INDEX], '/binary-arm64/Packages': [200, '<html>Not Found</html>'] }, async (base) => {
		await assert.rejects(fetchAptVersions(base), /not a Packages index/);
	});
	await withRepo({ '/binary-armhf/Packages': [200, ''] }, async (base) => {
		await assert.rejects(fetchAptVersions(base), /not a Packages index/);
	});
	await withRepo({ '/binary-armhf/Packages': [404, ''], '/binary-armhf/Packages.gz': [200, zlib.gzipSync('<html></html>')] }, async (base) => {
		await assert.rejects(fetchAptVersions(base), /not a Packages index/);
	});
	// a corrupt .gz is an error too
	await withRepo({ '/binary-armhf/Packages': [404, ''], '/binary-armhf/Packages.gz': [200, 'not gzip'] }, async (base) => {
		await assert.rejects(fetchAptVersions(base), /binary-armhf\/Packages\.gz/);
	});
});

test('apt index fetch: a network error fails, and no readable arch is an error', async () => {
	await assert.rejects(fetchAptVersions('http://127.0.0.1:9'), /could not read the apt main index/);
	await withRepo({}, async (base) => {
		await assert.rejects(fetchAptVersions(base), /could not read the apt main index/);
	});
});
