using System;
using System.IO;
using System.Runtime.InteropServices;
using CTFAK.FileReaders;
using CTFAK.Utils;
using Joveler.Compression.ZLib;

namespace CTFAK
{
    public class CTFAKCore
    {
        public delegate void SaveHandler(int index, int all);

        public static IFileReader currentReader;
        public static string parameters;
        public static string path;

        public static void Init()
        {
            AppDomain.CurrentDomain.UnhandledException += (o, e) =>
            {
                Console.WriteLine(e.ExceptionObject.ToString());
            };

            if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            {
                ZLibInit.GlobalInit("x64\\zlibwapi.dll");
                string libraryFile = Path.Combine(Path.GetDirectoryName(typeof(CTFAKCore).Assembly.Location), "x64", "CTFAK-Native.dll");
                NativeLib.LoadLibrary(libraryFile);
            }
            else
            {
                ZLibInit.GlobalInit("libz.so");
            }
        }
    }
}
