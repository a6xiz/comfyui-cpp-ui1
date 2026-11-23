#pragma once
// Edits your ComfyUI workflow (API format) before it is sent.
// ALL node numbers of your workflow are listed here, in one place.
#include "json.hpp"
#include <string>

namespace wfb {

using json = nlohmann::json;

inline constexpr const char* kNegativePrompt = "12";   // CLIP Text Encode (Negative)
inline constexpr const char* kSampler        = "19";   // KSampler (progress is read from this one)
inline constexpr const char* kLatent         = "53";   // Latent Presets (width / height)
inline constexpr const char* kTypedText      = "112";  // Text String (Multiline) = the text you type
inline constexpr const char* kPromptEncode   = "110";  // CLIP Text Encode that feeds the KSampler
inline constexpr const char* kLoadImage      = "103";  // Load Image for i2i
inline constexpr const char* kSaveWithPrompt = "95";   // Save Image (w/ Embedded Prompt)

// nodes that only exist for image-to-image (they are removed in text-to-image mode)
inline constexpr const char* kImageOnlyNodes[] = {
    "103",  // Load Image
    "52",   // Load Booru Tagger
    "51",   // Booru Tagger
    "62",   // Text String i2i (tagger text)
    "107",  // Preview as Text (tagger text -> Combine Text)
    "109",  // Combine Text
    "111",  // Preview as Text (combined)
};

// numbers: negative prompt, seed, steps, cfg, size
inline void applyNumbers(json& wf, const std::string& negative, long long seed,
                         int steps, double cfg, int width, int height)
{
    if (!negative.empty())
        wf[kNegativePrompt]["inputs"]["text"] = negative;
    wf[kSampler]["inputs"]["seed"]   = seed;
    wf[kSampler]["inputs"]["steps"]  = steps;
    wf[kSampler]["inputs"]["cfg"]    = cfg;
    wf[kLatent]["inputs"]["width"]   = width;
    wf[kLatent]["inputs"]["height"]  = height;
}

// NO image uploaded: only your text is used, the whole image-to-image part is removed
inline void applyTextOnly(json& wf, const std::string& text)
{
    wf[kTypedText]["inputs"]["value"] = text;
    // skip the Combine Text node: the encoder reads your text directly
    wf[kPromptEncode]["inputs"]["text"] = json::array({kTypedText, 0});
    wf[kSampler]["inputs"]["positive"]  = json::array({kPromptEncode, 0});
    wf[kSaveWithPrompt]["inputs"]["prompt_text"] = text;   // was linked to the tagger
    for (const char* id : kImageOnlyNodes)
        wf.erase(std::string(id));
}

// image uploaded: the workflow runs as you built it,
// your text + the tagger text are combined into one prompt by Combine Text
inline void applyWithImage(json& wf, const std::string& text, const std::string& uploadedName)
{
    wf[kLoadImage]["inputs"]["image"] = uploadedName;
    wf[kTypedText]["inputs"]["value"] = text;   // always overwritten, even when empty
    wf[kSampler]["inputs"]["positive"] = json::array({kPromptEncode, 0});
}

} // namespace wfb
