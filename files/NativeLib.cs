using System;
using System.Runtime.InteropServices;

namespace CTFAK.Utils
{
    public static class NativeLib
    {
        private const string DllPath = "libCTFAK-Native.so";

        [DllImport("libdl.so.2", EntryPoint = "dlopen", CharSet = CharSet.Ansi)]
        public static extern IntPtr LoadLibrary([MarshalAs(UnmanagedType.LPStr)] string lpFileName);

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
