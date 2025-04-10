#ifndef ASTRAEA_SCHEDULER_H__
#define ASTRAEA_SCHEDULER_H__

#include <semaphore.h>
#include <vector>

#include <doca_error.h>

/**
 * Forward declarations
 */
struct shared_resources;

class astraea_scheduler {
  private:
    /* Semaphores */
    std::vector<sem_t *> ec_token_sems;
    sem_t *metadata_sem = nullptr;

    /* Shared memory */
    int shm_fd = -1;
    shared_resources *shm_data = nullptr;

    void refresh_tokens();

  public:
    astraea_scheduler(doca_error_t *status);
    ~astraea_scheduler();

    void run();
};

#endif