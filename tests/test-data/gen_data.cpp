#include <fstream>
#include <iostream>
#include <random>

int main() {
  // --- Generate gaussian-1d.txt ---
  {
    const char *filename = "gaussian-1d.txt";
    const int num_points = 200;
    const double mean = 0.0;
    const double stddev = 1.0;

    std::ofstream outfile(filename);
    if (!outfile) {
      std::cerr << "Failed to open " << filename << " for writing.\n";
      return 1;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<> dist(mean, stddev);

    for (int i = 0; i < num_points; ++i) {
      outfile << dist(gen) << "\n";
    }
    outfile.close();
    std::cout << "Generated " << filename << " with " << num_points
              << " Gaussian noise points.\n";
  }

  // --- Generate linear-1d.txt ---
  {
    const char *filename = "linear-1d.txt";
    const int blocks = 100;
    const int points_per_block = 100;
    const double stddev = 1.0;
    const double mean_start = 0.0;
    const double mean_step = 1.0;

    std::ofstream outfile(filename);
    if (!outfile) {
      std::cerr << "Failed to open " << filename << " for writing.\n";
      return 1;
    }

    std::random_device rd;
    std::mt19937 gen(rd());

    for (int block = 0; block < blocks; ++block) {
      double mean = mean_start + mean_step * block;
      std::normal_distribution<> dist(mean, stddev);
      for (int i = 0; i < points_per_block; ++i) {
        outfile << dist(gen) << "\n";
      }
    }
    outfile.close();
    std::cout << "Generated " << filename << " with "
              << (blocks * points_per_block)
              << " points (mean increases linearly).\n";
  }

  return 0;
}
