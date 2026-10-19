Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public class HwndInspect {
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "GetClassNameW")]
    public static extern int GetClassName(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);
    [DllImport("user32.dll")]
    public static extern IntPtr GetAncestor(IntPtr hWnd, uint gaFlags);
    [DllImport("user32.dll")]
    public static extern bool IsWindow(IntPtr hWnd);
    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    public static void Check(IntPtr h) {
        StringBuilder cls = new StringBuilder(256);
        GetClassName(h, cls, 256);
        IntPtr root = GetAncestor(h, 2);
        StringBuilder clsRoot = new StringBuilder(256);
        GetClassName(root, clsRoot, 256);
        Console.WriteLine("HWND: 0x" + h.ToString("X") + " Class: " + cls.ToString() + " Visible: " + IsWindowVisible(h) + " | Root: 0x" + root.ToString("X") + " Class: " + clsRoot.ToString());
    }
}
"@
[HwndInspect]::Check([IntPtr]0x60046a);
