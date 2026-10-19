Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public class FgDiag {
    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    public static extern IntPtr GetAncestor(IntPtr hWnd, uint gaFlags);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "GetClassNameW")]
    public static extern int GetClassName(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "GetWindowTextW")]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);

    public const uint GA_ROOT = 2;

    public static void Check() {
        IntPtr fg = GetForegroundWindow();
        IntPtr root = GetAncestor(fg, GA_ROOT);
        StringBuilder clsFg = new StringBuilder(256);
        GetClassName(fg, clsFg, 256);
        StringBuilder txtFg = new StringBuilder(256);
        GetWindowText(fg, txtFg, 256);

        StringBuilder clsRoot = new StringBuilder(256);
        GetClassName(root, clsRoot, 256);
        StringBuilder txtRoot = new StringBuilder(256);
        GetWindowText(root, txtRoot, 256);

        Console.WriteLine("Foreground HWND: 0x{0:X} | Class: {1} | Title: {2}", fg.ToInt64(), clsFg, txtFg);
        Console.WriteLine("Root HWND: 0x{0:X} | Class: {1} | Title: {2}", root.ToInt64(), clsRoot, txtRoot);
    }
}
"@
[FgDiag]::Check()
