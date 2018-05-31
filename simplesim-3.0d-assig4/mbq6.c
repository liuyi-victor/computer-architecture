#include <stdlib.h>

int main() {
    // our open-end predictor can catch an access stride pattern of length 2
    // so if we try to access a two different strides in each run, we should get a high hit rate
    //

    int n = 10000000;
    int a[n];

    int i;
    for (i = 0; i < n - 8; i++) {
        int t;
        int index = i % 2 == 0 ? i + 3 : i + 7;
        // read different stride in memory
        // alternatively
        t = a[index];
    }
    // since our predictor has the ability to catch different stride pattern for the same pc index,
    // after a long run n = 10000000, we should get a high hit rate
}
