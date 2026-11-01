"""Run the manual Portal probe with a temporary, valid host application identity.

Never sends keyboard/mouse input or answers the desktop's permission prompt.
"""
import os
from pathlib import Path
import subprocess
import selectors
import sys
import time

executable = Path(__file__).resolve().parent / "shortcut_portal_live_linux_probe"
if not executable.exists():
    executable = Path(__file__).resolve().parents[1] / "build/tests/shortcut_portal_live_linux_probe"
assert executable.is_file(), "Build shortcut_portal_live_linux_probe first"
data = Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share")))
entry = data / "applications/org.libnativeapi.ShortcutProbe.desktop"
entry.parent.mkdir(parents=True, exist_ok=True)
# Exclusive creation ensures this fixture can never overwrite a user's entry.
with entry.open("x") as stream:
    stream.write("[Desktop Entry]\nType=Application\nName=NativeAPI shortcut verification\n"
                 f'Exec="{executable}"\nNoDisplay=true\nTerminal=false\n')
try:
    if "--dispatch" not in sys.argv:
        subprocess.run([str(executable)], timeout=20, check=True)
    else:
        # Target only this process's registered protocol action. This is not
        # keyboard injection and does not deliver keys to a focused window.
        env = dict(os.environ, NATIVEAPI_PROBE_WAIT_FOR_EVENT="1")
        session = subprocess.check_output(["systemctl", "--user", "show-environment"], text=True)
        for line in session.splitlines():
            if line.startswith("HYPRLAND_INSTANCE_SIGNATURE="):
                key, value = line.split("=", 1)
                env[key] = value
        child = subprocess.Popen([str(executable)], stdout=subprocess.PIPE, text=True, env=env)
        selector = selectors.DefaultSelector()
        selector.register(child.stdout, selectors.EVENT_READ)
        deadline = time.monotonic() + 20
        dispatched = False
        try:
            while time.monotonic() < deadline:
                for key, _ in selector.select(timeout=0.1):
                    line = key.fileobj.readline()
                    if not line:
                        selector.unregister(key.fileobj)
                        continue
                    print(line, end="", flush=True)
                    if line.startswith("REGISTERED key=nativeapi-shortcut-probe ") and not dispatched:
                        actions = subprocess.check_output(["hyprctl", "globalshortcuts"], env=env, text=True, timeout=3)
                        assert "org.libnativeapi.ShortcutProbe:nativeapi-shortcut-probe" in actions, actions
                        result = subprocess.check_output(["hyprctl", "dispatch",
                            'hl.dsp.global("org.libnativeapi.ShortcutProbe:nativeapi-shortcut-probe")'],
                            env=env, text=True, timeout=3)
                        print("COMPOSITOR_DISPATCH " + result.strip(), flush=True)
                        dispatched = True
                if child.poll() is not None and not selector.get_map():
                    break
            assert child.wait(timeout=1) == 0, "Probe did not receive the compositor event"
            assert dispatched
            actions = subprocess.check_output(["hyprctl", "globalshortcuts"], env=env, text=True, timeout=3)
            print("PROVIDER_CATALOG_RETAINED " + str("org.libnativeapi.ShortcutProbe:nativeapi-shortcut-probe" in actions), flush=True)
        finally:
            selector.close()
            if child.poll() is None:
                child.kill()
            child.wait()

finally:
    entry.unlink()
