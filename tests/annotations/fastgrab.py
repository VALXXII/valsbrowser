"""Fast screen grabs of a small desktop rectangle (GDI BitBlt + CAPTUREBLT, which includes layered windows), for the
V3 « no flash » probe: frames are kept raw in memory while the action runs, analysed afterwards."""
from __future__ import annotations
import ctypes
import ctypes.wintypes as W
import time

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
SRCCOPY, CAPTUREBLT = 0x00CC0020, 0x40000000


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [('biSize', W.DWORD), ('biWidth', W.LONG), ('biHeight', W.LONG), ('biPlanes', W.WORD), ('biBitCount', W.WORD),
                ('biCompression', W.DWORD), ('biSizeImage', W.DWORD), ('biXPelsPerMeter', W.LONG), ('biYPelsPerMeter', W.LONG),
                ('biClrUsed', W.DWORD), ('biClrImportant', W.DWORD)]


class Grabber:
    def __init__(self, box):
        self.x0, self.y0, x1, y1 = box
        self.w, self.h = max(1, x1 - self.x0), max(1, y1 - self.y0)
        gdi32.CreateDIBSection.restype = W.HBITMAP
        gdi32.CreateCompatibleDC.restype = W.HDC
        user32.GetDC.restype = W.HDC
        self.screen = user32.GetDC(None)
        self.mem = gdi32.CreateCompatibleDC(W.HDC(self.screen))
        bmi = BITMAPINFOHEADER(ctypes.sizeof(BITMAPINFOHEADER), self.w, -self.h, 1, 32, 0, 0, 0, 0, 0, 0)
        self.bits = ctypes.c_void_p()
        self.dib = gdi32.CreateDIBSection(W.HDC(self.mem), ctypes.byref(bmi), 0, ctypes.byref(self.bits), None, 0)
        gdi32.SelectObject(W.HDC(self.mem), W.HGDIOBJ(self.dib))

    def grab(self) -> bytes:
        gdi32.BitBlt(W.HDC(self.mem), 0, 0, self.w, self.h, W.HDC(self.screen), self.x0, self.y0, SRCCOPY | CAPTUREBLT)
        return ctypes.string_at(self.bits, self.w * self.h * 4)

    def image(self, raw: bytes):
        from PIL import Image
        return Image.frombuffer('RGBX', (self.w, self.h), raw, 'raw', 'BGRX', 0, 1).convert('RGB')

    def close(self):
        gdi32.DeleteObject(W.HGDIOBJ(self.dib)); gdi32.DeleteDC(W.HDC(self.mem)); user32.ReleaseDC(None, W.HDC(self.screen))


def accent_count_raw(raw: bytes, accent=(62, 103, 255), tol=16) -> int:
    """Pixels close to the accent in a BGRX buffer (fast path: bytes scanning in steps of 4)."""
    b, g, r = accent[2], accent[1], accent[0]; n = 0
    mv = memoryview(raw)
    for i in range(0, len(raw), 4):
        if abs(mv[i + 2] - r) < tol and abs(mv[i + 1] - g) < tol and abs(mv[i] - b) < tol:
            n += 1
    return n


if __name__ == '__main__':
    g = Grabber((0, 0, 600, 140)); t = time.perf_counter(); n = 0
    while time.perf_counter() - t < 1.0:
        g.grab(); n += 1
    print(f'{n} grabs of 600x140 per second ({1000 / n:.1f} ms each)'); g.close()
