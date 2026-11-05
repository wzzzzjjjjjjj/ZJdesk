using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Threading;
using System.Windows.Forms;

internal static class WindowsLauncher
{
    [DllImport("kernel32.dll")]
    private static extern IntPtr GetStdHandle(int nStdHandle);

    [STAThread]
    private static int Main(string[] args)
    {
        try
        {
            var assembly = Assembly.GetExecutingAssembly();
            string hash;
            using (var payload = assembly.GetManifestResourceStream("payload.zip"))
            using (var sha = SHA256.Create())
                hash = BitConverter.ToString(sha.ComputeHash(payload)).Replace("-", "");
            var cache = Environment.GetEnvironmentVariable("ZJDESK_PACKAGE_CACHE");
            var customCache = !string.IsNullOrEmpty(cache);
            if (!customCache)
                cache = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ZJdesk", "packages");
            var directory = Path.GetFullPath(Path.Combine(cache, hash));
            using (var mutex = new Mutex(false, "Local\\ZJdesk-package-" + hash))
            {
                try { mutex.WaitOne(); }
                catch (AbandonedMutexException) { }
                try
                {
                    try { ExtractPackage(assembly, directory); }
                    catch (UnauthorizedAccessException)
                    {
                        if (customCache) throw;
                        directory = Path.Combine(Path.GetDirectoryName(assembly.Location), "ZJdesk-cache", hash);
                        ExtractPackage(assembly, directory);
                    }
                }
                finally { mutex.ReleaseMutex(); }
            }
            var start = new ProcessStartInfo(Path.Combine(directory, "bin", "zjdesk.exe"));
            start.WorkingDirectory = Path.Combine(directory, "bin");
            start.UseShellExecute = false;
            start.Arguments = string.Join(" ", Array.ConvertAll(args, Quote));
            using (var client = Process.Start(start))
            {
                client.WaitForExit();
                return client.ExitCode;
            }
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            var stderr = GetStdHandle(-12);
            if (stderr == IntPtr.Zero || stderr == new IntPtr(-1) || !Console.IsErrorRedirected)
                MessageBox.Show(error.Message, "ZJdesk 启动失败", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }
    }

    private static void ExtractPackage(Assembly assembly, string directory)
    {
        if (Directory.Exists(directory)) return;
        // Keep staging paths short enough for the bundled license filenames.
        var temporary = Path.Combine(Path.GetDirectoryName(directory), Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(temporary);
            using (var payload = assembly.GetManifestResourceStream("payload.zip"))
            using (var archive = new ZipArchive(payload, ZipArchiveMode.Read))
                archive.ExtractToDirectory(temporary);
            Directory.Move(temporary, directory);
        }
        finally
        {
            if (Directory.Exists(temporary))
            {
                try { Directory.Delete(temporary, true); }
                catch (IOException) { }
                catch (UnauthorizedAccessException) { }
            }
        }
    }

    private static string Quote(string value)
    {
        return "\"" + System.Text.RegularExpressions.Regex.Replace(
            System.Text.RegularExpressions.Regex.Replace(value, "(\\\\*)\"", "$1$1\\\""),
            "(\\\\+)$", "$1$1") + "\"";
    }
}
