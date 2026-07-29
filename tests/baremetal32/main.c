typedef unsigned int uint32_t;

static volatile uint32_t scratch;

int main(void)
{
    scratch = 0x12345678U;
    __asm__ volatile("fence rw, rw" ::: "memory");

    if (scratch != 0x12345678U) {
        return 1;
    }

    uint32_t sum = 0;
    for (uint32_t value = 1; value <= 10; ++value) {
        sum += value;
    }

    return sum == 55U ? 0 : 2;
}
