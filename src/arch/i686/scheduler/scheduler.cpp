#include <stdint.h>
#include <misc/cpuio.h>
#include <misc/memory.h>
#include <scheduler/scheduler.h>
#include <kernio/kernio.h>
#include <pit/pit.h>
#include <paging/paging.h>

#define SLOT_COUNT  256

#define BochsBreak() outw(0x8A00,0x8A00); outw(0x8A00,0x08AE0);

struct TastState_t {
    uint32_t esp;
    bool paused;
    bool used;
    uint32_t priority;
    uint32_t countdown;
    uint32_t stackBase;
    uint32_t stackPages;
};

TastState_t tasks[SLOT_COUNT];
uint32_t currentId = 0;
uint32_t highId = 0;
bool first = true;

void doCountdowns() {
    for (int i = 0; i <= highId; i++) {
        if (!tasks[i].used) {
            continue;
        }
        if (tasks[i].countdown > 0) {
            tasks[i].countdown--;
        }
    }
}

void nextTask() {
    for (int i = currentId + 1; i <= highId; i++) {
        if (tasks[i].used && tasks[i].countdown == 0) {
            currentId = i;
            return;
        }
    }
    for (int i = 0; i <= currentId; i++) {
        if (tasks[i].used && tasks[i].countdown == 0) {
            currentId = i;
            return;
        }
    }
    currentId = SLOT_COUNT - 1;
}

void cleanUnused() {
    uint32_t newHighId = 0;
    for (int i = 0; i <= highId; i++) {
        if (!tasks[i].used) {
            paging::setAbsent(tasks[i].stackBase, tasks[i].stackPages);
            continue;
        }
        newHighId = i;
    }
    highId = newHighId;
}

extern "C"
uint32_t _cpp_switch_task(uint32_t esp) {
    tasks[currentId].esp = esp;
    cleanUnused();
    doCountdowns();
    nextTask();
    if (currentId == 1) {
        BochsBreak();
    }
    uint32_t* _stack = (uint32_t*)tasks[currentId].esp;
    _stack[11] = 1 << 9; // EFLAGS with interrupt bit set
    _stack[8] = 1 << 9;
    return tasks[currentId].esp;
}

uint16_t lastCount = 0;

extern "C"
uint32_t _cpp_switch_task_no_count(uint32_t esp) {
    uint16_t count = pit::getCurrentCount(PIT_PORT_CHAN_0);
    if (count == 0 || count > lastCount) {
        doCountdowns();
    }
    lastCount = count;
    tasks[currentId].esp = esp;
    nextTask();
    return tasks[currentId].esp;
}

namespace scheduler {
    void _fallback() {
        while (true) {
            asm volatile("nop");
        }
    }
    
    void init() {
        // Init main task
        tasks[0].used = true;
        tasks[0].paused = false;
        tasks[0].priority = 0;
        tasks[0].countdown = 0;
        
        // Init fallback task
        uint32_t stackTop = paging::allocPages(1) + 0xFCB; 
        TastState_t state;
        state.used = true;
        state.esp = stackTop;
        state.paused = false;
        state.countdown = 0;
        uint32_t* _stack = (uint32_t*)(stackTop); // EAX -> EDX + ESP -> EDI + EIP
        _stack[3] = stackTop; // esp
        _stack[9] = (uint32_t)_fallback; // eip
        _stack[10] = 8; // CS
        _stack[11] = 1 << 9; // EFLAGS with interrupt bit set
        _stack[8] = 1 << 9;
        tasks[SLOT_COUNT - 1] = state;
    }

    int findSlot() {
        for (int i = 0; i < SLOT_COUNT; i++) {
            if (!tasks[i].used) {
                return i;
            }
        }
        return -1;
    }

    Task_t createTask(uint32_t entry, uint32_t stackSize, uint32_t priority, uint32_t parentPID) {
        Task_t task;
        uint32_t stackPages = paging::sizeToPages(stackSize);
        uint32_t stackBase = paging::allocPages(stackPages);
        uint32_t stackTop = ((stackPages - 1) * 4096) + 0xFCB + stackBase;

        TastState_t state;
        state.used = true;
        state.esp = stackTop;
        state.paused = false;
        state.countdown = 0;
        state.stackBase = stackBase;
        state.stackPages = stackPages;
        uint32_t* _stack = (uint32_t*)(stackTop); // EAX -> EDX + ESP -> EDI + EIP
        _stack[3] = stackTop; // esp
        _stack[8] = 1 << 9; // eflags
        _stack[9] = entry; // eip
        _stack[10] = 8; // CS
        _stack[11] = 1 << 9; // EFLAGS with interrupt bit set
        _stack[12] = (uint32_t)endSelf;
        int id = findSlot();
        tasks[id] = state;
        if (id > highId) {
            highId = id;
        }
        task.entryPoint = entry;
        task.id = id;
        task.parentProcessId = parentPID;
        task.priority = priority;
        task.stackTop = stackTop;
        task.stackBase = stackBase;
        task.stackSize = stackSize;
        return task;
    }

    void endTask(Task_t task) {
        tasks[task.id].used = false;
        if (task.id == currentId) {
            _ASM_YIELD();
        }
    }

    void sleep(uint32_t ticks) {
        tasks[currentId].countdown = ticks * 2; // WARNING: Depends on PIT's frequency
        _ASM_YIELD();
    }

    void yield() {
        _ASM_YIELD();
    }

    void endSelf() {
        tasks[currentId].used = false;
        _ASM_YIELD();
    }
}