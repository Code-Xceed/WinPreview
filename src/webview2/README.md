WebView2 SDK 1.0.4370.26 (loader + headers)
==========================================

Vendored from the official Microsoft.Web.WebView2 NuGet package. The app loads
`WebView2Loader.dll` at runtime and uses the installed Evergreen WebView2
Runtime for browser-grade HTML rendering. If the runtime is missing, the app
falls back to its dependency-free HTML reader.

`WebView2Loader.dll` and the headers are Microsoft redistributables; see
LICENSE.txt and NOTICE.txt.
