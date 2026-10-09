#include "zlpng.h"
#include <string.h>
int main(void) {
    unsigned char raw[12] = {1,2,3,4,5,6,7,8,9,10,11,12};
    ZLPNG_ImageData image = {{raw, sizeof(raw)}, 2, 3, 2, 1, 12};
    ZLPNG_Buffer stored = ZLPNG_Compress(&image, 1);
    if (!stored.Data) return 1;
    ZLPNG_ImageData restored = ZLPNG_DecompressWithLimit(stored, sizeof(raw));
    int result = !restored.Buffer.Data || restored.Buffer.Bytes != sizeof(raw) ||
        restored.StrideBytes != 12 || memcmp(raw, restored.Buffer.Data, sizeof(raw));
    ZLPNG_Free(&stored); ZLPNG_Free(&restored.Buffer);
    return result;
}
