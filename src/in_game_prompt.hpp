#pragma once

#include "combine_duplicate_furniture/prompt_model.hpp"

#include <string>

namespace cdf {
bool ShowGamePrompt(void* scene, std::string title, std::string text,
                    bool english, bool confirmation, unsigned timeout_ms = 0);
PromptAction TickGamePrompt(void* scene);
bool GamePromptVisible();
void CloseGamePrompt();
void ShutdownGamePrompt();
} // namespace cdf
