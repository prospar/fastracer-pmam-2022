#ifndef DEBUG_TIME_H
#define DEBUG_TIME_H

#include "Common.H"
#include <cassert>

class Time_DR_Detector {
private:
public:
    my_lock time_DR_detector_lock = 0;
    uint64_t total_recordmem_time = 0;
    uint64_t taskid_map_find_time = 0;
    uint64_t num_tid_find = 0;
    std::map<uint32_t, uint32_t> tid_find_map;
    uint64_t vc_find_time = 0;
    uint64_t num_vc_find = 0;
    std::map<uint32_t, uint32_t> vc_find_map;
    uint64_t num_root_vc_find = 0;
    std::map<uint32_t, uint32_t> root_vc_find_map;
    void dump() {
        std::cout << "\n\n***********DR_detector TIME DUMP***********\n";
        std::cout << "Total time in recordmem: " << total_recordmem_time/1000000 << " milliseconds" << "\n";
        std::cout << "Taskid map find time: " << taskid_map_find_time/1000000 << " milliseconds" << "\n";
        std::cout << "Number of taskid map find calls: " << num_tid_find << "\n";
        std::cout << "Vector Clock find time: " << vc_find_time/1000000 << " milliseconds" << "\n";
        std::cout << "Number of Vector Clock find calls: " << num_vc_find << "\n";
        std::cout << "Number of Root Vector Clock find calls: " << num_root_vc_find << "\n";
        std::cout << "***********END DUMP***********\n\n";
    }
};

class Time_Task_Management {
private:
public:
    my_lock time_task_management_lock = 0;
    uint32_t spawn_time = 0;
    uint32_t tbb_spawn_time = 0;
    uint32_t num_spawn = 0;
    uint32_t spawn_root_and_wait_time = 0;
    uint32_t tbb_spawn_root_and_wait_time = 0;
    uint32_t num_spawn_root_and_wait = 0;
    uint32_t spawn_and_wait_for_all_time = 0;
    uint32_t tbb_spawn_and_wait_for_all_time = 0;
    uint32_t num_spawn_and_wait_for_all = 0;
    uint32_t wait_for_all_time = 0;
    uint32_t tbb_wait_for_all_time = 0;
    uint32_t num_wait_for_all = 0;
    void dump() {
        std::cout << "\n\n***********TASK MANAGEMENT TIME DUMP***********\n";
        std::cout << "Total spawn() time: " << spawn_time/1000000 << " milliseconds"  << "\n";
        std::cout << "Base tbb::spawn() time: " << tbb_spawn_time/1000000 << " milliseconds"  << "\n";
        std::cout << "Total number of spawn() calls: " << num_spawn << "\n\n";

        std::cout << "Total spawn_root_and_wait() time: " << spawn_root_and_wait_time/1000000 << " milliseconds"  << "\n";
        std::cout << "Base tbb::spawn_root_and_wait() time: " << tbb_spawn_root_and_wait_time/1000000 << " milliseconds"  << "\n";
        std::cout << "Total number of spawn_root_and_wait() calls: " << num_spawn_root_and_wait << "\n\n";

        std::cout << "Total spawn_and_wait_for_all() time: " << spawn_and_wait_for_all_time/1000000 << " milliseconds"  << "\n";
        std::cout << "Base tbb::spawn_and_wait_for_all() time: " << tbb_spawn_and_wait_for_all_time/1000000 << " milliseconds"  << "\n";
        std::cout << "Total number of spawn_and_wait_for_all() calls: " << num_spawn_and_wait_for_all << "\n\n";

        std::cout << "Total wait_for_all() time: " << wait_for_all_time/1000000 << " milliseconds"  << "\n";
        std::cout << "Base tbb::wait_for_all() time: " << tbb_wait_for_all_time/1000000 << " milliseconds"  << "\n";
        std::cout << "Total number of wait_for_all() calls: " << num_wait_for_all << "\n\n";
        std::cout << "***********END DUMP***********\n\n";
    }
};


#endif