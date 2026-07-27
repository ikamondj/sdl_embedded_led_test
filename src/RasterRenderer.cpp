#include "RasterRenderer.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>


constexpr int SUPERSAMPLE_GRID = 2;

// Applied to predicate coverage when rast() uses a 2x2 or larger grid.
// 0.0 is ordinary linear antialiasing.  Higher values increasingly suppress
// low-coverage samples while preserving both zero and full coverage.
constexpr float ANTIALIASING_COVERAGE_FALLOFF = 1.3f;


float applyAntialiasingCoverageFalloff(float coverage, int gridSize)
{
    coverage = std::clamp(coverage, 0.0f, 1.0f);

    if (gridSize < 2 || ANTIALIASING_COVERAGE_FALLOFF <= 0.0f) {
        return coverage;
    }

    return std::pow(
        coverage,
        1.0f + ANTIALIASING_COVERAGE_FALLOFF);
}




ColorF shadeSample(float x, float y, const RenderInputs& input);

namespace {
constexpr int PIXEL_COUNT =
    Hardware::MATRIX_WIDTH * Hardware::MATRIX_HEIGHT;
constexpr int PASS_COUNT = static_cast<int>(RasterPass::Count);

bool threadedRendering = false;
bool rasterMaskLoaded = false;
std::array<std::uint8_t, PIXEL_COUNT> rasterMask{};
std::array<std::array<std::uint8_t, PIXEL_COUNT>, PASS_COUNT> passMasks{};
std::vector<int> visibleRasterPixels;
bool recordingPassRelevance = false;
thread_local int activeRasterPixel = -1;

constexpr std::array<const char*, PASS_COUNT> PASS_NAMES{{
    "eye", "pupil", "pupil-highlight", "inner-pupil", "brow",
    "mouth", "tongue", "teeth", "top-bangs", "bottom-bangs"}};

class RasterPool {
public:
  RasterPool() {
    const unsigned detected = std::thread::hardware_concurrency();
    threadCount_ = std::clamp(
        static_cast<int>(detected == 0 ? 4 : detected), 1, 4);
    for (int index = 1; index < threadCount_; ++index) {
      workers_.emplace_back([this, index]() { workerLoop(index); });
    }
  }

  ~RasterPool() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
      ++generation_;
    }
    workReady_.notify_all();
    for (auto& worker : workers_) worker.join();
  }

  void run(const std::function<void(int, int)>& job, int begin, int end) {
    if (threadCount_ == 1 || end <= begin) {
      job(begin, end);
      return;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      job_ = job;
      begin_ = begin;
      end_ = end;
      completed_ = 0;
      ++generation_;
    }
    workReady_.notify_all();
    runSlice(0);

    std::unique_lock<std::mutex> lock(mutex_);
    workDone_.wait(lock, [this]() {
      return completed_ == static_cast<int>(workers_.size());
    });
    job_ = {};
  }

private:
  void runSlice(int index) {
    const int sliceBegin =
        begin_ + ((end_ - begin_) * index) / threadCount_;
    const int sliceEnd =
        begin_ + ((end_ - begin_) * (index + 1)) / threadCount_;
    job_(sliceBegin, sliceEnd);
  }

  void workerLoop(int index) {
    std::uint64_t observedGeneration = 0;
    for (;;) {
      {
        std::unique_lock<std::mutex> lock(mutex_);
        workReady_.wait(lock, [&]() {
          return stopping_ || generation_ != observedGeneration;
        });
        if (stopping_) return;
        observedGeneration = generation_;
      }

      runSlice(index);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        ++completed_;
      }
      workDone_.notify_one();
    }
  }

  int threadCount_ = 1;
  int begin_ = 0;
  int end_ = 0;
  int completed_ = 0;
  std::uint64_t generation_ = 0;
  bool stopping_ = false;
  std::function<void(int, int)> job_;
  std::mutex mutex_;
  std::condition_variable workReady_;
  std::condition_variable workDone_;
  std::vector<std::thread> workers_;
};

RasterPool& rasterPool() {
  static RasterPool pool;
  return pool;
}
}

void setThreadedRendering(bool enabled) {
  threadedRendering = enabled;
}

bool rasterPassRelevant(RasterPass pass) {
  if (recordingPassRelevance || !rasterMaskLoaded ||
      activeRasterPixel < 0) {
    return true;
  }
  return passMasks[static_cast<std::size_t>(pass)]
                  [static_cast<std::size_t>(activeRasterPixel)] != 0;
}

void recordRasterPassChange(RasterPass pass) {
  if (recordingPassRelevance && activeRasterPixel >= 0) {
    passMasks[static_cast<std::size_t>(pass)]
             [static_cast<std::size_t>(activeRasterPixel)] = 1;
  }
}

namespace {
using Frame = std::array<Hardware::Rgb, PIXEL_COUNT>;

Frame computeFrame(const RenderInputs& input, bool applyMask) {
  constexpr float worldWidth = 3.0f;
  constexpr float worldHeight = 1.5f;
  constexpr float pixelWidth = worldWidth / static_cast<float>(Hardware::MATRIX_WIDTH);
  constexpr float pixelHeight = worldHeight / static_cast<float>(Hardware::MATRIX_HEIGHT);
  constexpr int sampleCount = SUPERSAMPLE_GRID * SUPERSAMPLE_GRID;

  Frame frame{};

  auto renderPixel = [&](int pixelIndex) {
    activeRasterPixel = pixelIndex;
    const int pixelX = pixelIndex % Hardware::MATRIX_WIDTH;
    const int pixelY = pixelIndex / Hardware::MATRIX_WIDTH;
    ColorF accumulated{};

    for (int sampleY = 0; sampleY < SUPERSAMPLE_GRID; ++sampleY) {
      for (int sampleX = 0; sampleX < SUPERSAMPLE_GRID; ++sampleX) {
        const float subpixelX =
            (static_cast<float>(sampleX) + 0.5f) /
            static_cast<float>(SUPERSAMPLE_GRID);
        const float subpixelY =
            (static_cast<float>(sampleY) + 0.5f) /
            static_cast<float>(SUPERSAMPLE_GRID);

        // Pixel-center raster coordinates: X in [-1.5,+1.5],
        // Y in [-0.75,+0.75], with positive Y upward.
        const float x =
            -1.5f + (static_cast<float>(pixelX) + subpixelX) * pixelWidth;
        const float y =
            0.75f - (static_cast<float>(pixelY) + subpixelY) * pixelHeight;

        accumulated = add(accumulated, shadeSample(x, y, input));
      }
    }

    frame[static_cast<std::size_t>(pixelIndex)] =
        toRgb8(scale(accumulated, 1.0f / sampleCount));
  };

  // Always evaluate pixel zero to prime the stateful blink/gaze schedulers,
  // even when the visibility mask marks it black.
  renderPixel(0);

  const bool useMask = applyMask && rasterMaskLoaded;
  const int workBegin = useMask ? 0 : 1;
  const int workEnd = useMask
      ? static_cast<int>(visibleRasterPixels.size())
      : PIXEL_COUNT;
  auto renderRange = [&](int begin, int end) {
    for (int work = begin; work < end; ++work) {
      renderPixel(useMask
          ? visibleRasterPixels[static_cast<std::size_t>(work)]
          : work);
    }
  };
  if (threadedRendering) {
    rasterPool().run(renderRange, workBegin, workEnd);
  } else {
    renderRange(workBegin, workEnd);
  }

  return frame;
}
}  // namespace

void renderFrame(const RenderInputs& input) {
  const Frame frame = computeFrame(input, true);
  for (int pixel = 0; pixel < PIXEL_COUNT; ++pixel) {
    Hardware::setLed(
        pixel % Hardware::MATRIX_WIDTH,
        pixel / Hardware::MATRIX_WIDTH,
        frame[static_cast<std::size_t>(pixel)]);
  }
}

bool loadRasterMask(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return false;

  std::array<char, 8> magic{};
  input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
  constexpr std::array<char, 8> expected{{'S','O','T','N','P','A','S','S'}};
  if (!input || magic != expected) return false;

  for (auto& mask : passMasks) {
    input.read(
        reinterpret_cast<char*>(mask.data()),
        static_cast<std::streamsize>(mask.size()));
  }
  if (!input) return false;

  for (int pixel = 0; pixel < PIXEL_COUNT; ++pixel) {
    bool anyRelevant = false;
    for (const auto& mask : passMasks) {
      anyRelevant = anyRelevant ||
          mask[static_cast<std::size_t>(pixel)] != 0;
    }
    rasterMask[static_cast<std::size_t>(pixel)] = anyRelevant ? 0 : 1;
  }

  visibleRasterPixels.clear();
  visibleRasterPixels.reserve(PIXEL_COUNT);
  for (int pixel = 1; pixel < PIXEL_COUNT; ++pixel) {
    if (rasterMask[static_cast<std::size_t>(pixel)] == 0) {
      visibleRasterPixels.push_back(pixel);
    }
  }
  rasterMaskLoaded = true;

  for (int pass = 0; pass < PASS_COUNT; ++pass) {
    const int relevant = static_cast<int>(std::count(
        passMasks[static_cast<std::size_t>(pass)].begin(),
        passMasks[static_cast<std::size_t>(pass)].end(),
        static_cast<std::uint8_t>(1)));
    std::cout << "Raster pass " << PASS_NAMES[static_cast<std::size_t>(pass)]
              << ": " << relevant << " / " << PIXEL_COUNT
              << " relevant pixels; skipping "
              << (PIXEL_COUNT - relevant) << ".\n";
  }

  const int fullySkipped = static_cast<int>(std::count(
      rasterMask.begin(), rasterMask.end(), static_cast<std::uint8_t>(1)));
  std::cout << "Raster union: " << fullySkipped << " / " << PIXEL_COUNT
            << " pixels skip every pass.\n";
  return true;
}

bool generateRasterMask(const std::string& path) {
  constexpr int gridSize = 80;
  const bool previousThreading = threadedRendering;
  threadedRendering = true;
  rasterMaskLoaded = false;
  recordingPassRelevance = true;
  for (auto& mask : passMasks) mask.fill(0);

  std::cout << "Precomputing per-pass raster relevance ("
            << (2 * gridSize * gridSize) << " frames)...\n";
  int frameNumber = 0;
  for (int joystick = 0; joystick < 2; ++joystick) {
    for (int gridY = 0; gridY < gridSize; ++gridY) {
      const float y =
          -1.0f + 2.0f * static_cast<float>(gridY) /
                      static_cast<float>(gridSize - 1);
      for (int gridX = 0; gridX < gridSize; ++gridX) {
        const float x =
            -1.0f + 2.0f * static_cast<float>(gridX) /
                        static_cast<float>(gridSize - 1);
        RenderInputs input{};
        input.timeSeconds = static_cast<float>(frameNumber) * 0.001f;
        input.disableBlink = true;
        if (joystick == 0) input.joystick1 = {x, y};
        else input.joystick2 = {x, y};

        (void)computeFrame(input, false);
        ++frameNumber;
      }
      if ((gridY + 1) % 10 == 0) {
        std::cout << "  " << frameNumber << " / "
                  << (2 * gridSize * gridSize) << " frames\n";
      }
    }
  }

  recordingPassRelevance = false;
  threadedRendering = previousThreading;

  // Autonomous pupil offsets are deliberately not bounded by the joystick
  // sweep. Keep the three inexpensive circle passes universally relevant.
  passMasks[static_cast<std::size_t>(RasterPass::Pupil)].fill(1);
  passMasks[static_cast<std::size_t>(RasterPass::PupilHighlight)].fill(1);
  passMasks[static_cast<std::size_t>(RasterPass::InnerPupil)].fill(1);

  for (int pixel = 0; pixel < PIXEL_COUNT; ++pixel) {
    bool anyRelevant = false;
    for (const auto& mask : passMasks) {
      anyRelevant = anyRelevant ||
          mask[static_cast<std::size_t>(pixel)] != 0;
    }
    rasterMask[static_cast<std::size_t>(pixel)] = anyRelevant ? 0 : 1;
  }

  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  constexpr std::array<char, 8> magic{{'S','O','T','N','P','A','S','S'}};
  output.write(magic.data(), static_cast<std::streamsize>(magic.size()));
  for (const auto& mask : passMasks) {
    output.write(
        reinterpret_cast<const char*>(mask.data()),
        static_cast<std::streamsize>(mask.size()));
  }
  if (!output) {
    std::cerr << "Could not write raster mask: " << path << '\n';
    return false;
  }

  std::cout << "Wrote per-pass raster mask: " << path << '\n';
  for (int pass = 0; pass < PASS_COUNT; ++pass) {
    const int relevant = static_cast<int>(std::count(
        passMasks[static_cast<std::size_t>(pass)].begin(),
        passMasks[static_cast<std::size_t>(pass)].end(),
        static_cast<std::uint8_t>(1)));
    std::cout << "  " << PASS_NAMES[static_cast<std::size_t>(pass)]
              << ": " << relevant << " relevant, "
              << (PIXEL_COUNT - relevant) << " skipped\n";
  }
  return true;
}

bool inTri(
    float x,
    float y,

    float vertex1X,
    float vertex1Y,

    float vertex2X,
    float vertex2Y,

    float vertex3X,
    float vertex3Y,

    float bulge)
{
    constexpr float epsilon = 1e-6f;
    bulge = std::clamp(bulge, 0.0f, 1.0f);

    const float edge12X = vertex2X - vertex1X;
    const float edge12Y = vertex2Y - vertex1Y;
    const float toThirdX = vertex3X - vertex1X;
    const float toThirdY = vertex3Y - vertex1Y;
    const float winding =
        edge12X * toThirdY - edge12Y * toThirdX;
    if (std::fabs(winding) < epsilon) return false;

    // Each straight edge contributes its midpoint. Move that midpoint away
    // from the opposite corner to approximate the former curved boundary.
    // The result is a convex six-sided polygon requiring only multiply/add
    // operations during containment testing.
    constexpr float BULGE_SCALE = 0.25f;
    auto bulgedMidpoint = [bulge](
        float ax, float ay, float bx, float by, float ox, float oy) {
      const float mx = 0.5f * (ax + bx);
      const float my = 0.5f * (ay + by);
      const float amount = BULGE_SCALE * bulge;
      return std::array<float, 2>{
          mx + (mx - ox) * amount,
          my + (my - oy) * amount};
    };

    const auto midpoint12 = bulgedMidpoint(
        vertex1X, vertex1Y, vertex2X, vertex2Y, vertex3X, vertex3Y);
    const auto midpoint23 = bulgedMidpoint(
        vertex2X, vertex2Y, vertex3X, vertex3Y, vertex1X, vertex1Y);
    const auto midpoint31 = bulgedMidpoint(
        vertex3X, vertex3Y, vertex1X, vertex1Y, vertex2X, vertex2Y);
    const std::array<std::array<float, 2>, 6> vertices{{
        {vertex1X, vertex1Y}, midpoint12,
        {vertex2X, vertex2Y}, midpoint23,
        {vertex3X, vertex3Y}, midpoint31}};

    const float orientation = winding > 0.0f ? 1.0f : -1.0f;
    for (std::size_t index = 0; index < vertices.size(); ++index) {
      const auto& start = vertices[index];
      const auto& end = vertices[(index + 1) % vertices.size()];
      const float cross =
          (end[0] - start[0]) * (y - start[1]) -
          (end[1] - start[1]) * (x - start[0]);
      if (orientation * cross < -epsilon) return false;
    }
    return true;
}


float saturate(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

float lerpFloat(float a, float b, float amount)
{
    return a + (b - a) * amount;
}

float smoothstep01(float value)
{
    const float t = saturate(value);
    return t * t * (3.0f - 2.0f * t);
}

float easeOutCubic(float value)
{
    const float t = saturate(value);
    const float inverse = 1.0f - t;

    return 1.0f - inverse * inverse * inverse;
}
