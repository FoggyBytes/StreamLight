# Library layout MVP

The library opens in Grid mode by default. Left-stick click (LS/L3), the V key,
or the clickable layout prompt switches between Grid and List. The choice is
saved globally across hosts and restarts. X retains its existing Stop action.

Grid mode shows covers and game titles across the full page, without the
spotlight, store metadata, play time or session figures. Covers have no idle frame
or padding; the focused cover has an accent outline. Missing covers display
the title as a fallback. List mode keeps the existing sections and spotlight.

Run the JavaScript selection/session regressions without Qt dependencies:

```powershell
node --test tests/ui/library-layout.test.cjs
```

These tests execute production QML JavaScript against view/session doubles.
They do not validate QML loading, rendering or physical controller input.

Manual verification using the Windows CI build:

1. Browse a host with enough games for multiple rows. Use the D-pad, left stick
   and keyboard arrows to move in all four directions and scroll into later rows.
2. Select a game halfway down the grid. Click the left stick twice and verify
   that both layouts keep that game selected and visible. Repeat with V and the
   clickable prompt, and check an empty Games or Apps tab.
3. Launch and resume with A/Enter and by clicking a cover. Launch another game
   while one is running and check the existing quit confirmation. Return from
   a stream and verify selection follows the game that was just played.
4. Check LB/RB tabs, LT/RT profiles, Start pinning, right-stick category moves,
   Select per-game settings, Y Settings, B Hosts and X Stop retain their actions.
5. Resize the window, scroll with the mouse, and check missing/portrait covers
   and long titles. Grid mode must have no right-hand details panel.
6. Restart the application and check that the last layout choice is remembered.
