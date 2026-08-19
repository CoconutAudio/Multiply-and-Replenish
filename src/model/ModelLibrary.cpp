#include "model/ModelLibrary.h"

namespace tuner
{
namespace
{
    const char* applicationFolderName = "Tuner";
    const char* modelsFolderName = "models";
    const char* environmentVariableName = "TUNER_MODEL_PATH";
}

juce::File ModelLibrary::getUserModelDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile (applicationFolderName)
        .getChildFile (modelsFolderName);
}

ModelLibrary::ModelLibrary()
{
    searchPaths.push_back (juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
                               .getChildFile (applicationFolderName)
                               .getChildFile (modelsFolderName));

    searchPaths.push_back (getUserModelDirectory());

   #if defined (TUNER_DEVELOPMENT_MODEL_PATH)
    searchPaths.push_back (juce::File { TUNER_DEVELOPMENT_MODEL_PATH });
   #endif

    if (const auto extraPaths = juce::SystemStats::getEnvironmentVariable (environmentVariableName, {});
        extraPaths.isNotEmpty())
    {
        for (const auto& path : juce::StringArray::fromTokens (extraPaths, ":;", {}))
            if (path.isNotEmpty())
                searchPaths.push_back (juce::File { path });
    }

    // Beside the binary, which is where a bundled install puts them.
    searchPaths.push_back (juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                               .getParentDirectory()
                               .getChildFile (modelsFolderName));
}

juce::File ModelLibrary::find (const juce::String& relativePath) const
{
    for (const auto& searchPath : searchPaths)
        if (const auto candidate = searchPath.getChildFile (relativePath); candidate.existsAsFile())
            return candidate;

    return {};
}
}
