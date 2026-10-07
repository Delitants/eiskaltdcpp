#pragma once

namespace eiskalt::torrent {

enum class QuitRetirementStatus { Pending, Ready, Refused };
struct QuitRetirementResult {
    QuitRetirementStatus status;
    const char *component;
};

} // namespace eiskalt::torrent
