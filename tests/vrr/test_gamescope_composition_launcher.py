#!/usr/bin/env python3
"""Exercise restoration and failed test setup without changing a compositor."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


LAUNCHER = Path(__file__).resolve().parents[2] / "scripts/moonlight-gamescope-composition-test.py"
FAKE_CONTROL = r'''#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
p = Path(os.environ['MOONLIGHT_TEST_STATE'])
s = json.loads(p.read_text())
a = sys.argv[1:]
s['calls'].append(a)
if a == ['version']:
    print('test Gamescope')
elif a == ['help']:
    print('composite_force: Force composition')
elif a == ['composite_force']:
    print('Command not found.' if s.get('unknown') else 'console: composite_force: ' + str(s['value']), file=sys.stderr)
elif a[0] == 'composite_force':
    if not s.get('ignore_set'): s['value'] = int(a[1])
p.write_text(json.dumps(s))
'''


class CompositionLauncherTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        control = self.root / "gamescopectl"
        control.write_text(FAKE_CONTROL)
        control.chmod(0o755)
        self.state = self.root / "state.json"
        self.env = dict(os.environ, PATH=str(self.root) + os.pathsep + os.environ['PATH'],
                        GAMESCOPE_WAYLAND_DISPLAY="test-gamescope",
                        MOONLIGHT_DEV_LOG_DIR=str(self.root / "logs"),
                        MOONLIGHT_TEST_STATE=str(self.state))

    def run_launcher(self, value=0, code=0, extra=None, command=None):
        state = dict(value=value, calls=[])
        state.update(extra or {})
        self.state.write_text(json.dumps(state))
        if command is None:
            command = [sys.executable, '-c',
                       'import json, os, sys; from pathlib import Path; '
                       's=json.loads(Path(os.environ["MOONLIGHT_TEST_STATE"]).read_text()); '
                       'assert s["value"] == 1; sys.exit(' + str(code) + ')']
        result = subprocess.run([sys.executable, str(LAUNCHER), '--', *command],
                                env=self.env, capture_output=True, text=True, timeout=10)
        return result, json.loads(self.state.read_text())

    def test_force_and_restore(self):
        result, state = self.run_launcher()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(state['value'], 0)
        self.assertIn(['composite_force', '1'], state['calls'])
        self.assertIn(['composite_force', '0'], state['calls'])

    def test_preserve_already_forced_value(self):
        result, state = self.run_launcher(value=1)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(state['value'], 1)
        self.assertFalse(any(len(c) == 2 for c in state['calls']))

    def test_child_failure_still_restores(self):
        result, state = self.run_launcher(code=7)
        self.assertEqual(result.returncode, 7, result.stderr)
        self.assertEqual(state['value'], 0)

    def test_unknown_command_with_zero_exit_is_rejected(self):
        result, state = self.run_launcher(extra={'unknown': True})
        self.assertEqual(result.returncode, 2)
        self.assertFalse(any(len(c) == 2 for c in state['calls']))

    def test_ignored_setting_is_not_a_successful_test(self):
        result, state = self.run_launcher(extra={'ignore_set': True})
        self.assertEqual(result.returncode, 2)
        self.assertEqual(state['value'], 0)
        self.assertIn(['composite_force', '0'], state['calls'])

    def test_failed_launch_restores(self):
        result, state = self.run_launcher(command=[str(self.root / 'missing-program')])
        self.assertEqual(result.returncode, 2)
        self.assertEqual(state['value'], 0)

    def test_lost_condition_is_reported_as_failure(self):
        command = [sys.executable, '-c',
                   'import json, os, time; from pathlib import Path; '
                   'p=Path(os.environ["MOONLIGHT_TEST_STATE"]); '
                   's=json.loads(p.read_text()); s["value"]=0; '
                   'p.write_text(json.dumps(s)); time.sleep(3.6)']
        result, state = self.run_launcher(command=command)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn('TEST CONDITION LOST', result.stderr)
        self.assertEqual(state['value'], 0)

    def test_desktop_mode_does_not_touch_compositor(self):
        self.env.pop('GAMESCOPE_WAYLAND_DISPLAY')
        result, state = self.run_launcher()
        self.assertEqual(result.returncode, 2)
        self.assertEqual(state['calls'], [])


if __name__ == '__main__':
    unittest.main()
