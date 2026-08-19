#pragma once

namespace rvctuner
{
/** @brief What this build is.

    The editor is the same either way; the engine behind it is not, and it is fixed at build time
    rather than chosen at runtime so that each build is one thing and says so.
*/
struct Product
{
   #if RVCTUNER_VOICE_BUILD
    static constexpr bool isVoiceBuild = true;
   #else
    static constexpr bool isVoiceBuild = false;
   #endif

    static constexpr const char* name = isVoiceBuild ? "RVCTuner Voice" : "RVCTuner Mel";

    static constexpr const char* engineName = isVoiceBuild ? "RVC voice" : "PC-NSF-HiFiGAN";

    /** @brief The line the editor shows before anything is open. */
    static constexpr const char* summary =
        isVoiceBuild
            ? "Open a vocal take to begin.\n\n"
              "Its melody is heard note by note, corrected the way you ask, and sung back through "
              "an RVC voice. The voice you load is the voice that comes out, so it has to be the "
              "one in the recording."
            : "Open a vocal take to begin.\n\n"
              "Its melody is heard note by note, corrected the way you ask, and sung back by a mel "
              "vocoder, which keeps whichever voice was recorded and needs no model of it.";
};
}
