#include <stdlib.h>

int main() {
    int n = 100000000;
    int a[n];
    // cache line size is 64 bytes, int is 4 bytes
    
    int i;
    for (i = 0; i < n - 32; i++) {
        int j = i + 16;
        int a1, a2; //tmp registers to hold data in each cache line

        a1 = a[i]; // first read is a miss and prefetch next line
        a2 = a[j]; // a2 is the data from 2rd cache line, since a1 will prefeth a2, it will be a hit
    }

   // since we always prefetch next line when accessing a1, we should get a hit on a2
   // so after a long run n very large, we should get a high hit rate
}
    

        

