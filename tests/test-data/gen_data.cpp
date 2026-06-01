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
    const int num_points = 200;
    const double linear_min = 0.0;
    const double linear_max = 1.0;

    std::ofstream outfile(filename);
    if (!outfile) {
      std::cerr << "Failed to open " << filename << " for writing.\n";
      return 1;
    }

    for (int i = 0; i < num_points; ++i) {
      double value = linear_min + (linear_max - linear_min) *
                     (static_cast<double>(i) / (num_points - 1));
      outfile << value << "\n";
    }
    outfile.close();
    std::cout << "Generated " << filename << " with " << num_points
              << " linearly spaced points from " << linear_min
              << " to " << linear_max << ".\n";
  }

  return 0;
}
