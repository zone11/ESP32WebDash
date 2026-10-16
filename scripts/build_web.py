"""Packs the ESP32WebDash web UI into the project's LittleFS data folder.

Runs automatically as the library's PlatformIO extra script (library.json → build.extraScript),
so `pio run -t buildfs` / `uploadfs` always contains the current UI. No project setup needed.

Sources, later ones win:
  1. <library>/web/          generic UI (index.html, core.css, app.js, theme.css)
  2. <project>/web/          optional project overrides and additions (theme.css, logo, extra pages)
Output: <project data dir>/   text files gzipped (*.gz), others copied.

Only files this script generated before are replaced or removed (tracked in .webdash-files),
so other files you keep in data/ are left alone.

Standalone: python scripts/build_web.py <project_dir> [data_dir]
"""
import gzip
import os
import sys

TEXT = {".html", ".css", ".js", ".json", ".svg", ".txt", ".map", ".webmanifest"}
MANIFEST = ".webdash-files"


def collect(src):
    files = {}
    if not os.path.isdir(src):
        return files
    for root, dirs, names in os.walk(src):
        dirs[:] = [d for d in dirs if not d.startswith(".")]
        for name in names:
            if not name.startswith("."):
                path = os.path.join(root, name)
                files[os.path.relpath(path, src).replace(os.sep, "/")] = path
    return files


def build(lib_dir, project_dir, data_dir):
    sources = collect(os.path.join(lib_dir, "web"))
    sources.update(collect(os.path.join(project_dir, "web")))
    os.makedirs(data_dir, exist_ok=True)

    manifest = os.path.join(data_dir, MANIFEST)
    previous = set()
    if os.path.exists(manifest):
        with open(manifest) as f:
            previous = {line.strip() for line in f if line.strip()}

    written, size_in, size_out = set(), 0, 0
    for rel, path in sorted(sources.items()):
        with open(path, "rb") as f:
            raw = f.read()
        size_in += len(raw)
        gz = os.path.splitext(rel)[1].lower() in TEXT
        out_rel = rel + ".gz" if gz else rel
        data = gzip.compress(raw, 9, mtime=0) if gz else raw
        out = os.path.join(data_dir, out_rel)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        old = None
        if os.path.exists(out):
            with open(out, "rb") as f:
                old = f.read()
        if old != data:  # keep timestamps stable when nothing changed
            with open(out, "wb") as f:
                f.write(data)
        written.add(out_rel)
        size_out += len(data)

    for stale in previous - written:
        path = os.path.join(data_dir, stale)
        if os.path.isfile(path):
            os.remove(path)
    with open(manifest, "w") as f:
        f.write("\n".join(sorted(written)) + "\n")
    print(f"ESP32WebDash: web UI {size_in / 1024:.1f} KB -> {size_out / 1024:.1f} KB in {data_dir}")


def _script_lib_dir():
    # SCons executes this file without __file__; the code object still knows its path.
    import inspect
    return os.path.dirname(os.path.dirname(os.path.abspath(inspect.getframeinfo(inspect.currentframe()).filename)))


try:
    Import("env")  # noqa: F821  (PlatformIO SCons)
    build(_script_lib_dir(), env.subst("$PROJECT_DIR"), env.subst("$PROJECT_DATA_DIR"))  # noqa: F821
except NameError:
    _here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    _project = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.getcwd()
    _data = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else os.path.join(_project, "data")
    build(_here, _project, _data)
