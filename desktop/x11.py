#!/usr/bin/env python3
"""Drives the desktop build through X11: find its window, capture it, and press keys.

A console's UI can only be looked at by copying an NRO over and squinting; this runs the same
sources on the PC, so a screen can be photographed and navigated without a test cycle on hardware.
Not part of the app: a development tool, run by hand.

    python3 x11.py shot play.png            capture the launcher window
    python3 x11.py keys Down Return         press keys, in order
    python3 x11.py run                      a scripted look at every screen
"""
import ctypes
import ctypes.util
import subprocess
import sys
import time

DISPLAY = ":1"
TITLE = "Horizon Launcher"

x11 = ctypes.CDLL(ctypes.util.find_library("X11"))
xtst = ctypes.CDLL(ctypes.util.find_library("Xtst"))

x11.XOpenDisplay.restype = ctypes.c_void_p
x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
x11.XDefaultRootWindow.restype = ctypes.c_ulong
x11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
x11.XQueryTree.argtypes = [
    ctypes.c_void_p, ctypes.c_ulong,
    ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_ulong),
    ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)), ctypes.POINTER(ctypes.c_uint),
]
x11.XFetchName.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_char_p)]
x11.XStringToKeysym.restype = ctypes.c_ulong
x11.XStringToKeysym.argtypes = [ctypes.c_char_p]
x11.XKeysymToKeycode.restype = ctypes.c_ubyte
x11.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
x11.XFlush.argtypes = [ctypes.c_void_p]
x11.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
xtst.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]


def open_display():
    display = x11.XOpenDisplay(DISPLAY.encode())
    if not display:
        raise SystemExit(f"cannot open display {DISPLAY}")
    return display


def windows(display):
    """Every window under the root, as (id, name)."""
    root = x11.XDefaultRootWindow(display)
    root_return, parent_return = ctypes.c_ulong(), ctypes.c_ulong()
    children = ctypes.POINTER(ctypes.c_ulong)()
    count = ctypes.c_uint()
    if not x11.XQueryTree(display, root, ctypes.byref(root_return), ctypes.byref(parent_return),
                          ctypes.byref(children), ctypes.byref(count)):
        return []

    found = []
    for i in range(count.value):
        window = children[i]
        name = ctypes.c_char_p()
        if x11.XFetchName(display, window, ctypes.byref(name)) and name.value:
            found.append((window, name.value.decode("utf-8", "replace")))
        found.extend(windows_of(display, window))
    return found


def windows_of(display, window):
    root_return, parent_return = ctypes.c_ulong(), ctypes.c_ulong()
    children = ctypes.POINTER(ctypes.c_ulong)()
    count = ctypes.c_uint()
    if not x11.XQueryTree(display, window, ctypes.byref(root_return), ctypes.byref(parent_return),
                          ctypes.byref(children), ctypes.byref(count)):
        return []
    found = []
    for i in range(count.value):
        child = children[i]
        name = ctypes.c_char_p()
        if x11.XFetchName(display, child, ctypes.byref(name)) and name.value:
            found.append((child, name.value.decode("utf-8", "replace")))
        found.extend(windows_of(display, child))
    return found


def find(display, title=TITLE):
    for window, name in windows(display):
        if title.lower() in name.lower():
            return window
    return None


def shot(path, title=TITLE):
    display = open_display()
    window = find(display, title)
    if window is None:
        raise SystemExit("no launcher window")
    subprocess.run(["import", "-display", DISPLAY, "-window", hex(window), path], check=True)
    print(f"==> {path}")


def keys(names):
    display = open_display()
    window = find(display)
    if window is None:
        raise SystemExit("no launcher window")

    # The keys go wherever the X focus is: without this they land on whatever the desktop had
    # focused and the launcher never sees them.
    x11.XSetInputFocus.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
    x11.XSetInputFocus(display, window, 2, 0)  # RevertToParent, CurrentTime
    x11.XSync(display, 0)
    time.sleep(0.2)

    for name in names:
        keysym = x11.XStringToKeysym(name.encode())
        if not keysym:
            raise SystemExit(f"unknown key {name}")
        code = x11.XKeysymToKeycode(display, keysym)
        xtst.XTestFakeKeyEvent(display, code, 1, 0)
        xtst.XTestFakeKeyEvent(display, code, 0, 0)
        x11.XFlush(display)
        time.sleep(0.35)
    x11.XSync(display, 0)
    print("pressed " + " ".join(names))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    command = sys.argv[1]
    if command == "shot":
        shot(sys.argv[2])
    elif command == "keys":
        keys(sys.argv[2:])
    else:
        raise SystemExit(f"unknown command {command}")
