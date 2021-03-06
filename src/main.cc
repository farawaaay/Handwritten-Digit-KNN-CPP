#include <iostream>
#include <stdlib.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <thread>
#include <mutex>

#define INCBIN_STYLE INCBIN_STYLE_SNAKE
#include "../include/incbin.h"

INCBIN(_train_images, "mnist/train-images-idx3-ubyte");
INCBIN(_test_images, "mnist/t10k-images-idx3-ubyte");
INCBIN(_train_labels, "mnist/train-labels-idx1-ubyte");
INCBIN(_test_labels, "mnist/t10k-labels-idx1-ubyte");

constexpr const unsigned char *train_images_bin_start = g_train_images_data + 16;
constexpr const unsigned char *test_images_bin_start = g_test_images_data + 16;
constexpr const unsigned char *train_labels_bin_start = g_train_labels_data + 8;
constexpr const unsigned char *test_labels_bin_start = g_test_labels_data + 8;

constexpr int number_of_rows = 28;
constexpr int number_of_columns = 28;
constexpr int image_size = number_of_rows * number_of_columns * sizeof(u_int8_t);

#ifdef KNN_DEBUG
int number_of_train_images = 0;
int number_of_test_images = 0;
#else
const int number_of_train_images = (g_train_images_end - train_images_bin_start) / image_size;
const int number_of_test_images = (g_test_images_end - test_images_bin_start) / image_size;
#endif

struct distance : std::pair<u_int8_t, double>
{
    using std::pair<u_int8_t, double>::pair;
    bool operator<(const distance &str) const
    {
        return this->second < str.second;
    }
    friend std::ostream &operator<<(std::ostream &out, const distance &c)
    {
        out << "label: " << (char)(c.first + '0') << ", "
            << "distance: " << c.second;
        return out;
    }
};

struct counter
{
    std::mutex mutex;
    int value;
    counter() : value(0) {}
    void increment(int inc)
    {
        std::lock_guard<std::mutex> guard(mutex);
        value += inc;
    }
    void reset()
    {
        std::lock_guard<std::mutex> guard(mutex);
        value = 0;
    }
};

double minkowski_distance(const u_int8_t *a, const u_int8_t *b, int l, int p)
{
    double sum = 0;
    for (int i = 0; i < l; i++)
        sum += std::pow(std::abs(a[i] - b[i]), p);

    return std::pow(sum, 1 / (p + 0.0));
}

u_int8_t predict(const u_int8_t *test_image, int k, int p)
{
    std::vector<distance> distances(number_of_train_images);
    const u_int8_t *base = train_images_bin_start;

    for (int i = 0; i < number_of_train_images; i++)
    {
        double d = minkowski_distance(base, test_image, image_size, p);
        distances[i] = {train_labels_bin_start[i], d};

        base += image_size;
    }

    std::sort(distances.begin(), distances.end());

    std::vector<int> top_k(10, 0);
    for (int i = 0; i < k; i++)
        top_k[distances[i].first]++;

    u_int8_t result = 0;
    for (int i = 1; i < k; i++)
        if (top_k[i] > top_k[result])
            result = i;

    return result;
}

double run(int k, int p)
{
    counter wrong_count;
    counter all_count;

    unsigned int cpu_number = std::thread::hardware_concurrency();
    unsigned int load_per_cpu = number_of_test_images / cpu_number;

    std::vector<std::thread *> threads;
    for (unsigned int i = 0; i < cpu_number; i++)
        threads.push_back(new std::thread(
            [&wrong_count, &all_count](const u_int8_t *base, int start, int count, int k, int p) -> void {
                int local_wrong_count = 0;
                int local_all_count = 0;

                base += image_size * start;
                for (int i = 0; i < count; i++)
                {
                    u_int8_t result = predict(base, k, p);
                    u_int8_t right = test_labels_bin_start[start + i];

                    local_all_count++;
                    if (result != right)
                        local_wrong_count++;

                    base += image_size;
                }

                wrong_count.increment(local_wrong_count);
                all_count.increment(local_all_count);
            },
            test_images_bin_start,
            i *load_per_cpu,
            (int)load_per_cpu,
            k,
            p));

    for (unsigned int i = 0; i < cpu_number; i++)
    {
        threads[i]->join();
        delete threads[i];
    }

    return 100 - wrong_count.value / (all_count.value + 0.0) * 100;
}

int main(int argc, char **argv)
{
#ifdef KNN_DEBUG
    if (argc != 5)
    {
        std::cout << "Debug Usage: " << argv[0] << " K P TrainImageNum TestImageNum" << std::endl;
        return 1;
    }
    number_of_train_images = atoi(argv[3]);
    number_of_test_images = atoi(argv[4]);
#else
    if (argc != 3)
    {
        std::cout << "Usage: " << argv[0] << " K P" << std::endl;
        return 1;
    }
#endif

    std::cout << "number_of_train_images: " << number_of_train_images << std::endl;
    std::cout << "number_of_test_images: " << number_of_test_images << std::endl;

    // std::vector<int> P = {3};
    std::vector<int> P = {2, 3, 4};
    // std::vector<int> K = {10};
    std::vector<int> K = {8, 9, 10, 11, 12};

    printf("       ");
    for (auto p : P)
        printf("     p = %d", p);
    printf("\n");

    for (auto k : K)
    {
        printf("k = %2d ", k);
        for (auto p : P)
            printf("%10lf", run(k, p));
        printf("\n");
    }
}
