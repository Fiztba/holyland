#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "Syscalls/Helpers/State.h"
#include "ps2_syscalls.h"
#include "runtime/ee_scheduler.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <variant>
#include <vector>

#ifndef EXPECT_ORIGINAL_INITTHREAD
#define EXPECT_ORIGINAL_INITTHREAD 0
#endif

// _InitSys reaches libkernel's InitThread before main. When the generator
// binds that function to the runtime handler, the handler must leave the
// scheduler as the SDK code would: a priority-zero topThread waiting on its
// own semaphore, and the caller lowered to priority one.
namespace {
constexpr uint32_t kBoot = 0x00170000u;
constexpr uint32_t kAfterInit = kBoot + 0x10u;
constexpr uint32_t kRotate = kBoot + 0x20u;
constexpr uint32_t kAfterRotate = kBoot + 0x30u;
constexpr uint32_t kWorker = kBoot + 0x200u;
constexpr uint32_t kDescriptor = 0x00001800u;
constexpr uint32_t kSemaDescriptor = 0x00001840u;
constexpr uint32_t kStatus = 0x00001880u;

// PS2SDK t_ee_thread layout, written as an authentic guest RAM descriptor.
struct Descriptor {
    int32_t status;
    uint32_t entry, stack;
    int32_t stackSize;
    uint32_t gp;
    int32_t initialPriority, currentPriority;
    uint32_t attr, option;
};
static_assert(sizeof(Descriptor) == 0x24u);

size_t checks = 0u, failures = 0u;
int initResult = 0;
int topId = -1;
std::array<int, 2> workerIds{};
std::vector<int> order;
unsigned rotations = 0u;

void check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}
int result(const R5900Context* context) {
    return static_cast<int32_t>(getRegU32(context, 2));
}
int mainPriority(PS2Runtime* runtime) {
    return runtime->eeScheduler().thread(EeScheduler::kMainThreadId)->currentPriority;
}
void boot(uint8_t* ram, R5900Context* context, PS2Runtime* runtime) {
    check(mainPriority(runtime) == 0, "ExecPS2 bootstrap starts at priority zero");
    // A generated stub sets pc to $ra before entering the handler.
    context->pc = kAfterInit;
    ps2_syscalls::InitThread(ram, context, runtime);
}
void afterInit(uint8_t* ram, R5900Context* context, PS2Runtime* runtime) {
    initResult = result(context);
#if EXPECT_ORIGINAL_INITTHREAD
    check(initResult == EeScheduler::kMainThreadId, "original handler returns the caller's id");
    check(mainPriority(runtime) == 0, "original handler leaves the bootstrap at priority zero");
#else
    EeScheduler& ee = runtime->eeScheduler();
    topId = initResult;
    check(topId == EeScheduler::kFirstThreadId,
          "InitThread returns topThread's id, the first one the kernel hands out");
    const GuestThread* top = ee.thread(topId);
    check(top && top->status == EeThreadStatus::Waiting && top->wait.reason == EeWaitReason::Semaphore,
          "topThread waits on its semaphore");
    check(top && std::holds_alternative<EeSemaphoreWait>(top->wait.payload) &&
              std::get<EeSemaphoreWait>(top->wait.payload).id == 1,
          "topThread's semaphore is the first one the kernel hands out");
    check(top && top->initialPriority == 0 && top->currentPriority == 0, "topThread has priority zero");
    check(mainPriority(runtime) == 1, "InitThread lowers its caller to priority one");
    check(ee.currentThreadId() == EeScheduler::kMainThreadId, "the caller keeps running");

    SET_GPR_U32(context, 4, static_cast<uint32_t>(topId));
    SET_GPR_U32(context, 5, kStatus);
    ps2_syscalls::ReferThreadStatus(ram, context, runtime);
    check(result(context) == 0, "ReferThreadStatus reports topThread");
    int32_t status = -1, initialPriority = -1, currentPriority = -1;
    std::memcpy(&status, ram + kStatus, 4u);
    std::memcpy(&initialPriority, ram + kStatus + 0x14u, 4u);
    std::memcpy(&currentPriority, ram + kStatus + 0x18u, 4u);
    check(status == THS_WAIT && initialPriority == 0 && currentPriority == 0,
          "guest-visible topThread status is a priority-zero wait");

    const size_t threads = ee.snapshot().threads.size();
    ps2_syscalls::InitThread(ram, context, runtime);
    check(result(context) == -1, "a second InitThread fails as libkernel's does");
    check(ee.snapshot().threads.size() == threads, "a second InitThread creates nothing");
    check(mainPriority(runtime) == 1, "a second InitThread leaves the caller's priority alone");

    const int32_t sema[6] = {0, 1, 0, 0, 0, 0};
    std::memcpy(ram + kSemaDescriptor, sema, sizeof(sema));
    SET_GPR_U32(context, 4, kSemaDescriptor);
    ps2_syscalls::CreateSema(ram, context, runtime);
    check(result(context) == 2, "the game's first semaphore gets the id after topThread's");
#endif
    for (unsigned index = 0u; index < workerIds.size(); ++index) {
        Descriptor descriptor{0, kWorker, 0x24000u + index * 0x2000u, 0x1000, 0u, 1, 0, 0u, 0u};
        std::memcpy(ram + kDescriptor, &descriptor, sizeof(descriptor));
        SET_GPR_U32(context, 4, kDescriptor);
        ps2_syscalls::CreateThread(ram, context, runtime);
        workerIds[index] = result(context);
        check(workerIds[index] > EeScheduler::kMainThreadId, "priority-one worker is created");
#if !EXPECT_ORIGINAL_INITTHREAD
        check(workerIds[index] == topId + 1 + static_cast<int>(index),
              "game threads get the ids after topThread's");
#endif
        SET_GPR_U32(context, 4, static_cast<uint32_t>(workerIds[index]));
        SET_GPR_U32(context, 5, index + 1u);
        ps2_syscalls::StartThread(ram, context, runtime);
        check(result(context) == 0, "StartThread succeeds");
    }
    context->pc = kRotate;
}
void worker(uint8_t* ram, R5900Context* context, PS2Runtime* runtime) {
    order.push_back(static_cast<int>(getRegU32(context, 4)));
    ps2_syscalls::ExitThread(ram, context, runtime);
}
// The movie player's decode loop: yield to the other priority-one threads.
void rotate(uint8_t* ram, R5900Context* context, PS2Runtime* runtime) {
    ++rotations;
    context->pc = kAfterRotate;
    SET_GPR_U32(context, 4, 1u);
    ps2_syscalls::RotateThreadReadyQueue(ram, context, runtime);
}
void afterRotate(uint8_t*, [[maybe_unused]] R5900Context* context, PS2Runtime* runtime) {
#if EXPECT_ORIGINAL_INITTHREAD
    if (rotations < 128u) { context->pc = kRotate; return; }
    check(order.empty(), "128 RotateThreadReadyQueue(1) calls cannot yield a priority-zero bootstrap");
    for (const int id : workerIds)
        check(runtime->eeScheduler().thread(id)->status == EeThreadStatus::Ready,
              "original handler leaves the workers ready but starved");
#else
    check(rotations == 1u && order == std::vector<int>({1, 2}),
          "one rotation runs both workers in FIFO order before returning to the bootstrap");
    check(runtime->eeScheduler().thread(topId)->status == EeThreadStatus::Waiting,
          "topThread never runs: the runtime services the i* wrappers itself");
    check(mainPriority(runtime) == 1, "bootstrap stays at priority one");
#endif
    runtime->requestStop();
}
}

int main() {
    auto runtime = std::make_unique<PS2Runtime>();
    check(runtime->memory().initialize(), "actual runtime memory initializes");
    runtime->registerFunction(kBoot, boot);
    runtime->registerFunction(kAfterInit, afterInit);
    runtime->registerFunction(kRotate, rotate);
    runtime->registerFunction(kAfterRotate, afterRotate);
    runtime->registerFunction(kWorker, worker);
    R5900Context context{};
    context.pc = kBoot;
    SET_GPR_U32(&context, 29, 0x30000u);
    runtime->eeScheduler().reset(runtime->memory().getRDRAM(), context);
    runtime->eeScheduler().run();
    std::printf("%s: %zu checks, %zu failures, InitThread=%d, workers run=%zu, rotations=%u\n",
                EXPECT_ORIGINAL_INITTHREAD ? "Original InitThread starvation control" : "HLE InitThread regression",
                checks, failures, initResult, order.size(), rotations);
    return failures == 0u ? EXIT_SUCCESS : EXIT_FAILURE;
}
