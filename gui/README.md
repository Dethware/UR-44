UR44 Control
------------

Qt6 Widgets application for configuring the Steinberg UR44's internal DSP mixer.
The protocol is described in ../docs/PROTOCOL.md; ../tools/ur44.py is the reference client.

You need Qt6, CMake 3.16 or higher, the ALSA development files and Python 3 (the parameter and Sweet Spot
tables are generated from ../tools during the build).

-- Build instructions --

cd gui
cmake -B build
cmake --build build
ctest --test-dir build

build/src/application/ur44-control


-- Layout --

src/shared        generic code (JSON serialization, file system helpers), shared with other projects
src/shared_ui     generic UI code (the knob), shared with other projects
src/ur44          the UR44 protocol, device connection and effect logic (no UI, tested)
src/application   the application: mix pages, device page, effect editors, settings
tests             Qt Test unit tests


-- Tutorials and resources --

Qt online docs
https://doc.qt.io/
