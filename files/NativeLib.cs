using System;
using System.Runtime.InteropServices;

namespace CTFAK.Utils
{
    public static class NativeLib
    {
#if WINDOWS
        private const string DllPath = "CTFAK-Native.dll";
#else
        private const string DllPath = "libCTFAK-Native.so";
#endif

        [DllImport("kernel32.dll", EntryPoint = "LoadLibraryA", CharSet = CharSet.Ansi, SetLastError = true)]
        private static extern IntPtr LoadLibraryWin32(string lpFileName);

        [DllImport("libdl.so.2", EntryPoint = "dlopen", CharSet = CharSet.Ansi)]
        private static extern IntPtr dlopen(string lpFileName, int flags);

        private const int RTLD_NOW = 2;

        public static IntPtr LoadLibrary(string lpFileName)
        {
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            {
                IntPtr handle = LoadLibraryWin32(lpFileName);
                if (handle == IntPtr.Zero)
                {
                    int err = Marshal.GetLastWin32Error();
                    throw new DllNotFoundException($"Failed to load '{lpFileName}' (Win32 error {err})");
                }
                return handle;
            }

            return dlopen(lpFileName, RTLD_NOW);
        }

        [DllImport(DllPath, EntryPoint = "decompressOld", CharSet = CharSet.Auto)]
        public static extern int decompressOld(IntPtr source, int source_size, IntPtr output, int output_size);

        [DllImport(DllPath, EntryPoint = "TranslateToRGBMasked", CharSet = CharSet.Auto)]
        public static extern void TranslateToRGBMasked(IntPtr result, int width, int height, int alpha, int size, IntPtr imageData, int tranparent, int colorMode);

        [DllImport(DllPath, EntryPoint = "TranslateToRGBA", CharSet = CharSet.Auto)]
        public static extern void TranslateToRGBA(IntPtr result, int width, int height, int alpha, int size, IntPtr imageData, int tranparent, int colorMode);

        [DllImport(DllPath, EntryPoint = "TranslateToBGRA", CharSet = CharSet.Auto)]
        public static extern void TranslateToBGRA(IntPtr result, int width, int height, int alpha, int size, IntPtr imageData, int tranparent, int colorMode);
    }
}
