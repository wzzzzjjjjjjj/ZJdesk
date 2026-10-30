import argparse
import ctypes as c
import json
import os
import pathlib
import signal
import subprocess
import sys
import tempfile
import time

arguments = argparse.ArgumentParser(description='Exercise only a disposable Edge profile/window.')
arguments.add_argument('duration', type=int)
arguments.add_argument('--display', default=':77', help='Explicit X11 test display; :77 is the isolated fixture.')
args = arguments.parse_args()
os.environ.update(DISPLAY=args.display)
os.environ.setdefault('XAUTHORITY', '/run/user/1000/gdm/Xauthority')
x = c.CDLL('libX11.so.6')
x.XOpenDisplay.argtypes = [c.c_char_p]
x.XOpenDisplay.restype = c.c_void_p
x.XDefaultRootWindow.argtypes = [c.c_void_p]
x.XDefaultRootWindow.restype = c.c_ulong
x.XInternAtom.argtypes = [c.c_void_p, c.c_char_p, c.c_int]
x.XInternAtom.restype = c.c_ulong
x.XQueryTree.argtypes = [c.c_void_p, c.c_ulong, c.POINTER(c.c_ulong), c.POINTER(c.c_ulong), c.POINTER(c.POINTER(c.c_ulong)), c.POINTER(c.c_uint)]
x.XGetWindowProperty.argtypes = [c.c_void_p, c.c_ulong, c.c_ulong, c.c_long, c.c_long, c.c_int, c.c_ulong,
    c.POINTER(c.c_ulong), c.POINTER(c.c_int), c.POINTER(c.c_ulong), c.POINTER(c.c_ulong), c.POINTER(c.POINTER(c.c_ubyte))]
x.XIconifyWindow.argtypes = [c.c_void_p, c.c_ulong, c.c_int]
x.XMapRaised.argtypes = [c.c_void_p, c.c_ulong]
x.XMoveResizeWindow.argtypes = [c.c_void_p, c.c_ulong, c.c_int, c.c_int, c.c_uint, c.c_uint]
x.XFlush.argtypes = [c.c_void_p]
x.XFree.argtypes = [c.c_void_p]
x.XCloseDisplay.argtypes = [c.c_void_p]
x.XGetGeometry.argtypes = [c.c_void_p, c.c_ulong, c.POINTER(c.c_ulong), c.POINTER(c.c_int), c.POINTER(c.c_int),
    c.POINTER(c.c_uint), c.POINTER(c.c_uint), c.POINTER(c.c_uint), c.POINTER(c.c_uint)]
# Chromium creates and destroys small utility windows while we enumerate.
error_handler = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_void_p)(lambda *_: 0)
x.XSetErrorHandler.argtypes = [type(error_handler)]
x.XSetErrorHandler(error_handler)
d = x.XOpenDisplay(args.display.encode())
assert d, 'The requested X11 test display is not running'
root = x.XDefaultRootWindow(d)
pid_atom = x.XInternAtom(d, b'_NET_WM_PID', 0)

def descendants(window):
    r, parent, count = c.c_ulong(), c.c_ulong(), c.c_uint()
    children = c.POINTER(c.c_ulong)()
    if not x.XQueryTree(d, window, c.byref(r), c.byref(parent), c.byref(children), c.byref(count)):
        return []
    result = list(children[:count.value]) if count.value else []
    if children:
        x.XFree(children)
    return result

def find_window(pid):
    todo = descendants(root)
    while todo:
        window = todo.pop()
        actual, fmt, count, remaining = c.c_ulong(), c.c_int(), c.c_ulong(), c.c_ulong()
        data = c.POINTER(c.c_ubyte)()
        x.XGetWindowProperty(d, window, pid_atom, 0, 1, 0, 0, c.byref(actual), c.byref(fmt),
            c.byref(count), c.byref(remaining), c.byref(data))
        matched = data and fmt.value == 32 and count.value and c.cast(data, c.POINTER(c.c_ulong))[0] == pid
        if data:
            x.XFree(data)
        if matched:
            r, px, py, width, height, border, depth = c.c_ulong(), c.c_int(), c.c_int(), c.c_uint(), c.c_uint(), c.c_uint(), c.c_uint()
            if x.XGetGeometry(d, window, c.byref(r), c.byref(px), c.byref(py), c.byref(width),
                c.byref(height), c.byref(border), c.byref(depth)) and width.value >= 400 and height.value >= 300:
                return window
        todo.extend(descendants(window))
    return 0

duration = min(595, max(10, args.duration))
deadline = time.monotonic() + duration
counts = dict(launch=0, minimize=0, restore=0, resize=0, close=0)
process = None
active = pathlib.Path(__file__).resolve().parents[2] / 'build/linux-release/output/edge-stress-active'
active.parent.mkdir(parents=True, exist_ok=True)

def cleanup(*_):
    global process
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
    process = None

signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))
signal.signal(signal.SIGHUP, lambda *_: sys.exit(129))
try:
    active.touch()
    with tempfile.TemporaryDirectory(prefix='zjdesk-edge-') as temp:
        folder = pathlib.Path(temp)
        page = folder / 'desktop-test.html'
        page.write_text('''<!doctype html><meta charset="utf-8"><style>body{margin:0;font:16px sans-serif}canvas{width:100vw;height:70vh}p{padding:10px}</style>
<p>Edge remote desktop test 中文细字 AaBbCc 0123456789</p><canvas id="c" width="1600" height="700"></canvas><script>
const g=c.getContext('2d');let n=0;function draw(){n++;g.fillStyle=n%120<60?'#e5efff':'#fff';g.fillRect(0,0,1600,700);
for(let y=0;y<700;y+=40)for(let x=-80;x<1600;x+=100){g.fillStyle=(y/40)%2?'#176ee8':'#29846c';g.fillRect(x+n%100,y,50,30)}
g.fillStyle='#000';g.fillText('Browser frame '+n,40,40);requestAnimationFrame(draw)}draw();</script>''', encoding='utf-8')
        while time.monotonic() < deadline:
            with open(folder/'edge.log', 'ab') as log:
                process = subprocess.Popen(['/usr/bin/microsoft-edge', '--no-first-run', '--no-default-browser-check',
                    '--disable-background-networking', '--user-data-dir='+str(folder/'profile'),
                    '--window-size=1600,950', '--new-window', page.as_uri()], stdout=log, stderr=log)
            counts['launch'] += 1
            window = 0
            for _ in range(100):
                window = find_window(process.pid)
                if window or process.poll() is not None:
                    break
                time.sleep(.1)
            assert window, 'Could not identify the disposable Edge window'
            for _ in range(3):
                if time.monotonic() >= deadline:
                    break
                time.sleep(1)
                x.XIconifyWindow(d, window, 0); x.XFlush(d)
                counts['minimize'] += 1
                time.sleep(.6)
                x.XMapRaised(d, window); x.XFlush(d)
                counts['restore'] += 1
                for width, height in [(1100, 720), (1800, 1000), (1400, 850)]:
                    x.XMoveResizeWindow(d, window, 20, 20, width, height); x.XFlush(d)
                    counts['resize'] += 1
                    time.sleep(.4)
            cleanup()
            counts['close'] += 1
            print(json.dumps(dict(elapsed=duration-max(0, deadline-time.monotonic()), **counts)), flush=True)
        assert counts['launch'] >= 1 and counts['minimize'] >= 1
finally:
    cleanup()
    active.unlink(missing_ok=True)
    x.XCloseDisplay(d)
