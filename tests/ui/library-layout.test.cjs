const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

// Execute the production QML's JavaScript against small view/session doubles. This
// checks selection and launch behaviour without installing Qt on the developer's PC.
const source = fs.readFileSync(path.join(__dirname, '../../app/gui/AppsScreen.qml'), 'utf8');

test('grid covers reuse HeroCover styling without an idle frame or inner padding', () => {
    const start = source.indexOf('id: tileFrame');
    assert.ok(start >= 0, 'Missing grid cover container');
    const end = source.indexOf('anchors.top: tileFrame.bottom', start);
    assert.ok(end > start, 'Missing grid cover title');
    const tile = source.slice(start, end);
    assert.match(tile, /HeroCover\s*\{\s*id: tileArt/);
    assert.match(tile, /radius: appsRoot\._px\(10\)/);
    assert.match(tile, /shadow: appDelegate\._gridTile && !Theme\.reduceAnimations/);
    assert.match(tile, /shadowOffset: appsRoot\._px\(10\)/);
    assert.match(tile, /anchors\.fill: tileArt\s+radius: tileArt\.radius\s+visible: appDelegate\._lit\s+color: "transparent"\s+border\.width: 3\s+border\.color: Theme\.accent/);
    assert.doesNotMatch(tile, /paintedWidth|paintedHeight|anchors\.margins: appsRoot\._px\(5\)/);
    assert.equal((tile.match(/border\.width:/g) || []).length, 1);
});

function gridBadgeFixture() {
    const binding = source.match(/readonly property string _gridBadgeKind: ([\s\S]*?)(?=\n            width:)/);
    assert.ok(binding, 'Missing grid badge binding');
    const globals = {
        _gridTile: true, _running: false, _lit: false,
        model: { section: 'all' }, Theme: { libraryShowTitles: true },
    };
    const context = vm.createContext(globals);
    return { globals, kind: () => vm.runInContext(`(${binding[1]})`, context) };
}

test('grid streaming badge takes priority over last played and follows running state', () => {
    const f = gridBadgeFixture();
    for (const section of ['continue', 'pinned', 'all']) {
        f.globals.model.section = section;
        f.globals._running = true;
        assert.equal(f.kind(), 'streaming');
        f.globals._running = false;
        assert.equal(f.kind(), section === 'continue' ? 'lastPlayed' : '');
    }
});

test('grid last-played badge uses the existing single Continue role, not the first index', () => {
    const f = gridBadgeFixture();
    f.globals.index = 0;
    assert.equal(f.kind(), '');
    f.globals.model.section = 'pinned';
    assert.equal(f.kind(), '');
    f.globals.model.section = 'continue';
    assert.equal(f.kind(), 'lastPlayed');
    f.globals.model.section = 'all';
    assert.equal(f.kind(), '');
});

test('grid badges remain present without focus or titles and do not change List mode', () => {
    const f = gridBadgeFixture();
    for (const running of [false, true]) {
        f.globals._running = running;
        f.globals.model.section = 'continue';
        for (const focused of [false, true]) {
            f.globals._lit = focused;
            for (const titles of [false, true]) {
                f.globals.Theme.libraryShowTitles = titles;
                assert.equal(f.kind(), running ? 'streaming' : 'lastPlayed');
            }
        }
        f.globals._gridTile = false;
        assert.equal(f.kind(), '');
        f.globals._gridTile = true;
    }
});

test('grid badges are inset on the artwork, use status-specific colours and add no click handlers', () => {
    const start = source.indexOf('id: gridStatusBadge');
    assert.ok(start >= 0, 'Missing grid badge');
    const end = source.indexOf('anchors.top: tileFrame.bottom', start);
    assert.ok(end > start, 'Missing grid cover title');
    const badge = source.slice(start, end);
    assert.match(badge, /anchors\.top: tileArt\.top/);
    assert.match(badge, /anchors\.right: tileArt\.right/);
    assert.match(badge, /visible: appDelegate\._gridBadgeKind !== ""/);
    assert.match(badge, /color: appDelegate\._gridBadgeKind === "streaming" \? Theme\.accent/);
    assert.match(badge, /Qt\.rgba\(Theme\.card\.r, Theme\.card\.g, Theme\.card\.b, 0\.92\)/);
    assert.match(badge, /color: appDelegate\._gridBadgeKind === "streaming" \? Theme\.onAccent : Theme\.text/);
    assert.match(badge, /qsTr\("STREAMING"\)/);
    assert.match(badge, /qsTr\("LAST PLAYED"\)/);
    assert.doesNotMatch(badge, /MouseArea|TapHandler|onClicked|libraryShowTitles|_lit/);
    assert.match(source, /Accessible\.description: _gridBadgeKind === "streaming"/);
});

function gridMetrics(width, scale, coverSize = 1, showTitles = true) {
    const start = source.indexOf('id: coverGrid');
    const end = source.indexOf('// Shared model:', start);
    const grid = source.slice(start, end);
    const globals = {
        width,
        appsRoot: { _px: (value) => Math.round(value * scale) },
        Theme: { libraryCoverSize: coverSize, libraryShowTitles: showTitles },
    };
    for (const name of ['targetCellWidth', 'columns', 'cellWidth', 'cellHeight']) {
        const binding = grid.match(new RegExp(`\\b${name}: ([\\s\\S]*?)(?=\\n        \\S)`));
        assert.ok(binding, `Missing grid binding: ${name}`);
        globals[name] = vm.runInNewContext(`(${binding[1]})`, globals);
    }
    return globals;
}

test('cover sizes change density while Medium preserves the original grid dimensions', () => {
    for (const [width, scale] of [[760, 0.62], [1238, 1], [1933, 1.6]]) {
        const small = gridMetrics(width, scale, 0);
        const medium = gridMetrics(width, scale, 1);
        const large = gridMetrics(width, scale, 2);
        assert.ok(small.columns > medium.columns);
        assert.ok(medium.columns > large.columns);
        assert.ok(small.cellWidth < medium.cellWidth);
        assert.ok(medium.cellWidth < large.cellWidth);
        assert.equal(medium.columns, Math.max(1, Math.floor(width / Math.round(170 * scale))));
        assert.equal(medium.cellHeight,
            Math.round((medium.cellWidth - Math.round(20 * scale)) * 1.5) + Math.round(70 * scale));
    }
    for (const size of [0, 1, 2]) {
        const narrow = gridMetrics(100, 1, size);
        assert.equal(narrow.columns, 1);
        assert.equal(narrow.cellWidth, 100);
        assert.ok(narrow.cellHeight > 0);
    }
});

test('hiding grid titles removes label space without changing columns or cover size', () => {
    for (const size of [0, 1, 2]) {
        for (const scale of [0.62, 1, 1.6]) {
            const titled = gridMetrics(1238, scale, size, true);
            const covers = gridMetrics(1238, scale, size, false);
            assert.equal(covers.columns, titled.columns);
            assert.equal(covers.cellWidth, titled.cellWidth);
            assert.equal(titled.cellHeight - covers.cellHeight,
                Math.round(70 * scale) - Math.round(32 * scale));
        }
    }
    assert.match(source, /anchors\.top: tileFrame\.bottom\s+visible: Theme\.libraryShowTitles/);
    assert.match(source, /Accessible\.name: model\.name/);
    assert.match(source, /visible: tileArt\.status !== Image\.Ready\s+text: model\.name/);
});

test('Interface controls bind to persisted preferences with Medium and titles-on defaults', () => {
    const read = (file) => fs.readFileSync(path.join(__dirname, '../../', file), 'utf8');
    const settings = read('app/gui/SettingsScreen.qml');
    const theme = read('app/settings/theme.cpp');
    const header = read('app/settings/theme.h');
    assert.ok(settings.indexOf('qsTr("Date format")') < settings.indexOf('qsTr("Grid cover size")'));
    assert.ok(settings.indexOf('qsTr("Show grid titles")') < settings.indexOf('qsTr("Accent colour")'));
    assert.match(settings, /Binding on currentIndex \{ value: Theme\.libraryCoverSize \}/);
    assert.match(settings, /onActivated: function\(idx\) \{ Theme\.libraryCoverSize = idx \}/);
    assert.match(settings, /checked: Theme\.libraryShowTitles/);
    assert.match(settings, /onToggled: function\(v\) \{ Theme\.libraryShowTitles = v \}/);
    assert.match(theme, /m_LibraryCoverSize = qBound\(0, settings\.value\(SER_LIBRARY_COVER_SIZE, 1\)\.toInt\(\), 2\)/);
    assert.match(theme, /m_LibraryShowTitles = settings\.value\(SER_LIBRARY_SHOW_TITLES, true\)\.toBool\(\)/);
    assert.match(theme, /settings\.setValue\(SER_LIBRARY_COVER_SIZE, m_LibraryCoverSize\)/);
    assert.match(theme, /settings\.setValue\(SER_LIBRARY_SHOW_TITLES, m_LibraryShowTitles\)/);
    assert.match(theme, /void Theme::setLibraryCoverSize\(int size\)\s*\{\s*size = qBound\(0, size, 2\)/);
    assert.match(header, /Q_PROPERTY\(int libraryCoverSize READ libraryCoverSize WRITE setLibraryCoverSize NOTIFY libraryAppearanceChanged\)/);
    assert.match(header, /Q_PROPERTY\(bool libraryShowTitles READ libraryShowTitles WRITE setLibraryShowTitles NOTIFY libraryAppearanceChanged\)/);
});

function qmlFunction(name, globals) {
    const signature = new RegExp(`^([ \\t]*)function ${name}\\([^\\n]*\\) \\{`, 'm');
    const match = source.match(signature);
    assert.ok(match, `Missing QML function: ${name}`);
    const start = match.index + match[1].length;
    const end = source.indexOf(`\n${match[1]}}`, start);
    assert.ok(end > start, `Missing function end: ${name}`);
    const code = source.slice(start, end + match[1].length + 2);
    return vm.runInNewContext(`(${code})`, globals);
}

function layoutFixture({ count = 8, selected = 5, lookup = selected } = {}) {
    const callbacks = [];
    const calls = [];
    const model = { indexOfAppId(id) { assert.equal(id, 42); return lookup; } };
    const view = (name) => ({
        name, appModel: model, count, currentIndex: selected,
        currentItem: count ? { _appId: 42 } : null,
        positionViewAtIndex(index, mode) { calls.push([name, index, mode]); },
    });
    const grid = view('grid');
    const list = view('list');
    const Theme = { libraryGrid: true };
    const globals = {
        Theme, GridView: { Contain: 4 },
        Qt: { callLater(callback) { callbacks.push(callback); } },
        _playtimeEpoch: 0,
        focusLibrary() { calls.push(['focus', globals.appGrid.name]); },
        get gridLayout() { return Theme.libraryGrid; },
        get appGrid() { return Theme.libraryGrid ? grid : list; },
    };
    globals.appsRoot = globals;
    return { globals, grid, list, callbacks, calls, toggle: qmlFunction('toggleLibraryLayout', globals) };
}

test('layout round trip preserves the selected app and restores focus after layout', () => {
    const f = layoutFixture();
    f.list.currentIndex = 0;
    f.toggle();
    assert.equal(f.globals.Theme.libraryGrid, false);
    assert.equal(f.list.currentIndex, 5);
    assert.deepEqual(f.calls, []);
    f.callbacks.shift()();
    assert.deepEqual(f.calls, [['list', 5, 4], ['focus', 'list']]);
    f.toggle();
    f.callbacks.shift()();
    assert.equal(f.grid.currentIndex, 5);
    assert.equal(f.globals.Theme.libraryGrid, true);
});

test('selection follows app identity when the model reorders', () => {
    const f = layoutFixture({ selected: 5, lookup: 2 });
    f.toggle();
    assert.equal(f.list.currentIndex, 2);
});

test('switching an empty library leaves no selection and does not scroll', () => {
    const f = layoutFixture({ count: 0, selected: -1 });
    f.toggle();
    f.callbacks.shift()();
    assert.equal(f.list.currentIndex, -1);
    assert.deepEqual(f.calls, [['focus', 'list']]);
});

test('an app removed before the switch falls back to the first entry', () => {
    const f = layoutFixture({ lookup: -1 });
    f.toggle();
    assert.equal(f.list.currentIndex, 0);
});

test('rapid toggles do not scroll or focus the now-inactive layout', () => {
    const f = layoutFixture();
    f.toggle();
    f.toggle();
    f.callbacks.forEach((callback) => callback());
    assert.deepEqual(f.calls, [['grid', 5, 4], ['focus', 'grid']]);
});

function pageKeyHandler(globals) {
    const match = source.match(/^([ \t]*)Keys\.onPressed: (function\(event\) \{)/m);
    assert.ok(match, 'Missing page key handler');
    const start = match.index + match[0].indexOf('function');
    const end = source.indexOf(`\n${match[1]}}`, start);
    return vm.runInNewContext(`(${source.slice(start, end + match[1].length + 2)})`, globals);
}

test('LS and V switch layout while X, pin, move, tabs and profiles keep their actions', () => {
    const calls = [];
    const globals = {
        Qt: new Proxy({}, { get: (_, key) => key }),
        focusedAppIsRunning: true,
        toggleLibraryLayout: () => calls.push('layout'),
        stopFocusedApp: () => calls.push('stop'),
        togglePinFocused: () => calls.push('pin'),
        moveFocused: () => calls.push('move'),
        openCustomizeForFocused: () => calls.push('customize'),
        switchLibraryTab: (direction) => calls.push(['tab', direction]),
        cycleProfile: (direction) => calls.push(['profile', direction]),
    };
    const handler = pageKeyHandler(globals);
    const cases = [
        ['Key_F20', 'layout'], ['Key_V', 'layout'], ['Key_X', 'stop'],
        ['Key_F18', 'pin'], ['Key_F19', 'move'], ['Key_F13', 'customize'],
        ['Key_F16', ['tab', -1]], ['Key_F17', ['tab', 1]],
        ['Key_F14', ['profile', -1]], ['Key_F15', ['profile', 1]],
    ];
    for (const [key, expected] of cases) {
        const event = { key, isAutoRepeat: false, accepted: false };
        handler(event);
        assert.deepEqual(calls.pop(), expected);
        assert.equal(event.accepted, true);
    }
    handler({ key: 'Key_V', isAutoRepeat: true });
    handler({ key: 'Key_F20', isAutoRepeat: true });
    assert.deepEqual(calls, []);
});

function launchFixture(layout, runningId = 0, control = '') {
    const launches = [];
    const sessions = [];
    let confirmations = 0;
    const globals = {
        Window: { window: {} },
        model: { appid: 42, name: 'Test game', boxart: 'test.png', control },
        index: 5,
        libraryView: {
            name: layout,
            appModel: {
                getRunningAppId: () => runningId,
                getRunningAppName: () => 'Other game',
                getRunningAppBoxArt: () => 'other.png',
                createSessionForApp(index) { sessions.push(index); return 'session'; },
            },
        },
        appsRoot: { launchSegue(...args) { launches.push(args); } },
        quitAppDialog: { open() { confirmations++; } },
    };
    return {
        globals, launches, sessions,
        get confirmations() { return confirmations; },
        launch: qmlFunction('launchOrResumeSelectedApp', globals),
    };
}

for (const layout of ['grid', 'list']) {
    test(`${layout}: launch uses the selected model entry`, () => {
        const f = launchFixture(layout);
        f.launch(true);
        assert.deepEqual(f.sessions, [5]);
        assert.deepEqual(f.launches, [['Test game', 'test.png', 'session', false, 42]]);
    });

    test(`${layout}: the selected running app resumes`, () => {
        const f = launchFixture(layout, 42);
        f.launch(true);
        assert.equal(f.launches[0][3], true);
        assert.equal(f.confirmations, 0);
    });

    test(`${layout}: another running app needs confirmation before replacement`, () => {
        const f = launchFixture(layout, 99);
        f.launch(true);
        assert.equal(f.confirmations, 1);
        assert.equal(f.globals.quitAppDialog.nextAppId, 42);
        assert.equal(f.globals.quitAppDialog.nextAppName, 'Test game');
        assert.deepEqual(f.sessions, []);
        assert.deepEqual(f.launches, []);
    });

    test(`${layout}: host controls can run alongside a game`, () => {
        const f = launchFixture(layout, 99, 'remote-input');
        f.launch(true);
        assert.equal(f.confirmations, 0);
        assert.equal(f.launches.length, 1);
    });

    for (const guard of ['_streamLaunching', '_streamJustEnded']) {
        test(`${layout}: ${guard} prevents a duplicate/stale launch`, () => {
            const f = launchFixture(layout);
            f.globals.Window.window[guard] = true;
            f.launch(true);
            assert.deepEqual(f.sessions, []);
            assert.deepEqual(f.launches, []);
        });
    }
}
