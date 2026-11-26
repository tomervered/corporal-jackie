"""Group Windows libraries by purpose and teach the player where to find them."""
from fnmatch import fnmatchcase
from pathlib import Path
import sys
import subprocess
from tempfile import TemporaryDirectory
from xml.etree import ElementTree as XML

# A pattern also matches future version numbers in filenames.
groups = {
    "Playing video and audio": {
        "libmpv-*": "Runs the media player.",
        "avcodec-*": "Decodes compressed audio and video.",
        "avformat-*": "Reads media containers and finds the tracks inside.",
        "avfilter-*": "Applies audio and video filters.",
        "avutil-*": "Provides shared tools for the FFmpeg media libraries.",
        "swresample-*": "Converts audio sample rates and formats.",
        "swscale-*": "Resizes video and converts pixel formats.",
        "libdav1d-*": "Decodes AV1 video.",
        "libplacebo-*": "Draws and scales video using the graphics processor.",
        "libdovi": "Reads Dolby Vision information.",
        "libzimg-*": "Resizes images and converts colors.",
        "liblcms2-*": "Manages color profiles.",
    },
    "Subtitles, fonts and languages": {
        "libass-*": "Draws styled subtitles.",
        "libfontconfig-*": "Finds matching fonts.",
        "libfreetype-*": "Draws letters from font files.",
        "libfribidi-*": "Orders text that mixes left-to-right and right-to-left writing.",
        "libgraphite2": "Shapes letters for fonts that use Graphite.",
        "libharfbuzz-*": "Shapes text into the correct glyphs.",
        "libunibreak-*": "Finds places where a line of text can wrap.",
        "libicu*": "Provides Unicode text and language support.",
        "libiconv-*": "Converts between text encodings.",
        "libintl-*": "Provides translation support.",
        "libuchardet": "Identifies the encoding of text.",
    },
    "Windows, menus and artwork": {
        "Qt6LabsFolderListModel": "Lists files and folders for the interface.",
        "Qt6Network": "Handles network requests.",
        "Qt6OpenGL": "Connects Qt to OpenGL graphics.",
        "Qt6QuickDialogs*": "Builds dialogs such as file pickers.",
        "Qt6QuickControls*": "Builds and styles buttons, menus and other controls.",
        "Qt6QuickLayouts": "Arranges controls on screen.",
        "Qt6QuickEffects": "Draws visual effects.",
        "Qt6QuickShapes": "Draws shapes.",
        "Qt6QuickTemplates2": "Provides the foundations for on-screen controls.",
        "Qt6Quick": "Draws the interactive interface.",
        "Qt6Qml*": "Runs the QML code that describes the interface.",
        "Qt6Gui": "Handles windows, graphics and input.",
        "Qt6Core": "Provides shared services used throughout Qt.",
        "libjpeg-*": "Handles JPEG pictures.",
        "libpng*": "Handles PNG pictures.",
        "libwebp*": "Handles WebP pictures and their container format.",
        "libsharpyuv-*": "Converts colors for WebP pictures.",
    },
    "Saved data, secure connections and shared tools": {
        "libsqlite3-*": "Stores settings, the library and watch history.",
        "libssl-*": "Handles encrypted network connections.",
        "libcrypto-*": "Provides encryption and related calculations.",
        "libb2-*": "Computes BLAKE2 fingerprints of data.",
        "libbrotli*": "Supports Brotli-compressed data.",
        "libbz2-*": "Supports bzip2-compressed data.",
        "libzstd*": "Supports Zstandard-compressed data.",
        "libz-*": "Supports zlib-compressed data.",
        "zlib1": "Supports zlib-compressed data.",
        "libgcc*": "Provides runtime support for compiled code.",
        "libstdc++*": "Provides the C++ standard library.",
        "libwinpthread*": "Helps background tasks run on Windows.",
        "libglib-*": "Provides common tools for other libraries.",
        "libdouble-conversion": "Converts numbers to and from text.",
        "libexpat-*": "Reads XML documents.",
        "libmd4c": "Reads Markdown text.",
        "libpcre2-*": "Matches patterns in text.",
    },
}


folders = {
    "Playing video and audio": "VideoAndAudio",
    "Subtitles, fonts and languages": "SubtitlesAndFonts",
    "Windows, menus and artwork": "InterfaceAndArtwork",
    "Saved data, secure connections and shared tools": "SharedTools",
    "Other libraries": "OtherLibraries",
}


def manifest():
    return XML.Element("assembly", xmlns="urn:schemas-microsoft-com:asm.v1", manifestVersion="1.0")


def identify(parent, name):
    XML.SubElement(parent, "assemblyIdentity", name=name, version="1.0.0.0", type="win32", processorArchitecture="amd64")


def organize(folder, manifest_tool):
    player = folder / "CorporalJackie.exe"
    sections = {}
    for dll in sorted(folder.glob("*.dll"), key=lambda file: file.name.lower()):
        section, description = next(
            ((group, purpose) for group, patterns in groups.items()
             for pattern, purpose in patterns.items() if fnmatchcase(dll.stem, pattern)),
            ("Other libraries", "See the third-party notices for this build."),
        )
        sections.setdefault(section, []).append((dll, description))
    if not sections or not player.is_file():
        raise SystemExit("Expected a freshly staged Windows player and its DLLs.")

    # Windows reads these private assembly manifests before it loads the player.
    # This works for both app entry points and does not depend on the user's PATH.
    dependencies = manifest()
    lines = ["What these files do", "", "Each folder contains libraries used for the tasks listed below.", ""]
    for group, name in folders.items():
        if group not in sections:
            continue
        destination = folder / name
        destination.mkdir()
        assembly = manifest()
        identify(assembly, name)
        identify(XML.SubElement(XML.SubElement(dependencies, "dependency"), "dependentAssembly"), name)
        lines.append(f"{name} - {group}")
        for dll, purpose in sections[group]:
            XML.SubElement(assembly, "file", name=dll.name)
            dll.rename(destination / dll.name)
            lines.append(f"  {dll.name} - {purpose}")
        lines.append("")
        XML.ElementTree(assembly).write(destination / f"{name}.manifest", encoding="utf-8", xml_declaration=True)

    with TemporaryDirectory() as temp:
        original = Path(temp) / "original.xml"
        additions = Path(temp) / "libraries.xml"
        XML.ElementTree(dependencies).write(additions, encoding="utf-8", xml_declaration=True)
        subprocess.run([manifest_tool, "-nologo", f"-inputresource:{player};#1", f"-out:{original}"], check=True)
        subprocess.run([manifest_tool, "-nologo", "-manifest", original, additions,
                        f"-outputresource:{player};#1"], check=True)
    (folder / "What these files do.txt").write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    organize(Path(sys.argv[1]).resolve(), sys.argv[2])
