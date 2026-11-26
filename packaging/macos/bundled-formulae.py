"""List the Homebrew packages that own the libraries and plugins in a Mac build."""
import os
from pathlib import Path
import sys


def bundled_formulae(contents, cellar, playback_libs):
    frameworks = contents / "Frameworks"
    libraries = {path.name for path in frameworks.iterdir()
             if path.suffix in {".dylib", ".framework"} and not (playback_libs / path.name).exists()}
    plugins = {path.name for folder in (contents / "PlugIns", contents / "Resources" / "qml")
               for path in folder.rglob("*.dylib")}

    # macdeployqt moves some QML plugins into PlugIns/quick as well. Match the
    # original filename and follow any symlink back to the keg that owns it.
    owners = {name: set() for name in libraries | plugins}
    for directory, folders, files in os.walk(cellar):
        folder = Path(directory)
        parts = folder.relative_to(cellar).parts
        if parts[2:] == ("lib",):
            matches = libraries.intersection(folders + files)
        elif "plugins" in parts[2:] or "qml" in parts[2:]:
            matches = plugins.intersection(files)
        else:
            continue
        for name in matches:
            source = (folder / name).resolve()
            if source.is_relative_to(cellar) and source.exists():
                owners[name].add(source.relative_to(cellar).parts[0])

    formulae = set()
    for name, matches in sorted(owners.items()):
        if len(matches) != 1:
            raise SystemExit(f"Expected one Homebrew owner for {name}, found {sorted(matches)}")
        formulae.update(matches)
    return sorted(formulae)


if __name__ == "__main__":
    print(*bundled_formulae(*(Path(arg).resolve() for arg in sys.argv[1:])), sep="\n")
