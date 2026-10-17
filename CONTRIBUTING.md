# Contributing

This fork targets MediaTek Mali devices. Before changing anything, read the display pipeline in
`README.md`: most "it does not show a picture" reports come from the Xvfb → presenter chain, not from
the app.

## Ground rules

* Comments should be rare, factual, and explain only non-obvious external constraints.
* One change per commit; say which device and Android version you tested on.
* Never bundle Valve binaries, game files, or anything you do not have the right to redistribute.
* Logs and device reports are welcome in issues. Remove serial numbers and account names first.

## Build and test

```bash
python3 tools/release/runtime_inputs.py record   # after changing tools/linuxfs/overlay/**
./gradlew :app:assembleDebug
```

Guest-side changes can be tested without reinstalling when you only edit scripts that the app stages
at session start (`SessionFiles.stage`).

When reporting a display problem, include:

* `droiddeck-env` contents,
* `files/logs/<session>/session.log` and `wayland.log`,
* `xpresent.out` from the guest,
* the output of `xwin`/`xwindows` on the session display, if a window is missing.
