# Security and issue reports

This research project has no security certification or supported production
release. The Android app executes a JIT and native GPU/restore code. Known GPU
faults and host reboots remain unresolved. Use a disposable research environment
with no sensitive guest accounts or workloads. This does not override the
applicable software licenses.

## Reporting

For a suspected vulnerability, use the repository's **Security → Advisories →
Report a vulnerability** if the maintainer has enabled private reporting. If
that control is unavailable, open an issue asking for a private reporting channel
without including exploit details, secrets or personal data. No response time or
private-reporting service is promised before the repository is configured.

Ordinary crashes and device compatibility reports can use the issue forms.
Include model/SoC, Android version, graphics choice, guest CPU/RAM settings and
the smallest reproduction. Share only the relevant, manually reviewed log lines.
Remove usernames, serials, IP addresses, file paths and account information.
Do not attach full bugreports, guest disks/ROM/IPSW, private screenshots, keys or
credentials. Preserve third-party copyright notices when sharing source excerpts.

## Dependency and release boundaries

The source workflow has read-only permissions and performs host/source checks;
it neither signs nor publishes an APK. SHA pins identify downloaded inputs but do
not establish that an input is safe or that a binary matches source. Optional
Turnip is explicitly supplied and loaded inside the app process; no system-driver
replacement is part of this project.

See [docs/publication.md](docs/publication.md) for the separate binary-release
conditions and [docs/licensing.md](docs/licensing.md) for source obligations.
