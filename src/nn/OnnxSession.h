#pragma once

#include <onnxruntime_cxx_api.h>

#include <juce_core/juce_core.h>

#include <cstdint>
#include <string>
#include <vector>

namespace multiplyandreplenish
{
/** @brief One ONNX Runtime session, loaded from a file and run on borrowed buffers. */
class OnnxSession
{
public:
    /** @brief A tensor that aliases the caller's buffer for the duration of a run. */
    struct TensorView
    {
        const char* name { nullptr };
        const float* floatData { nullptr };
        const std::int64_t* intData { nullptr };
        const bool* boolData { nullptr };
        std::vector<std::int64_t> shape;

        static TensorView floats (const char* tensorName, const float* values, std::vector<std::int64_t> tensorShape)
        {
            return { tensorName, values, nullptr, nullptr, std::move (tensorShape) };
        }

        static TensorView integers (const char* tensorName, const std::int64_t* values, std::vector<std::int64_t> tensorShape)
        {
            return { tensorName, nullptr, values, nullptr, std::move (tensorShape) };
        }

        /** @brief A boolean tensor, which the note segmenter passes masks and boundaries in. */
        static TensorView booleans (const char* tensorName, const bool* values, std::vector<std::int64_t> tensorShape)
        {
            return { tensorName, nullptr, nullptr, values, std::move (tensorShape) };
        }

        /** @brief A scalar, which ONNX writes as a tensor of rank zero. */
        static TensorView scalar (const char* tensorName, const float* value)
        {
            return { tensorName, value, nullptr, nullptr, {} };
        }
    };

    OnnxSession() = default;
    ~OnnxSession();

    OnnxSession (const OnnxSession&) = delete;
    OnnxSession& operator= (const OnnxSession&) = delete;
    OnnxSession (OnnxSession&&) noexcept;
    OnnxSession& operator= (OnnxSession&&) noexcept;

    bool load (const juce::File& file, int numThreads = 0);

    [[nodiscard]] std::vector<Ort::Value> run (const std::vector<TensorView>& inputs,
                                               const std::vector<const char*>& outputNames) const;

    [[nodiscard]] bool isLoaded() const noexcept { return session != nullptr; }

    [[nodiscard]] const juce::String& getError() const noexcept { return error; }

    [[nodiscard]] std::vector<std::string> getInputNames() const;

    [[nodiscard]] std::vector<std::string> getOutputNames() const;

private:
    static Ort::Env& getSharedEnvironment();

    std::unique_ptr<Ort::Session> session;
    mutable juce::String error;
};
}
