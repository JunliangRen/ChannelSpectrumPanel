#pragma once
#include <thread>
#include "../src/background_save.h"
void reset_allocation_count();
std::size_t get_allocation_count();
inline void reservation_stress_tests(){
    auto waitResult=[](const std::shared_ptr<csp::SaveResult>& result){
        const auto deadline=GetTickCount64()+5000;
        while(result->status.load(std::memory_order_acquire)==csp::SaveStatus::running && GetTickCount64()<deadline)Sleep(1);
        return result->status.load(std::memory_order_acquire);
    };
    struct Race {
        HANDLE start=CreateEventW(nullptr,TRUE,FALSE,nullptr),finish=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        std::atomic<unsigned> ready{0};
        std::array<int,8> outcomes{};
        std::array<std::shared_ptr<csp::SaveResult>,8> results;
        ~Race(){CloseHandle(start);CloseHandle(finish);}
    };
    const auto path=std::filesystem::path("build")/L"reservation-stress.csm";
    for(unsigned round=0;round<8;++round){
        auto race=std::make_shared<Race>();check(race->start && race->finish,"create reservation race events");
        std::vector<std::jthread> threads;threads.reserve(8);
        for(unsigned i=0;i<8;++i)threads.emplace_back([race,path,i]{
            ++race->ready;
            if(WaitForSingleObject(race->start,5000)!=WAIT_OBJECT_0){race->outcomes[i]=3;return;}
            try {
                race->results[i]=csp::start_path_save(path,[race](const std::filesystem::path&){
                    if(WaitForSingleObject(race->finish,5000)!=WAIT_OBJECT_0)throw std::runtime_error("race gate timeout");
                });
                race->outcomes[i]=1;
            }catch(const std::runtime_error& e){
                race->outcomes[i]=strcmp(e.what(),"A save to this preset path is still in progress. Try again after it finishes.")==0?2:3;
            }catch(...){race->outcomes[i]=3;}
        });
        const auto deadline=GetTickCount64()+5000;
        while(race->ready.load()!=8 && GetTickCount64()<deadline)Sleep(1);
        const bool allReady=race->ready.load()==8;
        SetEvent(race->start);
        threads.clear(); // Join all submitters before reading their result slots.
        unsigned winners=0,conflicts=0;for(auto value:race->outcomes){winners+=value==1;conflicts+=value==2;}
        SetEvent(race->finish); // Always release workers before assertions can throw.
        bool completed=true;
        for(const auto& result:race->results)if(result)completed &= waitResult(result)==csp::SaveStatus::succeeded;
        check(allReady,"all concurrent submitters reach start gate");
        check(winners==1 && conflicts==7,"eight simultaneous contenders have exactly one reservation owner");
        check(completed,"winning reservation finishes without deadlock");
        auto retry=csp::start_path_save(path,[](const std::filesystem::path&){});
        check(waitResult(retry)==csp::SaveStatus::succeeded,"reservation can be reacquired after contention");
    }
    // Measure this target's actual setup allocations, then fail every one in turn.
    // Counting is thread-local: background completion cannot change the injection index.
    reset_allocation_count();
    auto baseline=csp::start_path_save(path,[](const std::filesystem::path&){});
    const auto positions=get_allocation_count();
    check(waitResult(baseline)==csp::SaveStatus::succeeded,"allocation sweep baseline completes");
    check(positions>0 && positions<256,"allocation sweep has bounded measured coverage");
    size_t failures=0;bool recovered=true;
    for(size_t index=0;index<positions;++index){
        std::shared_ptr<csp::SaveResult> result;
        fail_allocation_after(int(index));
        try{result=csp::start_path_save(path,[](const std::filesystem::path&){});}
        catch(const std::bad_alloc&){++failures;}
        catch(...){fail_allocation_after(-1);throw;}
        fail_allocation_after(-1);
        if(result)recovered &= waitResult(result)==csp::SaveStatus::succeeded;
        auto retry=csp::start_path_save(path,[](const std::filesystem::path&){});
        recovered &= waitResult(retry)==csp::SaveStatus::succeeded;
    }
    check(failures==positions,"every measured setup allocation fails under injection");
    check(recovered,"every setup allocation failure releases registry lock and reservation");
    std::cout<<"Reservation stress: 8 rounds x 8 contenders; "<<positions<<" allocation failure positions recovered.\n";
}
