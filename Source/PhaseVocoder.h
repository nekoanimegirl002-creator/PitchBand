#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

// "Complex" engine: sinusoidal (peak based) phase vocoder with formant control.
//  - every spectral peak is treated as a sinusoid and re-synthesised at pitchRatio x its frequency
//  - its loudness comes from the spectral envelope (interpolated through the source peaks) read at
//    position  newFrequency / formantRatio.  formantRatio = 1 keeps the formants where they were
//    (pitch changes, timbre stays); formantRatio = pitchRatio gives a plain "tape-like" shift.
// One instance processes one channel. Latency = fft size.
class PhaseVocoder
{
public:
    void prepare (double sampleRate, int fftSize, int overlap = 8)
    {
        sr = sampleRate;
        N = fftSize;
        half = N / 2;
        H = N / overlap;

        cosT.resize ((size_t) half);
        sinT.resize ((size_t) half);
        for (int i = 0; i < half; ++i)
        {
            cosT[(size_t) i] = (float) std::cos (2.0 * kPi * i / N);
            sinT[(size_t) i] = (float) std::sin (2.0 * kPi * i / N);
        }

        win.resize ((size_t) N);
        for (int i = 0; i < N; ++i)
            win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / N));

        // Overlap-add gain of Hann x Hann is 3/8 * overlap
        olaScale = (float) (1.0 / (0.375 * overlap));

        re.assign ((size_t) N, 0.0f);
        im.assign ((size_t) N, 0.0f);
        const size_t nb = (size_t) half + 1;
        mag.assign (nb, 0.0f);
        ph.assign (nb, 0.0f);
        trueF.assign (nb, 0.0f);
        accRe.assign (nb, 0.0);
        accIm.assign (nb, 0.0);
        lastPh.assign (nb, 0.0);
        peaks.clear();
        peaks.reserve (nb);
        pf.reserve (nb);  pa.reserve (nb);  pt.reserve (nb);  pl.reserve (nb);
        prevF.reserve (nb);  prevTheta.reserve (nb);  nextF.reserve (nb);  nextTheta.reserve (nb);
        inRing.assign ((size_t) N, 0.0f);
        outAccum.assign ((size_t) N, 0.0f);
        reset();
    }

    void reset()
    {
        std::fill (inRing.begin(), inRing.end(), 0.0f);
        std::fill (outAccum.begin(), outAccum.end(), 0.0f);
        std::fill (lastPh.begin(), lastPh.end(), 0.0);
        prevF.clear();
        prevTheta.clear();
        pos = 0;
        sinceFrame = 0;
    }

    int getLatency() const { return N; }

    float process (float in, float pitchRatio, float formantRatio)
    {
        inRing[(size_t) pos] = in;
        const float y = outAccum[(size_t) pos];
        outAccum[(size_t) pos] = 0.0f;
        pos = (pos + 1 == N) ? 0 : pos + 1;

        if (++sinceFrame >= H)
        {
            sinceFrame = 0;
            runFrame (pitchRatio, formantRatio);
        }
        return y;
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    // In-place iterative radix-2 FFT of size N (inverse is not normalised)
    void fft (bool inverse)
    {
        float* a = re.data();
        float* b = im.data();

        for (int i = 1, j = 0; i < N; ++i)
        {
            int bit = N >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { std::swap (a[i], a[j]); std::swap (b[i], b[j]); }
        }

        for (int len = 2; len <= N; len <<= 1)
        {
            const int stepT = N / len;
            const int h = len / 2;
            for (int i = 0; i < N; i += len)
            {
                for (int k = 0; k < h; ++k)
                {
                    const float wr = cosT[(size_t) (k * stepT)];
                    const float wi = inverse ? sinT[(size_t) (k * stepT)] : -sinT[(size_t) (k * stepT)];
                    const int u = i + k, v = i + k + h;
                    const float tr = a[v] * wr - b[v] * wi;
                    const float ti = a[v] * wi + b[v] * wr;
                    a[v] = a[u] - tr;  b[v] = b[u] - ti;
                    a[u] += tr;        b[u] += ti;
                }
            }
        }
    }

    static double wrapPi (double x)
    {
        return x - 2.0 * kPi * std::floor ((x + kPi) / (2.0 * kPi));
    }

    // sum_{n=0}^{N-1} exp(-j 2 pi x n / N)
    void dirichlet (double x, double& r, double& i) const
    {
        double m;
        if (std::fabs (x) < 1.0e-9)
            m = (double) N;
        else
            m = std::sin (kPi * x) / std::sin (kPi * x / (double) N);
        const double ang = -kPi * x * (double) (N - 1) / (double) N;
        r = m * std::cos (ang);
        i = m * std::sin (ang);
    }

    // Frequency response (complex) of the periodic Hann window at an offset of 'd' bins
    void hannResponse (double d, double& rOut, double& iOut) const
    {
        double r0, i0, r1, i1, r2, i2;
        dirichlet (d, r0, i0);
        dirichlet (d - 1.0, r1, i1);
        dirichlet (d + 1.0, r2, i2);
        rOut = 0.5 * r0 - 0.25 * r1 - 0.25 * r2;
        iOut = 0.5 * i0 - 0.25 * i1 - 0.25 * i2;
    }

    // Envelope through the detected sinusoids (log amplitude, linear in frequency)
    double envelopeAt (double x) const
    {
        const size_t n = pf.size();
        if (n == 0) return 0.0;
        if (x <= pf[0]) return pa[0];
        if (x >= pf[n - 1])
        {
            const double r = pf[n - 1] / std::max (x, 1.0e-6);
            return pa[n - 1] * r * r;                    // gentle roll-off above the last sinusoid
        }
        size_t lo = 0, hi = n - 1;
        while (hi - lo > 1)
        {
            const size_t mid = (lo + hi) / 2;
            if (pf[mid] <= x) lo = mid; else hi = mid;
        }
        const double span = pf[hi] - pf[lo];
        const double t = span > 1.0e-9 ? (x - pf[lo]) / span : 0.0;
        return std::exp (pl[lo] * (1.0 - t) + pl[hi] * t);
    }

    void runFrame (float pitchRatio, float formantRatio)
    {
        // ---- 1. analysis ----
        for (int n = 0; n < N; ++n)
        {
            int idx = pos + n;
            if (idx >= N) idx -= N;
            re[(size_t) n] = inRing[(size_t) idx] * win[(size_t) n];
            im[(size_t) n] = 0.0f;
        }
        fft (false);

        const double expectedStep = 2.0 * kPi * (double) H / (double) N;
        float maxMag = 0.0f;
        for (int k = 0; k <= half; ++k)
        {
            const float r = re[(size_t) k], i = im[(size_t) k];
            const float m = std::sqrt (r * r + i * i);
            mag[(size_t) k] = m;
            maxMag = std::max (maxMag, m);

            const double p = std::atan2 ((double) i, (double) r);
            ph[(size_t) k] = (float) p;
            const double d = wrapPi (p - lastPh[(size_t) k] - expectedStep * k);
            lastPh[(size_t) k] = p;
            trueF[(size_t) k] = (float) (k + d / expectedStep);
        }

        if (maxMag < 1.0e-7f)
        {
            prevF.clear();
            prevTheta.clear();
            return;
        }

        // ---- 2. find sinusoids (spectral peaks) ----
        peaks.clear();
        const float peakFloor = maxMag * 3.0e-4f;
        for (int k = 2; k < half - 1; ++k)
        {
            const float m = mag[(size_t) k];
            if (m > peakFloor && m > mag[(size_t) (k - 1)] && m > mag[(size_t) (k - 2)]
                && m >= mag[(size_t) (k + 1)] && m >= mag[(size_t) (k + 2)])
                peaks.push_back (k);
        }

        pf.clear(); pa.clear(); pt.clear(); pl.clear();
        for (const int p : peaks)
        {
            double f = (double) trueF[(size_t) p];
            if (std::fabs (f - (double) p) > 1.0) f = (double) p;

            double sRe, sIm;
            hannResponse ((double) p - f, sRe, sIm);
            const double sMag = std::sqrt (sRe * sRe + sIm * sIm);
            const double sArg = std::atan2 (sIm, sRe);
            const double amp  = 2.0 * (double) mag[(size_t) p] / std::max (sMag, 1.0e-9);
            const double theta = (double) ph[(size_t) p] - sArg;   // phase at the start of the frame

            // drop duplicates (the same sinusoid seen from a neighbouring bin)
            if (! pf.empty() && f - pf.back() < 0.6)
            {
                if (amp > pa.back()) { pf.back() = f; pa.back() = amp; pt.back() = theta; }
                continue;
            }
            pf.push_back (f);
            pa.push_back (amp);
            pt.push_back (theta);
        }
        pl.resize (pa.size());
        for (size_t i = 0; i < pa.size(); ++i)
            pl[i] = std::log (std::max (pa[i], 1.0e-12));

        std::fill (accRe.begin(), accRe.end(), 0.0);
        std::fill (accIm.begin(), accIm.end(), 0.0);

        nextF.clear();
        nextTheta.clear();

        const double psiStep = 2.0 * kPi * (double) H / (double) N;

        // ---- 3. move every sinusoid and re-synthesise its spectral lobe ----
        for (size_t i = 0; i < pf.size(); ++i)
        {
            const double f = pf[i];
            const double fNew = f * (double) pitchRatio;
            if (fNew < 1.0 || fNew > (double) half - 2.0) continue;

            // Continue the phase of the closest sinusoid of the previous frame
            double thetaNew = pt[i];
            double bestDist = 1.5;
            for (size_t j = 0; j < prevF.size(); ++j)
            {
                const double dist = std::fabs (prevF[j] - f);
                if (dist < bestDist)
                {
                    bestDist = dist;
                    thetaNew = wrapPi (prevTheta[j] + psiStep * fNew);
                }
            }
            nextF.push_back (f);
            nextTheta.push_back (thetaNew);

            // loudness: envelope read at (new frequency / formantRatio)
            const double ampOut = 0.5 * envelopeAt (fNew / (double) formantRatio);

            const double c = std::cos (thetaNew), s = std::sin (thetaNew);
            const int q = (int) std::lround (fNew);
            for (int t = q - 3; t <= q + 3; ++t)
            {
                if (t < 1 || t >= half) continue;
                double lr, li;
                hannResponse ((double) t - fNew, lr, li);
                accRe[(size_t) t] += ampOut * (c * lr - s * li);
                accIm[(size_t) t] += ampOut * (c * li + s * lr);
            }
        }
        prevF.swap (nextF);
        prevTheta.swap (nextTheta);

        // ---- 4. synthesis ----
        for (int t = 0; t <= half; ++t)
        {
            re[(size_t) t] = (float) accRe[(size_t) t];
            im[(size_t) t] = (float) accIm[(size_t) t];
        }
        im[0] = 0.0f;
        im[(size_t) half] = 0.0f;
        for (int t = 1; t < half; ++t)
        {
            re[(size_t) (N - t)] =  re[(size_t) t];
            im[(size_t) (N - t)] = -im[(size_t) t];
        }
        fft (true);

        const float invN = 1.0f / (float) N;
        for (int n = 0; n < N; ++n)
        {
            int idx = pos + n;
            if (idx >= N) idx -= N;
            outAccum[(size_t) idx] += re[(size_t) n] * invN * win[(size_t) n] * olaScale;
        }
    }

    double sr = 44100.0;
    int N = 2048, half = 1024, H = 256;
    int pos = 0, sinceFrame = 0;
    float olaScale = 1.0f;

    std::vector<float> cosT, sinT, win, re, im;
    std::vector<float> mag, ph, trueF;
    std::vector<double> accRe, accIm, lastPh;
    std::vector<float> inRing, outAccum;
    std::vector<int> peaks;
    std::vector<double> pf, pa, pt, pl;          // frequency, amplitude, phase, log amplitude of the sinusoids
    std::vector<double> prevF, prevTheta, nextF, nextTheta;
};
