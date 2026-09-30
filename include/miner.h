#pragma once

namespace miner {
    void hw_task(void* arg);   // Hardware-SHA-Miner (Core 1)
    void sw_task(void* arg);   // Software-SHA-Miner (Core 0)
}
