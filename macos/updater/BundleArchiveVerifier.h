#pragma once
#include <functional>
#include <string>
namespace updates {
struct BundleArchiveIdentity {
    std::string bundleId, build, version, cpu, minimumOs;
};
// Worker-only preflight after archive signature verification. Reads bounded ZIP
// metadata without extracting files or executing archive contents. Not consent.
// The caller exclusively owns immutable private staging through the subsequent
// handoff and must reauthenticate if it changes. Supports thin macOS ZIP releases.
bool bundleArchiveMatches(const std::string& path, const BundleArchiveIdentity&,
    const std::function<bool()>& cancelled = {});
}
