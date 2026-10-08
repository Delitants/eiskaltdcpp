#include "AntispamQuitPersistence.h"
#include <QSaveFile>
#include <algorithm>
#include <utility>

namespace antispamquit {
Receipt execute(const Attempt& attempt) noexcept {
    Receipt receipt{attempt.token, attempt.committed};
    for(std::size_t i = 0; i < receipt.committed.size(); ++i) {
        if(receipt.committed[i]) continue;
        try {
            const auto& entry = attempt.snapshot.files[i];
            QSaveFile file(entry.path);
            receipt.committed[i] = file.open(QIODevice::WriteOnly) &&
                file.write(entry.bytes) == entry.bytes.size() && file.commit();
        } catch(...) {
            // One failed file cannot discard commits from the other entries.
        }
    }
    return receipt;
}

Checkpoint::Checkpoint(Snapshot snapshot) : frozen(std::move(snapshot)) {
    if(frozen.id.isNull()) state = Status::TerminalFailure;
}

std::optional<Attempt> Checkpoint::begin() {
    if(state != Status::NotStarted && state != Status::RetryableFailure) return std::nullopt;
    const Token token{frozen.id, QUuid::createUuid()};
    if(token.attempt.isNull()) { state = Status::TerminalFailure; return std::nullopt; }
    // Copy all bytes before publishing admission, so copy failure is retryable.
    Attempt attempt{frozen, token, saved};
    pending = token;
    state = Status::Pending;
    return attempt;
}

Status Checkpoint::complete(const Token& token, const std::optional<Receipt>& receipt) noexcept {
    bool valid = state == Status::Pending && pending && token == *pending &&
        receipt && receipt->token == token;
    if(valid) {
        for(std::size_t i = 0; i < saved.size(); ++i)
            if(saved[i] && !receipt->committed[i]) valid = false;
    }
    if(!valid) { state = Status::TerminalFailure; return state; }
    saved = receipt->committed;
    pending.reset();
    state = std::all_of(saved.begin(), saved.end(), [](bool bit) { return bit; })
        ? Status::Ready : Status::RetryableFailure;
    return state;
}

bool Checkpoint::cancelUnstarted(const Token& token) noexcept {
    if(state != Status::Pending || !pending || token != *pending) {
        state = Status::TerminalFailure;
        return false;
    }
    pending.reset();
    state = Status::RetryableFailure;
    return true;
}
}
