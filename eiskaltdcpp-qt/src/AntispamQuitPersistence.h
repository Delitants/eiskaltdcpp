#pragma once

#include <QByteArray>
#include <QString>
#include <QUuid>
#include <array>
#include <optional>

namespace antispamquit {
using Mask = std::array<bool, 4>;
struct File { QString path; QByteArray bytes; };
struct Snapshot { QUuid id; std::array<File, 4> files; };
struct Token {
    QUuid snapshot, attempt;
    bool operator==(const Token&) const noexcept = default;
};
struct Attempt { Snapshot snapshot; Token token; Mask committed{}; };
struct Receipt { Token token; Mask committed{}; };
enum class Status { NotStarted, Pending, Ready, RetryableFailure, TerminalFailure };

Receipt execute(const Attempt&) noexcept;

// GUI-owned admission and receipts; the writer receives only value copies.
class Checkpoint {
public:
    explicit Checkpoint(Snapshot);
    std::optional<Attempt> begin();
    Status complete(const Token&, const std::optional<Receipt>&) noexcept;
    bool cancelUnstarted(const Token&) noexcept;
    Status status() const noexcept { return state; }
    Mask committed() const noexcept { return saved; }
private:
    Snapshot frozen;
    Mask saved{};
    std::optional<Token> pending;
    Status state = Status::NotStarted;
};
}
