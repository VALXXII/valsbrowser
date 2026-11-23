# Dot-sourced by the vault E2E scripts: every process the script starts (test hosts, their WebView2 engine, the local
# fixture servers) belongs to a Windows job object created with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE, whose only handle
# is held by this PowerShell process. When the script ends, normally or interrupted (Ctrl+C, closed window, killed
# process), Windows closes that handle and kills every process still in the job. The scripts' own clean-up (by exact
# executable path) stays in place; this is the guarantee when that clean-up cannot run.
if (-not ('VaultJobGuard' -as [type])) {
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class VaultJobGuard {
  [StructLayout(LayoutKind.Sequential)] struct BASIC { public long PerProcessUserTimeLimit, PerJobUserTimeLimit; public uint LimitFlags; public UIntPtr MinimumWorkingSetSize, MaximumWorkingSetSize; public uint ActiveProcessLimit; public UIntPtr Affinity; public uint PriorityClass, SchedulingClass; }
  [StructLayout(LayoutKind.Sequential)] struct IO { public ulong a, b, c, d, e, f; }
  [StructLayout(LayoutKind.Sequential)] struct EXTENDED { public BASIC Basic; public IO Io; public UIntPtr ProcessMemoryLimit, JobMemoryLimit, PeakProcessMemoryUsed, PeakJobMemoryUsed; }
  [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)] static extern IntPtr CreateJobObjectW(IntPtr attributes, string name);
  [DllImport("kernel32.dll", SetLastError = true)] static extern bool SetInformationJobObject(IntPtr job, int infoClass, ref EXTENDED info, int length);
  [DllImport("kernel32.dll", SetLastError = true)] static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
  [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
  static IntPtr job = IntPtr.Zero;
  public static string Enter() {
    if (job != IntPtr.Zero) return "already";
    var j = CreateJobObjectW(IntPtr.Zero, null); if (j == IntPtr.Zero) return "CreateJobObject failed " + Marshal.GetLastWin32Error();
    var info = new EXTENDED(); info.Basic.LimitFlags = 0x2000;   // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    if (!SetInformationJobObject(j, 9, ref info, Marshal.SizeOf(typeof(EXTENDED)))) return "SetInformationJobObject failed " + Marshal.GetLastWin32Error();
    if (!AssignProcessToJobObject(j, GetCurrentProcess())) return "AssignProcessToJobObject failed " + Marshal.GetLastWin32Error();
    job = j; return "ok";
  }
}
"@
}
$jobGuard = [VaultJobGuard]::Enter()
"     job guard (every process started here dies with this script): $jobGuard"
