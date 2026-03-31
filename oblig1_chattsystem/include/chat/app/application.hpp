#pragma once

namespace chat {

/// Orkestrerer oppstart, hovedløkke, avslutning og kobling mellom CLI, nett og tjenester.
class ChatApplication {
public:
    ChatApplication() = default;
    ~ChatApplication() = default;

    ChatApplication(const ChatApplication&) = delete;
    ChatApplication& operator=(const ChatApplication&) = delete;
    ChatApplication(ChatApplication&&) = delete;
    ChatApplication& operator=(ChatApplication&&) = delete;

    int run(int argc, char** argv);
};

}  // namespace chat
