#include <stdlib.h>

int main() {
    int n = 10000000;
    int a[n];
    // allocate a large array and access it in constant stride

    int i;
    for (i = 0; i < n; i += 2) {
        int t;
        t = a[i];
    }

    // since we have a constant stride, stride predictor can easily catch such a pattern
    // and we choose a very large n, so that hit rate is almost 100%
}
