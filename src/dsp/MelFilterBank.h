#pragma once

#include <vector>

namespace rvctuner
{
/** @brief Which mel scale a filter bank is spaced on. */
enum class MelScale
{
    /** @brief The piecewise linear-then-logarithmic scale librosa uses by default. */
    slaney,

    /** @brief 1127 ln(1 + f/700), as used by HTK and by RMVPE's front end. */
    htk
};

/** @brief Builds a triangular mel filter bank, laid out as numMelBins rows of numBins weights.

    The result matches librosa.filters.mel for the same arguments, including its area
    normalisation, so a bank generated here can stand in for one exported from Python.

    @param scale        Spacing of the band centres.
    @param sampleRate   Rate the spectra were taken at.
    @param fftSize      Transform length; the row length is fftSize / 2 + 1.
    @param numMelBins   Number of bands.
    @param minimumHz    Lower edge of the lowest band.
    @param maximumHz    Upper edge of the highest band.
    @param isNormalised Whether to scale each band by the reciprocal of its width.
    @return The bank, row major.
*/
[[nodiscard]] std::vector<float> makeMelFilterBank (MelScale scale,
                                                    double sampleRate,
                                                    int fftSize,
                                                    int numMelBins,
                                                    double minimumHz,
                                                    double maximumHz,
                                                    bool isNormalised = true);
}
