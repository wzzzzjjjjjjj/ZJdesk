# Runs the manual control probe on an isolated native Windows desktop so the
# operator's own desktop keeps showing its normal content.
# Example:
#   tests\scripts\run-probe-background.ps1 -Arguments "--server ws://100.87.60.58:8765 --peer 100.87.60.58 --media-only --present --seconds 300 --output output/wan.jsonl"
param([Parameter(Mandatory = $true)][string]$Arguments,
    [ValidateSet('windows-release', 'windows-debug')][string]$Preset = 'windows-release',
    [ValidateSet('zjdesk-control-probe', 'zjdesk-latency-probe', 'zjdesk-ui-tests')][string]$Executable = 'zjdesk-control-probe',
    [ValidateRange(1, 7500)][int]$TimeoutSeconds = 3900)
$ErrorActionPreference = 'Stop'
$taskQt = 'F:/Qt6/6.8.3/msvc2022_64'
$taskGst = 'F:/gstreamer/1.26.11/PFiles64/gstreamer/1.0/msvc_x86_64'
$env:PATH = "$taskQt/bin;$taskGst/bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$taskQt/plugins"
$env:GST_PLUGIN_SYSTEM_PATH_1_0 = "$taskGst/lib/gstreamer-1.0"
$env:GST_PLUGIN_PATH_1_0 = ''
$env:GST_PLUGIN_SCANNER = "$taskGst/libexec/gstreamer-1.0/gst-plugin-scanner.exe"
$buildRoot = Join-Path (Resolve-Path "$PSScriptRoot/../../build") $Preset
$env:GST_REGISTRY_1_0 = Join-Path $buildRoot 'gst-registry-windows-release.bin'
if (-not ('ZjdeskBackgroundDesktop' -as [type])) {
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
public static class ZjdeskBackgroundDesktop {
 [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
 struct StartupInfo {
  public int cb; public string reserved; public string desktop; public string title;
  public int x,y,width,height,charsX,charsY,fill,flags;
  public short show,reservedCount; public IntPtr reservedBytes,input,output,error;
 }
 [StructLayout(LayoutKind.Sequential)]
 struct ProcessInfo { public IntPtr process,thread; public int processId,threadId; }
 [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
 static extern IntPtr CreateDesktopW(string name,IntPtr device,IntPtr mode,int flags,uint access,IntPtr security);
 [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr desktop);
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
 static extern bool CreateProcessW(string application,StringBuilder command,IntPtr ps,IntPtr ts,bool inherit,int flags,IntPtr environment,string directory,ref StartupInfo startup,out ProcessInfo process);
 [DllImport("kernel32.dll", SetLastError=true)] static extern uint WaitForSingleObject(IntPtr handle,uint milliseconds);
 [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetExitCodeProcess(IntPtr handle,out uint code);
 [DllImport("kernel32.dll", SetLastError=true)] static extern bool TerminateProcess(IntPtr handle,uint code);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
 public static int Run(string executable,string arguments,string directory,uint timeout) {
  string name="ZJdeskProbe-"+Guid.NewGuid().ToString("N");
  IntPtr desktop=CreateDesktopW(name,IntPtr.Zero,IntPtr.Zero,0,0x000F01FF,IntPtr.Zero);
  if(desktop==IntPtr.Zero) throw new Win32Exception();
  ProcessInfo process=new ProcessInfo();
  try {
   StartupInfo startup=new StartupInfo(); startup.cb=Marshal.SizeOf(startup); startup.desktop=name;
   if(!CreateProcessW(executable,new StringBuilder("\""+executable+"\" "+arguments),IntPtr.Zero,IntPtr.Zero,false,0x08000000,IntPtr.Zero,directory,ref startup,out process)) throw new Win32Exception();
   Console.WriteLine("NativePid="+process.processId+" Desktop="+name);
   uint wait=WaitForSingleObject(process.process,timeout);
   if(wait!=0) {
    int error=Marshal.GetLastWin32Error();
    if(!TerminateProcess(process.process,124)) throw new Win32Exception();
    if(WaitForSingleObject(process.process,10000)!=0) throw new Win32Exception();
    if(wait==0xFFFFFFFF) throw new Win32Exception(error);
    return 124;
   }
   uint code; if(!GetExitCodeProcess(process.process,out code)) throw new Win32Exception();
   return (int)code;
  } finally {
   if(process.thread!=IntPtr.Zero) CloseHandle(process.thread);
   if(process.process!=IntPtr.Zero) CloseHandle(process.process);
   CloseDesktop(desktop);
  }
 }
}
'@
}
$taskExe = Join-Path $buildRoot "bin/$Executable.exe"
$taskExit = [ZjdeskBackgroundDesktop]::Run($taskExe, $Arguments, $buildRoot, [uint32]($TimeoutSeconds * 1000))
Write-Output "NativeExit=$taskExit"
exit $taskExit
