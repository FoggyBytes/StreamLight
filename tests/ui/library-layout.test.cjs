const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

// Execute the production QML's JavaScript against small view/session doubles. This
// checks selection and launch behaviour without installing Qt on the developer's PC.
const source = fs.readFileSync(path.join(__dirname, '../../app/gui/AppsScreen.qml'), 'utf8');

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
