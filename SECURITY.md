# Security policy

## Reporting a vulnerability

Please do not open a public issue for a security problem.

Report it privately with GitHub's private vulnerability reporting:

1. Open https://github.com/VALXXII/valsbrowser/security/advisories/new
   (or the **Security** tab of the repository, then **Report a vulnerability**).
2. Describe the problem, the version of the browser (shown in « À propos ») and the steps to
   reproduce it.

Only the maintainer can read the report. You will get an answer in the advisory thread, and the
fix and the advisory are published together once a corrected version is available.

## Supported versions

Only the latest released version is supported. The browser updates itself, and fixes are
delivered in a new version, never back-ported:
https://github.com/VALXXII/browser-releases/releases/latest

## Scope

In scope: the browser itself (`browser.exe`), its updater (`update-helper.exe`), the installer
(`browser-<version>-win-x64-setup.exe`) and the annotations bridge (`browser-annotations-mcp.exe`).

Out of scope: vulnerabilities of the Microsoft Edge WebView2 runtime, of websites, of uBlock
Origin Lite, or of the assistant command-line tools the chat connects to. Please report those to
their own maintainers.
