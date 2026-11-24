/* Third-hart background traffic generator for T4C. */
#include <stdint.h>

#define REG32(address) (*(volatile uint32_t *)(address))

#define NOISE_CTL  REG32(0xC0000068u)
#define NOISE_STAT REG32(0xC000006Cu)

#define WIN_BASE   0xC2001D00u
#define WIN_WORDS  128u

#define NOISE_ENABLE_BIT 0x1u
#define NOISE_RANDOM_BIT 0x2u
#define NOISE_READ_BIT   0x4u
#define NOISE_WRITE_BIT  0x8u

static uint32_t next_lfsr_value(uint32_t current_value)
{
    uint32_t shifted_value;

    shifted_value = current_value >> 1;

    if ((current_value & 1u) != 0u) {
        shifted_value ^= 0x80200003u;
    }

    return shifted_value;
}

static uint32_t choose_noise_index(uint32_t control_word,
                                   uint32_t lfsr_value,
                                   uint32_t heartbeat)
{
    if ((control_word & NOISE_RANDOM_BIT) != 0u) {
        return lfsr_value % WIN_WORDS;
    }

    return heartbeat % WIN_WORDS;
}

static void run_one_burst(uint32_t control_word,
                          uint32_t *lfsr_state,
                          uint32_t *heartbeat)
{
    uint32_t access_number;

    for (access_number = 0u; access_number < 16u; access_number++) {
        uint32_t noise_index;
        volatile uint32_t *noise_word;

        *lfsr_state = next_lfsr_value(*lfsr_state);
        noise_index = choose_noise_index(control_word, *lfsr_state, *heartbeat);
        noise_word = (volatile uint32_t *)(WIN_BASE + (4u * noise_index));

        if ((control_word & NOISE_READ_BIT) != 0u) {
            uint32_t read_value;

            read_value = *noise_word;
            (void)read_value;
        }

        if ((control_word & NOISE_WRITE_BIT) != 0u) {
            *noise_word = 0x4E200000u | (*heartbeat & 0xFFFFu);
        }

        (*heartbeat)++;
    }
}

static void wait_between_bursts(uint32_t control_word)
{
    uint32_t duty_delay;
    volatile uint32_t delay_count;

    duty_delay = ((control_word >> 8) & 0xFFu) * 16u;

    for (delay_count = 0u; delay_count < duty_delay; delay_count++) {
        /* Busy wait. */
    }
}

int main(void)
{
    uint32_t lfsr_state;
    uint32_t heartbeat;

    lfsr_state = 0xACE12026u;
    heartbeat = 0u;

    NOISE_STAT = 0x1D1E0000u;

    for (;;) {
        uint32_t control_word;

        control_word = NOISE_CTL;

        if ((control_word & NOISE_ENABLE_BIT) == 0u) {
            NOISE_STAT = 0x1D1E0000u | (heartbeat & 0xFFFFu);
            continue;
        }

        run_one_burst(control_word, &lfsr_state, &heartbeat);

        NOISE_STAT = 0xBEA70000u | (heartbeat & 0xFFFFu);
        wait_between_bursts(control_word);
    }

    return 0;
}
