# Troubleshooting

## What To Include In A Bug Report

- Application version/build, operating system and architecture.
- Exact steps, expected behavior and observed behavior.
- Whether the entire interface freezes or only transfer traffic stops.
- Relevant Live Log entries and, for a crash, the operating-system crash report.
- Whether the route is direct, SOCKS5, SOCKS5-over-TLS, Shadowsocks or GOST, and
  whether Torrents inherit it or use an override. Do not include credentials.

Remove passwords, tokens, private keys, personal paths and private endpoints
before attaching logs or profile snippets. Sharing an entire profile is rarely
necessary. A screenshot may expose hub users' contact details.

## Proxy Warnings

A SOCKS5 relay-control EOF means the relay connection closed. Existing TCP peer
connections may continue while UDP-dependent operations reconnect. Read the
warning's time and route; a last warning is not necessarily a current total
outage. Direct fallback remains disabled on strict routes.

Run TCP and UDP diagnostics separately. If only hostname destinations fail,
check the proxy server's DNS configuration and policies. If TCP succeeds and UDP
fails, check server relay support, firewall rules and transport configuration.
Do not turn off certificate verification as a diagnostic shortcut.

## Missing File Metadata Or Flags

Remote file lists can omit dates, resolution, codecs and other metadata. TTH
hashing does not derive media resolution or codecs. Missing values should remain
blank rather than display an epoch date. Country flags require local GeoIP data;
unmapped or proxied peers may have no reliable country.

## Queue And Sharing

When both endpoints are passive, ordinary direct transfer cannot start. Use an
active reachable source or an explicitly supported traversal method rather than
repeating the same request. For Torrent DC publication, check completion,
selection, private-Torrent status and the per-job exclusion before assuming a
hash-cache fault.

## Reproducible Testing

Use temporary download/share directories and an isolated profile. Keep the real
profile backed up before upgrades. Do not delete a hash cache as a first step:
that can force expensive rebuilding and destroy useful diagnostic evidence.
Native UI tests, core tests and live-server acceptance establish different
things; include which one reproduced the problem.
