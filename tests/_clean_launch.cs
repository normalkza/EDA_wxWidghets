// Temporary helper: launch a child with a de-duplicated environment block.
// The harness process environment contains both "HTTP_PROXY" and "http_proxy",
// which makes .NET Framework's ProcessStartInfo.EnvironmentVariables throw.
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

internal static class CleanLaunch
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct STARTUPINFO
    {
        public int cb;
        public string lpReserved;
        public string lpDesktop;
        public string lpTitle;
        public int dwX;
        public int dwY;
        public int dwXSize;
        public int dwYSize;
        public int dwXCountChars;
        public int dwYCountChars;
        public int dwFillAttribute;
        public int dwFlags;
        public short wShowWindow;
        public short cbReserved2;
        public IntPtr lpReserved2;
        public IntPtr hStdInput;
        public IntPtr hStdOutput;
        public IntPtr hStdError;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct PROCESS_INFORMATION
    {
        public IntPtr hProcess;
        public IntPtr hThread;
        public int dwProcessId;
        public int dwThreadId;
    }

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr GetEnvironmentStringsW();

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool FreeEnvironmentStringsW(IntPtr block);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern bool CreateProcessW(string applicationName, string commandLine, IntPtr processAttributes,
        IntPtr threadAttributes, bool inheritHandles, uint creationFlags, IntPtr environment, string currentDirectory,
        ref STARTUPINFO startupInfo, out PROCESS_INFORMATION processInformation);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetExitCodeProcess(IntPtr process, out uint exitCode);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr handle);

    public static int Main(string[] args)
    {
        string exe = null;
        string cwd = Environment.CurrentDirectory;
        var rest = new List<string>();
        for (int i = 0; i < args.Length; ++i)
        {
            if (args[i] == "--exe") { exe = args[++i]; }
            else if (args[i] == "--cwd") { cwd = args[++i]; }
            else { rest.Add(args[i]); }
        }
        if (exe == null) { Console.Error.WriteLine("clean-launch: --exe is required"); return 125; }

        IntPtr raw = GetEnvironmentStringsW();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var kept = new List<string>();
        int offset = 0;
        while (true)
        {
            string entry = Marshal.PtrToStringUni(IntPtr.Add(raw, offset * 2));
            if (entry == null || entry.Length == 0) break;
            offset += entry.Length + 1;
            int equals = entry.IndexOf('=');
            // Drive letters arrive as "=C:=C:\..."; only a leading name is de-duplicated.
            string name = equals <= 0 ? entry : entry.Substring(0, equals);
            if (seen.Add(name)) kept.Add(entry);
        }
        FreeEnvironmentStringsW(raw);

        var block = new StringBuilder();
        foreach (string entry in kept) { block.Append(entry).Append('\0'); }
        block.Append('\0');
        IntPtr envBlock = Marshal.StringToHGlobalUni(block.ToString());

        var command = new StringBuilder();
        command.Append('"').Append(exe).Append('"');
        foreach (string arg in rest) { command.Append(' ').Append(arg); }

        var startup = new STARTUPINFO();
        startup.cb = Marshal.SizeOf(typeof(STARTUPINFO));
        PROCESS_INFORMATION info;
        const uint CREATE_UNICODE_ENVIRONMENT = 0x00000400;
        bool started = CreateProcessW(exe, command.ToString(), IntPtr.Zero, IntPtr.Zero, false,
            CREATE_UNICODE_ENVIRONMENT, envBlock, cwd, ref startup, out info);
        Marshal.FreeHGlobal(envBlock);
        if (!started)
        {
            Console.Error.WriteLine("clean-launch: CreateProcess failed: " + Marshal.GetLastWin32Error());
            return 125;
        }
        WaitForSingleObject(info.hProcess, 0xFFFFFFFF);
        uint code;
        GetExitCodeProcess(info.hProcess, out code);
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
        return unchecked((int)code);
    }
}
