import ctypes, subprocess

user32 = ctypes.windll.user32
out = subprocess.check_output(['tasklist', '/FI', 'IMAGENAME eq explorer.exe', '/FO', 'CSV', '/NH']).decode()
exp_pid = int(out.split(',')[1].strip('\"'))

WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
def callback(hwnd, lparam):
    pid = ctypes.c_uint32()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    if pid.value == exp_pid:
        cls = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(hwnd, cls, 256)
        txt = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(hwnd, txt, 256)
        vis = user32.IsWindowVisible(hwnd)
        if txt.value or 'Shell' in cls.value or 'Cabinet' in cls.value or 'Progman' in cls.value or 'Worker' in cls.value:
            print(f'HWND: {hex(hwnd)}, Vis: {vis}, Class: {cls.value}, Title: {txt.value}')
    return True

user32.EnumWindows(WNDENUMPROC(callback), 0)
