#include "mining_job.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <string.h>

namespace jobs {

static SemaphoreHandle_t s_mutex;
static QueueHandle_t     s_shareQueue;
static MiningJob         s_job;
static volatile bool     s_valid = false;
static volatile uint32_t s_generation = 0;
static volatile double   s_difficulty = 1.0;
static volatile bool     s_newWorkRequested = false;

void begin()
{
    s_mutex      = xSemaphoreCreateMutex();
    s_shareQueue = xQueueCreate(16, sizeof(ShareSubmit));
}

void publish(const MiningJob& job)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_job = job;
    s_job.generation = s_generation + 1;
    s_valid = true;
    s_generation = s_job.generation;
    xSemaphoreGive(s_mutex);
}

void invalidate()
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_valid = false;
    s_generation = s_generation + 1;
    xSemaphoreGive(s_mutex);
}

bool valid()          { return s_valid; }
uint32_t generation() { return s_generation; }

bool copy(MiningJob& out)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool ok = s_valid;
    if (ok) out = s_job;
    xSemaphoreGive(s_mutex);
    return ok;
}

void   setDifficulty(double d) { s_difficulty = d; }
double difficulty()            { return s_difficulty; }

void requestNewWork() { s_newWorkRequested = true; }

bool takeNewWorkRequest()
{
    if (!s_newWorkRequested) return false;
    s_newWorkRequested = false;
    return true;
}

bool pushShare(const ShareSubmit& s) { return xQueueSend(s_shareQueue, &s, 0) == pdTRUE; }
bool popShare(ShareSubmit& s)        { return xQueueReceive(s_shareQueue, &s, 0) == pdTRUE; }

}  // namespace jobs
