# Focused Linux Torrent QA

`Torrent Linux QA` is a manually dispatched, non-publishing GitHub Actions
workflow in `.github/workflows/torrent-linux-qa.yml`. It is separate from the
legacy DC-only Build/package matrix. Start it only on a reviewed source ref:

```sh
gh workflow run torrent-linux-qa.yml --repo Delitants/eiskaltdcpp --ref REVIEWED_REF
```

The first supported target is Ubuntu 24.04 x86_64, Qt6, Release. Windows is a
follow-up, not implicitly covered by this workflow. No macOS application,
installed profile, updater feed or release asset is changed.

## Build And Evidence

The job has a 60-minute limit and uses two compiler jobs. Ubuntu development
packages are installed on the disposable hosted runner; their exact versions
are recorded, but are not claimed to be reproducible dependency inputs.
Libtorrent uses the repository's checksum-pinned 2.1.1 archive and maintained
ABI-2 patch, installed in a workspace-local prefix. Application configuration
requires `USE_TORRENT=ON`, `BUILD_TESTS=ON` and
`EISKALT_REQUIRE_SELECTED_RECHECK=ON`. Missing ABI confirmation is a failure.

The dependency executable is an explicit build/run target, independent of
CTest. Core, Qt and Torrent test commands must all appear in the saved CTest
inventory. All tests execute serially with a 600-second per-test limit; the
dependency executable has its own 600-second limit and a 10-second termination
grace. The workflow's outer limit still applies to the aggregate job.

Test discovery and execution run as the runner account, not root, inside a new
Linux network namespace containing only the enabled loopback interface. Failure
to create that namespace fails the job; there is no unrestricted-network retry.
The test helper verifies the interface inventory before invoking test binaries.
Build-time downloads remain permitted before this isolation boundary. Tests use
a disposable HOME and XDG directories, with Qt's offscreen platform.

Always-uploaded artifacts include source identity, dependency input hashes,
package/compiler versions, configure/build logs, CMake cache and configure log,
interface inventory, CTest inventory, JUnit reports and summary. Profiles and
payload directories are not uploaded. Artifacts expire after 14 days; download
important qualification or failure evidence before expiration.

The final summary lists skipped tests. Core and Qt must each execute at least
one successful test; the Torrent backend and dependency tests cannot be skipped.
Opt-in skips (for example, a real GOST binary fixture not supplied on this runner)
remain explicit coverage gaps. A green job does not establish real-server GOST
interoperability, interactive GUI quality, installer correctness, Windows
compatibility, native minimum-macOS coverage or signed release acceptance.
The dependency suite's syscall-level partfile EIO/ENOSPC injection is currently
macOS-only; Linux runs the non-injected branches, including permission errors.
Passing Linux dependency tests must not be reported as injected-fault coverage.

## Offline Contract Tests

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tests/ci/test_linux_torrent_qa.py -v
bash -n tools/ci/test-linux-torrent.sh
```

The Python suite needs only the standard library. It validates workflow
requirements, rejects missing/failed/skipped required evidence, and exercises
shell orchestration with fake commands in disposable directories. These checks
do not build the application, create a network namespace or qualify native Linux
execution. Hosted execution is reported separately after an actual run.
