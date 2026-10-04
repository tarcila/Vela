"""Actual XTest input only on an explicitly isolated authenticated fixture."""
import ctypes
import os
import sys
import time
assert os.environ.get('VSR_VIEWER_TEST_ISOLATED') == '1'
assert '/.scratch/viewport-integration/tmp/' in os.environ['XAUTHORITY']
assert os.environ['DISPLAY'].startswith('127.0.0.1:')
x = ctypes.CDLL('/nix/store/4wkicwb2jx5h63d79g5qyp9kn4vx27yb-libx11-1.8.13/lib/libX11.so.6')
t = ctypes.CDLL('/nix/store/gczb0k0hdx7dv6d1f5kp44k5hqrwayqj-libxtst-1.2.5/lib/libXtst.so.6')
x.XOpenDisplay.argtypes = [ctypes.c_char_p]
x.XOpenDisplay.restype = ctypes.c_void_p
x.XFlush.argtypes = [ctypes.c_void_p]
x.XCloseDisplay.argtypes = [ctypes.c_void_p]
t.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
t.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
d = x.XOpenDisplay(os.environ['DISPLAY'].encode())
assert d
try:
    x.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
    x.XDefaultRootWindow.restype = ctypes.c_ulong
    x.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)), ctypes.POINTER(ctypes.c_uint)]
    x.XGetGeometry.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint)]
    root = x.XDefaultRootWindow(d)
    root_out, parent_out = ctypes.c_ulong(), ctypes.c_ulong()
    children = ctypes.POINTER(ctypes.c_ulong)()
    count = ctypes.c_uint()
    assert x.XQueryTree(d, root, ctypes.byref(root_out), ctypes.byref(parent_out), ctypes.byref(children), ctypes.byref(count))
    candidates = []
    for i in range(count.value):
        gx, gy = ctypes.c_int(), ctypes.c_int()
        width, height, border, depth = (ctypes.c_uint() for _ in range(4))
        assert x.XGetGeometry(d, children[i], ctypes.byref(root_out), ctypes.byref(gx), ctypes.byref(gy), ctypes.byref(width), ctypes.byref(height), ctypes.byref(border), ctypes.byref(depth))
        if width.value > 300 and height.value > 300:
            candidates.append((gx.value, gy.value))
    assert len(candidates) == 1
    gx, gy = candidates[0]
    print('private window origin', gx, gy, 'target', int(sys.argv[1])+gx, int(sys.argv[2])+gy)
    assert t.XTestFakeMotionEvent(d, -1, int(sys.argv[1])+gx, int(sys.argv[2])+gy, 0)
    x.XFlush(d)
    time.sleep(.1)
    assert t.XTestFakeButtonEvent(d, 1, 1, 0)
    x.XFlush(d)
    time.sleep(.1)
    assert t.XTestFakeButtonEvent(d, 1, 0, 0)
    x.XFlush(d)
    time.sleep(.2)
finally:
    x.XCloseDisplay(d)
