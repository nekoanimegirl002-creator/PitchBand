#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

// "Default" engine: real-time pitch shifter built from two crossfaded grains that read a delay
// line at a speed set by the pitch ratio (like a synth pitch wheel: pitch and timbre move together).
// Whenever a grain runs out of its window it jumps back; the jump target is chosen by waveform
// correlation so that the two grains stay in phase, which removes the "pumping".
class GrainShifter
{
public:
    void setTuning (double windowMs, double searchMs, double corrMs, float minStagger = 0.25f)
    {
        winMs = windowMs; srchMs = searchMs; corrMsV = corrMs; staggerMin = minStagger;
    }

    // Returns the actual latency in samples (>= latencyTarget is not guaranteed, see dMin below)
    int prepare (double sampleRate, int latencyTarget)
    {
        int w = (int) std::lround (winMs * 0.001 * sampleRate);
        w += (w & 1);
        W = (float) w;
        corrLen = ((int) std::lround (corrMsV * 0.001 * sampleRate)) & ~3;
        searchRange = (int) std::lround (srchMs * 0.001 * sampleRate);

        dMin = (float) std::max (corrLen + 4, latencyTarget - w / 2 - 2);
        dMax = dMin + W;

        int size = 1;
        while (size < (int) dMax + 64) size <<= 1;
        mask = size - 1;

        for (auto& b : buf) b.assign ((size_t) size, 0.0f);
        mono.assign ((size_t) size, 0.0f);
        ref.assign ((size_t) (corrLen / 2 + 2), 0.0f);
        scores.assign ((size_t) (searchRange + 3), 0.0f);

        wp = 0;
        dA = dMin;
        dB = dMin + 0.5f * W;
        latency = (int) dMin + w / 2 + 2;
        return latency;
    }

    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        std::fill (mono.begin(), mono.end(), 0.0f);
        wp = 0;
        dA = dMin;
        dB = dMin + 0.5f * W;
    }

    int getLatency() const { return latency; }

    // One stereo sample in, wet (pitch shifted) stereo sample out
    void process (float inL, float inR, float ratio, float& outL, float& outR)
    {
        buf[0][(size_t) wp] = inL;
        buf[1][(size_t) wp] = inR;
        mono[(size_t) wp] = 0.5f * (inL + inR);

        float wA = windowAt (dA);
        float wB = windowAt (dB);
        const float norm = 1.0f / std::max (0.25f, wA + wB);
        wA *= norm;
        wB *= norm;

        outL = wA * readHermite (0, dA) + wB * readHermite (0, dB);
        outR = wA * readHermite (1, dA) + wB * readHermite (1, dB);

        // Read position moves at 'ratio' speed relative to the write position
        const float step = 1.0f - ratio;
        dA += step;
        dB += step;

        if (dA < dMin)       dA = chooseReseat (dB, true);
        else if (dA > dMax)  dA = chooseReseat (dB, false);

        if (dB < dMin)       dB = chooseReseat (dA, true);
        else if (dB > dMax)  dB = chooseReseat (dA, false);

        wp = (wp + 1) & mask;
    }

private:
    float windowAt (float d) const
    {
        const float x = std::min (1.0f, std::max (0.0f, (d - dMin) / W));
        const float s = std::sin (3.14159265f * x);
        return s * s;
    }

    // 4-point Hermite interpolation, 'delay' in samples (fractional)
    float readHermite (int ch, float delay) const
    {
        const float rp = (float) wp - delay - 2.0f;
        const int   i0 = (int) std::floor (rp);
        const float f  = rp - (float) i0;

        const float* d = buf[(size_t) ch].data();
        const float y0 = d[(i0 - 1) & mask];
        const float y1 = d[ i0      & mask];
        const float y2 = d[(i0 + 1) & mask];
        const float y3 = d[(i0 + 2) & mask];

        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    // Where should a grain that left its window restart? Candidate positions are compared with
    // what the other grain plays (normalised cross-correlation). The first strong correlation peak
    // wins (smallest jump) and is refined to sub-sample precision.
    float chooseReseat (float dOther, bool toTop)
    {
        const int refD = (int) std::lround (dOther);
        const int n    = corrLen / 2;

        // How far the other grain has travelled through its window (0..1)
        const float tauO = toTop ? (dMax - dOther) / W : (dOther - dMin) / W;

        // Keep the two grains at least a quarter window apart so they never collapse together
        const int sMax = (int) std::min ((float) searchRange, std::max (0.0f, (tauO - staggerMin) * W));

        for (int k = 0; k < n; ++k)
            ref[(size_t) k] = mono[(size_t) ((wp - refD + 2 * k) & mask)];

        float best = -1.0e30f;
        for (int s = 0; s <= sMax; ++s)
        {
            const int dc = toTop ? (int) dMax - s : (int) dMin + s;
            float dot = 0.0f, en = 1.0e-9f;
            for (int k = 0; k < n; ++k)
            {
                const float c = mono[(size_t) ((wp - dc + 2 * k) & mask)];
                dot += ref[(size_t) k] * c;
                en  += c * c;
            }
            const float sc = dot / std::sqrt (en);
            scores[(size_t) s] = sc;
            if (sc > best) best = sc;
        }

        // First local maximum that is close to the best score
        int chosen = -1;
        const float thr = best > 0.0f ? 0.97f * best : best;
        for (int s = 0; s <= sMax; ++s)
        {
            const float prev = s > 0 ? scores[(size_t) (s - 1)] : -1.0e30f;
            const float next = s < sMax ? scores[(size_t) (s + 1)] : -1.0e30f;
            if (scores[(size_t) s] >= thr && scores[(size_t) s] >= prev && scores[(size_t) s] > next)
            {
                chosen = s;
                break;
            }
        }
        if (chosen < 0)
        {
            chosen = 0;
            for (int s = 1; s <= sMax; ++s)
                if (scores[(size_t) s] > scores[(size_t) chosen]) chosen = s;
        }

        // Parabolic refinement
        float sf = (float) chosen;
        if (chosen > 0 && chosen < sMax)
        {
            const float y0 = scores[(size_t) (chosen - 1)];
            const float y1 = scores[(size_t) chosen];
            const float y2 = scores[(size_t) (chosen + 1)];
            const float den = y0 - 2.0f * y1 + y2;
            if (den < -1.0e-12f)
                sf += std::max (-0.5f, std::min (0.5f, 0.5f * (y0 - y2) / den));
        }

        return toTop ? dMax - sf : dMin + sf;
    }

    double winMs = 50.0, srchMs = 12.0, corrMsV = 16.0;
    float staggerMin = 0.25f;
    std::vector<float> buf[2];
    std::vector<float> mono, ref, scores;
    int mask = 0, wp = 0, corrLen = 0, searchRange = 0, latency = 0;
    float W = 2400.0f, dMin = 0.0f, dMax = 0.0f, dA = 0.0f, dB = 0.0f;
};
