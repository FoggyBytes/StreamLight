# Library layout MVP

The library opens in Grid mode by default. Left-stick click (LS/L3), the V key,
or the clickable layout prompt switches between Grid and List. The choice is
saved globally across hosts and restarts. X retains its existing Stop action.

In Settings, under Session > Interface (after Date format), Grid cover size offers
Small / Medium / Large, and Show grid titles toggles the names below covers. Both
apply without restarting and are saved globally. Defaults are Medium and titles
on. Hiding titles removes their reserved row space, but missing-cover names and
accessible game names remain available. List mode is unchanged.

Grid mode shows covers and game titles across the full page, without the
spotlight, store metadata, play time or session figures. Covers reuse the spotlight's
rounded corners, shadow and 2:3 crop, with no idle frame or padding; the focused
cover has a rounded accent outline. Missing covers display
the title as a fallback. List mode keeps the existing sections and spotlight.

Run the JavaScript selection/session regressions without Qt dependencies:

```powershell
node --test tests/ui/library-layout.test.cjs
```

These tests execute production QML JavaScript against view/session doubles and
check that grid covers use the shared styling component.
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
   and long titles. Check rounded corners and shadows match the spotlight, with
   no grey frame or inner padding and a rounded cyan outline only on the focused
   cover. Grid mode must have no right-hand details panel.
6. Restart the application and check that the last layout choice is remembered.
7. Change Grid cover size through all three choices, then switch Show grid titles
   off and on. Check column count, row spacing, scrolling, selection and launch,
   including the last row and missing covers. Verify both options survive a restart
   and do not affect List mode.
